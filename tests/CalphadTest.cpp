// CALPHAD: the Redlich-Kister model, the DFT -> .tdb pipeline, the TDB
// expression evaluator, and the phase-diagram construction.
//
// Everything here is checked against a CLOSED FORM or a thermodynamic
// identity, never against a previous run:
//
//  - Redlich-Kister and ideal-mixing values against the algebra they are.
//  - The least-squares fit against EXACT recovery: the model is linear in its
//    coefficients and the data is noiseless, so a correct fit must return the
//    coefficients it was built from, to round-off.
//  - The written .tdb against a round trip through the project's own parser,
//    which is the only end-to-end check a database writer can have — it pins
//    the syntax, the alphabetical constituent ordering, the number formatting
//    and the Redlich-Kister sign convention in one assertion.
//  - The binary phase diagram against the IDEAL LENS solution, which is
//    analytic: with both phases ideal, equating chemical potentials gives
//        x_s = (1 − a)/(b − a),  x_l = b·x_s,
//        a = exp(−ΔG_A/RT),      b = exp(−ΔG_B/RT)
//    (Lupis, *Chemical Thermodynamics of Materials*, ch. VIII; the same result
//    is in every phase-equilibria text as the "lens" or "cigar" diagram).
//  - The regular-solution miscibility gap against its binodal, dG/dx = 0, i.e.
//        RT ln[x/(1−x)] + Ω(1 − 2x) = 0,
//    solved independently here by bisection, and against the critical
//    temperature T_c = Ω/2R (Porter & Easterling, *Phase Transformations in
//    Metals and Alloys*, §1.4).
//  - The 3D lower hull against an area identity: the projected areas of its
//    triangles must sum to the area of the composition domain, exactly.
//  - Real SGTE functions against the Third Law and thermodynamic stability:
//    dG/dT = −S < 0 and d²G/dT² = −C_p/T < 0 for every substance above 0 K.
//    Neither number is quoted anywhere; both are properties any correct
//    evaluation of a real Gibbs function must have.
//
// No Qt, no Python, no pycalphad.

#include "core/CalphadModel.hpp"
#include "core/PhaseDiagram.hpp"
#include "core/PhononThermodynamics.hpp"
#include "core/TdbDatabase.hpp"
#include "core/TdbExpression.hpp"
#include "core/TdbWriter.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <clocale>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace {

using namespace calango::core;

int failures = 0;

void check(bool ok, const std::string& what)
{
    std::printf("  %s %s\n", ok ? "ok  " : "FAIL", what.c_str());
    if (!ok)
        ++failures;
}

void checkClose(double got, double expected, double tolerance,
                const std::string& what)
{
    const bool ok = std::fabs(got - expected) <= tolerance;
    std::printf("  %s %s\n", ok ? "ok  " : "FAIL", what.c_str());
    if (!ok) {
        std::printf("        got %.10g, expected %.10g (tolerance %.3g)\n", got,
                    expected, tolerance);
        ++failures;
    }
}

std::string readFile(const std::string& path)
{
    std::ifstream file(path);
    if (!file)
        return {};
    std::ostringstream buffer;
    buffer << file.rdbuf();
    return buffer.str();
}

/// The regular-solution binodal, solved independently of anything in the
/// module under test: the symmetric miscibility gap has a HORIZONTAL common
/// tangent, so its ends are the two minima of G(x) and satisfy dG/dx = 0.
double regularSolutionBinodal(double omega, double temperatureK)
{
    const auto derivative = [omega, temperatureK](double x) {
        return kGasConstantJPerMolK * temperatureK * std::log(x / (1.0 - x))
            + omega * (1.0 - 2.0 * x);
    };
    // The lower branch lies in (0, 0.5). dG/dx runs from −∞ at x → 0 up
    // through zero at the minimum and stays positive until it returns to zero
    // at the central MAXIMUM, x = 1/2 — so the bracket is (negative, positive)
    // and not the other way round. Getting that backwards converges neatly on
    // x = 1/2, which is a root of the same equation and the wrong one.
    double lo = 1e-12;          // derivative < 0 here
    double hi = 0.5 - 1e-12;    // derivative > 0 here
    for (int i = 0; i < 200; ++i) {
        const double mid = 0.5 * (lo + hi);
        if (derivative(mid) > 0.0)
            hi = mid;
        else
            lo = mid;
    }
    return 0.5 * (lo + hi);
}

} // namespace

int main()
{
    // =====================================================================
    std::printf("Redlich-Kister algebra:\n");
    // =====================================================================
    {
        const std::vector<RedlichKisterTerm> regular{{20000.0, 0.0}};
        // G_ex = Ω x_A x_B, the regular solution.
        checkClose(redlichKisterExcess(regular, 0.3, 1000.0),
                   20000.0 * 0.7 * 0.3, 1e-9,
                   "a single term is the regular solution Ω x_A x_B");
        checkClose(redlichKisterExcess(regular, 0.0, 1000.0), 0.0, 1e-12,
                   "and vanishes at the endpoints");

        // The polynomial variable is (x_A − x_B): an odd term makes the excess
        // ASYMMETRIC, and reflecting x must flip only the odd contribution.
        const std::vector<RedlichKisterTerm> skew{{20000.0, 0.0}, {6000.0, 0.0}};
        const double x = 0.3;
        checkClose(redlichKisterExcess(skew, x, 1000.0),
                   x * (1.0 - x) * (20000.0 + 6000.0 * (1.0 - 2.0 * x)), 1e-9,
                   "a first-order term follows x_A x_B [L0 + L1 (x_A − x_B)]");
        checkClose(redlichKisterExcess(skew, 1.0 - x, 1000.0),
                   x * (1.0 - x) * (20000.0 - 6000.0 * (1.0 - 2.0 * x)), 1e-9,
                   "and reflecting the composition negates exactly the odd "
                   "term — the sign convention, stated as a symmetry");

        // L(T) = a + bT.
        const std::vector<RedlichKisterTerm> hot{{10000.0, -4.0}};
        checkClose(redlichKisterExcess(hot, 0.5, 1500.0),
                   0.25 * (10000.0 - 4.0 * 1500.0), 1e-9,
                   "the temperature-dependent half is linear in T");

        // Ideal mixing at x = 1/2 is R T ln(1/2) per mole.
        checkClose(idealMixingGibbs(0.5, 1000.0),
                   kGasConstantJPerMolK * 1000.0 * std::log(0.5), 1e-9,
                   "ideal mixing at equiatomic is RT ln(1/2)");
        checkClose(idealMixingGibbs(0.0, 1000.0), 0.0, 1e-12,
                   "and is exactly zero at a pure endpoint (0·ln 0, not NaN)");
    }

    // =====================================================================
    std::printf("Least-squares fit — exact recovery of a known model:\n");
    // =====================================================================
    {
        // Noiseless samples from a known three-term, temperature-dependent
        // model. A linear least-squares fit to noiseless data from its own
        // model has an exact answer, so anything but recovery is a bug.
        const std::vector<RedlichKisterTerm> truth{
            {-14000.0, 3.5}, {5200.0, -1.25}, {-900.0, 0.4}};
        std::vector<RedlichKisterSample> samples;
        for (double t : {400.0, 800.0, 1200.0, 1600.0}) {
            for (int i = 1; i < 10; ++i) {
                RedlichKisterSample sample;
                sample.moleFractionB = i / 10.0;
                sample.temperatureK = t;
                sample.excessJPerMol =
                    redlichKisterExcess(truth, sample.moleFractionB, t);
                samples.push_back(sample);
            }
        }
        const RedlichKisterFit fit = fitRedlichKister(samples, 2, true);
        check(fit.ok, "a temperature-dependent order-2 fit succeeds");
        check(fit.terms.size() == 3, "and returns three terms");
        for (std::size_t nu = 0; nu < truth.size() && nu < fit.terms.size(); ++nu) {
            checkClose(fit.terms[nu].a, truth[nu].a, 1e-5,
                       "L" + std::to_string(nu) + " enthalpy recovered");
            checkClose(fit.terms[nu].b, truth[nu].b, 1e-8,
                       "L" + std::to_string(nu) + " entropy recovered");
        }
        checkClose(fit.rmsResidualJPerMol, 0.0, 1e-6,
                   "with a residual at round-off");

        // Endpoints are dropped, not fitted: every basis function is zero
        // there and they can only flatter the statistics.
        std::vector<RedlichKisterSample> withEnds = samples;
        withEnds.push_back({0.0, 800.0, 0.0, 1.0});
        withEnds.push_back({1.0, 800.0, 0.0, 1.0});
        const RedlichKisterFit fitEnds = fitRedlichKister(withEnds, 2, true);
        check(fitEnds.usedSamples == fit.usedSamples,
              "adding x = 0 and x = 1 changes nothing — they are excluded");
        check(fitEnds.note.find("x = 0") != std::string::npos,
              "and the exclusion is reported rather than silent");
    }
    {
        // Rank deficiency, refused with a reason instead of solved anyway.
        std::vector<RedlichKisterSample> oneTemperature;
        for (int i = 1; i < 10; ++i)
            oneTemperature.push_back({i / 10.0, 900.0, 100.0 * i, 1.0});
        const RedlichKisterFit fit = fitRedlichKister(oneTemperature, 1, true);
        check(!fit.ok,
              "a temperature-dependent fit from one temperature is refused");
        check(fit.note.find("one temperature") != std::string::npos,
              "naming the actual cause (a and b·T are the same column)");

        std::vector<RedlichKisterSample> tooFew{{0.25, 900.0, 100.0, 1.0},
                                                {0.75, 900.0, 120.0, 1.0}};
        const RedlichKisterFit sparse = fitRedlichKister(tooFew, 4, false);
        check(!sparse.ok, "five coefficients from two compositions is refused");
    }

    // =====================================================================
    std::printf("DFT -> excess Gibbs energy:\n");
    // =====================================================================
    {
        // A synthetic regular-solution alloy: E_form(x) = Ω x_A x_B, expressed
        // in the units DFT reports (eV/atom), with the endpoints at a nonzero
        // reference energy so the endpoint subtraction is actually exercised.
        constexpr double kOmega = 24000.0; // J/mol
        CalphadAssessmentInput input;
        input.elementA = "AG";
        input.elementB = "AU";
        input.phaseName = "FCC_A1";
        input.referenceEnergyAEvPerAtom = -2.75;
        input.referenceEnergyBEvPerAtom = -3.20;
        input.order = 0;
        input.temperatureDependent = false;
        input.temperaturesK = {600.0};
        for (int i = 1; i < 8; ++i) {
            const double x = i / 8.0;
            CalphadConfiguration config;
            config.label = "x=" + std::to_string(x);
            config.moleFractionB = x;
            const double formationEv =
                kOmega * x * (1.0 - x) / kEvPerAtomToJPerMol;
            // The per-atom energy the DFT run would have reported.
            config.energyEvPerAtom = formationEv
                + (1.0 - x) * input.referenceEnergyAEvPerAtom
                + x * input.referenceEnergyBEvPerAtom;
            input.configurations.push_back(config);
        }
        const CalphadAssessment assessment =
            assessBinaryFromFirstPrinciples(input);
        check(assessment.ok, "the assessment succeeds");
        check(!assessment.vibrational,
              "and reports itself STATIC when no phonons were supplied");
        check(assessment.note.find("Static") != std::string::npos,
              "saying so in a note rather than leaving it to be inferred");
        check(assessment.fit.terms.size() == 1, "one Redlich-Kister term");
        if (!assessment.fit.terms.empty()) {
            checkClose(assessment.fit.terms[0].a, kOmega, 1e-4,
                       "which is Ω, recovered through the eV -> J/mol "
                       "conversion and the endpoint referencing");
            checkClose(assessment.fit.terms[0].b, 0.0, 1e-12,
                       "with exactly zero excess entropy — not a small one");
        }

        // The static hull. A positive Ω means every intermediate composition
        // is ABOVE the tie-line joining the pure elements, so nothing but the
        // two endpoints can be stable: a closed-form statement about a
        // positive-enthalpy solution, and the standard sanity check on an
        // assessment.
        int onHull = 0;
        for (const HullPoint& point : assessment.staticHull.points)
            if (point.onHull)
                ++onHull;
        check(onHull == 2,
              "a purely repulsive alloy puts only the two pure elements on the "
              "static hull");

        // --- The same system, now with a vibrational free energy -----------
        // The vibrational term is taken from the project's own harmonic
        // machinery, not reimplemented: a Debye-like DOS per endpoint, softened
        // in the alloy, run through computePhononThermodynamics.
        const auto debyeDos = [](double cutoffCm) {
            std::vector<double> frequencies;
            std::vector<double> dos;
            for (int i = 0; i <= 600; ++i) {
                const double w = i * (cutoffCm / 600.0);
                frequencies.push_back(w);
                dos.push_back(w <= cutoffCm ? w * w : 0.0);
            }
            // Normalize to 3 modes per atom, so freeEnergyEv is already the
            // per-atom quantity the assessment wants.
            double integral = 0.0;
            for (std::size_t i = 1; i < frequencies.size(); ++i)
                integral += 0.5 * (dos[i] + dos[i - 1])
                    * (frequencies[i] - frequencies[i - 1]);
            for (double& value : dos)
                value *= 3.0 / integral;
            return std::make_pair(frequencies, dos);
        };
        const std::vector<double> grid{300.0, 600.0, 900.0, 1200.0};
        const auto freeEnergies = [&](double cutoffCm) {
            const auto [frequencies, dos] = debyeDos(cutoffCm);
            std::vector<double> out;
            for (const double t : grid) {
                // PhononThermodynamics is the existing implementation and is
                // used as-is; a second copy of the harmonic integrals here
                // would be testing this file against itself.
                const PhononThermoResult result = computePhononThermodynamics(
                    frequencies, dos, t, t, 1);
                out.push_back(result.points.front().freeEnergyEv);
            }
            return out;
        };
        CalphadAssessmentInput vib = input;
        vib.temperaturesK = grid;
        vib.temperatureDependent = true;
        vib.referenceVibAEvPerAtom = freeEnergies(220.0);
        vib.referenceVibBEvPerAtom = freeEnergies(180.0);
        for (CalphadConfiguration& config : vib.configurations) {
            // A softer alloy than either endpoint — the ordinary case, and the
            // one that produces a NEGATIVE excess vibrational entropy.
            config.vibFreeEnergyEvPerAtom = freeEnergies(150.0);
        }
        const CalphadAssessment vibAssessment =
            assessBinaryFromFirstPrinciples(vib);
        check(vibAssessment.ok, "the vibrational assessment succeeds");
        check(vibAssessment.vibrational,
              "and reports itself as carrying real vibrational data");
        if (vibAssessment.ok && !vibAssessment.fit.terms.empty()) {
            // Softening the alloy relative to the endpoints RAISES its
            // vibrational entropy of mixing, and L(T) = a + bT with an excess
            // entropy of −b, so b must be negative. The sign is the physics;
            // its magnitude is a property of the made-up DOS and is not
            // asserted.
            check(vibAssessment.fit.terms[0].b < 0.0,
                  "a vibrationally softer alloy yields a POSITIVE excess "
                  "entropy, i.e. a negative b in L = a + bT");
        }
        // One configuration missing its phonons must demote the whole
        // assessment rather than mix two kinds of free energy.
        CalphadAssessmentInput partial = vib;
        partial.configurations[2].vibFreeEnergyEvPerAtom.clear();
        check(!assessBinaryFromFirstPrinciples(partial).vibrational,
              "one configuration without phonons demotes the assessment to "
              "static — the alternative is a fit over two different free "
              "energies");
    }

    // =====================================================================
    std::printf("TDB expression evaluation:\n");
    // =====================================================================
    {
        const TdbSymbolResolver none = [](const std::string&, double, double*) {
            return false;
        };
        const double t = 1000.0;
        // The real SGTE form for iron's GHSERFE, evaluated against the same
        // arithmetic written out by hand.
        const double expected = 1225.7 + 124.134 * t - 23.5143 * t * std::log(t)
            - 0.00439752 * t * t - 5.8927e-8 * t * t * t + 77359.0 / t;
        const TdbEvalResult result = evaluateTdbExpression(
            "+1225.7+124.134*T-23.5143*T*LN(T)-0.00439752*T**2"
            "-5.8927E-8*T**3+77359*T**(-1)",
            t, none);
        check(result.ok, "an SGTE-shaped expression evaluates");
        checkClose(result.value, expected, 1e-6,
                   "to the value its algebra says, including T**(-1) and the "
                   "E-notation exponent");

        // The E-notation trap on its own: `5.8927E-8` must lex as one number.
        // Read as a product it would be 5.8927 · E · (−8) with E an unknown
        // symbol, which is a refusal, or worse a silent zero.
        const TdbEvalResult exponent =
            evaluateTdbExpression("3.6751551E-21*T**7", 1000.0, none);
        check(exponent.ok, "a scientific-notation coefficient lexes as one number");
        checkClose(exponent.value, 3.6751551e-21 * std::pow(1000.0, 7), 1e-9,
                   "with the right value");

        // `**` before `*`.
        const TdbEvalResult power = evaluateTdbExpression("2*T**2", 3.0, none);
        checkClose(power.value, 18.0, 1e-12,
                   "'**' is the power operator and binds tighter than '*'");

        const TdbEvalResult unknown =
            evaluateTdbExpression("+GHSERXX#", 1000.0, none);
        check(!unknown.ok,
              "an unresolvable function reference is refused, not taken as 0 — "
              "a missing lattice stability of zero is a phase stable "
              "everywhere");
        check(unknown.error.find("GHSERXX") != std::string::npos,
              "and the message names the symbol");

        // The trailing '#' is punctuation: the resolver must be asked for the
        // bare name.
        std::string asked;
        const TdbEvalResult hashed = evaluateTdbExpression(
            "+GHSERFE#+100", 1000.0,
            [&asked](const std::string& name, double, double* value) {
                asked = name;
                *value = 7.0;
                return true;
            });
        check(hashed.ok && asked == "GHSERFE",
              "a '#'-terminated reference resolves under its bare name");
        checkClose(hashed.value, 107.0, 1e-12, "and contributes its value");
    }

    // =====================================================================
    std::printf("Writing a .tdb and reading it back:\n");
    // =====================================================================
    {
        // Elements chosen so the writer MUST re-order them: the model's
        // "first" constituent is ZN and TDB requires alphabetical order, so AL
        // is written first and every ODD Redlich-Kister coefficient has to be
        // negated to keep the polynomial variable pointing the same way. If it
        // is not, G_ex(x) comes back reflected and nothing else notices.
        CalphadAssessmentInput input;
        input.elementA = "ZN";
        input.elementB = "AL";
        input.phaseName = "FCC_A1";
        input.order = 2;
        input.temperatureDependent = false;
        input.temperaturesK = {800.0};

        const std::vector<RedlichKisterTerm> truth{
            {12000.0, 0.0}, {5000.0, 0.0}, {-2500.0, 0.0}};
        for (int i = 1; i < 10; ++i) {
            const double x = i / 10.0;
            CalphadConfiguration config;
            config.moleFractionB = x;
            config.energyEvPerAtom =
                redlichKisterExcess(truth, x, 800.0) / kEvPerAtomToJPerMol;
            input.configurations.push_back(config);
        }
        const CalphadAssessment assessment =
            assessBinaryFromFirstPrinciples(input);
        check(assessment.ok, "the assessment succeeds");

        const std::string text =
            writeTdb(tdbOptionsForAssessment(input, assessment));
        check(text.find("PARAMETER L(FCC_A1,AL,ZN;0)") != std::string::npos,
              "the interaction is written with its constituents in the "
              "alphabetical order TDB requires");
        check(text.find("PARAMETER L(FCC_A1,ZN,AL") == std::string::npos,
              "and never in the caller's order");

        // --- The round trip -------------------------------------------------
        TdbDatabase parsed;
        std::string error;
        check(parsed.parse(text, &error),
              "the emitted file parses with the project's own .tdb parser");
        check(parsed.selectableElements().size() == 2,
              "carrying exactly the two chemical elements (VA and /- excluded)");
        check(parsed.phases.size() == 1 && parsed.phases[0].name == "FCC_A1",
              "and the phase it declared");

        const TdbSubstitutionalPhase model =
            tdbSubstitutionalPhase(parsed, "FCC_A1", {"AL", "ZN"}, 800.0);
        check(model.ok, "which reads back as a substitutional solution");
        check(model.constituents.size() == 2
                  && model.constituents[0] == "AL"
                  && model.constituents[1] == "ZN",
              "with its constituents in canonical order");

        // The invariant that actually matters: the EXCESS GIBBS ENERGY is a
        // physical function of composition and cannot depend on which name the
        // file happened to write first. Compare the original model against the
        // recovered one at the same physical composition.
        //
        // Recovered constituents are (AL, ZN); the recovered polynomial
        // variable is therefore (x_AL − x_ZN), while the original was
        // (x_ZN − x_AL) with x = x_AL. So the recovered series is evaluated at
        // 1 − x.
        double worst = 0.0;
        if (model.ok && !model.interaction.empty()) {
            std::vector<RedlichKisterTerm> recovered;
            for (const double value : model.interaction[0][1])
                recovered.push_back({value, 0.0});
            for (int i = 0; i <= 20; ++i) {
                const double x = i / 20.0; // fraction of AL
                const double original = redlichKisterExcess(truth, x, 800.0);
                const double readBack =
                    redlichKisterExcess(recovered, 1.0 - x, 800.0);
                worst = std::max(worst, std::fabs(original - readBack));
            }
        }
        checkClose(worst, 0.0, 1e-6,
                   "and reproduces the excess Gibbs energy at every "
                   "composition — the sign convention, end to end");

        // Coefficient-level recovery, so a failure says WHICH term is wrong.
        if (model.ok && model.interaction[0][1].size() >= 3) {
            checkClose(model.interaction[0][1][0], truth[0].a, 1e-6,
                       "L0 survives the round trip");
            checkClose(model.interaction[0][1][1], -truth[1].a, 1e-6,
                       "L1 survives it NEGATED, because the constituent order "
                       "was reversed");
            checkClose(model.interaction[0][1][2], truth[2].a, 1e-6,
                       "and L2, an even order, is unchanged");
        }
    }

    // =====================================================================
    std::printf("Binary phase diagram — the ideal lens (analytic):\n");
    // =====================================================================
    {
        // Both phases ideal. A melts at 1000 K, B at 1500 K.
        constexpr double kMeltA = 1000.0;
        constexpr double kMeltB = 1500.0;
        constexpr double kFusionA = 10000.0; // J/mol
        constexpr double kFusionB = 15000.0;
        const auto deltaGa = [&](double t) {
            return kFusionA * (1.0 - t / kMeltA); // G_liq − G_sol for A
        };
        const auto deltaGb = [&](double t) {
            return kFusionB * (1.0 - t / kMeltB);
        };

        std::vector<GibbsPhase> phases;
        phases.push_back({"SOLID",
                          [](double x, double t) {
                              return idealMixingGibbs(x, t);
                          },
                          0.0, 1.0});
        phases.push_back({"LIQUID",
                          [&](double x, double t) {
                              return (1.0 - x) * deltaGa(t) + x * deltaGb(t)
                                  + idealMixingGibbs(x, t);
                          },
                          0.0, 1.0});

        constexpr double kTemperature = 1200.0;
        const BinarySection section =
            computeBinarySection(phases, kTemperature, 2001);
        check(section.tieLines.size() == 1,
              "one two-phase field at 1200 K — the solidus/liquidus lens");
        if (section.tieLines.size() == 1) {
            const double rt = kGasConstantJPerMolK * kTemperature;
            const double a = std::exp(-deltaGa(kTemperature) / rt);
            const double b = std::exp(-deltaGb(kTemperature) / rt);
            const double xSolid = (1.0 - a) / (b - a);
            const double xLiquid = b * xSolid;
            const BinaryTieLine& tie = section.tieLines.front();
            // The construction resolves a boundary to one composition step.
            const double tolerance = 1.5 / 2000.0;
            const std::string left =
                section.vertexPhase.empty()
                    ? std::string()
                    : phases[static_cast<std::size_t>(tie.leftPhase)].name;
            check(left == "LIQUID",
                  "whose B-poor end is the LIQUID (B raises the melting point)");
            checkClose(tie.xLeft, xLiquid, tolerance,
                       "the liquidus composition matches the analytic lens");
            checkClose(tie.xRight, xSolid, tolerance,
                       "and so does the solidus");
        }

        // Above the higher melting point everything is liquid; below the lower
        // one everything is solid. Both are single-phase — no tie-line at all.
        check(computeBinarySection(phases, 1600.0, 801).tieLines.empty(),
              "above both melting points the system is single-phase liquid");
        check(computeBinarySection(phases, 900.0, 801).tieLines.empty(),
              "and below both, single-phase solid");
        const BinarySection hot = computeBinarySection(phases, 1600.0, 801);
        check(!hot.vertexPhase.empty()
                  && phases[static_cast<std::size_t>(hot.vertexPhase.front())]
                             .name
                      == "LIQUID",
              "and that single phase above the melting points IS the liquid");
    }

    // =====================================================================
    std::printf("Binary phase diagram — the regular-solution miscibility gap:\n");
    // =====================================================================
    {
        constexpr double kOmega = 20000.0;
        const double criticalT = kOmega / (2.0 * kGasConstantJPerMolK);
        std::vector<GibbsPhase> phases;
        phases.push_back({"ALPHA",
                          [](double x, double t) {
                              return idealMixingGibbs(x, t)
                                  + redlichKisterExcess({{kOmega, 0.0}}, x, t);
                          },
                          0.0, 1.0});

        constexpr double kTemperature = 800.0;
        const BinarySection cold =
            computeBinarySection(phases, kTemperature, 2001);
        check(cold.tieLines.size() == 1,
              "a single phase with a positive interaction unmixes into itself");
        if (cold.tieLines.size() == 1) {
            const BinaryTieLine& tie = cold.tieLines.front();
            check(tie.leftPhase == tie.rightPhase,
                  "and the two-phase field has the SAME phase at both ends — a "
                  "miscibility gap, which a phase-index test would have missed");
            const double binodal = regularSolutionBinodal(kOmega, kTemperature);
            const double tolerance = 1.5 / 2000.0;
            checkClose(tie.xLeft, binodal, tolerance,
                       "its lower end is the binodal root of dG/dx = 0");
            checkClose(tie.xRight, 1.0 - binodal, tolerance,
                       "and its upper end the symmetric partner");
        }

        // The critical temperature is a closed form: T_c = Ω/2R.
        check(computeBinarySection(phases, criticalT * 1.05, 2001)
                  .tieLines.empty(),
              "the gap is closed 5% above T_c = Ω/2R");
        check(!computeBinarySection(phases, criticalT * 0.95, 2001)
                   .tieLines.empty(),
              "and open 5% below it");

        // An ideal solution never unmixes, at any temperature.
        std::vector<GibbsPhase> ideal;
        ideal.push_back({"IDEAL",
                         [](double x, double t) {
                             return idealMixingGibbs(x, t);
                         },
                         0.0, 1.0});
        bool everUnmixed = false;
        for (double t = 200.0; t <= 2000.0; t += 100.0)
            if (!computeBinarySection(ideal, t, 401).tieLines.empty())
                everUnmixed = true;
        check(!everUnmixed, "an ideal solution is single-phase at every "
                            "temperature, as its convexity requires");

        // And the whole-diagram sweep agrees with the sections it is made of.
        BinaryPhaseDiagramOptions options;
        options.minTemperatureK = 400.0;
        options.maxTemperatureK = 1600.0;
        options.temperatureSteps = 25;
        options.compositionSteps = 401;
        const BinaryPhaseDiagram diagram =
            computeBinaryPhaseDiagram(phases, options);
        check(diagram.sections.size() == 25, "the sweep produces every section");
        bool monotone = true;
        double previousWidth = 1.0;
        for (const BinarySection& section : diagram.sections) {
            const double width = section.tieLines.empty()
                ? 0.0
                : section.tieLines.front().xRight - section.tieLines.front().xLeft;
            if (width > previousWidth + 1e-9)
                monotone = false;
            previousWidth = width;
        }
        check(monotone,
              "and the gap narrows monotonically with temperature, closing at "
              "T_c — the shape of every binodal");

        // The assemblage lookup agrees with the tie-lines it is derived from.
        const std::vector<int> inside = binaryAssemblageAt(cold, 0.5);
        check(inside.size() == 1 && inside.front() == 0,
              "inside a miscibility gap the assemblage is that one phase");
        const std::vector<int> outside = binaryAssemblageAt(cold, 0.001);
        check(outside.size() == 1,
              "and outside it the single stable phase is reported");
    }

    // =====================================================================
    std::printf("Monotone interpolation — it must not invent structure:\n");
    // =====================================================================
    {
        // The case that separates a shape-preserving scheme from a pretty one.
        // A natural cubic spline (or Catmull-Rom) through a flat run followed
        // by a step OVERSHOOTS: it dips below 0 before the rise and above 1
        // after it. On a solvus that overshoot is a terminal solubility the
        // database does not contain, drawn with the same confidence as the
        // real curve — so the requirement is not "smooth", it is "smooth and
        // incapable of leaving the data's own envelope".
        const std::vector<double> t{0.0, 1.0, 2.0, 3.0, 4.0, 5.0};
        const std::vector<double> y{0.0, 0.0, 0.0, 1.0, 1.0, 1.0};
        std::vector<double> rt;
        std::vector<double> ry;
        check(monotoneCubicResample(t, y, 16, &rt, &ry),
              "a step-shaped boundary resamples");
        double lowest = 1e30;
        double highest = -1e30;
        for (const double value : ry) {
            lowest = std::min(lowest, value);
            highest = std::max(highest, value);
        }
        check(lowest >= -1e-12 && highest <= 1.0 + 1e-12,
              "and never leaves [0, 1] — a natural spline overshoots both "
              "ends here, and an overshoot on a solvus is a fabricated "
              "solubility limit");
        bool nonDecreasing = true;
        for (std::size_t i = 1; i < ry.size(); ++i)
            if (ry[i] < ry[i - 1] - 1e-12)
                nonDecreasing = false;
        check(nonDecreasing,
              "and stays monotone, because the data is");

        // Interpolation, not approximation: the original points survive.
        check(std::fabs(ry.front() - y.front()) < 1e-12
                  && std::fabs(ry.back() - y.back()) < 1e-12,
              "the end points are reproduced exactly");
        // And it is denser than the input, which is the entire purpose.
        check(ry.size() > y.size() * 8, "with the curve genuinely resampled");

        // A local maximum must stay AT the data point rather than being
        // sailed past — the retrograde-solvus shape.
        const std::vector<double> hump{0.0, 1.0, 2.0, 3.0, 4.0};
        const std::vector<double> peak{0.0, 1.0, 2.0, 1.0, 0.0};
        std::vector<double> ht;
        std::vector<double> hy;
        check(monotoneCubicResample(hump, peak, 16, &ht, &hy),
              "a curve with a maximum resamples");
        double top = -1e30;
        for (const double value : hy)
            top = std::max(top, value);
        checkClose(top, 2.0, 1e-9,
                   "and its peak is the data's own peak, not higher");

        // Refusals rather than nonsense.
        std::vector<double> ignoredT;
        std::vector<double> ignoredY;
        check(!monotoneCubicResample({1.0}, {1.0}, 8, &ignoredT, &ignoredY),
              "a single point cannot be a curve");
        check(!monotoneCubicResample({0.0, 1.0, 0.5}, {0.0, 1.0, 2.0}, 8,
                                     &ignoredT, &ignoredY),
              "and a non-increasing temperature axis is refused");
    }

    // =====================================================================
    std::printf("Phase fields — regions, and invariants that stay sharp:\n");
    // =====================================================================
    {
        // A textbook EUTECTIC, built to have one: two terminal solid solutions
        // that barely dissolve each other (a large positive interaction) and an
        // ideal liquid. Below the eutectic the assemblage is alpha + beta;
        // above it, L + alpha on one side and L + beta on the other. Nothing
        // continues across, which is what a eutectic IS.
        constexpr double kMeltA = 1200.0;
        constexpr double kMeltB = 1000.0;
        constexpr double kFusionA = 12000.0;
        constexpr double kFusionB = 10000.0;
        constexpr double kSolidOmega = 40000.0; // strong demixing in the solid
        std::vector<GibbsPhase> phases;
        phases.push_back({"ALPHA",
                          [](double x, double t) {
                              return idealMixingGibbs(x, t)
                                  + redlichKisterExcess({{kSolidOmega, 0.0}}, x, t);
                          },
                          0.0, 1.0});
        phases.push_back({"LIQUID",
                          [](double x, double t) {
                              const double ga = kFusionA * (1.0 - t / kMeltA);
                              const double gb = kFusionB * (1.0 - t / kMeltB);
                              return (1.0 - x) * ga + x * gb
                                  + idealMixingGibbs(x, t);
                          },
                          0.0, 1.0});

        BinaryPhaseDiagramOptions options;
        options.minTemperatureK = 500.0;
        options.maxTemperatureK = 1300.0;
        options.temperatureSteps = 161; // 5 K per step
        options.compositionSteps = 801;
        const BinaryPhaseDiagram diagram =
            computeBinaryPhaseDiagram(phases, options);

        // Bands tile each isotherm exactly: no gaps, no overlaps. That is what
        // makes the regions closed areas rather than a cloud of segments.
        bool tiled = true;
        for (const BinarySection& section : diagram.sections) {
            const std::vector<DiagramBand> bands = binarySectionBands(section);
            if (bands.empty())
                continue;
            if (std::fabs(bands.front().xLow - section.vertexX.front()) > 1e-9)
                tiled = false;
            if (std::fabs(bands.back().xHigh - section.vertexX.back()) > 1e-9)
                tiled = false;
            for (std::size_t i = 1; i < bands.size(); ++i)
                if (std::fabs(bands[i].xLow - bands[i - 1].xHigh) > 1e-9)
                    tiled = false;
        }
        check(tiled,
              "the bands of every isotherm tile it end to end, with no gap and "
              "no overlap");

        const std::vector<PhaseField> fields = tracePhaseFields(diagram);
        check(!fields.empty(), "the diagram traces into fields");

        // The eutectic temperature, located independently of the tracing: the
        // lowest temperature at which any liquid-containing field exists.
        double eutectic = 1e30;
        for (const BinarySection& section : diagram.sections) {
            bool hasLiquid = false;
            for (const int phase : section.vertexPhase)
                if (phase == 1)
                    hasLiquid = true;
            if (hasLiquid)
                eutectic = std::min(eutectic, section.temperatureK);
        }
        check(eutectic > options.minTemperatureK && eutectic < kMeltB,
              "a eutectic exists below the lower melting point");

        // THE INVARIANT IS SHARP. No field may span the eutectic: the
        // alpha+beta field must END there and the L+alpha / L+beta fields must
        // BEGIN there. A field crossing it would be smoothed through, and a
        // eutectic smoothed through is a rounded minimum — a beautiful,
        // wrong diagram.
        int spanning = 0;
        for (const PhaseField& field : fields) {
            if (!field.twoPhase())
                continue;
            if (field.temperatureK.front() < eutectic - 1e-6
                && field.temperatureK.back() > eutectic + 1e-6)
                ++spanning;
        }
        check(spanning == 0,
              "and no two-phase field is traced across it — every curve breaks "
              "at the invariant instead of being interpolated through it");

        // The fields either side are the ones the phase rule requires.
        bool sawSolidPair = false;
        bool sawLiquidPair = false;
        for (const PhaseField& field : fields) {
            if (!field.twoPhase())
                continue;
            const bool belowOnly = field.temperatureK.back() <= eutectic + 1e-6;
            const bool aboveOnly = field.temperatureK.front() >= eutectic - 1e-6;
            // ALPHA is phase 0, LIQUID is phase 1.
            if (belowOnly && field.phaseA == 0 && field.phaseB == 0)
                sawSolidPair = true;
            if (aboveOnly && (field.phaseA == 1 || field.phaseB == 1))
                sawLiquidPair = true;
        }
        check(sawSolidPair,
              "below it the two terminal solid solutions coexist (one phase "
              "index at both ends — a miscibility gap)");
        check(sawLiquidPair, "and above it the liquid appears");

        // Every field's temperature axis is strictly increasing, which is what
        // the interpolation requires of it.
        bool ordered = true;
        for (const PhaseField& field : fields)
            for (std::size_t i = 1; i < field.temperatureK.size(); ++i)
                if (!(field.temperatureK[i] > field.temperatureK[i - 1]))
                    ordered = false;
        check(ordered, "and every field is ordered in temperature");

        // Window edges are marked as such, so the renderer does not draw the
        // bottom of the plot as though it were a reaction.
        const bool markedOpen = std::any_of(
            fields.begin(), fields.end(),
            [](const PhaseField& f) { return f.openBelow || f.openAbove; });
        check(markedOpen,
              "fields cut off by the temperature window say so, rather than "
              "presenting the edge of the plot as an invariant");
    }
    {
        // The ideal lens again, now as regions: exactly one two-phase field,
        // and it must be traced as ONE field across the whole lens rather than
        // broken into fragments — the identity test must not be so strict that
        // a continuous boundary is shattered.
        constexpr double kMeltA = 1000.0;
        constexpr double kMeltB = 1500.0;
        std::vector<GibbsPhase> phases;
        phases.push_back({"SOLID",
                          [](double x, double t) {
                              return idealMixingGibbs(x, t);
                          },
                          0.0, 1.0});
        phases.push_back({"LIQUID",
                          [](double x, double t) {
                              return (1.0 - x) * 10000.0 * (1.0 - t / kMeltA)
                                  + x * 15000.0 * (1.0 - t / kMeltB)
                                  + idealMixingGibbs(x, t);
                          },
                          0.0, 1.0});
        BinaryPhaseDiagramOptions options;
        options.minTemperatureK = 1050.0;
        options.maxTemperatureK = 1450.0;
        options.temperatureSteps = 81;
        options.compositionSteps = 1601;
        const std::vector<PhaseField> fields =
            tracePhaseFields(computeBinaryPhaseDiagram(phases, options));
        int twoPhaseFields = 0;
        std::size_t longest = 0;
        for (const PhaseField& field : fields) {
            if (!field.twoPhase())
                continue;
            ++twoPhaseFields;
            longest = std::max(longest, field.temperatureK.size());
        }
        check(twoPhaseFields == 1,
              "a lens is exactly one two-phase field, not a stack of "
              "fragments");
        check(longest == 81,
              "spanning every temperature in the window");
    }

    // =====================================================================
    std::printf("Ternary isothermal sections:\n");
    // =====================================================================
    {
        // The hull's own identity: the projected areas of its triangles tile
        // the composition domain exactly. Independent of any thermodynamics.
        std::vector<double> xs;
        std::vector<double> ys;
        std::vector<double> zs;
        constexpr int kSteps = 12;
        for (int i = 0; i <= kSteps; ++i) {
            for (int j = 0; i + j <= kSteps; ++j) {
                const double x = static_cast<double>(i) / kSteps;
                const double y = static_cast<double>(j) / kSteps;
                xs.push_back(x);
                ys.push_back(y);
                // Any strictly convex surface: its whole graph is its own
                // lower hull, so the triangles must tile the domain.
                zs.push_back(x * x + y * y + 0.5 * x * y);
            }
        }
        const auto facets = lowerConvexHull3d(xs, ys, zs);
        double area = 0.0;
        for (const auto& facet : facets) {
            const double ax = xs[static_cast<std::size_t>(facet[0])];
            const double ay = ys[static_cast<std::size_t>(facet[0])];
            const double bx = xs[static_cast<std::size_t>(facet[1])];
            const double by = ys[static_cast<std::size_t>(facet[1])];
            const double cx = xs[static_cast<std::size_t>(facet[2])];
            const double cy = ys[static_cast<std::size_t>(facet[2])];
            area += 0.5 * std::fabs((bx - ax) * (cy - ay) - (by - ay) * (cx - ax));
        }
        check(!facets.empty(), "a convex surface has a lower hull");
        checkClose(area, 0.5, 1e-9,
                   "whose triangles tile the composition triangle exactly "
                   "(total projected area 1/2)");

        // An ideal ternary solution is convex everywhere, so every triangle of
        // its hull lies inside the single phase: no tie-lines, no three-phase
        // triangles. That is what "ideal" means, stated as a diagram.
        const auto idealTernary = [](double xB, double xC, double t) {
            const double xA = 1.0 - xB - xC;
            if (xA < -1e-12)
                return std::numeric_limits<double>::quiet_NaN();
            const auto term = [](double x) {
                return x > 0.0 ? x * std::log(x) : 0.0;
            };
            return kGasConstantJPerMolK * t
                * (term(std::max(0.0, xA)) + term(xB) + term(xC));
        };
        TernarySectionOptions options;
        options.temperatureK = 1000.0;
        options.gridSteps = 24;
        const TernaryIsothermalSection idealSection =
            computeTernaryIsothermalSection({{"FCC", idealTernary}}, options);
        check(idealSection.ok, "an ideal ternary section is computed");
        int multiPhase = 0;
        for (const TernaryFacet& facet : idealSection.facets) {
            const int p0 = idealSection.points[
                static_cast<std::size_t>(facet.vertex[0])].phase;
            for (int k = 1; k < 3; ++k)
                if (idealSection.points[
                        static_cast<std::size_t>(facet.vertex[k])].phase != p0)
                    ++multiPhase;
        }
        check(multiPhase == 0,
              "and is single-phase throughout — an ideal solution has no "
              "two-phase field anywhere");

        // Three mutually insoluble components: each phase exists only at its
        // own corner. The answer must be exactly one three-phase triangle
        // spanning the whole diagram, which is also the coplanar path through
        // the hull.
        const auto corner = [](double cx, double cy) {
            return [cx, cy](double xB, double xC, double) {
                return (std::fabs(xB - cx) < 1e-12 && std::fabs(xC - cy) < 1e-12)
                    ? 0.0
                    : std::numeric_limits<double>::quiet_NaN();
            };
        };
        const TernaryIsothermalSection immiscible =
            computeTernaryIsothermalSection({{"A", corner(0.0, 0.0)},
                                             {"B", corner(1.0, 0.0)},
                                             {"C", corner(0.0, 1.0)}},
                                            options);
        check(immiscible.ok, "three insoluble components give a section");
        check(immiscible.facets.size() == 1,
              "consisting of exactly one triangle");
        if (immiscible.facets.size() == 1) {
            std::vector<int> distinct;
            for (const int vertex : immiscible.facets[0].vertex) {
                const int phase =
                    immiscible.points[static_cast<std::size_t>(vertex)].phase;
                if (std::find(distinct.begin(), distinct.end(), phase)
                    == distinct.end())
                    distinct.push_back(phase);
            }
            check(distinct.size() == 3,
                  "with all three phases at its corners — a three-phase field, "
                  "not a degeneracy to reject");
        }
    }

    // =====================================================================
    std::printf("Decimal-comma locales:\n");
    // =====================================================================
    {
        // THIS IS A REGRESSION TEST FOR A BUG THAT REACHED A WRITTEN FILE.
        //
        // printf("%g") and std::stod both follow LC_NUMERIC, and Qt sets
        // LC_NUMERIC from the environment when a QApplication is built. On a
        // machine whose locale uses a decimal comma — pt_BR, de_DE, fr_FR,
        // most of Europe and South America — the writer emitted
        //
        //     PARAMETER L(FCC_A1,AG,AU;0) 298,14999999999998 +17999,99…
        //
        // and a comma is the SUBLATTICE SEPARATOR in TDB, so every number
        // became two tokens and the file was silently not a database. Reading
        // ran the same fault backwards: std::stod("298.15") is 298 under such
        // a locale, truncating every temperature limit in an imported file.
        //
        // Invisible to every unit test in the project, because none of them
        // constructs a QApplication.
        const char* candidates[] = {"pt_BR.UTF-8", "de_DE.UTF-8",
                                    "fr_FR.UTF-8", "es_ES.UTF-8", "de_DE"};
        const char* applied = nullptr;
        for (const char* name : candidates) {
            if (std::setlocale(LC_NUMERIC, name)) {
                applied = name;
                break;
            }
        }
        if (!applied) {
            std::printf("    (no decimal-comma locale installed — skipped; "
                        "the writer uses std::to_chars, which is locale-"
                        "independent by definition)\n");
        } else {
            std::printf("    (under %s)\n", applied);
            TdbWriteOptions options;
            options.elements.push_back({"AG", "FCC_A1", 107.8682, 5745.0, 42.55});
            options.elements.push_back({"AU", "FCC_A1", 196.96654, 6016.0, 47.49});
            TdbPhaseSpec phase;
            phase.name = "FCC_A1";
            phase.constituents = {"AG", "AU"};
            phase.interactions.push_back(
                {"AG", "AU", {{-15000.25, 2.5}, {3000.5, 0.0}}});
            options.phases.push_back(phase);
            options.lowTemperatureK = 298.15;

            const std::string text = writeTdb(options);
            check(text.find("298,15") == std::string::npos
                      && text.find("15000,25") == std::string::npos,
                  "no decimal comma survives into the emitted file");
            check(text.find("298.15") != std::string::npos,
                  "the temperature limit is written with a point");

            TdbDatabase parsed;
            std::string error;
            check(parsed.parse(text, &error),
                  "and the file still parses under this locale");
            const TdbSubstitutionalPhase model =
                tdbSubstitutionalPhase(parsed, "FCC_A1", {"AG", "AU"}, 1000.0);
            check(model.ok, "as a substitutional solution");
            if (model.ok && model.interaction[0][1].size() >= 2) {
                checkClose(model.interaction[0][1][0],
                           -15000.25 + 2.5 * 1000.0, 1e-6,
                           "with L0(T) recovered exactly");
                checkClose(model.interaction[0][1][1], 3000.5, 1e-6,
                           "and L1 too");
            }
            const auto silver = std::find_if(
                parsed.elements.begin(), parsed.elements.end(),
                [](const TdbElement& e) { return e.name == "AG"; });
            check(silver != parsed.elements.end()
                      && std::fabs(silver->massKgPerMol - 107.8682) < 1e-6,
                  "and the atomic mass keeps its fractional part, which "
                  "std::stod would have truncated");
            // Leave the process as it was found: later checks read real files.
            std::setlocale(LC_NUMERIC, "C");
        }
    }

    // =====================================================================
    std::printf("Real SGTE functions (ATAT distribution):\n");
    // =====================================================================
    {
        const std::string path =
            std::string(std::getenv("HOME") ? std::getenv("HOME") : "")
            + "/Codes/atat/data/sgte_freee.tdb";
        const std::string text = readFile(path);
        if (text.empty()) {
            std::printf("    (not present here — skipped)\n");
        } else {
            TdbDatabase database;
            std::string error;
            check(database.parse(text, &error), "the SGTE file parses");
            check(!database.functions.empty(),
                  "and its FUNCTIONs keep their expressions, not just names");
            check(database.functions.size() == database.functionNames.size(),
                  "one expression record per declared name");

            // Every real Gibbs function must obey the Third Law and thermo-
            // dynamic stability: dG/dT = −S < 0, and d²G/dT² = −C_p/T < 0.
            // Neither number is quoted from anywhere — they are properties any
            // correct evaluation must have, which makes them a check on the
            // evaluator rather than on a remembered result.
            TdbEvaluator evaluator(database);
            int evaluated = 0;
            int decreasing = 0;
            int concave = 0;
            for (const TdbFunction& function : database.functions) {
                double low = 0.0;
                double mid = 0.0;
                double high = 0.0;
                if (!evaluator.functionValue(function.name, 500.0, &low, nullptr)
                    || !evaluator.functionValue(function.name, 800.0, &mid, nullptr)
                    || !evaluator.functionValue(function.name, 1100.0, &high,
                                                nullptr))
                    continue;
                ++evaluated;
                if (high < mid && mid < low)
                    ++decreasing;
                // Second difference on a uniform grid.
                if (low - 2.0 * mid + high < 0.0)
                    ++concave;
            }
            check(evaluated > 50,
                  "at least fifty real Gibbs functions evaluate ("
                      + std::to_string(evaluated) + ")");
            // Not every FUNCTION in a database is a Gibbs energy — the files
            // carry magnetic and excess helpers too — so this is a large
            // majority, not a universal.
            check(decreasing * 10 >= evaluated * 8,
                  "and at least 80% of them decrease with temperature, as "
                  "dG/dT = −S < 0 requires ("
                      + std::to_string(decreasing) + "/"
                      + std::to_string(evaluated) + ")");
            check(concave * 10 >= evaluated * 8,
                  "and are concave in T, as d²G/dT² = −C_p/T < 0 requires ("
                      + std::to_string(concave) + "/"
                      + std::to_string(evaluated) + ")");

            // --- MELTING POINTS, against the external reference -------------
            //
            // The strongest check available on the expression evaluator, and
            // it uses no number produced by this project: a pure element melts
            // where its liquid and solid Gibbs energies cross, so solving
            //     G_liquid(T) − G_solid(T) = 0
            // from the real SGTE unary functions must return the element's
            // known melting point. Those are measured quantities (CRC
            // Handbook; the same values the SGTE compilation was fitted to
            // reproduce), so agreeing with them exercises the whole chain —
            // the piecewise range selection, LN, T**n, T**(-1), the E-notation
            // coefficients and the FUNCTION references — against physics
            // rather than against a remembered output.
            struct MeltingCase {
                const char* element;
                const char* solidFunction;
                double referenceK;
                double toleranceK;
                const char* note;
            };
            // The ATAT distribution names its unaries SGTE_<phase>_ELEM_<X>.
            const MeltingCase cases[] = {
                {"AL", "SGTE_FCC_A1_ELEM_AL", 933.47, 0.5, ""},
                {"CU", "SGTE_FCC_A1_ELEM_CU", 1357.77, 0.5, ""},
                {"AG", "SGTE_FCC_A1_ELEM_AG", 1234.93, 0.5, ""},
                {"NI", "SGTE_FCC_A1_ELEM_NI", 1728.0, 0.5, ""},
                {"MG", "SGTE_HCP_A3_ELEM_MG", 923.0, 0.5, ""},
                // Iron is the documented exception and is kept BECAUSE it
                // fails tightly. Its BCC Gibbs energy carries the
                // Inden-Hillert magnetic term, which this evaluator
                // deliberately does not model (tdbSubstitutionalPhase reports
                // magneticIgnored for exactly this reason), and the ferro-
                // magnetic stabilisation of bcc iron is worth about 10 K of
                // melting point. Pinning the SIZE of the known limitation is
                // what stops it from quietly growing.
                {"FE", "SGTE_BCC_A2_ELEM_FE", 1811.0, 20.0,
                 " (magnetic term not modelled)"},
            };
            for (const MeltingCase& melting : cases) {
                const std::string liquid =
                    std::string("SGTE_LIQUID_ELEM_") + melting.element;
                const auto difference = [&](double t) {
                    double a = 0.0;
                    double b = 0.0;
                    if (!evaluator.functionValue(liquid, t, &a, nullptr)
                        || !evaluator.functionValue(melting.solidFunction, t, &b,
                                                    nullptr))
                        return std::numeric_limits<double>::quiet_NaN();
                    return a - b;
                };
                if (!std::isfinite(difference(1000.0))) {
                    std::printf("    (%s not in this file — skipped)\n",
                                melting.element);
                    continue;
                }
                // Below the melting point the solid is lower, above it the
                // liquid is: the difference changes sign exactly once.
                double lo = 300.0;
                double hi = 3000.0;
                for (int i = 0; i < 200; ++i) {
                    const double mid = 0.5 * (lo + hi);
                    if (difference(mid) > 0.0)
                        lo = mid;
                    else
                        hi = mid;
                }
                checkClose(0.5 * (lo + hi), melting.referenceK,
                           melting.toleranceK,
                           std::string(melting.element)
                               + " melts where its liquid and solid Gibbs "
                                 "energies cross"
                               + melting.note);
            }
        }
    }

    if (failures == 0) {
        std::printf("\nAll CALPHAD checks passed.\n");
        return EXIT_SUCCESS;
    }
    std::printf("\n%d CALPHAD check(s) FAILED.\n", failures);
    return EXIT_FAILURE;
}
