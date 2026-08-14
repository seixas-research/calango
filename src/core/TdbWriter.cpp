#include "core/TdbWriter.hpp"

#include "core/LocaleSafeNumber.hpp"
#include "core/TextUtils.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <sstream>

namespace calango::core {

namespace {

/// A double in a form that reads back bit-for-bit.
///
/// Via std::to_chars, NOT printf("%.17g"), and the difference is not cosmetic:
/// printf follows LC_NUMERIC, so on a decimal-comma locale it wrote
/// "298,15" — and a comma is the sublattice separator in TDB, so the file
/// stopped being a database. See core/LocaleSafeNumber.hpp for the whole story.
/// to_chars also gives the shortest exactly-round-tripping form, so the
/// output is "298.15" rather than "298.14999999999998".
std::string number(double value)
{
    return localeSafeFormat(value);
}

/// A coefficient as a signed term, so terms concatenate into an expression
/// without the caller tracking whether a '+' is needed.
std::string signedTerm(double value, const char* suffix)
{
    std::ostringstream out;
    if (value >= 0.0)
        out << '+';
    out << number(value) << suffix;
    return out.str();
}

/// `L_ν(T) = a + b·T` as a TDB expression, or "0" when both halves vanish.
std::string termExpression(const RedlichKisterTerm& term)
{
    std::string text;
    if (term.a != 0.0)
        text += signedTerm(term.a, "");
    if (term.b != 0.0)
        text += signedTerm(term.b, "*T");
    return text.empty() ? std::string("0") : text;
}

/// Re-comment a free-text block so every line is a legal `$` comment. A stray
/// un-commented line in the header would be parsed as a statement, and since
/// statements end at '!' and not at a newline it would swallow the first real
/// declaration after it.
void writeComment(std::ostringstream& out, const std::string& text)
{
    std::istringstream lines(text);
    std::string line;
    while (std::getline(lines, line)) {
        // A '!' inside a comment is harmless (comments are stripped before
        // statements are split) but a '$' is not needed twice.
        out << "$ " << line << "\n";
    }
}

} // namespace

std::string writeTdb(const TdbWriteOptions& options)
{
    std::ostringstream out;
    writeComment(out, options.title);
    out << "$\n"
           "$ Written by Calango. Every quantity below is in J/mol of atoms\n"
           "$ and every temperature in K, which is what a .tdb means by them.\n"
           "$\n";

    // --- Elements ---------------------------------------------------------
    // The electron and the vacancy first, as every database in the wild does.
    // They are not optional decoration: a sublattice model that names VA needs
    // VA declared, and pycalphad refuses a database without /- .
    out << " ELEMENT /-   ELECTRON_GAS       0.0000E+00  0.0000E+00  "
           "0.0000E+00 !\n"
           " ELEMENT VA   VACUUM             0.0000E+00  0.0000E+00  "
           "0.0000E+00 !\n";
    for (const TdbElementSpec& element : options.elements) {
        out << " ELEMENT " << upperCase(element.name) << "   "
            << upperCase(element.referencePhase.empty()
                             ? std::string("BLANK")
                             : element.referencePhase)
            << "   " << number(element.massGPerMol) << "  "
            << number(element.enthalpy298) << "  " << number(element.entropy298)
            << " !\n";
    }
    out << "\n";

    for (const TdbPhaseSpec& phase : options.phases) {
        const std::string phaseName = upperCase(phase.name);
        // TDB requires the constituents of a sublattice in alphabetical order,
        // and every interaction parameter is read against that order. Sorting
        // a COPY (with the endmember expressions carried along) keeps the
        // caller's own ordering meaningful while the file gets the canonical
        // one.
        std::vector<std::size_t> order(phase.constituents.size());
        for (std::size_t i = 0; i < order.size(); ++i)
            order[i] = i;
        std::sort(order.begin(), order.end(),
                  [&phase](std::size_t a, std::size_t b) {
                      return upperCase(phase.constituents[a])
                          < upperCase(phase.constituents[b]);
                  });

        out << " PHASE " << phaseName << " " << phase.typeCode << "  1 "
            << number(phase.siteRatio) << " !\n";
        out << " CONSTITUENT " << phaseName << " :";
        for (std::size_t k = 0; k < order.size(); ++k)
            out << (k ? "," : "") << upperCase(phase.constituents[order[k]]);
        out << " : !\n";

        for (std::size_t k = 0; k < order.size(); ++k) {
            const std::size_t i = order[k];
            const std::string expression =
                i < phase.endmemberExpressions.size()
                    && !phase.endmemberExpressions[i].empty()
                ? phase.endmemberExpressions[i]
                : std::string("0");
            out << " PARAMETER G(" << phaseName << ","
                << upperCase(phase.constituents[i]) << ";0) "
                << number(options.lowTemperatureK) << " " << expression << "; "
                << number(options.highTemperatureK) << " N !\n";
        }

        for (const TdbInteractionSpec& interaction : phase.interactions) {
            std::string first = upperCase(interaction.first);
            std::string second = upperCase(interaction.second);
            // THE SIGN TRAP, in its writing direction. The polynomial variable
            // is (x_first − x_second) as the caller defined it; the file must
            // name the pair alphabetically. Swapping the names negates the
            // variable, which leaves even orders alone and flips odd ones.
            const bool swap = second < first;
            if (swap)
                std::swap(first, second);
            for (std::size_t nu = 0; nu < interaction.terms.size(); ++nu) {
                RedlichKisterTerm term = interaction.terms[nu];
                if (swap && (nu % 2 == 1)) {
                    term.a = -term.a;
                    term.b = -term.b;
                }
                // A term that is identically zero is not written: an explicit
                // `L(...;3) 0` in a database reads as "assessed and found to
                // be zero", which is a claim this writer is not entitled to
                // make about an order the fit was never asked for. Order 0 is
                // always written, because its absence would mean the phase has
                // no interaction at all.
                if (nu > 0 && term.a == 0.0 && term.b == 0.0)
                    continue;
                out << " PARAMETER L(" << phaseName << "," << first << ","
                    << second << ";" << nu << ") "
                    << number(options.lowTemperatureK) << " "
                    << termExpression(term) << "; "
                    << number(options.highTemperatureK) << " N !\n";
            }
        }
        out << "\n";
    }
    return out.str();
}

TdbWriteOptions tdbOptionsForAssessment(const CalphadAssessmentInput& input,
                                        const CalphadAssessment& assessment)
{
    TdbWriteOptions options;

    std::ostringstream title;
    title << "First-principles assessment of the " << upperCase(input.elementA)
          << "-" << upperCase(input.elementB) << " " << upperCase(input.phaseName)
          << " phase, generated by Calango.\n"
          << "\n"
          << "Redlich-Kister order " << input.order << ", fitted to "
          << assessment.fit.usedSamples << " sample(s) from "
          << input.configurations.size() << " configuration(s).\n"
          << "RMS residual " << assessment.fit.rmsResidualJPerMol
          << " J/mol, worst " << assessment.fit.maxResidualJPerMol << " J/mol.\n"
          << "\n"
          << (assessment.vibrational
                  ? "Includes the harmonic vibrational free energy, so the "
                    "excess entropy is a fitted quantity."
                  : "STATIC: no vibrational free energy was supplied, so every "
                    "excess entropy here is exactly zero rather than small.")
          << "\n"
          << "\n"
          << "REFERENCE STATE: the pure elements in this same phase, at their "
             "own\n"
             "computed energies. That is NOT the SER reference an SGTE "
             "database uses,\n"
             "so the G parameters below are zero and this file must not be "
             "merged with\n"
             "one that is SER-referenced without first adding the lattice "
             "stabilities.";
    options.title = title.str();

    options.elements.push_back({upperCase(input.elementA), "BLANK", 0.0, 0.0, 0.0});
    options.elements.push_back({upperCase(input.elementB), "BLANK", 0.0, 0.0, 0.0});

    TdbPhaseSpec phase;
    phase.name = upperCase(input.phaseName);
    phase.constituents = {upperCase(input.elementA), upperCase(input.elementB)};
    phase.endmemberExpressions = {"0", "0"};

    TdbInteractionSpec interaction;
    // x in this module is the fraction of element B, and the polynomial
    // variable of redlichKisterExcess is (x_A − x_B) — so A is `first`.
    interaction.first = upperCase(input.elementA);
    interaction.second = upperCase(input.elementB);
    interaction.terms = assessment.fit.terms;
    phase.interactions.push_back(interaction);
    options.phases.push_back(phase);
    return options;
}

} // namespace calango::core
