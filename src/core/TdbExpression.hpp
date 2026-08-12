#pragma once

#include "core/TdbDatabase.hpp"

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace calango::core {

/// Resolve a named symbol (a TDB FUNCTION, or `R`, or a user constant) to its
/// value at a temperature. Return false for a name that is not known — the
/// evaluator then reports it by name rather than substituting zero, because a
/// missing lattice-stability function that silently evaluates to 0 J/mol is a
/// phase that appears stable everywhere.
using TdbSymbolResolver =
    std::function<bool(const std::string& name, double temperatureK,
                       double* value)>;

struct TdbEvalResult {
    bool ok = false;
    double value = 0.0;
    std::string error;
};

/// Evaluate one TDB Gibbs-energy expression at a temperature.
///
/// The grammar is the small arithmetic language every `.tdb` writes its
/// thermodynamic functions in:
///
///   expr   := term (('+' | '-') term)*
///   term   := power (('*' | '/') power)*
///   power  := unary ('**' unary)*            -- right-associative
///   unary  := ('+' | '-')* primary
///   primary:= number | 'T' | 'P' | 'R' | NAME | NAME '(' expr ')' | '(' expr ')'
///
/// Three details are not optional and each of them is a bug if missed:
///
///  - `**` is the power operator and `*` is multiplication, so the lexer must
///    look at TWO characters. A one-character lexer reads `T**2` as T*(*2) and
///    fails, or worse reads `T**(-1)` as a product and returns T·(−1).
///  - A FUNCTION reference is written with a trailing `#` (`+GHSERFE#`). The
///    hash is punctuation, not part of the name, and a resolver asked for
///    "GHSERFE#" answers no such symbol.
///  - Numbers carry exponents (`3.6751551E-21`). The `E` must be consumed as
///    part of the number only when a signed digit follows it, or every
///    scientific constant in the SGTE file turns into a product of a number
///    and an unknown symbol called E.
///
/// Only `LN`, `LOG` and `EXP` are recognized as calls; anything else followed
/// by a parenthesis is reported rather than guessed at.
TdbEvalResult evaluateTdbExpression(const std::string& expression,
                                    double temperatureK,
                                    const TdbSymbolResolver& resolver);

/// Evaluates a parsed database's FUNCTIONs and PARAMETERs.
///
/// pycalphad-free by construction, and that is the point: everything this
/// class does is arithmetic over text the parser already found, so a phase
/// diagram drawn from a real database needs no solver installed. What it does
/// NOT do is model sublattices — see calango::core::tdbSubstitutionalPhase.
class TdbEvaluator {
public:
    explicit TdbEvaluator(const TdbDatabase& database);

    /// Value of a named FUNCTION at T. Follows references to other functions,
    /// and refuses a cycle rather than recursing until the stack dies.
    bool functionValue(const std::string& name, double temperatureK,
                       double* value, std::string* error) const;

    /// Value of one PARAMETER's expression at T (J/mol of formula units).
    bool parameterValue(const TdbParameter& parameter, double temperatureK,
                        double* value, std::string* error) const;

    /// True when T fell outside every declared range of the last evaluation
    /// and the nearest range was used instead. Extrapolating an SGTE unary
    /// below 298.15 K is routine and harmless; extrapolating one thousands of
    /// kelvin above its top limit is not, and a caller that wants to say so
    /// needs to know it happened.
    bool lastEvaluationExtrapolated() const { return extrapolated_; }

private:
    /// Pick the range covering T, or the nearest one, setting extrapolated_.
    const TdbExpressionRange*
    selectRange(const std::vector<TdbExpressionRange>& ranges,
                double temperatureK) const;

    const TdbDatabase* database_ = nullptr;
    /// Names currently being evaluated, for cycle detection. Mutable because
    /// evaluation is logically const and the guard is bookkeeping.
    mutable std::vector<std::string> stack_;
    mutable bool extrapolated_ = false;
};

/// A binary (or higher) SUBSTITUTIONAL reading of a database phase.
///
/// Real databases model phases with sublattices, and a general sublattice
/// solver — site fractions, internal equilibria, the Inden-Hillert magnetic
/// term — is a much larger program than a phase diagram needs to get the
/// common cases right. What IS tractable, and covers LIQUID and the ordinary
/// FCC_A1 / BCC_A2 solution phases in every binary database, is the case where
/// exactly one sublattice mixes and every other holds a single species. BCC_A2
/// as (Cr,Fe)1(Va)3 is that case: the vacancy sublattice contributes nothing
/// that varies with composition.
///
/// A phase outside the subset is REFUSED with a reason. Modelling a genuine
/// two-mixing-sublattice phase as though it were substitutional would produce
/// a plausible-looking curve that is simply not the phase in the database, and
/// the user would have no way to tell.
///
/// TRAP FOR THE NEXT PERSON — THE ATAT SGTE FILES DO NOT WORK HERE, and it is
/// not a parser fault. `~/Codes/atat/data/sgte_freee.tdb` holds 379 perfectly
/// good Gibbs functions, but it names them `SGTE_<PHASE>_ELEM_<X>` and writes
/// no `PARAMETER G(PHASE,X;0)` statements binding them to the phases declared
/// in `sgte_phases.tdb`. That binding is ATAT's own convention, applied by
/// ATAT's tools. Everything below reads PARAMETERs, so on those files it finds
/// a phase with no endmember energies and says so. Evaluating the functions
/// directly by name works and is exactly what the melting-point checks in the
/// CALPHAD test do; building a diagram needs a database that carries its own
/// PARAMETER statements, which any standard `.tdb` does.
struct TdbSubstitutionalPhase {
    bool ok = false;
    std::string reason;   ///< why not, when !ok
    std::string phase;
    /// The mixing constituents, in the order the RK parameters are read
    /// against.
    std::vector<std::string> constituents;
    /// Pure-endmember Gibbs energies at the evaluation temperature, per mole
    /// of ATOMS (the site-ratio normalization is already applied).
    std::vector<double> endmemberJPerMol;
    /// Interaction coefficients by constituent pair. `terms[i][j]` is the
    /// Redlich-Kister series for the pair (constituents[i], constituents[j])
    /// with the polynomial variable (x_i − x_j); only i < j is filled.
    std::vector<std::vector<std::vector<double>>> interaction;
    /// Set when the phase carries TC / BMAGN parameters that this model does
    /// not include. Ferromagnetic iron's Curie contribution is worth several
    /// kJ/mol, so a diagram computed without it must say so.
    bool magneticIgnored = false;
    /// Atoms per formula unit — the divisor already applied above, kept so a
    /// caller can report it.
    double atomsPerFormulaUnit = 1.0;

    /// The temperature every coefficient above was evaluated at. Stored
    /// because the struct is a snapshot: its numbers are already numbers, and
    /// gibbsAtMoleFraction() needs the T that produced them to add the
    /// configurational entropy consistently.
    double temperatureK = 0.0;
    /// Site ratio of the mixing sublattice (1 for a plain substitutional
    /// solution). The ideal entropy is per SITE, so it carries this factor.
    double mixingSites = 1.0;
    /// Atoms of each mixing constituent contributed by the sublattices that do
    /// NOT mix, index-aligned with `constituents`. For (Re)10(Nb)4(Re,Nb)16
    /// this is 4 for NB and 10 for RE.
    std::vector<double> fixedAtoms;
    /// The overall mole-fraction range of `constituents[1]` the phase can
    /// reach, which is the site fraction range [0,1] mapped through the
    /// sublattice model. A stoichiometric-ish phase like sigma occupies a
    /// narrow band, and drawing it across the whole axis — as a model that
    /// confused site fraction with mole fraction does — invents solubility
    /// that the database does not describe.
    double minMoleFraction = 0.0;
    double maxMoleFraction = 1.0;

    /// Site fraction of `constituents[1]` at overall mole fraction `x`.
    ///
    ///   x = (fixedAtoms[1] + mixingSites·y) / atomsPerFormulaUnit
    ///
    /// SITE FRACTION IS NOT MOLE FRACTION, and for any phase with more than
    /// one sublattice they differ. Substituting one for the other puts sigma's
    /// Gibbs curve on the wrong part of the diagram and gives it a composition
    /// range it does not have.
    double siteFractionFor(double moleFraction) const;

    /// Molar Gibbs energy in J per mole of ATOMS at overall mole fraction `x`
    /// of `constituents[1]`. NaN outside [minMoleFraction, maxMoleFraction].
    double gibbsAtMoleFraction(double moleFraction) const;
};

/// Read one phase of `database` as a substitutional solution at temperature T,
/// restricted to `elements`.
TdbSubstitutionalPhase tdbSubstitutionalPhase(const TdbDatabase& database,
                                              const std::string& phaseName,
                                              const std::vector<std::string>& elements,
                                              double temperatureK);

} // namespace calango::core
