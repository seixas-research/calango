#include "core/TdbExpression.hpp"

#include "core/CalphadModel.hpp"
#include "core/LocaleSafeNumber.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <limits>

namespace calango::core {

namespace {

std::string upperCase(std::string text)
{
    std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) {
        return static_cast<char>(std::toupper(c));
    });
    return text;
}

/// Recursive-descent evaluator over one expression string.
///
/// Hand-written rather than table-driven because the grammar is five rules
/// long and the interesting part is the lexing, not the parsing — see the
/// header for the three lexical traps that actually bite.
class Parser {
public:
    Parser(const std::string& text, double temperatureK,
           const TdbSymbolResolver& resolver)
        : text_(text), temperature_(temperatureK), resolver_(resolver)
    {
    }

    bool run(double* value, std::string* error)
    {
        skipSpace();
        const double result = parseExpression();
        if (!failure_.empty()) {
            *error = failure_;
            return false;
        }
        skipSpace();
        if (pos_ != text_.size()) {
            *error = "Unexpected '" + text_.substr(pos_, 12)
                + "' in the expression.";
            return false;
        }
        if (!std::isfinite(result)) {
            *error = "The expression did not evaluate to a finite number.";
            return false;
        }
        *value = result;
        return true;
    }

private:
    void skipSpace()
    {
        while (pos_ < text_.size()
               && (std::isspace(static_cast<unsigned char>(text_[pos_]))
                   // A '#' terminates a FUNCTION reference and carries no
                   // meaning of its own; treated as whitespace so `+GHSERFE#`
                   // and `+GHSERFE` parse identically.
                   || text_[pos_] == '#'))
            ++pos_;
    }

    bool consume(char c)
    {
        skipSpace();
        if (pos_ < text_.size() && text_[pos_] == c) {
            ++pos_;
            return true;
        }
        return false;
    }

    /// The two-character power operator. Checked BEFORE '*' everywhere.
    bool consumePower()
    {
        skipSpace();
        if (pos_ + 1 < text_.size() && text_[pos_] == '*'
            && text_[pos_ + 1] == '*') {
            pos_ += 2;
            return true;
        }
        return false;
    }

    void fail(const std::string& message)
    {
        if (failure_.empty())
            failure_ = message;
    }

    double parseExpression()
    {
        double value = parseTerm();
        for (;;) {
            skipSpace();
            if (pos_ < text_.size() && text_[pos_] == '+') {
                ++pos_;
                value += parseTerm();
            } else if (pos_ < text_.size() && text_[pos_] == '-') {
                ++pos_;
                value -= parseTerm();
            } else {
                return value;
            }
            if (!failure_.empty())
                return 0.0;
        }
    }

    double parseTerm()
    {
        double value = parsePower();
        for (;;) {
            skipSpace();
            // '**' must not be mistaken for '*': check the pair first.
            if (pos_ + 1 < text_.size() && text_[pos_] == '*'
                && text_[pos_ + 1] == '*')
                return value;
            if (pos_ < text_.size() && text_[pos_] == '*') {
                ++pos_;
                value *= parsePower();
            } else if (pos_ < text_.size() && text_[pos_] == '/') {
                ++pos_;
                const double divisor = parsePower();
                if (divisor == 0.0) {
                    fail("Division by zero in the expression.");
                    return 0.0;
                }
                value /= divisor;
            } else {
                return value;
            }
            if (!failure_.empty())
                return 0.0;
        }
    }

    double parsePower()
    {
        const double base = parseUnary();
        if (!failure_.empty())
            return 0.0;
        if (consumePower()) {
            // Right-associative: a**b**c is a**(b**c). Databases do not nest
            // powers, but getting the associativity wrong on the one that does
            // would be silent.
            const double exponent = parsePower();
            if (!failure_.empty())
                return 0.0;
            return std::pow(base, exponent);
        }
        return base;
    }

    double parseUnary()
    {
        skipSpace();
        if (pos_ < text_.size() && text_[pos_] == '-') {
            ++pos_;
            return -parseUnary();
        }
        if (pos_ < text_.size() && text_[pos_] == '+') {
            ++pos_;
            return parseUnary();
        }
        return parsePrimary();
    }

    double parsePrimary()
    {
        skipSpace();
        if (pos_ >= text_.size()) {
            fail("The expression ended where a value was expected.");
            return 0.0;
        }
        const char c = text_[pos_];
        if (c == '(') {
            ++pos_;
            const double value = parseExpression();
            if (!consume(')'))
                fail("Unbalanced parenthesis in the expression.");
            return value;
        }
        if (std::isdigit(static_cast<unsigned char>(c)) || c == '.')
            return parseNumber();
        if (std::isalpha(static_cast<unsigned char>(c)) || c == '_')
            return parseName();
        fail(std::string("Unexpected character '") + c + "' in the expression.");
        return 0.0;
    }

    double parseNumber()
    {
        const std::size_t start = pos_;
        while (pos_ < text_.size()
               && (std::isdigit(static_cast<unsigned char>(text_[pos_]))
                   || text_[pos_] == '.'))
            ++pos_;
        // The exponent, but ONLY when a signed digit follows: `2E-3` is a
        // number, `2*EXP(...)` is not, and `1E` followed by a letter is a
        // number times a symbol.
        if (pos_ < text_.size() && (text_[pos_] == 'E' || text_[pos_] == 'e')) {
            std::size_t look = pos_ + 1;
            if (look < text_.size() && (text_[look] == '+' || text_[look] == '-'))
                ++look;
            if (look < text_.size()
                && std::isdigit(static_cast<unsigned char>(text_[look]))) {
                pos_ = look;
                while (pos_ < text_.size()
                       && std::isdigit(static_cast<unsigned char>(text_[pos_])))
                    ++pos_;
            }
        }
        // Locale-independent. std::stod would follow LC_NUMERIC and read
        // "0.00439752" as 0 under a decimal-comma locale, which turns every
        // heat-capacity term in an SGTE function into zero without erroring.
        double value = 0.0;
        const std::string token = text_.substr(start, pos_ - start);
        if (!localeSafeParse(token, &value)) {
            fail("'" + token + "' is not a number.");
            return 0.0;
        }
        return value;
    }

    double parseName()
    {
        const std::size_t start = pos_;
        while (pos_ < text_.size()
               && (std::isalnum(static_cast<unsigned char>(text_[pos_]))
                   || text_[pos_] == '_'))
            ++pos_;
        const std::string name = upperCase(text_.substr(start, pos_ - start));

        // A call, if a parenthesis follows immediately (before the '#'-and-
        // whitespace skip, which would let `LN (T)` through too — harmless).
        std::size_t look = pos_;
        while (look < text_.size()
               && std::isspace(static_cast<unsigned char>(text_[look])))
            ++look;
        if (look < text_.size() && text_[look] == '(') {
            pos_ = look + 1;
            const double argument = parseExpression();
            if (!consume(')')) {
                fail("Unbalanced parenthesis after " + name + ".");
                return 0.0;
            }
            if (!failure_.empty())
                return 0.0;
            if (name == "LN" || name == "LOG") {
                if (argument <= 0.0) {
                    fail("LN of a non-positive value; the expression is "
                         "being evaluated outside its temperature range.");
                    return 0.0;
                }
                return std::log(argument);
            }
            if (name == "EXP")
                return std::exp(argument);
            fail("Unknown function " + name + "() in the expression.");
            return 0.0;
        }

        // Bare identifiers. T and P are the state variables; R is the gas
        // constant, which some databases write literally.
        if (name == "T")
            return temperature_;
        if (name == "P")
            return 101325.0; // one atmosphere, the CALPHAD reference pressure
        if (name == "R")
            return kGasConstantJPerMolK;
        double value = 0.0;
        if (resolver_ && resolver_(name, temperature_, &value))
            return value;
        fail("Unknown symbol " + name
             + " — the database references a function it does not declare.");
        return 0.0;
    }

    const std::string& text_;
    double temperature_;
    const TdbSymbolResolver& resolver_;
    std::size_t pos_ = 0;
    std::string failure_;
};

} // namespace

TdbEvalResult evaluateTdbExpression(const std::string& expression,
                                    double temperatureK,
                                    const TdbSymbolResolver& resolver)
{
    TdbEvalResult result;
    if (expression.empty()) {
        result.error = "The expression is empty.";
        return result;
    }
    Parser parser(expression, temperatureK, resolver);
    result.ok = parser.run(&result.value, &result.error);
    return result;
}

TdbEvaluator::TdbEvaluator(const TdbDatabase& database)
    : database_(&database)
{
}

const TdbExpressionRange*
TdbEvaluator::selectRange(const std::vector<TdbExpressionRange>& ranges,
                          double temperatureK) const
{
    if (ranges.empty())
        return nullptr;
    for (const TdbExpressionRange& range : ranges) {
        if (temperatureK >= range.lowerLimit && temperatureK <= range.upperLimit)
            return &range;
    }
    // Outside every declared range. Clamping to the nearest one rather than
    // refusing is the standard behaviour and the useful one: SGTE unaries
    // start at 298.15 K and a diagram drawn down to 250 K would otherwise have
    // a blank strip along its bottom edge for a purely notational reason.
    extrapolated_ = true;
    const TdbExpressionRange* best = &ranges.front();
    double bestDistance = std::numeric_limits<double>::max();
    for (const TdbExpressionRange& range : ranges) {
        const double distance =
            temperatureK < range.lowerLimit
            ? range.lowerLimit - temperatureK
            : temperatureK - range.upperLimit;
        if (distance < bestDistance) {
            bestDistance = distance;
            best = &range;
        }
    }
    return best;
}

bool TdbEvaluator::functionValue(const std::string& name, double temperatureK,
                                 double* value, std::string* error) const
{
    const std::string key = upperCase(name);
    // Cycle guard. A database that defines A in terms of B and B in terms of A
    // is malformed, but it is text somebody typed and the failure mode without
    // this is a stack overflow with no message at all.
    if (std::find(stack_.begin(), stack_.end(), key) != stack_.end()) {
        if (error)
            *error = "The function " + key + " is defined in terms of itself.";
        return false;
    }
    const auto it = std::find_if(database_->functions.begin(),
                                 database_->functions.end(),
                                 [&key](const TdbFunction& f) {
                                     return f.name == key;
                                 });
    if (it == database_->functions.end()) {
        if (error)
            *error = "The database declares no function called " + key + ".";
        return false;
    }
    const TdbExpressionRange* range = selectRange(it->ranges, temperatureK);
    if (!range) {
        if (error)
            *error = "The function " + key + " has no usable expression.";
        return false;
    }
    stack_.push_back(key);
    const TdbEvalResult result = evaluateTdbExpression(
        range->expression, temperatureK,
        [this](const std::string& inner, double t, double* out) {
            return functionValue(inner, t, out, nullptr);
        });
    stack_.pop_back();
    if (!result.ok) {
        if (error)
            *error = key + ": " + result.error;
        return false;
    }
    *value = result.value;
    return true;
}

bool TdbEvaluator::parameterValue(const TdbParameter& parameter,
                                  double temperatureK, double* value,
                                  std::string* error) const
{
    const TdbExpressionRange* range =
        selectRange(parameter.ranges, temperatureK);
    if (!range) {
        if (error)
            *error = "Parameter " + parameter.symbol + "(" + parameter.phase
                + ") carries no expression this parser could read.";
        return false;
    }
    const TdbEvalResult result = evaluateTdbExpression(
        range->expression, temperatureK,
        [this](const std::string& name, double t, double* out) {
            return functionValue(name, t, out, nullptr);
        });
    if (!result.ok) {
        if (error)
            *error = parameter.symbol + "(" + parameter.phase
                + "): " + result.error;
        return false;
    }
    *value = result.value;
    return true;
}

TdbSubstitutionalPhase tdbSubstitutionalPhase(
    const TdbDatabase& database, const std::string& phaseName,
    const std::vector<std::string>& elements, double temperatureK)
{
    TdbSubstitutionalPhase model;
    model.phase = upperCase(phaseName);
    model.temperatureK = temperatureK;

    const auto phase = std::find_if(database.phases.begin(),
                                    database.phases.end(),
                                    [&model](const TdbPhase& p) {
                                        return p.name == model.phase;
                                    });
    if (phase == database.phases.end()) {
        model.reason = "The database has no phase called " + model.phase + ".";
        return model;
    }
    if (phase->constituents.empty()) {
        model.reason = "Phase " + model.phase
            + " has no CONSTITUENT statement in this database, so its "
              "sublattice model is unknown here.";
        return model;
    }

    // --- Which sublattice mixes? -------------------------------------------
    std::vector<std::string> wanted;
    for (const std::string& element : elements)
        wanted.push_back(tdbBareElementName(element));

    int mixingIndex = -1;
    double atomsPerFormula = 0.0;
    for (std::size_t s = 0; s < phase->constituents.size(); ++s) {
        // Only constituents the caller's element set can supply are candidates:
        // an FCC_A1 declared (Ag,Au,Cu) in a database is a binary phase when
        // the system is Ag-Au.
        std::vector<std::string> available;
        for (const std::string& species : phase->constituents[s]) {
            const std::string bare = tdbBareElementName(species);
            if (bare == "VA"
                || std::find(wanted.begin(), wanted.end(), bare) != wanted.end())
                available.push_back(bare);
        }
        const double sites = s < phase->siteRatios.size()
            ? phase->siteRatios[s] : 1.0;
        // A sublattice holding only vacancies contributes no atoms and no
        // composition dependence — the interstitial sublattice of BCC_A2.
        const bool vacancyOnly =
            available.size() == 1 && available.front() == "VA";
        if (!vacancyOnly)
            atomsPerFormula += sites;
        if (available.size() <= 1)
            continue;
        if (mixingIndex >= 0) {
            model.reason = "Phase " + model.phase
                + " mixes on more than one sublattice. Modelling it needs site "
                  "fractions and an internal equilibrium, which this "
                  "pycalphad-free evaluator deliberately does not attempt.";
            return model;
        }
        mixingIndex = static_cast<int>(s);
    }
    if (atomsPerFormula <= 0.0)
        atomsPerFormula = 1.0;
    model.atomsPerFormulaUnit = atomsPerFormula;

    if (mixingIndex < 0) {
        // A stoichiometric compound, or a solution phase restricted to one
        // element. Legitimate and useful: it is a single point on the
        // composition axis.
        for (const std::string& species : phase->constituents[0]) {
            const std::string bare = tdbBareElementName(species);
            if (bare != "VA")
                model.constituents.push_back(bare);
        }
    } else {
        for (const std::string& species : phase->constituents[
                 static_cast<std::size_t>(mixingIndex)]) {
            const std::string bare = tdbBareElementName(species);
            if (bare == "VA")
                continue;
            if (std::find(wanted.begin(), wanted.end(), bare) != wanted.end())
                model.constituents.push_back(bare);
        }
    }
    if (model.constituents.empty()) {
        model.reason = "Phase " + model.phase
            + " has no constituent among the chosen elements.";
        return model;
    }
    // TDB lists constituents alphabetically and every interaction parameter is
    // written against that order; sorting here makes the parameter lookup and
    // the polynomial sign convention agree without a second convention.
    std::sort(model.constituents.begin(), model.constituents.end());

    // --- The site-fraction <-> mole-fraction map ---------------------------
    // Atoms of each mixing constituent that the NON-mixing sublattices
    // contribute. For (Re)10(Nb)4(Re,Nb)16 that is Re:10 and Nb:4, and it is
    // what makes sigma occupy a band in the middle of the diagram rather than
    // spanning it.
    model.fixedAtoms.assign(model.constituents.size(), 0.0);
    model.mixingSites = 1.0;
    for (std::size_t s = 0; s < phase->constituents.size(); ++s) {
        const double sites =
            s < phase->siteRatios.size() ? phase->siteRatios[s] : 1.0;
        if (static_cast<int>(s) == mixingIndex) {
            model.mixingSites = sites;
            continue;
        }
        // A non-mixing sublattice holds exactly one species; if it is one of
        // the mixing constituents it adds a fixed number of those atoms.
        for (const std::string& raw : phase->constituents[s]) {
            const std::string bare = tdbBareElementName(raw);
            if (bare == "VA")
                continue;
            const auto it = std::find(model.constituents.begin(),
                                      model.constituents.end(), bare);
            if (it != model.constituents.end())
                model.fixedAtoms[static_cast<std::size_t>(
                    it - model.constituents.begin())] += sites;
        }
    }
    if (model.constituents.size() > 1) {
        const double fixedSecond = model.fixedAtoms[1];
        model.minMoleFraction = fixedSecond / atomsPerFormula;
        model.maxMoleFraction =
            (fixedSecond + model.mixingSites) / atomsPerFormula;
    } else {
        // Stoichiometric in the chosen system: one composition, not a range.
        const double atoms = model.fixedAtoms.empty() ? 0.0 : model.fixedAtoms[0];
        model.minMoleFraction = atoms / atomsPerFormula;
        model.maxMoleFraction = model.minMoleFraction;
    }

    TdbEvaluator evaluator(database);
    const std::size_t n = model.constituents.size();
    model.endmemberJPerMol.assign(n, 0.0);
    model.interaction.assign(
        n, std::vector<std::vector<double>>(n, std::vector<double>()));
    std::vector<bool> haveEndmember(n, false);

    const auto indexOf = [&model](const std::string& name) -> int {
        const auto it = std::find(model.constituents.begin(),
                                  model.constituents.end(), name);
        return it == model.constituents.end()
            ? -1
            : static_cast<int>(it - model.constituents.begin());
    };

    // --- Endmember G and interaction L, from the PARAMETER list ------------
    for (const TdbParameter& parameter : database.parameters) {
        if (parameter.phase != model.phase)
            continue;
        if (parameter.symbol == "TC" || parameter.symbol == "BMAGN"
            || parameter.symbol == "NT" || parameter.symbol == "BM") {
            model.magneticIgnored = true;
            continue;
        }
        if (parameter.symbol != "G" && parameter.symbol != "L"
            && parameter.symbol != "GD")
            continue;

        // Work on the SUBLATTICE TUPLE, not the flattened species list. In a
        // multi-sublattice phase the endmember is the whole occupation —
        // G(SIGMA,RE:NB:RE) and G(SIGMA,RE:NB:NB) flatten to the same multiset
        // and are two different energies.
        //
        // Vacancy-only sublattices are dropped so that a (Cr,Fe)(Va) model
        // reduces to the one-sublattice case it physically is.
        std::vector<std::vector<std::string>> tuple;
        for (const auto& sublattice : parameter.sublattices) {
            std::vector<std::string> species;
            for (const std::string& raw : sublattice) {
                const std::string bare = tdbBareElementName(raw);
                if (bare != "VA" && !bare.empty())
                    species.push_back(bare);
            }
            if (!species.empty())
                tuple.push_back(species);
        }
        if (tuple.empty())
            continue;

        // Which position in the tuple carries more than one species: that is
        // the interacting sublattice. More than one such position is a
        // reciprocal parameter, which this model cannot represent.
        int interacting = -1;
        bool reciprocal = false;
        for (std::size_t s = 0; s < tuple.size(); ++s) {
            if (tuple[s].size() < 2)
                continue;
            if (interacting >= 0)
                reciprocal = true;
            interacting = static_cast<int>(s);
        }
        if (reciprocal || (interacting >= 0 && tuple[static_cast<std::size_t>(
                                                    interacting)].size() > 2)) {
            model.reason = "Phase " + model.phase
                + " carries a reciprocal or ternary interaction parameter, "
                  "which needs a full sublattice solver.";
            return model;
        }

        double value = 0.0;
        if (!evaluator.parameterValue(parameter, temperatureK, &value, nullptr))
            continue;
        // Per mole of ATOMS: a parameter is written for one mole of the
        // FORMULA, which for sigma holds thirty atoms.
        value /= atomsPerFormula;

        if (interacting < 0) {
            // An endmember. Its identity is the species sitting in the mixing
            // position; for a single-sublattice phase that is the only one.
            const std::size_t position = mixingIndex < 0
                ? 0
                : static_cast<std::size_t>(
                      std::min<std::size_t>(static_cast<std::size_t>(mixingIndex),
                                            tuple.size() - 1));
            const int i = indexOf(tuple[position].front());
            if (i < 0)
                continue;
            model.endmemberJPerMol[static_cast<std::size_t>(i)] = value;
            haveEndmember[static_cast<std::size_t>(i)] = true;
        } else {
            const int i = indexOf(tuple[static_cast<std::size_t>(interacting)][0]);
            const int j = indexOf(tuple[static_cast<std::size_t>(interacting)][1]);
            if (i < 0 || j < 0 || i == j)
                continue;
            const int lo = std::min(i, j);
            const int hi = std::max(i, j);
            auto& series =
                model.interaction[static_cast<std::size_t>(lo)][static_cast<std::size_t>(hi)];
            const std::size_t order =
                static_cast<std::size_t>(std::max(0, parameter.order));
            if (series.size() <= order)
                series.resize(order + 1, 0.0);
            // The database writes the pair in the order the parameter names
            // it, and the polynomial variable is (y_first − y_second). Our
            // constituents are sorted, so a parameter written the other way
            // round has its ODD orders negated — the sign trap documented on
            // redlichKisterExcess, here in its reading direction.
            const bool reversed = i > j;
            const double sign = (reversed && (order % 2 == 1)) ? -1.0 : 1.0;
            series[order] = sign * value;
        }
    }

    // REFUSE rather than return zeros. A missing endmember means the phase's
    // Gibbs energy is unknown, and an unknown energy of 0 J/mol is not a
    // conservative default — it is a phase that undercuts every real one and
    // appears stable across the whole diagram. This is the failure the sigma
    // phase of a real Nb-Re database produced before the sublattice tuples
    // above were understood.
    for (std::size_t i = 0; i < n; ++i) {
        if (!haveEndmember[i]) {
            model.reason = "Phase " + model.phase
                + " has no Gibbs-energy parameter for its "
                + model.constituents[i]
                + " endmember in this database, so its energy is unknown "
                  "rather than zero.";
            return model;
        }
    }

    model.ok = true;
    return model;
}

double TdbSubstitutionalPhase::siteFractionFor(double moleFraction) const
{
    if (constituents.size() < 2 || mixingSites <= 0.0)
        return 0.0;
    return (moleFraction * atomsPerFormulaUnit - fixedAtoms[1]) / mixingSites;
}

double TdbSubstitutionalPhase::gibbsAtMoleFraction(double moleFraction) const
{
    if (!ok || endmemberJPerMol.empty())
        return std::numeric_limits<double>::quiet_NaN();
    if (constituents.size() < 2 || endmemberJPerMol.size() < 2) {
        // Stoichiometric: defined at one composition and nowhere else.
        return std::fabs(moleFraction - minMoleFraction) < 1e-9
            ? endmemberJPerMol.front()
            : std::numeric_limits<double>::quiet_NaN();
    }
    // A tolerance of one part in 10^9 rather than an exact compare: the caller
    // samples a grid, and the phase's own limits will not land on it exactly.
    if (moleFraction < minMoleFraction - 1e-9
        || moleFraction > maxMoleFraction + 1e-9)
        return std::numeric_limits<double>::quiet_NaN();

    const double y = std::clamp(siteFractionFor(moleFraction), 0.0, 1.0);
    double value = (1.0 - y) * endmemberJPerMol[0] + y * endmemberJPerMol[1];
    // The ideal term is per SITE of the mixing sublattice, so it carries that
    // sublattice's site ratio; everything here is already per mole of atoms,
    // hence the division. For a plain substitutional phase mixingSites and
    // atomsPerFormulaUnit are both 1 and this reduces to the familiar
    // RT[y ln y + (1-y) ln(1-y)].
    value += idealMixingGibbs(y, temperatureK)
        * (mixingSites / atomsPerFormulaUnit);
    std::vector<RedlichKisterTerm> terms;
    for (const double coefficient : interaction[0][1])
        terms.push_back({coefficient, 0.0});
    value += redlichKisterExcess(terms, y, temperatureK);
    return value;
}

} // namespace calango::core
