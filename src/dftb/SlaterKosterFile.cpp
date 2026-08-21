#include "dftb/SlaterKosterFile.hpp"

#include "core/LocaleSafeNumber.hpp"

#include <cmath>
#include <fstream>
#include <sstream>

namespace calango::dftb {

namespace {

/// Split one line on whitespace and commas (some .skf files in the wild use
/// comma-separated repulsive-header fields); empty tokens are dropped.
std::vector<std::string> tokenize(const std::string& line)
{
    std::vector<std::string> tokens;
    std::string current;
    for (char ch : line) {
        if (ch == ' ' || ch == '\t' || ch == ',' || ch == '\r') {
            if (!current.empty()) {
                tokens.push_back(current);
                current.clear();
            }
        } else {
            current.push_back(ch);
        }
    }
    if (!current.empty())
        tokens.push_back(current);
    return tokens;
}

std::vector<std::string> splitLines(const std::string& text)
{
    std::vector<std::string> lines;
    std::string current;
    for (char ch : text) {
        if (ch == '\n') {
            lines.push_back(current);
            current.clear();
        } else {
            current.push_back(ch);
        }
    }
    if (!current.empty())
        lines.push_back(current);
    return lines;
}

bool parseDoubles(const std::vector<std::string>& tokens, std::size_t first,
                   std::size_t count, double* out)
{
    if (tokens.size() < first + count)
        return false;
    for (std::size_t i = 0; i < count; ++i) {
        if (!core::localeSafeParse(tokens[first + i], &out[i]))
            return false;
    }
    return true;
}

/// Local 4-point Lagrange interpolation of `values` (sampled at
/// r = gridDist, 2*gridDist, ...) at `r`. `values.size()` is the number of
/// tabulated points (nGridPoints - 1 in the .skf's own terms). Returns 0 for
/// r outside [gridDist, values.size() * gridDist].
double lagrangeInterpolate(const std::vector<double>& values, double gridDist,
                            double r, bool wantDerivative)
{
    const auto n = static_cast<std::ptrdiff_t>(values.size());
    if (n == 0 || gridDist <= 0.0)
        return 0.0;
    // Index of the grid point at or just below r (grid point i is at
    // (i + 1) * gridDist, i = 0 .. n-1, so "just below" is:
    double idxReal = r / gridDist - 1.0;
    // A bond whose TRUE distance is exactly the first (or last) tabulated
    // point can land a floating-point epsilon on the wrong side of it —
    // from an Angstrom<->bohr round trip, from a file written with limited
    // decimal precision, from any ordinary source of rounding noise, not
    // from the bond genuinely being shorter/longer than the table. Snapping
    // a THIS-tiny excursion back onto the boundary is what keeps a real
    // structure's bond at the tabulated distance from silently reading as
    // "outside the table" and returning 0 — found via a real end-to-end
    // run (a hand-written extxyz's 6-decimal-digit position was enough to
    // trigger it), not a hypothetical.
    constexpr double kBoundaryEpsilon = 1.0e-6;
    if (idxReal < 0.0 && idxReal > -kBoundaryEpsilon)
        idxReal = 0.0;
    const double upperBound = static_cast<double>(n - 1);
    if (idxReal > upperBound && idxReal < upperBound + kBoundaryEpsilon)
        idxReal = upperBound;
    if (idxReal < 0.0 || idxReal > upperBound)
        return 0.0;
    // 4-point window [i0, i0+3], clamped so it stays inside [0, n-1] near
    // either edge — the standard boundary handling for a local stencil.
    std::ptrdiff_t i0 = static_cast<std::ptrdiff_t>(std::floor(idxReal)) - 1;
    if (i0 < 0)
        i0 = 0;
    if (i0 > n - 4)
        i0 = n - 4;
    if (i0 < 0) {
        // Fewer than 4 tabulated points; fall back to however many exist.
        i0 = 0;
    }
    const std::ptrdiff_t windowSize = std::min<std::ptrdiff_t>(4, n);
    double result = 0.0;
    for (std::ptrdiff_t k = 0; k < windowSize; ++k) {
        const std::ptrdiff_t idx = i0 + k;
        const double xk = static_cast<double>(idx + 1) * gridDist;
        if (!wantDerivative) {
            double term = values[static_cast<std::size_t>(idx)];
            for (std::ptrdiff_t m = 0; m < windowSize; ++m) {
                if (m == k)
                    continue;
                const double xm = static_cast<double>(i0 + m + 1) * gridDist;
                term *= (r - xm) / (xk - xm);
            }
            result += term;
        } else {
            // d/dr of the Lagrange basis polynomial L_k(r) at r: sum over
            // each factor left out in turn (product rule on a product of
            // linear factors).
            double basisDerivative = 0.0;
            for (std::ptrdiff_t j = 0; j < windowSize; ++j) {
                if (j == k)
                    continue;
                const double xj = static_cast<double>(i0 + j + 1) * gridDist;
                double term = 1.0 / (xk - xj);
                for (std::ptrdiff_t m = 0; m < windowSize; ++m) {
                    if (m == k || m == j)
                        continue;
                    const double xm =
                        static_cast<double>(i0 + m + 1) * gridDist;
                    term *= (r - xm) / (xk - xm);
                }
                basisDerivative += term;
            }
            result += values[static_cast<std::size_t>(idx)] * basisDerivative;
        }
    }
    return result;
}

} // namespace

double SlaterKosterFile::integral(bool isOverlap, SkChannel channel,
                                   double rBohr) const
{
    const auto channelIndex = static_cast<std::size_t>(channel);
    const std::size_t column =
        channelIndex + (isOverlap ? static_cast<std::size_t>(SkChannel::kCount)
                                   : 0);
    std::vector<double> column_values;
    column_values.reserve(table.size());
    for (const auto& row : table)
        column_values.push_back(row[column]);
    return lagrangeInterpolate(column_values, gridDistanceBohr, rBohr, false);
}

double SlaterKosterFile::integralDerivative(bool isOverlap, SkChannel channel,
                                             double rBohr) const
{
    const auto channelIndex = static_cast<std::size_t>(channel);
    const std::size_t column =
        channelIndex + (isOverlap ? static_cast<std::size_t>(SkChannel::kCount)
                                   : 0);
    std::vector<double> column_values;
    column_values.reserve(table.size());
    for (const auto& row : table)
        column_values.push_back(row[column]);
    return lagrangeInterpolate(column_values, gridDistanceBohr, rBohr, true);
}

double SlaterKosterFile::repulsiveEnergyHartree(double rBohr) const
{
    if (hasSpline) {
        if (splineIntervals.empty())
            return 0.0;
        if (rBohr < splineIntervals.front().startBohr) {
            return std::exp(-splineExpA1 * rBohr + splineExpA2) + splineExpA3;
        }
        if (rBohr >= splineCutoffBohr)
            return 0.0;
        for (std::size_t i = 0; i < splineIntervals.size(); ++i) {
            const auto& interval = splineIntervals[i];
            const bool isLast = (i + 1 == splineIntervals.size());
            const double upper =
                isLast ? splineCutoffBohr : splineIntervals[i + 1].startBohr;
            if (rBohr < upper || isLast) {
                const double dr = rBohr - interval.startBohr;
                double value = interval.c[0] + interval.c[1] * dr
                    + interval.c[2] * dr * dr + interval.c[3] * dr * dr * dr;
                if (isLast)
                    value += interval.c[4] * dr * dr * dr * dr
                        + interval.c[5] * dr * dr * dr * dr * dr;
                return value;
            }
        }
        return 0.0;
    }
    if (polyRcutBohr <= 0.0 || rBohr >= polyRcutBohr)
        return 0.0;
    double value = 0.0;
    const double base = polyRcutBohr - rBohr;
    double power = base * base; // (rcut - r)^2, the first term (i = 2)
    for (std::size_t i = 0; i < polyCoefficients.size(); ++i) {
        value += polyCoefficients[i] * power;
        power *= base;
    }
    return value;
}

double SlaterKosterFile::repulsiveEnergyDerivativeHartree(double rBohr) const
{
    if (hasSpline) {
        if (splineIntervals.empty())
            return 0.0;
        if (rBohr < splineIntervals.front().startBohr)
            return -splineExpA1
                * std::exp(-splineExpA1 * rBohr + splineExpA2);
        if (rBohr >= splineCutoffBohr)
            return 0.0;
        for (std::size_t i = 0; i < splineIntervals.size(); ++i) {
            const auto& interval = splineIntervals[i];
            const bool isLast = (i + 1 == splineIntervals.size());
            const double upper =
                isLast ? splineCutoffBohr : splineIntervals[i + 1].startBohr;
            if (rBohr < upper || isLast) {
                const double dr = rBohr - interval.startBohr;
                double deriv = interval.c[1] + 2.0 * interval.c[2] * dr
                    + 3.0 * interval.c[3] * dr * dr;
                if (isLast)
                    deriv += 4.0 * interval.c[4] * dr * dr * dr
                        + 5.0 * interval.c[5] * dr * dr * dr * dr;
                return deriv;
            }
        }
        return 0.0;
    }
    if (polyRcutBohr <= 0.0 || rBohr >= polyRcutBohr)
        return 0.0;
    // E = sum_i c_i (rcut - r)^i  =>  dE/dr = -sum_i i c_i (rcut - r)^(i-1)
    double deriv = 0.0;
    const double base = polyRcutBohr - rBohr;
    double power = base; // (rcut - r)^1, the first derivative term (i = 2)
    for (std::size_t k = 0; k < polyCoefficients.size(); ++k) {
        const double i = static_cast<double>(k + 2);
        deriv += -i * polyCoefficients[k] * power;
        power *= base;
    }
    return deriv;
}

Outcome parseSlaterKosterFile(const std::string& text, SlaterKosterFile& out)
{
    out = SlaterKosterFile{};
    const std::vector<std::string> lines = splitLines(text);
    if (lines.empty())
        return Outcome::invalid("empty .skf file");

    // The extended (angular momentum up to f) format is flagged by '@' as
    // the very first character of the file — not yet supported.
    if (!lines[0].empty() && lines[0][0] == '@')
        return Outcome::invalid(
            "extended .skf format (angular momentum up to f, '@'-prefixed) "
            "is not supported yet; every mainstream light/medium-element set "
            "(mio, 3ob, pbc, matsci) ships the simple up-to-d format this "
            "parser reads — see FUTURE.md");

    std::size_t lineIndex = 0;
    const auto nextTokens = [&]() -> std::vector<std::string> {
        while (lineIndex < lines.size() && tokenize(lines[lineIndex]).empty())
            ++lineIndex; // skip genuinely blank lines
        if (lineIndex >= lines.size())
            return {};
        return tokenize(lines[lineIndex++]);
    };

    // Line 1: gridDist nGridPoints
    {
        const auto tokens = nextTokens();
        double values[2] = {0.0, 0.0};
        if (!parseDoubles(tokens, 0, 2, values))
            return Outcome::invalid(
                "line 1 must be 'gridDist nGridPoints'");
        out.gridDistanceBohr = values[0];
        out.gridPointCount = static_cast<int>(values[1] + 0.5);
    }
    if (out.gridDistanceBohr <= 0.0 || out.gridPointCount < 2)
        return Outcome::invalid(
            "gridDist must be positive and nGridPoints >= 2");

    // Line 2: either the 10-value on-site line (homonuclear) or the
    // 20-value repulsive header (heteronuclear) — the two widths are what
    // distinguishes them, per the format spec (section 2.1.1 vs 2.1.3).
    std::array<double, 20> repulsiveHeader{};
    {
        const auto tokens = nextTokens();
        if (tokens.size() == 10) {
            out.homonuclear = true;
            double values[10] = {};
            if (!parseDoubles(tokens, 0, 10, values))
                return Outcome::invalid("malformed on-site line");
            // Ed Ep Es SPE Ud Up Us fd fp fs
            out.onsite[2] = {values[0], values[4], values[7]}; // d
            out.onsite[1] = {values[1], values[5], values[8]}; // p
            out.onsite[0] = {values[2], values[6], values[9]}; // s
            // values[3] (SPE) is read but not interpreted, per spec.

            const auto headerTokens = nextTokens();
            if (headerTokens.size() != 20)
                return Outcome::invalid(
                    "homonuclear repulsive header must have 20 fields "
                    "(mass c2..c9 rcut d1..d10)");
            if (!parseDoubles(headerTokens, 0, 20, repulsiveHeader.data()))
                return Outcome::invalid(
                    "malformed homonuclear repulsive header");
        } else if (tokens.size() == 20) {
            out.homonuclear = false;
            if (!parseDoubles(tokens, 0, 20, repulsiveHeader.data()))
                return Outcome::invalid(
                    "malformed heteronuclear repulsive header");
        } else {
            std::ostringstream msg;
            msg << "line 2 must be either the 10-field on-site line "
                   "(homonuclear) or the 20-field repulsive header "
                   "(heteronuclear); found "
                << tokens.size() << " field(s)";
            return Outcome::invalid(msg.str());
        }
    }
    out.massAmu = repulsiveHeader[0];
    for (std::size_t i = 0; i < 8; ++i)
        out.polyCoefficients[i] = repulsiveHeader[1 + i];
    out.polyRcutBohr = repulsiveHeader[9];
    // repulsiveHeader[10..19] (d1..d10) are inert placeholders, per spec.

    // The integral table: nGridPoints - 1 rows of 20 values each.
    const int rowCount = out.gridPointCount - 1;
    out.table.reserve(static_cast<std::size_t>(rowCount));
    for (int row = 0; row < rowCount; ++row) {
        const auto tokens = nextTokens();
        if (tokens.size() != 20) {
            std::ostringstream msg;
            msg << "integral table row " << row << " must have 20 fields "
                   "(10 H + 10 S); found " << tokens.size();
            return Outcome::invalid(msg.str());
        }
        std::array<double, 20> values{};
        if (!parseDoubles(tokens, 0, 20, values.data()))
            return Outcome::invalid("malformed integral table row");
        out.table.push_back(values);
    }

    // Optional spline repulsive block.
    {
        // Peek without consuming, so a file with no Spline block (and
        // nothing else) leaves lineIndex untouched.
        std::size_t peek = lineIndex;
        while (peek < lines.size() && tokenize(lines[peek]).empty())
            ++peek;
        if (peek < lines.size()) {
            const auto peekTokens = tokenize(lines[peek]);
            if (peekTokens.size() == 1 && peekTokens[0] == "Spline") {
                lineIndex = peek + 1;
                out.hasSpline = true;

                const auto nIntTokens = nextTokens();
                double nIntCutoff[2] = {0.0, 0.0};
                if (!parseDoubles(nIntTokens, 0, 2, nIntCutoff))
                    return Outcome::invalid(
                        "Spline block: malformed 'nInt cutoff' line");
                const int nInt = static_cast<int>(nIntCutoff[0] + 0.5);
                out.splineCutoffBohr = nIntCutoff[1];
                if (nInt < 1)
                    return Outcome::invalid(
                        "Spline block: nInt must be >= 1");

                const auto expTokens = nextTokens();
                double expValues[3] = {0.0, 0.0, 0.0};
                if (!parseDoubles(expTokens, 0, 3, expValues))
                    return Outcome::invalid(
                        "Spline block: malformed exponential 'a1 a2 a3' "
                        "line");
                out.splineExpA1 = expValues[0];
                out.splineExpA2 = expValues[1];
                out.splineExpA3 = expValues[2];

                out.splineIntervals.reserve(static_cast<std::size_t>(nInt));
                for (int interval = 0; interval < nInt; ++interval) {
                    const bool isLast = (interval + 1 == nInt);
                    const auto tokens = nextTokens();
                    const std::size_t expected = isLast ? 8 : 6;
                    if (tokens.size() != expected) {
                        std::ostringstream msg;
                        msg << "Spline interval " << interval
                            << " must have " << expected << " field(s)";
                        return Outcome::invalid(msg.str());
                    }
                    RepulsiveSplineInterval iv;
                    double values[8] = {};
                    if (!parseDoubles(tokens, 0, expected, values))
                        return Outcome::invalid(
                            "malformed spline interval line");
                    iv.startBohr = values[0];
                    iv.endBohr = values[1];
                    for (std::size_t c = 0; c < expected - 2; ++c)
                        iv.c[c] = values[2 + c];
                    out.splineIntervals.push_back(iv);
                }
            }
        }
    }

    return Outcome::success();
}

Outcome loadSlaterKosterFile(const std::string& path, SlaterKosterFile& out)
{
    std::ifstream file(path, std::ios::binary);
    if (!file)
        return Outcome::invalid("cannot open " + path);
    std::ostringstream buffer;
    buffer << file.rdbuf();
    return parseSlaterKosterFile(buffer.str(), out);
}

} // namespace calango::dftb
