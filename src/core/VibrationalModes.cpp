#include "core/VibrationalModes.hpp"

#include "core/Element.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace calango::core {

namespace {

/// cm⁻¹ → meV.
constexpr double kCmToMev = 0.1239841984;
/// cm⁻¹ → THz.
constexpr double kCmToThz = 0.0299792458;
/// cm⁻¹ → Hz (c in cm/s), for the angular frequency the restoring force and
/// the velocities need.
constexpr double kCmToHz = 2.99792458e10;
/// amu·Å·(rad/s)² → eV/Å: m_u·Å²/eV, so that
/// F[eV/Å] = M[u]·ω²[s⁻²]·u[Å] × this.
constexpr double kForceUnit = 1.66053907e-27 * 1e-20 / 1.602176634e-19;
/// Å/s → ASE's velocity unit, which is Å per ASE time unit.
///
/// ASE works in eV, Å and u, which fixes its time unit at Å·√(u/eV) =
/// 1.0180505671e-14 s (≈ 10.18 fs) — so a velocity in Å/s is multiplied by that
/// many seconds to express it per ASE time unit. Written out rather than left
/// in Å/fs because ase.Atoms.set_velocities() is what an export goes through,
/// and a velocity in the wrong unit is not an error anywhere: it round-trips,
/// plots, and integrates — as a trajectory ten times too fast.
///
/// Note that kVelocityUnit² and kForceUnit are the same number (t₀² = m_u·Å²/eV
/// is the identity behind both). The test relies on that: if either constant
/// were mistyped, the mode's kinetic + potential energy would stop being
/// conserved over a period, which nothing about the animation would reveal.
constexpr double kVelocityUnit = 1.0180505671156725e-14;
/// A mass this small means the element table had no entry (out-of-range Z), and
/// dividing by its square root would explode the displacement pattern. Treated
/// as "leave this atom's component alone" instead.
constexpr double kMinMassAmu = 1e-6;

/// The mass-weighted complex eigenvector of one branch, w_i = √M_i·u_i, in the
/// metric where the dynamical matrix's eigenvectors are orthonormal.
///
/// Every diagnostic works here rather than on the raw file components, so a
/// mode set exported in either convention is checked against the same physics.
struct MassWeighted {
    std::vector<Vec3> real;
    std::vector<Vec3> imag;
};

std::vector<double> massesOf(const Structure& structure)
{
    std::vector<double> masses(structure.size(), 0.0);
    for (std::size_t i = 0; i < structure.size(); ++i)
        masses[i] = Elements::atomicMass(structure.atoms()[i].atomicNumber);
    return masses;
}

/// The imaginary part of a branch, or an empty stand-in. A run that exported
/// real eigenvectors (Γ, or the ASE driver) writes no "im" at all, so the two
/// arrays are not guaranteed to be the same length here even though everything
/// downstream indexes them alike.
const std::vector<Vec3>& imagPartOf(const VibrationalQPoint& qpoint,
                                    std::size_t branch)
{
    static const std::vector<Vec3> kNone;
    return branch < qpoint.eigenvectorsImag.size()
        ? qpoint.eigenvectorsImag[branch]
        : kNone;
}

/// Rescale one branch into the mass-weighted metric, whatever the file used.
MassWeighted toMassWeighted(const VibrationalQPoint& qpoint, std::size_t branch,
                            const std::vector<double>& masses,
                            EigenvectorConvention convention)
{
    MassWeighted out;
    if (branch >= qpoint.eigenvectorsReal.size())
        return out;
    const std::vector<Vec3>& real = qpoint.eigenvectorsReal[branch];
    const std::vector<Vec3>& imag = imagPartOf(qpoint, branch);
    const std::size_t count = std::min(real.size(), masses.size());
    out.real.resize(count);
    out.imag.resize(count);
    for (std::size_t i = 0; i < count; ++i) {
        const double mass = masses[i];
        const double scale =
            convention == EigenvectorConvention::MassWeighted || mass < kMinMassAmu
            ? 1.0
            : std::sqrt(mass);
        out.real[i] = real[i] * scale;
        out.imag[i] = (i < imag.size() ? imag[i] : Vec3{}) * scale;
    }
    return out;
}

/// The complex displacement pattern u_i of one branch, in whatever arbitrary
/// units the file's normalization implies (the caller rescales).
MassWeighted toDisplacements(const VibrationalQPoint& qpoint, std::size_t branch,
                             const std::vector<double>& masses,
                             EigenvectorConvention convention)
{
    MassWeighted out;
    if (branch >= qpoint.eigenvectorsReal.size())
        return out;
    const std::vector<Vec3>& real = qpoint.eigenvectorsReal[branch];
    const std::vector<Vec3>& imag = imagPartOf(qpoint, branch);
    const std::size_t count = std::min(real.size(), masses.size());
    out.real.resize(count);
    out.imag.resize(count);
    for (std::size_t i = 0; i < count; ++i) {
        const double mass = masses[i];
        // u = w/√M for a mass-weighted export. An unknown mass (Z outside the
        // table) would otherwise divide by zero; leaving that atom's component
        // unscaled keeps the rest of the pattern usable.
        const double scale =
            convention == EigenvectorConvention::MassWeighted && mass >= kMinMassAmu
            ? 1.0 / std::sqrt(mass)
            : 1.0;
        out.real[i] = real[i] * scale;
        out.imag[i] = (i < imag.size() ? imag[i] : Vec3{}) * scale;
    }
    return out;
}

/// Largest per-atom excursion of a complex pattern, √(|Re|² + |Im|²).
///
/// The true maximum of |u_i(t)| over a period is the semi-major axis of the
/// ellipse the atom traces, which this bounds from above (and equals for a real
/// eigenvector, i.e. at Γ). Close enough for a visualization scale, and cheap;
/// the alternative is diagonalizing a 2×2 per atom per frame.
double peakExcursion(const MassWeighted& pattern)
{
    double peak = 0.0;
    for (std::size_t i = 0; i < pattern.real.size(); ++i) {
        const double re2 = pattern.real[i].dot(pattern.real[i]);
        const double im2 = i < pattern.imag.size()
            ? pattern.imag[i].dot(pattern.imag[i])
            : 0.0;
        peak = std::max(peak, std::sqrt(re2 + im2));
    }
    return peak;
}

} // namespace

double wavenumberToMev(double cm) { return cm * kCmToMev; }
double wavenumberToThz(double cm) { return cm * kCmToThz; }

std::shared_ptr<Structure> displaceByMode(const Structure& reference,
                                          const VibrationalModeSet& modes,
                                          std::size_t qIndex, std::size_t branch,
                                          const ModeDisplacement& options)
{
    if (qIndex >= modes.qpoints.size() || reference.empty())
        return nullptr;
    const VibrationalQPoint& qpoint = modes.qpoints[qIndex];
    if (branch >= qpoint.eigenvectorsReal.size()
        || branch >= qpoint.frequenciesCm.size())
        return nullptr;

    const std::vector<double> masses = massesOf(reference);
    const MassWeighted pattern =
        toDisplacements(qpoint, branch, masses, modes.convention);
    const double peak = peakExcursion(pattern);
    if (peak <= 0.0)
        return nullptr;
    const double scale = options.amplitudeAng / peak;

    auto displaced = std::make_shared<Structure>(reference);
    std::vector<Atom>& atoms = displaced->atoms();
    const bool periodic = displaced->cell().isDefined();

    // ω in rad/s from the frequency in cm⁻¹. |ω| because an imaginary mode is
    // stored as a negative frequency: it still has a displacement pattern worth
    // looking at (that is how a soft mode is identified), and the restoring
    // "force" it is drawn with is then the magnitude of an unstable one.
    const double frequencyCm = qpoint.frequenciesCm[branch];
    const double omega = 2.0 * M_PI * std::abs(frequencyCm) * kCmToHz;

    std::vector<Vec3> displacements(atoms.size());
    std::vector<Vec3> velocities(atoms.size());
    for (std::size_t i = 0; i < atoms.size() && i < pattern.real.size(); ++i) {
        // u_α(t) = Re[ e_α(q) · exp(i(q·R_α − ωt)) ]. The q·R phase is what
        // makes a zone-boundary mode show neighbouring cells in antiphase
        // instead of every cell moving identically; dropping it would render
        // every q as if it were Γ.
        double qr = 0.0;
        if (periodic) {
            const Vec3 fractional =
                displaced->cell().cartesianToFractional(atoms[i].position);
            qr = 2.0 * M_PI
                * (qpoint.q[0] * fractional.x + qpoint.q[1] * fractional.y
                   + qpoint.q[2] * fractional.z);
        }
        const double angle = qr - options.phase;
        const double c = std::cos(angle);
        const double sn = std::sin(angle);
        const Vec3& re = pattern.real[i];
        const Vec3& im = i < pattern.imag.size() ? pattern.imag[i] : Vec3{};
        // Re[(re + i·im)(c + i·s)] = re·c − im·s
        const Vec3 u = (re * c - im * sn) * scale;
        displacements[i] = u;
        atoms[i].position = atoms[i].position + u;

        // v = du/dt, differentiated in closed form rather than by differencing
        // consecutive frames. With angle = q·R − ωt,
        //     du/dt = du/dangle · (−ω) = A·ω·(re·sin(angle) + im·cos(angle)),
        // which is exact at every phase; a finite difference over the sampled
        // frames would be wrong by ~cos(π/frames) and would have no value at
        // all for the last frame.
        //
        // Note the quarter-period offset this puts between u and v: the atoms
        // are fastest as they pass through their equilibrium positions and
        // momentarily at rest at the turning points. That is the check to make
        // if these ever look wrong — velocities in phase with the displacement
        // mean a sign or a sin/cos has been swapped.
        const double speed = scale * omega * kVelocityUnit;
        velocities[i] = (re * sn + im * c) * speed;
    }

    if (options.withDynamics) {
        // Velocities travel as their own field. AseBridge routes a field named
        // "velocities" through ase.Atoms.set_velocities(), which stores it as
        // momenta — so the extended-XYZ writer emits a momenta column that a
        // reader converts back to the same velocities, rather than an opaque
        // extra column nothing interprets.
        displaced->setVectorField("velocities", velocities);

        // Harmonic restoring force F_α = −M_α ω² u_α. Converted from
        // u·amu·rad²/s² to eV/Å so it lands in the same units as every other
        // force in the app — the Vector Overlay scale is calibrated for those,
        // and a raw SI magnitude would render as an invisible or absurd arrow.
        std::vector<Vec3> forces(atoms.size());
        for (std::size_t i = 0; i < atoms.size(); ++i) {
            const double factor = -masses[i] * omega * omega * kForceUnit;
            forces[i] = displacements[i] * factor;
        }
        displaced->setVectorField("forces", std::move(forces));
    }
    return displaced;
}

std::vector<std::shared_ptr<Structure>> modeTrajectory(
    const Structure& reference, const VibrationalModeSet& modes,
    std::size_t qIndex, std::size_t branch, const ModeDisplacement& options,
    int frames)
{
    std::vector<std::shared_ptr<Structure>> out;
    if (frames <= 0)
        return out;
    out.reserve(static_cast<std::size_t>(frames));
    ModeDisplacement frameOptions = options;
    frameOptions.withDynamics = true;
    for (int i = 0; i < frames; ++i) {
        frameOptions.phase = 2.0 * M_PI * static_cast<double>(i) / frames;
        if (auto frame =
                displaceByMode(reference, modes, qIndex, branch, frameOptions))
            out.push_back(std::move(frame));
    }
    return out;
}

std::vector<ModeDiagnostics> analyzeModes(const Structure& reference,
                                          const VibrationalQPoint& qpoint,
                                          EigenvectorConvention convention,
                                          double degeneracyToleranceCm)
{
    std::vector<ModeDiagnostics> out;
    if (!qpoint.hasEigenvectors() || reference.empty())
        return out;

    const std::vector<double> masses = massesOf(reference);
    double totalMass = 0.0;
    for (double mass : masses)
        totalMass += mass;

    const std::size_t branches = qpoint.eigenvectorsReal.size();
    std::vector<MassWeighted> weighted(branches);
    std::vector<double> norms(branches, 0.0);
    for (std::size_t b = 0; b < branches; ++b) {
        weighted[b] = toMassWeighted(qpoint, b, masses, convention);
        double sum = 0.0;
        for (std::size_t i = 0; i < weighted[b].real.size(); ++i) {
            sum += weighted[b].real[i].dot(weighted[b].real[i]);
            if (i < weighted[b].imag.size())
                sum += weighted[b].imag[i].dot(weighted[b].imag[i]);
        }
        norms[b] = std::sqrt(sum);
    }

    out.resize(branches);
    for (std::size_t b = 0; b < branches; ++b) {
        ModeDiagnostics& diagnostics = out[b];
        diagnostics.norm = norms[b];

        // Overlaps against every other branch, as |⟨w_b|w_c⟩| between unit
        // vectors. Hermitian inner product: the imaginary parts contribute
        // through Re[w_b* · w_c] = re·re + im·im, and the |·| of the full
        // complex overlap needs the cross terms too.
        for (std::size_t c = 0; c < branches; ++c) {
            if (c == b || norms[b] <= 0.0 || norms[c] <= 0.0)
                continue;
            double overlapRe = 0.0;
            double overlapIm = 0.0;
            const std::size_t count =
                std::min(weighted[b].real.size(), weighted[c].real.size());
            for (std::size_t i = 0; i < count; ++i) {
                const Vec3& br = weighted[b].real[i];
                const Vec3& bi = weighted[b].imag[i];
                const Vec3& cr = weighted[c].real[i];
                const Vec3& ci = weighted[c].imag[i];
                overlapRe += br.dot(cr) + bi.dot(ci);
                overlapIm += br.dot(ci) - bi.dot(cr);
            }
            const double magnitude =
                std::sqrt(overlapRe * overlapRe + overlapIm * overlapIm)
                / (norms[b] * norms[c]);
            diagnostics.maxOverlap = std::max(diagnostics.maxOverlap, magnitude);
        }

        // Projection onto the three rigid translations T^α_i = √M_i·ê_α/√ΣM,
        // which is the acoustic sum rule Σ_i M_i u_i = 0 written as a
        // measurement instead of an assertion.
        if (norms[b] > 0.0 && totalMass > 0.0) {
            Vec3 sumRe{};
            Vec3 sumIm{};
            for (std::size_t i = 0; i < weighted[b].real.size(); ++i) {
                const double root = std::sqrt(masses[i]);
                sumRe += weighted[b].real[i] * root;
                if (i < weighted[b].imag.size())
                    sumIm += weighted[b].imag[i] * root;
            }
            const double weight =
                (sumRe.dot(sumRe) + sumIm.dot(sumIm))
                / (totalMass * norms[b] * norms[b]);
            diagnostics.translationWeight = std::min(1.0, weight);
            diagnostics.rigidTranslation = diagnostics.translationWeight > 0.99;
        }

        // Degeneracy by frequency proximity. At Γ this correctly reports the
        // three acoustic modes as a triple (they all sit at ω = 0), which is
        // also what their shared irrep label says.
        if (b < qpoint.frequenciesCm.size()) {
            int count = 0;
            for (double other : qpoint.frequenciesCm)
                if (std::abs(other - qpoint.frequenciesCm[b])
                    <= degeneracyToleranceCm)
                    ++count;
            diagnostics.degeneracy = std::max(1, count);
        }
    }
    return out;
}

} // namespace calango::core
