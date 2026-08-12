// Vibrational normal-mode analysis test.
//
// Nothing here compares against a previous run. Every expectation is either a
// closed form (the harmonic oscillator, the reduced mass of a diatomic, the
// plane-wave phase factor) or a conservation law that any correct
// implementation must satisfy (the acoustic sum rule, orthonormality of the
// dynamical matrix's eigenbasis, energy conservation over a period).
//
// The three things this exists to catch, all of which produce a perfectly
// convincing animation when broken:
//
//   1. MASS WEIGHTING. phonopy returns eigenvectors of the dynamical matrix,
//      w = sqrt(M)*u; ASE returns u directly. Animating w as if it were u makes
//      an OH stretch move the oxygen 4x too far. The ratio is pinned to the
//      textbook M_O/M_H, which is what separates the two conventions.
//   2. UNIT CONVERSIONS. The velocities are in ASE units (A per A*sqrt(u/eV))
//      and the forces in eV/A, converted by two constants written independently
//      in the source. If either is wrong, kinetic + potential energy stops
//      being conserved over a period - and nothing on screen looks different.
//   3. THE q.R PHASE. Dropping it renders every q-point as if it were Gamma;
//      the pattern still oscillates, it is just the wrong mode.
//
// GUI-free, Python-free.

#include "core/Element.hpp"
#include "core/Structure.hpp"
#include "core/UnitCell.hpp"
#include "core/VibrationalModes.hpp"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

using namespace calango::core;

namespace {

int failures = 0;

void check(bool condition, const std::string& what)
{
    std::printf("  %s %s\n", condition ? "ok  " : "FAIL", what.c_str());
    if (!condition)
        ++failures;
}

void checkClose(double actual, double expected, double tolerance,
                const std::string& what)
{
    const bool ok = std::abs(actual - expected) <= tolerance;
    std::printf("  %s %s  (got %.10g, expected %.10g)\n", ok ? "ok  " : "FAIL",
                what.c_str(), actual, expected);
    if (!ok)
        ++failures;
}

// -- Physical constants, spelled out here rather than imported ---------------
//
// The whole point of the energy check below is that it does NOT reuse the
// module's own conversion factors: these are CODATA 2018 values, and the
// expected energy is assembled from them independently.
constexpr double kSpeedOfLightCmPerS = 2.99792458e10;
constexpr double kAtomicMassKg = 1.66053907e-27;
constexpr double kElectronVoltJ = 1.602176634e-19;
constexpr double kAngstromM = 1e-10;

/// Angular frequency in rad/s from a wavenumber in cm^-1.
double angularFrequency(double cm)
{
    return 2.0 * M_PI * cm * kSpeedOfLightCmPerS;
}

Vec3 axis(int component, double value)
{
    Vec3 v;
    (component == 0 ? v.x : component == 1 ? v.y : v.z) = value;
    return v;
}

/// A hydroxyl "molecule": H at the origin, O one bond length along x.
///
/// Two atoms with a 16:1 mass ratio is the sharpest possible probe of the mass
/// weighting - the two conventions differ by sqrt(16) = 4 in the displacement
/// ratio, which no tolerance can blur.
struct Hydroxyl {
    static constexpr double kBondAng = 0.97;
    Structure structure;
    double massH = 0.0;
    double massO = 0.0;
    double totalMass = 0.0;
    /// Positions relative to the centre of mass, along x.
    double xH = 0.0;
    double xO = 0.0;

    Hydroxyl()
    {
        Atom h;
        h.atomicNumber = 1;
        h.position = {0.0, 0.0, 0.0};
        Atom o;
        o.atomicNumber = 8;
        o.position = {kBondAng, 0.0, 0.0};
        structure.addAtom(h);
        structure.addAtom(o);
        massH = Elements::atomicMass(1);
        massO = Elements::atomicMass(8);
        totalMass = massH + massO;
        const double centre = massO * kBondAng / totalMass;
        xH = -centre;
        xO = kBondAng - centre;
    }
};

/// The exact 6-branch mass-weighted eigenbasis of a free diatomic: three rigid
/// translations, two rotations, one stretch.
///
/// Every vector is written in closed form, so orthonormality is a property of
/// the physics rather than of a diagonalizer that ran earlier. That is what
/// makes analyzeModes()'s overlap and translation-weight numbers checkable at
/// all: the answers are known before the code runs.
VibrationalQPoint hydroxylModes(const Hydroxyl& molecule,
                                const std::vector<double>& frequenciesCm)
{
    VibrationalQPoint gamma;
    gamma.label = "G";
    gamma.frequenciesCm = frequenciesCm;

    const double rootH = std::sqrt(molecule.massH);
    const double rootO = std::sqrt(molecule.massO);
    const double rootTotal = std::sqrt(molecule.totalMass);

    // Branches 0-2: rigid translation along x, y, z. w_i = sqrt(M_i)*e_a/sqrt(M).
    for (int component = 0; component < 3; ++component)
        gamma.eigenvectorsReal.push_back(
            {axis(component, rootH / rootTotal), axis(component, rootO / rootTotal)});

    // Branches 3-4: rotation about z (displacement along y proportional to x)
    // and about y (along z). Orthogonal to the translations exactly because
    // sum_i M_i x_i = 0 defines the centre of mass.
    const double inertia = std::sqrt(molecule.massH * molecule.xH * molecule.xH
                                     + molecule.massO * molecule.xO * molecule.xO);
    for (int component : {1, 2})
        gamma.eigenvectorsReal.push_back(
            {axis(component, rootH * molecule.xH / inertia),
             axis(component, rootO * molecule.xO / inertia)});

    // Branch 5: the stretch. w = (sqrt(M_O), -sqrt(M_H))/sqrt(M) along x, which
    // is the unique combination orthogonal to the x translation.
    gamma.eigenvectorsReal.push_back(
        {axis(0, rootO / rootTotal), axis(0, -rootH / rootTotal)});

    // Real eigenvectors: a Gamma-point (or ASE) export writes no imaginary
    // part at all, which is the case the module has to survive.
    return gamma;
}

/// Harmonic energy of one frame, from the fields the module attached.
///
/// Potential energy comes out of the FORCES (E = -1/2 sum F.u, exact for a
/// harmonic restoring force) and kinetic energy out of the VELOCITIES, so the
/// two independently-written unit conversions are what is being compared.
void frameEnergy(const Structure& reference, const Structure& frame,
                 double& kineticEv, double& potentialEv)
{
    kineticEv = 0.0;
    potentialEv = 0.0;
    const auto& forces = frame.vectorFields().at("forces");
    const auto& velocities = frame.vectorFields().at("velocities");
    for (std::size_t i = 0; i < frame.size(); ++i) {
        const double mass =
            Elements::atomicMass(frame.atoms()[i].atomicNumber);
        const Vec3 u = frame.atoms()[i].position - reference.atoms()[i].position;
        potentialEv -= 0.5 * forces[i].dot(u);
        // 1/2 M v^2 with M in u and v in A per ASE time unit IS energy in eV -
        // that is the definition of the ASE unit system, and the reason the
        // velocity conversion can be checked without any further factor.
        kineticEv += 0.5 * mass * velocities[i].dot(velocities[i]);
    }
}

void testDiagnostics()
{
    std::printf("Eigenbasis diagnostics (closed-form OH normal modes)\n");
    const Hydroxyl molecule;
    // Frequencies chosen to exercise the degeneracy counter: the three
    // translations sit at zero (they must, by the acoustic sum rule), the two
    // rotations are given an equal non-zero value so a genuine doublet exists,
    // and the stretch is a singlet.
    const VibrationalQPoint gamma =
        hydroxylModes(molecule, {0.0, 0.0, 0.0, 42.0, 42.0, 3700.0});

    const std::vector<ModeDiagnostics> diagnostics = analyzeModes(
        molecule.structure, gamma, EigenvectorConvention::MassWeighted);
    check(diagnostics.size() == 6, "one diagnostic per branch");
    if (diagnostics.size() != 6)
        return;

    double worstNorm = 0.0;
    double worstOverlap = 0.0;
    for (const ModeDiagnostics& mode : diagnostics) {
        worstNorm = std::max(worstNorm, std::abs(mode.norm - 1.0));
        worstOverlap = std::max(worstOverlap, mode.maxOverlap);
    }
    checkClose(worstNorm, 0.0, 1e-12, "every eigenvector is normalized");
    checkClose(worstOverlap, 0.0, 1e-12,
               "the six branches are mutually orthogonal");

    // The acoustic sum rule, branch by branch: only the three translations
    // fail sum_i M_i u_i = 0, and they fail it completely.
    for (int b = 0; b < 3; ++b) {
        checkClose(diagnostics[static_cast<std::size_t>(b)].translationWeight,
                   1.0, 1e-12,
                   "branch " + std::to_string(b) + " is a rigid translation");
        check(diagnostics[static_cast<std::size_t>(b)].rigidTranslation,
              "branch " + std::to_string(b) + " flagged acoustic");
    }
    for (int b = 3; b < 6; ++b) {
        checkClose(diagnostics[static_cast<std::size_t>(b)].translationWeight,
                   0.0, 1e-12,
                   "branch " + std::to_string(b) + " conserves momentum");
        check(!diagnostics[static_cast<std::size_t>(b)].rigidTranslation,
              "branch " + std::to_string(b) + " not flagged acoustic");
    }

    const int expected[6] = {3, 3, 3, 2, 2, 1};
    for (std::size_t b = 0; b < 6; ++b)
        check(diagnostics[b].degeneracy == expected[b],
              "branch " + std::to_string(b) + " degeneracy "
                  + std::to_string(expected[b]));

    // A mass-weighted file read as if it were displacements: the same numbers,
    // the wrong metric. The translations stop looking like translations, which
    // is exactly the symptom the convention flag exists to prevent.
    const std::vector<ModeDiagnostics> misread = analyzeModes(
        molecule.structure, gamma, EigenvectorConvention::Displacement);
    check(!misread.empty() && !misread[0].rigidTranslation,
          "the same file read in the wrong convention is NOT a translation");
}

void testMassWeighting()
{
    std::printf("Mass weighting (OH stretch, M_O/M_H = 15.9)\n");
    const Hydroxyl molecule;
    VibrationalModeSet modes;
    modes.convention = EigenvectorConvention::MassWeighted;
    modes.qpoints.push_back(
        hydroxylModes(molecule, {0.0, 0.0, 0.0, 42.0, 42.0, 3700.0}));

    ModeDisplacement options;
    options.amplitudeAng = 0.3;
    options.phase = 0.0;
    const auto frame = displaceByMode(molecule.structure, modes, 0, 5, options);
    check(frame != nullptr, "the stretch branch produces a displaced structure");
    if (!frame)
        return;

    const Vec3 uH = frame->atoms()[0].position - molecule.structure.atoms()[0].position;
    const Vec3 uO = frame->atoms()[1].position - molecule.structure.atoms()[1].position;

    // The amplitude names the largest excursion, so the light atom is pinned at
    // exactly the requested value and the heavy one follows from momentum.
    checkClose(uH.norm(), 0.3, 1e-12, "H moves by the requested amplitude");
    checkClose(uO.norm(), 0.3 * molecule.massH / molecule.massO, 1e-12,
               "O moves by A*M_H/M_O");
    checkClose(uH.norm() / uO.norm(), molecule.massO / molecule.massH, 1e-9,
               "displacement ratio is M_O/M_H, not sqrt(M_O/M_H)");
    check(uH.x * uO.x < 0.0, "the two atoms move in opposite directions");

    // The acoustic sum rule on the actual geometry: a vibration moves no
    // centre of mass. Exact, not approximate.
    const Vec3 momentum = uH * molecule.massH + uO * molecule.massO;
    checkClose(momentum.norm(), 0.0, 1e-13, "sum_i M_i u_i = 0 for the stretch");

    // The same components read as displacements: the ratio collapses to the
    // square root, which is the wrong answer the convention flag prevents.
    modes.convention = EigenvectorConvention::Displacement;
    const auto wrong = displaceByMode(molecule.structure, modes, 0, 5, options);
    check(wrong != nullptr, "the displacement convention also produces a frame");
    if (wrong) {
        const double ratio =
            (wrong->atoms()[0].position - molecule.structure.atoms()[0].position).norm()
            / (wrong->atoms()[1].position - molecule.structure.atoms()[1].position).norm();
        checkClose(ratio, std::sqrt(molecule.massO / molecule.massH), 1e-9,
                   "the displacement convention gives sqrt(M_O/M_H)");
    }

    // A rigid translation moves every atom identically - the bond length is
    // untouched, which is the geometric statement of "acoustic at Gamma".
    modes.convention = EigenvectorConvention::MassWeighted;
    const auto translated = displaceByMode(molecule.structure, modes, 0, 0, options);
    check(translated != nullptr, "the acoustic branch produces a frame");
    if (translated) {
        const double before =
            (molecule.structure.atoms()[1].position
             - molecule.structure.atoms()[0].position).norm();
        const double after =
            (translated->atoms()[1].position - translated->atoms()[0].position).norm();
        checkClose(after, before, 1e-12,
                   "the acoustic mode leaves the bond length unchanged");
        checkClose(
            (translated->atoms()[0].position - molecule.structure.atoms()[0].position)
                .norm(),
            0.3, 1e-12, "every atom translates by the full amplitude");
    }
}

void testEnergyConservation()
{
    std::printf("Harmonic energy over one period (1/2 mu omega^2 x^2)\n");
    const Hydroxyl molecule;
    constexpr double kFrequencyCm = 3700.0;
    constexpr double kAmplitudeAng = 0.05;
    constexpr int kFrames = 32;

    VibrationalModeSet modes;
    modes.convention = EigenvectorConvention::MassWeighted;
    modes.qpoints.push_back(
        hydroxylModes(molecule, {0.0, 0.0, 0.0, 42.0, 42.0, kFrequencyCm}));

    ModeDisplacement options;
    options.amplitudeAng = kAmplitudeAng;
    const auto frames =
        modeTrajectory(molecule.structure, modes, 0, 5, options, kFrames);
    check(frames.size() == static_cast<std::size_t>(kFrames),
          "one frame per sampled phase");
    if (frames.size() != static_cast<std::size_t>(kFrames))
        return;

    // Closed-form reference: a diatomic stretch is a single oscillator of
    // reduced mass mu = M_H*M_O/(M_H+M_O) in the bond-length coordinate. H is
    // pinned at the amplitude, O follows at A*M_H/M_O, so the bond stretches by
    // A*(1 + M_H/M_O).
    const double reduced =
        molecule.massH * molecule.massO / molecule.totalMass;
    const double bondAmplitude =
        kAmplitudeAng * (1.0 + molecule.massH / molecule.massO);
    const double omega = angularFrequency(kFrequencyCm);
    const double expectedEv = 0.5 * reduced * kAtomicMassKg * omega * omega
        * (bondAmplitude * kAngstromM) * (bondAmplitude * kAngstromM)
        / kElectronVoltJ;

    double kinetic0 = 0.0;
    double potential0 = 0.0;
    frameEnergy(molecule.structure, *frames[0], kinetic0, potential0);
    checkClose(kinetic0, 0.0, 1e-18,
               "phase 0 is a turning point: no kinetic energy");
    checkClose(potential0, expectedEv, expectedEv * 1e-6,
               "potential energy at the turning point is 1/2 mu omega^2 x^2");

    // Frame 8 of 32 is exactly a quarter period, where the motion is entirely
    // kinetic. This is the quarter-period offset between u and v: if a sin/cos
    // or a sign were swapped, the two would peak together and this frame would
    // hold the full potential energy instead.
    double kineticQuarter = 0.0;
    double potentialQuarter = 0.0;
    frameEnergy(molecule.structure, *frames[kFrames / 4], kineticQuarter,
                potentialQuarter);
    checkClose(potentialQuarter, 0.0, expectedEv * 1e-9,
               "a quarter period on: no potential energy");
    checkClose(kineticQuarter, expectedEv, expectedEv * 1e-6,
               "a quarter period on: all of it is kinetic");

    // And the whole period: E_kin + E_pot is a constant of the motion. This is
    // the check that ties the velocity unit to the force unit - they are the
    // same physical conversion (t0^2 = m_u*A^2/eV) written twice.
    double worst = 0.0;
    for (const auto& frame : frames) {
        double kinetic = 0.0;
        double potential = 0.0;
        frameEnergy(molecule.structure, *frame, kinetic, potential);
        worst = std::max(worst, std::abs(kinetic + potential - expectedEv));
    }
    checkClose(worst / expectedEv, 0.0, 1e-6,
               "total energy is conserved across the period");

    // The closing frame is omitted so a looped playback does not stutter:
    // frame 0 is phase 0 and the last frame is phase 2*pi*(N-1)/N, not 2*pi.
    ModeDisplacement wrapped = options;
    wrapped.phase = 2.0 * M_PI;
    wrapped.withDynamics = true;
    const auto closing = displaceByMode(molecule.structure, modes, 0, 5, wrapped);
    check(closing != nullptr, "phase 2*pi is a valid frame");
    if (closing)
        checkClose((closing->atoms()[0].position - frames[0]->atoms()[0].position).norm(),
                   0.0, 1e-12, "phase 2*pi reproduces phase 0");
    check((frames.back()->atoms()[0].position - frames[0]->atoms()[0].position).norm()
              > 1e-6,
          "the last sampled frame is not a duplicate of the first");
}

void testPropagationPhase()
{
    std::printf("The q.R phase factor (zone-boundary running wave)\n");
    // Two identical atoms in a cubic cell, one at the origin and one at the
    // cell centre along x: fractional coordinates 0 and 1/2.
    Structure crystal;
    Atom a;
    a.atomicNumber = 14;
    a.position = {0.0, 0.0, 0.0};
    crystal.addAtom(a);
    a.position = {4.0, 0.0, 0.0};
    crystal.addAtom(a);
    crystal.setCell(UnitCell({8.0, 0.0, 0.0}, {0.0, 8.0, 0.0}, {0.0, 0.0, 8.0}));

    VibrationalQPoint boundary;
    boundary.label = "X";
    boundary.q[0] = 0.5;
    boundary.frequenciesCm = {250.0};
    // Equal masses, so an identical mass-weighted component is an identical
    // displacement: everything that separates the two atoms below is the
    // exp(i q.R) factor and nothing else.
    boundary.eigenvectorsReal.push_back(
        {{std::sqrt(0.5), 0.0, 0.0}, {std::sqrt(0.5), 0.0, 0.0}});

    VibrationalModeSet modes;
    modes.convention = EigenvectorConvention::MassWeighted;
    modes.qpoints.push_back(boundary);

    ModeDisplacement options;
    options.amplitudeAng = 0.2;

    // q.R is 0 for the atom at the origin and 2*pi*(1/2)*(1/2) = pi/2 for the
    // one at the cell centre, so the two are a quarter cycle apart: when the
    // first is at its turning point the second is passing through equilibrium.
    const auto atZero = displaceByMode(crystal, modes, 0, 0, options);
    check(atZero != nullptr, "the zone-boundary mode produces a frame");
    if (atZero) {
        checkClose((atZero->atoms()[0].position - crystal.atoms()[0].position).x,
                   0.2, 1e-12, "q.R = 0: full displacement at phase 0");
        checkClose((atZero->atoms()[1].position - crystal.atoms()[1].position).x,
                   0.0, 1e-12, "q.R = pi/2: zero displacement at phase 0");
    }

    options.phase = 0.5 * M_PI;
    const auto atQuarter = displaceByMode(crystal, modes, 0, 0, options);
    if (atQuarter) {
        checkClose((atQuarter->atoms()[0].position - crystal.atoms()[0].position).x,
                   0.0, 1e-12, "a quarter cycle later the roles are exchanged");
        checkClose((atQuarter->atoms()[1].position - crystal.atoms()[1].position).x,
                   0.2, 1e-12, "the second atom now carries the full amplitude");
    }

    // The same mode set on a NON-periodic structure: with no cell there are no
    // lattice translations for q to be conjugate to, so q.R must be dropped
    // entirely and both atoms move together. A molecular phonon run that
    // silently applied a q phase would shear the molecule.
    Structure molecule;
    molecule.addAtom(crystal.atoms()[0]);
    molecule.addAtom(crystal.atoms()[1]);
    options.phase = 0.0;
    const auto free = displaceByMode(molecule, modes, 0, 0, options);
    if (free) {
        checkClose((free->atoms()[0].position - molecule.atoms()[0].position).x,
                   0.2, 1e-12, "no cell: first atom at full amplitude");
        checkClose((free->atoms()[1].position - molecule.atoms()[1].position).x,
                   0.2, 1e-12, "no cell: q.R is dropped, both move together");
    }
}

void testRefusals()
{
    std::printf("Refusals (nothing to animate is not the same as no motion)\n");
    const Hydroxyl molecule;
    VibrationalModeSet modes;

    // Frequencies without eigenvectors: phonon_band.json alone. The mode list
    // is browsable but there is no displacement pattern, and fabricating one
    // would look entirely convincing.
    VibrationalQPoint frequenciesOnly;
    frequenciesOnly.label = "G";
    frequenciesOnly.frequenciesCm = {0.0, 0.0, 0.0, 42.0, 42.0, 3700.0};
    modes.qpoints.push_back(frequenciesOnly);
    check(!modes.hasEigenvectors(), "a band-only set reports no eigenvectors");
    check(displaceByMode(molecule.structure, modes, 0, 0, ModeDisplacement{})
              == nullptr,
          "no eigenvectors: no displaced structure");
    check(analyzeModes(molecule.structure, frequenciesOnly,
                       EigenvectorConvention::MassWeighted).empty(),
          "no eigenvectors: no diagnostics");

    // An all-zero branch (a truncated export) is refused rather than returned
    // as the undisplaced structure, which would assert a flat mode.
    VibrationalQPoint empty;
    empty.frequenciesCm = {100.0};
    empty.eigenvectorsReal.push_back({Vec3{}, Vec3{}});
    modes.qpoints.push_back(empty);
    check(displaceByMode(molecule.structure, modes, 1, 0, ModeDisplacement{})
              == nullptr,
          "an all-zero eigenvector is refused, not silently flattened");
    check(displaceByMode(molecule.structure, modes, 9, 0, ModeDisplacement{})
              == nullptr,
          "an out-of-range q-point is refused");
    check(modeTrajectory(molecule.structure, modes, 0, 0, ModeDisplacement{}, 8)
              .empty(),
          "a trajectory of an unanimatable mode is empty, not 8 copies");
}

} // namespace

int main()
{
    std::printf("Vibrational mode analysis\n");
    testDiagnostics();
    testMassWeighting();
    testEnergyConservation();
    testPropagationPhase();
    testRefusals();
    std::printf("%s (%d failure(s))\n", failures == 0 ? "PASSED" : "FAILED",
                failures);
    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
