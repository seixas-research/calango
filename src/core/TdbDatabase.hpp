#pragma once

#include <string>
#include <vector>

namespace calango::core {

/// One element declaration from a `.tdb` file.
struct TdbElement {
    std::string name;           ///< as written ("FE", or "ELEM_FE" in ATAT files)
    std::string referencePhase; ///< SER reference phase (FCC_A1, BCC_A2, …)
    /// The atomic mass exactly as the file writes it, which every real
    /// database writes in G/MOL (iron is 55.847, not 0.055847). The field name
    /// says kg/mol and is wrong; it is kept because it is published API, and
    /// TdbWriter emits g/mol to match what every database in the wild contains.
    double massKgPerMol = 0.0;
    double enthalpy298 = 0.0;   ///< H(298.15) − H(0), J/mol
    double entropy298 = 0.0;    ///< S(298.15), J/mol/K
    /// True for the two entries every database carries that are NOT chemical
    /// elements: the vacancy VA and the electron /-. They are constituents of
    /// the sublattice models, not things a user selects, so the element
    /// checkboxes must not offer them.
    bool isChemical = true;
};

/// One phase, with its sublattice model.
struct TdbPhase {
    std::string name;
    /// The type code as written (`%`, `I`, `G`, `L`, `B`…). Kept verbatim
    /// rather than interpreted: the codes are a per-database convention and a
    /// parser that guessed at them would be wrong quietly.
    std::string typeCode;
    /// Site ratios, one per sublattice. Its length IS the sublattice count,
    /// so the two cannot disagree.
    std::vector<double> siteRatios;
    /// Constituents per sublattice, filled by the CONSTITUENT statement. Empty
    /// until one is seen — an unconstituted phase is legal in a fragment file
    /// (the SGTE phase list is exactly that) and is not an error.
    std::vector<std::vector<std::string>> constituents;

    std::size_t sublatticeCount() const { return siteRatios.size(); }
};

/// One temperature range of a piecewise Gibbs-energy expression.
///
/// The expression is kept VERBATIM rather than evaluated here. Evaluating it
/// needs a symbol table (a FUNCTION may reference another FUNCTION) and a
/// temperature, neither of which the parser has, so the split is: this file
/// finds the pieces, core/TdbExpression.hpp evaluates them. Keeping the text
/// is also what makes a written-then-read-back database checkable coefficient
/// by coefficient, which is the only end-to-end test a TDB writer can have.
struct TdbExpressionRange {
    double lowerLimit = 298.15;
    double upperLimit = 6000.0;
    std::string expression; ///< e.g. "+20500-9.68*T" or "+GHSERFE"
};

/// One PARAMETER declaration.
///
/// What the UI needs from a parameter is which phase and which interaction it
/// belongs to; what an evaluator needs is the piecewise expression. Both are
/// kept, and neither is interpreted here.
struct TdbParameter {
    std::string symbol;                    ///< G, L, TC, BMAGN, …
    std::string phase;
    std::vector<std::string> constituents; ///< flattened, in declaration order
    /// The same species, KEPT IN THEIR SUBLATTICES: `G(SIGMA,RE:NB:RE,NB;0)`
    /// gives {{RE}, {NB}, {RE, NB}}.
    ///
    /// The flattened list above cannot express this and it is not a detail. In
    /// a phase like (Re)10(Nb)4(Re,Nb)16 the endmembers are identified by the
    /// WHOLE occupation tuple, not by a single species — `RE:NB:RE` and
    /// `RE:NB:NB` are two different endmembers that flatten to the same
    /// multiset. A reader that only understands one- and two-species
    /// parameters skips both and is left with a phase whose Gibbs energy is
    /// zero, i.e. one that appears stable everywhere.
    std::vector<std::vector<std::string>> sublattices;
    int order = 0;                         ///< the ;N Redlich-Kister order
    /// Number of temperature ranges the expression is split into. A unary
    /// SGTE parameter typically has two or three.
    int temperatureRanges = 1;
    /// The piecewise expression itself, one entry per range, in file order.
    /// Empty when the tail of the statement was not in the standard
    /// `<low> <expr>; <high> N` shape — a vendor extension, say — which is
    /// reported as a warning rather than treated as a parse failure.
    std::vector<TdbExpressionRange> ranges;
};

/// One FUNCTION declaration: a named piecewise expression that PARAMETERs
/// reference by name. The SGTE unary file is 379 of these and almost nothing
/// else.
struct TdbFunction {
    std::string name;
    std::vector<TdbExpressionRange> ranges;
};

/// A parsed thermodynamic database.
///
/// SELF-CONTAINED BY DESIGN. Reading a `.tdb` to discover which elements and
/// phases it contains is text parsing, and making it depend on pycalphad would
/// mean the module could not even populate its own checkboxes on a machine
/// without the solver installed — which is most machines, since pycalphad is
/// not part of any Calango environment by default. The solver is needed to
/// COMPUTE (equilibria, phase diagrams), and that runs through a generated
/// script like every other engine in the project, reporting its own absence.
///
/// The parser is deliberately tolerant. Real databases in the wild carry
/// vendor extensions, `TYPE_DEFINITION` blocks whose semantics differ between
/// tools, and dialect quirks (the ATAT distribution encodes newlines as a
/// literal `<NL>` token). An unrecognized statement is recorded as a warning
/// and skipped rather than failing the load: a database that lists its
/// elements and phases correctly is useful even when one exotic block is not
/// understood, and refusing the whole file would be the less honest outcome.
struct TdbDatabase {
    std::vector<TdbElement> elements;
    std::vector<TdbPhase> phases;
    std::vector<TdbParameter> parameters;
    std::vector<std::string> functionNames;
    /// The same declarations as `functionNames`, with their expressions. Kept
    /// alongside rather than replacing it: `functionNames` is what the UI
    /// counts, and this is what an evaluator resolves against.
    std::vector<TdbFunction> functions;
    /// Statements that parsed but were not understood, and dialect notes.
    /// Surfaced in the UI so a partially-understood database says so.
    std::vector<std::string> warnings;

    /// Parse `text`. Returns false only for input that is not a TDB at all
    /// (no recognizable statement); `error` then says why.
    bool parse(const std::string& text, std::string* error);

    /// Element names a user can select — chemical elements only, with the
    /// ATAT `ELEM_` prefix stripped so "FE" appears rather than "ELEM_FE".
    std::vector<std::string> selectableElements() const;
    /// Phase names, in declaration order.
    std::vector<std::string> phaseNames() const;
    /// The phases whose every constituent is inside `selected`, i.e. those
    /// that can exist in a system restricted to those elements.
    ///
    /// A phase with no CONSTITUENT statement is included: its model is not
    /// known here, and hiding it would silently drop phases from a database
    /// that simply keeps its constituents in a companion file.
    std::vector<std::string>
    phasesForElements(const std::vector<std::string>& selected) const;
};

/// Strip the `ELEM_` prefix the ATAT distribution's SGTE files use, and
/// normalize to the bare symbol. "ELEM_FE" -> "FE"; "FE" -> "FE".
std::string tdbBareElementName(const std::string& name);

} // namespace calango::core
