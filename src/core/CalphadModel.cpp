#include "core/CalphadModel.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace calango::core {

namespace {

/// Solve A·x = b in place by Gaussian elimination with partial pivoting.
///
/// Returns false when the matrix is numerically singular, which for a
/// Redlich-Kister fit is not an exotic failure but the ordinary consequence of
/// asking for more terms than the data can support (four coefficients from
/// three compositions) or for a temperature-dependent fit from samples at one
/// temperature. The caller turns that into a sentence; silently returning the
/// result of dividing by a 10^-18 pivot would produce coefficients of 10^18
/// and a phase diagram that is empty for reasons nobody could diagnose.
bool solveInPlace(std::vector<std::vector<double>>& a, std::vector<double>& b)
{
    const std::size_t n = b.size();
    // Scale-relative singularity threshold: the columns are normalized before
    // this is called, so the entries are O(1) and an absolute epsilon is
    // meaningful.
    constexpr double kPivotFloor = 1e-12;
    for (std::size_t col = 0; col < n; ++col) {
        std::size_t pivot = col;
        for (std::size_t row = col + 1; row < n; ++row)
            if (std::fabs(a[row][col]) > std::fabs(a[pivot][col]))
                pivot = row;
        if (std::fabs(a[pivot][col]) < kPivotFloor)
            return false;
        std::swap(a[col], a[pivot]);
        std::swap(b[col], b[pivot]);
        for (std::size_t row = col + 1; row < n; ++row) {
            const double factor = a[row][col] / a[col][col];
            if (factor == 0.0)
                continue;
            for (std::size_t k = col; k < n; ++k)
                a[row][k] -= factor * a[col][k];
            b[row] -= factor * b[col];
        }
    }
    for (std::size_t i = n; i-- > 0;) {
        double sum = b[i];
        for (std::size_t k = i + 1; k < n; ++k)
            sum -= a[i][k] * b[k];
        b[i] = sum / a[i][i];
    }
    return true;
}

} // namespace

double redlichKisterExcess(const std::vector<RedlichKisterTerm>& terms,
                           double moleFractionB, double temperatureK)
{
    const double xB = moleFractionB;
    const double xA = 1.0 - xB;
    // (x_A − x_B), the polynomial variable. See the header on why the order of
    // the subtraction is not a free choice.
    const double v = xA - xB;
    double sum = 0.0;
    double power = 1.0;
    for (const RedlichKisterTerm& term : terms) {
        sum += term.at(temperatureK) * power;
        power *= v;
    }
    return xA * xB * sum;
}

double idealMixingGibbs(double moleFractionB, double temperatureK)
{
    const double xB = std::clamp(moleFractionB, 0.0, 1.0);
    const double xA = 1.0 - xB;
    // x·ln x → 0 as x → 0. Evaluating it there would be a NaN from 0·(−inf),
    // and the endpoints of a phase diagram are sampled exactly, not nearly.
    const double a = xA > 0.0 ? xA * std::log(xA) : 0.0;
    const double b = xB > 0.0 ? xB * std::log(xB) : 0.0;
    return kGasConstantJPerMolK * temperatureK * (a + b);
}

double binarySolutionGibbs(double gibbsAJPerMol, double gibbsBJPerMol,
                           const std::vector<RedlichKisterTerm>& terms,
                           double moleFractionB, double temperatureK)
{
    const double xB = std::clamp(moleFractionB, 0.0, 1.0);
    const double xA = 1.0 - xB;
    return xA * gibbsAJPerMol + xB * gibbsBJPerMol
        + idealMixingGibbs(xB, temperatureK)
        + redlichKisterExcess(terms, xB, temperatureK);
}

RedlichKisterFit fitRedlichKister(const std::vector<RedlichKisterSample>& samples,
                                  int order, bool temperatureDependent)
{
    RedlichKisterFit fit;
    if (order < 0) {
        fit.note = "A Redlich-Kister order below zero is not a model.";
        return fit;
    }
    const int termCount = order + 1;
    const int columns = temperatureDependent ? 2 * termCount : termCount;

    // Endpoints carry an identically-zero row (every basis function has the
    // factor x_A·x_B) and are dropped rather than fitted — see the header.
    std::vector<RedlichKisterSample> used;
    used.reserve(samples.size());
    bool sawEndpoint = false;
    for (const RedlichKisterSample& sample : samples) {
        if (!std::isfinite(sample.moleFractionB)
            || !std::isfinite(sample.excessJPerMol)
            || !std::isfinite(sample.temperatureK) || sample.weight <= 0.0)
            continue;
        if (sample.moleFractionB <= 0.0 || sample.moleFractionB >= 1.0) {
            sawEndpoint = true;
            continue;
        }
        used.push_back(sample);
    }
    fit.usedSamples = static_cast<int>(used.size());
    if (static_cast<int>(used.size()) < columns) {
        fit.note = "Not enough interior compositions: " + std::to_string(used.size())
            + " usable sample(s) cannot determine " + std::to_string(columns)
            + " coefficient(s). Lower the Redlich-Kister order, add "
              "configurations, or turn off the temperature dependence.";
        return fit;
    }
    if (temperatureDependent) {
        // a and b·T are the same column when T never varies; the normal matrix
        // is then exactly singular and the pivoting would report an arbitrary
        // split of one coefficient between the two.
        const double first = used.front().temperatureK;
        const bool varies = std::any_of(
            used.begin(), used.end(), [first](const RedlichKisterSample& s) {
                return std::fabs(s.temperatureK - first) > 1e-9;
            });
        if (!varies) {
            fit.note = "A temperature-dependent fit needs samples at more than "
                       "one temperature: a + b·T is one column when T is "
                       "constant, and the split between them would be "
                       "arbitrary.";
            return fit;
        }
    }

    // --- Design matrix ----------------------------------------------------
    const std::size_t rows = used.size();
    std::vector<std::vector<double>> design(rows,
                                            std::vector<double>(columns, 0.0));
    std::vector<double> target(rows, 0.0);
    std::vector<double> rowWeight(rows, 1.0);
    for (std::size_t r = 0; r < rows; ++r) {
        const RedlichKisterSample& s = used[r];
        const double xB = s.moleFractionB;
        const double xA = 1.0 - xB;
        const double v = xA - xB;
        double power = 1.0;
        for (int nu = 0; nu < termCount; ++nu) {
            const double basis = xA * xB * power;
            design[r][temperatureDependent ? 2 * nu : nu] = basis;
            if (temperatureDependent)
                design[r][2 * nu + 1] = basis * s.temperatureK;
            power *= v;
        }
        target[r] = s.excessJPerMol;
        rowWeight[r] = std::sqrt(s.weight);
    }

    // Column normalization. The T-dependent columns are ~10^3 times the
    // others purely because temperature is quoted in kelvin, and the normal
    // matrix squares that into a 10^6 condition penalty paid for nothing.
    std::vector<double> scale(columns, 1.0);
    for (int c = 0; c < columns; ++c) {
        double norm = 0.0;
        for (std::size_t r = 0; r < rows; ++r) {
            const double e = design[r][c] * rowWeight[r];
            norm += e * e;
        }
        norm = std::sqrt(norm);
        scale[c] = norm > 0.0 ? norm : 1.0;
    }

    // --- Normal equations, on the weighted and scaled system ---------------
    std::vector<std::vector<double>> normal(columns,
                                            std::vector<double>(columns, 0.0));
    std::vector<double> rhs(columns, 0.0);
    for (std::size_t r = 0; r < rows; ++r) {
        for (int i = 0; i < columns; ++i) {
            const double ai = design[r][i] * rowWeight[r] / scale[i];
            for (int j = 0; j < columns; ++j)
                normal[i][j] += ai * design[r][j] * rowWeight[r] / scale[j];
            rhs[i] += ai * target[r] * rowWeight[r];
        }
    }
    if (!solveInPlace(normal, rhs)) {
        fit.note = "The fit is rank-deficient: the requested Redlich-Kister "
                   "terms are not independent over the compositions supplied. "
                   "Lower the order.";
        return fit;
    }

    fit.terms.assign(static_cast<std::size_t>(termCount), RedlichKisterTerm{});
    for (int nu = 0; nu < termCount; ++nu) {
        if (temperatureDependent) {
            fit.terms[static_cast<std::size_t>(nu)].a =
                rhs[2 * nu] / scale[2 * nu];
            fit.terms[static_cast<std::size_t>(nu)].b =
                rhs[2 * nu + 1] / scale[2 * nu + 1];
        } else {
            fit.terms[static_cast<std::size_t>(nu)].a = rhs[nu] / scale[nu];
        }
    }

    // --- Residuals, on the ORIGINAL samples (unweighted) -------------------
    double sumSquares = 0.0;
    for (const RedlichKisterSample& s : used) {
        const double predicted =
            redlichKisterExcess(fit.terms, s.moleFractionB, s.temperatureK);
        const double residual = predicted - s.excessJPerMol;
        sumSquares += residual * residual;
        fit.maxResidualJPerMol =
            std::max(fit.maxResidualJPerMol, std::fabs(residual));
    }
    fit.rmsResidualJPerMol = std::sqrt(sumSquares / static_cast<double>(rows));
    fit.ok = true;
    if (sawEndpoint)
        fit.note = "Samples at x = 0 and x = 1 were excluded: every "
                   "Redlich-Kister basis function vanishes there, so they "
                   "cannot constrain a coefficient and would only flatter the "
                   "residual statistics.";
    return fit;
}

CalphadAssessment assessBinaryFromFirstPrinciples(
    const CalphadAssessmentInput& input)
{
    CalphadAssessment assessment;
    if (input.configurations.empty()) {
        assessment.note = "No configurations were supplied. An assessment "
                          "needs first-principles energies at intermediate "
                          "compositions; the two endpoints alone determine "
                          "nothing but the reference.";
        return assessment;
    }
    std::vector<double> temperatures = input.temperaturesK;
    if (temperatures.empty())
        temperatures.push_back(298.15);

    // --- Is there vibrational data, and is it complete? --------------------
    // All-or-nothing on purpose. Mixing configurations that carry F_vib with
    // ones that do not would put a temperature-dependent excess energy on some
    // compositions and a static one on others, and the fit would read the
    // difference as real physics.
    const std::size_t grid = temperatures.size();
    const auto hasGrid = [grid](const std::vector<double>& v) {
        return v.size() == grid;
    };
    bool vibrational = hasGrid(input.referenceVibAEvPerAtom)
        && hasGrid(input.referenceVibBEvPerAtom);
    if (vibrational) {
        for (const CalphadConfiguration& config : input.configurations) {
            if (!hasGrid(config.vibFreeEnergyEvPerAtom)) {
                vibrational = false;
                break;
            }
        }
    }
    assessment.vibrational = vibrational;

    // --- The static hull, before any entropy -------------------------------
    std::vector<HullPoint> hullPoints;
    hullPoints.reserve(input.configurations.size() + 2);
    const auto addHullPoint = [&](const std::string& label, double x,
                                  double energyPerAtom, int frame) {
        HullPoint point;
        point.concentration = x;
        point.energyPerAtom = energyPerAtom;
        point.formationEnergy = formationEnergyPerAtom(
            energyPerAtom, x, input.referenceEnergyAEvPerAtom,
            input.referenceEnergyBEvPerAtom);
        point.label = label;
        point.frameIndex = frame;
        hullPoints.push_back(point);
    };
    // The endpoints are added explicitly. Without them the hull is built over
    // whatever composition range happened to be sampled and its endpoints are
    // not at zero formation energy, which makes "on the hull" mean something
    // different from what every published hull diagram means by it.
    addHullPoint(input.elementA, 0.0, input.referenceEnergyAEvPerAtom, -1);
    addHullPoint(input.elementB, 1.0, input.referenceEnergyBEvPerAtom, -1);
    for (std::size_t i = 0; i < input.configurations.size(); ++i) {
        const CalphadConfiguration& config = input.configurations[i];
        addHullPoint(config.label, config.moleFractionB, config.energyEvPerAtom,
                     static_cast<int>(i));
    }
    assessment.staticHull = computeConvexHull(std::move(hullPoints));

    // --- Excess samples ----------------------------------------------------
    for (const CalphadConfiguration& config : input.configurations) {
        const double x = config.moleFractionB;
        if (!std::isfinite(x) || x <= 0.0 || x >= 1.0)
            continue;
        const double deltaE0 = formationEnergyPerAtom(
            config.energyEvPerAtom, x, input.referenceEnergyAEvPerAtom,
            input.referenceEnergyBEvPerAtom);
        for (std::size_t t = 0; t < grid; ++t) {
            const double temperature = temperatures[t];
            double deltaEv = deltaE0;
            if (vibrational) {
                // The SAME endpoint-referenced difference as the static term,
                // taken on the vibrational free energy. Referencing F_vib to
                // the endpoints is what makes it an EXCESS quantity; using the
                // absolute F_vib would add the (large) endpoint zero-point
                // energies into every interaction parameter.
                const double referenceVib =
                    (1.0 - x) * input.referenceVibAEvPerAtom[t]
                    + x * input.referenceVibBEvPerAtom[t];
                deltaEv += config.vibFreeEnergyEvPerAtom[t] - referenceVib;
            }
            RedlichKisterSample sample;
            sample.moleFractionB = x;
            sample.temperatureK = temperature;
            // THE EXCESS IS THE WHOLE DFT MIXING ENERGY, with NO ideal term
            // removed. This is the step it is natural to get wrong, and the
            // error is invisible in the numbers:
            //
            // A DFT total energy is the energy of ONE atomic arrangement. It
            // contains no configurational entropy at all — an SQS is a single
            // microstate whose energy approximates the average over the
            // disordered ensemble, not its free energy. The −T·S_config term
            // is supplied by the CALPHAD model, in idealMixingGibbs, and
            // G_ex is by definition what is left over ON TOP of it.
            //
            // Subtracting the ideal term here (which looks like the symmetric
            // counterpart of "remove the ideal part before fitting the excess"
            // and is how the quantity is treated when the INPUT is an
            // experimental free energy) would cancel it against the model's
            // own, leaving a solution phase with zero configurational entropy:
            // no terminal solubility, no lens, and a miscibility gap that
            // never closes. It also invents a spurious b·T coefficient, since
            // the subtracted term is linear in T.
            sample.excessJPerMol = deltaEv * kEvPerAtomToJPerMol;
            assessment.samples.push_back(sample);
        }
    }

    // A static assessment has no temperature dependence to find: every
    // temperature yields the SAME excess sample, so asking for b would be the
    // constant-T rank deficiency in disguise. Refusing here rather than in the
    // solver lets the note name the actual cause.
    const bool wantTemperature = input.temperatureDependent && vibrational;
    assessment.fit =
        fitRedlichKister(assessment.samples, input.order, wantTemperature);
    assessment.ok = assessment.fit.ok;
    assessment.note = assessment.fit.note;
    if (assessment.ok && !vibrational) {
        assessment.note =
            "Static assessment: no vibrational free energy reached this "
            "system, so the excess Gibbs energy is a pure enthalpy and every "
            "excess entropy is exactly zero. The solidus and liquidus of a "
            "diagram built from it will be wrong by whatever the vibrational "
            "entropy of mixing is — typically a few tenths of k_B per atom.";
    }
    return assessment;
}

} // namespace calango::core
