#include "core/StructureFactor.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace calango::core {

namespace {

/// Number density matching the RDF's normalization volume.
double numberDensity(const Structure& structure, bool usePbc)
{
    if (structure.empty())
        return 0.0;
    if (usePbc && structure.cell().isDefined())
        return static_cast<double>(structure.size()) / structure.cell().volume();

    Vec3 lo{std::numeric_limits<double>::max(), std::numeric_limits<double>::max(),
            std::numeric_limits<double>::max()};
    Vec3 hi{-lo.x, -lo.y, -lo.z};
    for (const Atom& atom : structure.atoms()) {
        lo = {std::min(lo.x, atom.position.x), std::min(lo.y, atom.position.y),
              std::min(lo.z, atom.position.z)};
        hi = {std::max(hi.x, atom.position.x), std::max(hi.y, atom.position.y),
              std::max(hi.z, atom.position.z)};
    }
    const double volume =
        (hi.x - lo.x + 2.0) * (hi.y - lo.y + 2.0) * (hi.z - lo.z + 2.0);
    return static_cast<double>(structure.size()) / volume;
}

StructureFactorResult transform(const RdfResult& rdf, double density,
                                const StructureFactorOptions& options)
{
    StructureFactorResult result;
    if (rdf.r.size() < 2 || density <= 0.0 || options.qPoints < 2)
        return result;

    const double rMax = options.rdf.rMax;
    result.q.resize(static_cast<std::size_t>(options.qPoints));
    result.s.resize(static_cast<std::size_t>(options.qPoints));

    for (int qi = 0; qi < options.qPoints; ++qi) {
        const double q = options.qMin
            + (options.qMax - options.qMin) * qi
                / static_cast<double>(options.qPoints - 1);
        // Trapezoidal quadrature of [g(r)-1] r² sinc(qr), Lorch-windowed
        // to suppress truncation ripples from the finite rMax.
        double integral = 0.0;
        double previous = 0.0;
        double previousR = 0.0;
        for (std::size_t k = 0; k < rdf.r.size(); ++k) {
            const double r = rdf.r[k];
            const double qr = q * r;
            const double sinc = qr > 1e-12 ? std::sin(qr) / qr : 1.0;
            const double x = M_PI * r / rMax; // Lorch window argument
            const double window = x > 1e-12 ? std::sin(x) / x : 1.0;
            const double value = (rdf.g[k] - 1.0) * r * r * sinc * window;
            integral += 0.5 * (previous + value) * (r - previousR);
            previous = value;
            previousR = r;
        }
        result.q[static_cast<std::size_t>(qi)] = q;
        result.s[static_cast<std::size_t>(qi)] = 1.0 + 4.0 * M_PI * density * integral;
    }
    return result;
}

} // namespace

StructureFactorResult
computeStructureFactorAveraged(const std::vector<Structure>& frames,
                               const StructureFactorOptions& options)
{
    if (frames.empty())
        return {};
    const RdfResult rdf = computeRdfAveraged(frames, options.rdf);
    double density = 0.0;
    for (const Structure& frame : frames)
        density += numberDensity(frame, options.rdf.usePbc);
    density /= static_cast<double>(frames.size());
    return transform(rdf, density, options);
}

} // namespace calango::core
