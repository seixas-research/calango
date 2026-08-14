#include "core/TdbDatabase.hpp"

#include "core/LocaleSafeNumber.hpp"
#include "core/TextUtils.hpp"

#include <algorithm>
#include <cctype>
#include <sstream>

namespace calango::core {

namespace {

/// Whitespace-separated tokens.
std::vector<std::string> tokenize(const std::string& text)
{
    std::vector<std::string> out;
    std::istringstream stream(text);
    std::string token;
    while (stream >> token)
        out.push_back(token);
    return out;
}

/// Split on a delimiter, dropping empties.
std::vector<std::string> split(const std::string& text, char delimiter)
{
    std::vector<std::string> out;
    std::string current;
    for (const char c : text) {
        if (c == delimiter) {
            if (!trimmed(current).empty())
                out.push_back(trimmed(current));
            current.clear();
        } else {
            current.push_back(c);
        }
    }
    if (!trimmed(current).empty())
        out.push_back(trimmed(current));
    return out;
}

/// Locale-independent: std::stod follows LC_NUMERIC, which Qt sets from the
/// environment, so under a decimal-comma locale it parsed every "298.15" in a
/// database as 298. See core/LocaleSafeNumber.hpp.
double toDouble(const std::string& text, double fallback = 0.0)
{
    return localeSafeToDouble(text, fallback);
}

/// Remove `$` comments, honouring that `$` is only a comment starter outside
/// the middle of a token in every dialect seen — TDB has no string literals,
/// so this is a plain to-end-of-line strip.
std::string stripComments(const std::string& text)
{
    std::string out;
    out.reserve(text.size());
    bool comment = false;
    for (const char c : text) {
        if (c == '\n') {
            comment = false;
            out.push_back('\n');
            continue;
        }
        if (c == '$')
            comment = true;
        if (!comment)
            out.push_back(c);
    }
    return out;
}

/// Split the tail of a PARAMETER or FUNCTION into its temperature ranges.
///
/// The syntax is
///
///     <lowT> <expr>; <T1> Y <expr>; <T2> Y … <expr>; <highT> N
///
/// so the ';' characters are the range separators and each piece after the
/// first opens with the PREVIOUS range's upper limit and a Y/N continuation
/// flag. Reading it any other way — for instance treating every ';' as an
/// end-of-expression — loses the second and later branches of every SGTE
/// unary, which is where all of the high-temperature physics lives.
///
/// Returns an empty vector for a tail that does not have this shape; the
/// caller warns rather than failing, because a database whose elements and
/// phases are readable is still useful when one vendor extension is not.
std::vector<TdbExpressionRange> parseExpressionRanges(const std::string& tail)
{
    const std::vector<std::string> pieces = split(tail, ';');
    if (pieces.size() < 2)
        return {};

    std::vector<TdbExpressionRange> ranges;
    // First piece: "<lowT> <expression>".
    {
        const std::string piece = trimmed(pieces[0]);
        const auto space = piece.find_first_of(" \t");
        if (space == std::string::npos)
            return {};
        TdbExpressionRange range;
        range.lowerLimit = toDouble(piece.substr(0, space), 298.15);
        range.expression = trimmed(piece.substr(space + 1));
        if (range.expression.empty())
            return {};
        ranges.push_back(range);
    }
    for (std::size_t i = 1; i < pieces.size(); ++i) {
        const std::vector<std::string> tokens = tokenize(pieces[i]);
        if (tokens.empty())
            return {};
        // The upper limit closes the range opened by the previous piece.
        ranges.back().upperLimit = toDouble(tokens[0], ranges.back().lowerLimit);
        if (tokens.size() < 2)
            break; // trailing "6000" with no N — tolerated
        const std::string flag = upperCase(tokens[1]);
        if (flag == "N")
            break;
        if (flag != "Y")
            break;
        // Y: another range follows, starting where this one ended.
        TdbExpressionRange next;
        next.lowerLimit = ranges.back().upperLimit;
        next.upperLimit = ranges.back().upperLimit;
        // Everything after the Y is the next range's expression. Rejoined from
        // the raw text rather than from the tokens so that a spacing quirk
        // inside the expression cannot change its meaning.
        const auto flagPos = pieces[i].find(tokens[1]);
        next.expression = flagPos == std::string::npos
            ? std::string()
            : trimmed(pieces[i].substr(flagPos + tokens[1].size()));
        if (next.expression.empty())
            break;
        ranges.push_back(next);
    }
    return ranges;
}

} // namespace

std::string tdbBareElementName(const std::string& name)
{
    const std::string up = upperCase(trimmed(name));
    // The ATAT distribution's SGTE files prefix every element with ELEM_.
    // Standard databases do not, so this has to be a strip-if-present rather
    // than an assumption either way.
    if (up.rfind("ELEM_", 0) == 0)
        return up.substr(5);
    return up;
}

bool TdbDatabase::parse(const std::string& text, std::string* error)
{
    elements.clear();
    phases.clear();
    parameters.clear();
    functionNames.clear();
    functions.clear();
    warnings.clear();

    std::string body = stripComments(text);
    // ATAT's SGTE files encode line breaks inside a statement as a literal
    // "<NL>" token rather than a newline. Treated as whitespace, which is what
    // it stands for — otherwise it welds itself onto the neighbouring number
    // and every expression in the file parses as one giant token.
    bool sawNlToken = false;
    for (std::string::size_type pos = body.find("<NL>");
         pos != std::string::npos; pos = body.find("<NL>", pos)) {
        body.replace(pos, 4, " ");
        sawNlToken = true;
    }
    if (sawNlToken)
        warnings.push_back(
            "Newlines inside statements were encoded as <NL> (the ATAT "
            "dialect); treated as whitespace.");

    // Statements are terminated by '!', not by newlines: a single PARAMETER
    // routinely spans four lines.
    int recognized = 0;
    for (const std::string& raw : split(body, '!')) {
        const std::string statement = trimmed(raw);
        if (statement.empty())
            continue;
        const std::vector<std::string> tokens = tokenize(statement);
        if (tokens.empty())
            continue;
        const std::string keyword = upperCase(tokens[0]);

        if (keyword == "ELEMENT") {
            // ELEMENT <name> <ref phase> <mass> <H298> <S298>
            if (tokens.size() < 3) {
                warnings.push_back("Malformed ELEMENT statement: " + statement);
                continue;
            }
            TdbElement element;
            element.name = upperCase(tokens[1]);
            element.referencePhase = upperCase(tokens[2]);
            element.massKgPerMol = tokens.size() > 3 ? toDouble(tokens[3]) : 0.0;
            element.enthalpy298 = tokens.size() > 4 ? toDouble(tokens[4]) : 0.0;
            element.entropy298 = tokens.size() > 5 ? toDouble(tokens[5]) : 0.0;
            // VA is the vacancy and /- the electron. Both are legitimate
            // sublattice constituents and neither is something a user picks.
            const std::string bare = tdbBareElementName(element.name);
            element.isChemical = bare != "VA" && bare != "/-" && bare != "-";
            elements.push_back(element);
            ++recognized;
        } else if (keyword == "PHASE") {
            // PHASE <name> <type code> <#sublattices> <site ratios...>
            if (tokens.size() < 4) {
                warnings.push_back("Malformed PHASE statement: " + statement);
                continue;
            }
            TdbPhase phase;
            phase.name = upperCase(tokens[1]);
            phase.typeCode = tokens[2];
            const int declared = static_cast<int>(toDouble(tokens[3], 0.0));
            for (std::size_t i = 4; i < tokens.size(); ++i)
                phase.siteRatios.push_back(toDouble(tokens[i]));
            // The declared count and the ratios that follow can disagree in a
            // hand-edited file. The ratios are what the model actually has, so
            // they win, and the disagreement is reported rather than resolved
            // silently.
            if (declared > 0
                && declared != static_cast<int>(phase.siteRatios.size()))
                warnings.push_back(
                    "Phase " + phase.name + " declares " + std::to_string(declared)
                    + " sublattices but lists "
                    + std::to_string(phase.siteRatios.size()) + " site ratios.");
            phases.push_back(phase);
            ++recognized;
        } else if (keyword == "CONSTITUENT") {
            // CONSTITUENT <phase> :<a,b>:<c>: — sublattices separated by ':',
            // constituents within one by ',' (and '%' marking a major one).
            if (tokens.size() < 2) {
                warnings.push_back("Malformed CONSTITUENT: " + statement);
                continue;
            }
            const std::string phaseName = upperCase(tokens[1]);
            const auto colon = statement.find(':');
            if (colon == std::string::npos) {
                warnings.push_back("CONSTITUENT without sublattices: "
                                   + statement);
                continue;
            }
            std::vector<std::vector<std::string>> model;
            for (const std::string& sublattice :
                 split(statement.substr(colon), ':')) {
                std::vector<std::string> species;
                for (std::string name : split(sublattice, ',')) {
                    // '%' marks the major constituent of a sublattice; it is
                    // an annotation, not part of the name.
                    name.erase(std::remove(name.begin(), name.end(), '%'),
                               name.end());
                    name = trimmed(name);
                    if (!name.empty())
                        species.push_back(upperCase(name));
                }
                if (!species.empty())
                    model.push_back(species);
            }
            const auto it =
                std::find_if(phases.begin(), phases.end(),
                             [&phaseName](const TdbPhase& p) {
                                 return p.name == phaseName;
                             });
            if (it == phases.end()) {
                warnings.push_back("CONSTITUENT names unknown phase "
                                   + phaseName + ".");
                continue;
            }
            it->constituents = model;
            ++recognized;
        } else if (keyword == "PARAMETER") {
            // PARAMETER G(PHASE,A,B;0) 298.15 <expr>; 6000 N
            const auto open = statement.find('(');
            const auto close = statement.find(')', open == std::string::npos
                                                       ? 0
                                                       : open);
            if (open == std::string::npos || close == std::string::npos) {
                warnings.push_back("Malformed PARAMETER: " + statement);
                continue;
            }
            TdbParameter parameter;
            parameter.symbol = upperCase(trimmed(statement.substr(
                keyword.size(), open - keyword.size())));
            const std::string inside =
                statement.substr(open + 1, close - open - 1);
            const auto semicolon = inside.find(';');
            const std::string body2 = semicolon == std::string::npos
                ? inside
                : inside.substr(0, semicolon);
            if (semicolon != std::string::npos)
                parameter.order = static_cast<int>(
                    toDouble(trimmed(inside.substr(semicolon + 1)), 0.0));
            // The phase name is up to the first ','; everything after it is
            // the constituent spec. It has to be split ':' FIRST and ',' only
            // within a sublattice, because the two separators interleave:
            // "RE:NB:RE,NB" is three sublattices whose last one holds two
            // species, and splitting on ',' first tears "RE,NB" apart into
            // different groups and loses which sublattice they shared.
            const auto comma = body2.find(',');
            parameter.phase =
                upperCase(trimmed(comma == std::string::npos ? body2
                                                      : body2.substr(0, comma)));
            if (comma != std::string::npos) {
                for (const std::string& sublattice :
                     split(body2.substr(comma + 1), ':')) {
                    std::vector<std::string> species;
                    for (const std::string& name : split(sublattice, ','))
                        species.push_back(upperCase(name));
                    if (!species.empty())
                        parameter.sublattices.push_back(species);
                }
            }
            // The flattened view, unchanged in content and order, for the
            // callers that only ask "which species does this concern".
            for (const auto& sublattice : parameter.sublattices)
                for (const std::string& species : sublattice)
                    parameter.constituents.push_back(species);
            // Each additional range is introduced by a ';' in the expression
            // followed by an upper limit and Y/N. Counting the Y markers is
            // the cheapest reliable proxy and needs no expression parsing.
            parameter.temperatureRanges = 1;
            for (const std::string& token : tokens)
                if (upperCase(token) == "Y")
                    ++parameter.temperatureRanges;
            parameter.ranges = parseExpressionRanges(statement.substr(close + 1));
            if (parameter.ranges.empty())
                warnings.push_back(
                    "PARAMETER " + parameter.symbol + "(" + parameter.phase
                    + ") has an expression this parser could not split into "
                      "temperature ranges; it is listed but cannot be "
                      "evaluated.");
            parameters.push_back(parameter);
            ++recognized;
        } else if (keyword == "FUNCTION") {
            if (tokens.size() >= 2) {
                functionNames.push_back(upperCase(tokens[1]));
                TdbFunction function;
                function.name = upperCase(tokens[1]);
                // The tail starts after the NAME, not after the keyword: a
                // function's first token is its own identifier and swallowing
                // it as the lower temperature limit would make every FUNCTION
                // start at 0 K.
                const auto namePos = statement.find(tokens[1]);
                function.ranges = parseExpressionRanges(
                    namePos == std::string::npos
                        ? std::string()
                        : statement.substr(namePos + tokens[1].size()));
                functions.push_back(std::move(function));
                ++recognized;
            }
        } else if (keyword == "TYPE_DEFINITION" || keyword == "TYPE_DEF"
                   || keyword == "DEFINE_SYSTEM_DEFAULT"
                   || keyword == "DEFAULT_COMMAND" || keyword == "DATABASE_INFO"
                   || keyword == "LIST_OF_REFERENCES" || keyword == "SPECIES"
                   || keyword == "ASSESSED_SYSTEMS" || keyword == "VERSION_DATE"
                   || keyword == "REFERENCE_FILE" || keyword == "ADD_REFERENCES"
                   || keyword == "TEMPERATURE_LIMITS") {
            // Recognized and deliberately not modelled: none of it changes
            // which elements or phases the database offers, which is what this
            // parser exists to answer.
            ++recognized;
        } else {
            warnings.push_back("Unrecognized statement skipped: "
                               + statement.substr(0, 60));
        }
    }

    if (recognized == 0) {
        if (error)
            *error = "No thermodynamic-database statements were found. A .tdb "
                     "file declares at least one ELEMENT or PHASE, with "
                     "statements terminated by '!'.";
        return false;
    }
    return true;
}

std::vector<std::string> TdbDatabase::selectableElements() const
{
    std::vector<std::string> out;
    for (const TdbElement& element : elements) {
        if (!element.isChemical)
            continue;
        const std::string bare = tdbBareElementName(element.name);
        if (std::find(out.begin(), out.end(), bare) == out.end())
            out.push_back(bare);
    }
    return out;
}

std::vector<std::string> TdbDatabase::phaseNames() const
{
    std::vector<std::string> out;
    out.reserve(phases.size());
    for (const TdbPhase& phase : phases)
        out.push_back(phase.name);
    return out;
}

std::vector<std::string>
TdbDatabase::phasesForElements(const std::vector<std::string>& selected) const
{
    std::vector<std::string> normalized;
    normalized.reserve(selected.size());
    for (const std::string& name : selected)
        normalized.push_back(tdbBareElementName(name));

    std::vector<std::string> out;
    for (const TdbPhase& phase : phases) {
        if (phase.constituents.empty()) {
            // Model unknown here — see the header. Included rather than
            // dropped.
            out.push_back(phase.name);
            continue;
        }
        bool usable = true;
        for (const auto& sublattice : phase.constituents) {
            // A sublattice is satisfiable if ANY of its constituents is
            // available: that is what a sublattice means. Requiring all of
            // them would exclude every solution phase from a binary system.
            bool any = false;
            for (const std::string& species : sublattice) {
                const std::string bare = tdbBareElementName(species);
                // The vacancy is always available; it is not an element the
                // user has to select.
                if (bare == "VA"
                    || std::find(normalized.begin(), normalized.end(), bare)
                        != normalized.end()) {
                    any = true;
                    break;
                }
            }
            if (!any) {
                usable = false;
                break;
            }
        }
        if (usable)
            out.push_back(phase.name);
    }
    return out;
}

} // namespace calango::core
