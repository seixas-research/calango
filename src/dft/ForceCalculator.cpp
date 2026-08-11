#include "dft/ForceCalculator.hpp"

#include "core/Structure.hpp"
#include "dft/CalangoDFTEngine.hpp"

#include <algorithm>
#include <cmath>

namespace calango::dft {
namespace {

/// Displace one atom of a copy of the structure, in Å.
core::Structure displaced(const core::Structure& structure, std::size_t atom,
                          int axis, double amount)
{
    core::Structure moved = structure;
    core::Vec3& position = moved.atoms()[atom].position;
    if (axis == 0)
        position.x += amount;
    else if (axis == 1)
        position.y += amount;
    else
        position.z += amount;
    return moved;
}

double component(const core::Vec3& v, int axis)
{
    return axis == 0 ? v.x : (axis == 1 ? v.y : v.z);
}

} // namespace

ForceCalculator::ForceCalculator(Parameters parameters)
    : parameters_(std::move(parameters))
{
}

AtomicForces ForceCalculator::finiteDifference(
    const core::Structure& structure, double stepA, bool verify) const
{
    AtomicForces forces;
    const std::size_t count = structure.size();
    if (count == 0) {
        forces.outcome = Outcome::invalid("forces: the structure has no atoms");
        return forces;
    }
    if (!(stepA > 0.0)) {
        forces.outcome =
            Outcome::invalid("forces: the displacement must be positive");
        return forces;
    }
    forces.total.assign(count, {{0.0, 0.0, 0.0}});
    forces.fromFiniteDifference = true;
    // The same force at twice the displacement. An exact derivative barely
    // moves between the two; grid noise halves. Comparing them is the only
    // way to know which one this is.
    std::vector<std::array<double, 3>> doubled(count, {{0.0, 0.0, 0.0}});

    CalangoDFTEngine engine(parameters_);
    for (std::size_t atom = 0; atom < count; ++atom) {
        for (int axis = 0; axis < 3; ++axis) {
            if (verify) {
                const CalangoDFTEngine::Result p2 =
                    engine.run(displaced(structure, atom, axis, 2.0 * stepA));
                const CalangoDFTEngine::Result m2 =
                    engine.run(displaced(structure, atom, axis, -2.0 * stepA));
                if (p2.outcome.ok() && m2.outcome.ok())
                    doubled[atom][static_cast<std::size_t>(axis)] =
                        -(p2.energy.total - m2.energy.total) / (4.0 * stepA);
            }
            const CalangoDFTEngine::Result plus =
                engine.run(displaced(structure, atom, axis, stepA));
            if (!plus.outcome.ok()) {
                forces.outcome = plus.outcome;
                return forces;
            }
            const CalangoDFTEngine::Result minus =
                engine.run(displaced(structure, atom, axis, -stepA));
            if (!minus.outcome.ok()) {
                forces.outcome = minus.outcome;
                return forces;
            }
            // F = −dE/dR. Central difference, so the leading error is
            // quadratic in the step rather than linear.
            forces.total[atom][static_cast<std::size_t>(axis)] =
                -(plus.energy.total - minus.energy.total) / (2.0 * stepA);
        }
    }
    for (const std::array<double, 3>& f : forces.total)
        for (const double value : f)
            forces.maxComponentEvPerA =
                std::max(forces.maxComponentEvPerA, std::abs(value));
    for (std::size_t atom = 0; atom < count; ++atom)
        for (int c = 0; c < 3; ++c) {
            const auto o = static_cast<std::size_t>(c);
            forces.noiseEstimateEvPerA =
                std::max(forces.noiseEstimateEvPerA,
                         std::abs(forces.total[atom][o] - doubled[atom][o]));
        }
    // A derivative that changes when the step changes is not a derivative.
    // Refusing here is the whole point: a force of the right order of
    // magnitude and the wrong sign would drive a relaxation confidently into
    // nonsense, and nothing downstream could tell.
    if (verify
        && forces.noiseEstimateEvPerA
            > 0.25 * std::max(forces.maxComponentEvPerA, 0.05)) {
        forces.outcome = {
            Status::NumericalFailure,
            "the finite-difference force changes by "
                + std::to_string(forces.noiseEstimateEvPerA)
                + " eV/A when the displacement is doubled, against a largest "
                  "component of "
                + std::to_string(forces.maxComponentEvPerA)
                + " eV/A: it is dominated by the integration grid's own error, "
                  "which does not cancel between two displaced geometries "
                  "because the grid moves with the atoms. A denser grid does "
                  "not fix it. The analytic force is the route that avoids "
                  "differencing two grids at all."};
        return forces;
    }
    forces.outcome = Outcome::success();
    return forces;
}

AtomicForces ForceCalculator::analytic(const core::Structure& structure,
                                       bool compare) const
{
    AtomicForces forces;
    const std::size_t count = structure.size();
    if (count == 0) {
        forces.outcome = Outcome::invalid("forces: the structure has no atoms");
        return forces;
    }

    Parameters parameters = parameters_;
    parameters.computeForces = true;
    CalangoDFTEngine engine(parameters);
    const CalangoDFTEngine::Result result = engine.run(structure);
    if (!result.outcome.ok()) {
        forces.outcome = result.outcome;
        return forces;
    }
    if (result.hellmannFeynman.size() != count) {
        forces.outcome = Outcome::notImplemented(
            "the analytic force decomposition is available for finite systems "
            "only; a periodic cell needs the nuclear lattice sum and the "
            "strain derivative of the neutral-atom reference, neither of "
            "which is implemented. Use finiteDifference(), which is exact for "
            "both.");
        return forces;
    }
    forces.hellmannFeynman = result.hellmannFeynman;
    forces.pulay = result.pulayForce;
    forces.total.assign(count, {{0.0, 0.0, 0.0}});
    for (std::size_t atom = 0; atom < count; ++atom)
        for (int c = 0; c < 3; ++c)
            forces.total[atom][static_cast<std::size_t>(c)] =
                forces.hellmannFeynman[atom][static_cast<std::size_t>(c)]
                + forces.pulay[atom][static_cast<std::size_t>(c)];
    for (const std::array<double, 3>& f : forces.total)
        for (const double value : f)
            forces.maxComponentEvPerA =
                std::max(forces.maxComponentEvPerA, std::abs(value));

    if (compare) {
        // The only test that matters. An analytic force that has not been
        // measured against the energy it claims to differentiate is a number
        // with no standing.
        const AtomicForces reference = finiteDifference(structure);
        if (reference.outcome.ok()) {
            for (std::size_t atom = 0; atom < count; ++atom)
                for (int c = 0; c < 3; ++c)
                    forces.analyticResidualEvPerA = std::max(
                        forces.analyticResidualEvPerA,
                        std::abs(
                            forces.total[atom][static_cast<std::size_t>(c)]
                            - reference.total[atom][static_cast<std::size_t>(c)]));
        }
    }
    forces.outcome = Outcome::success();
    return forces;
}

namespace {

/// Apply x → (1 + ε)x to the lattice vectors AND the atomic positions, with ε
/// symmetric. Straining the positions along with the cell is what makes this
/// a uniform deformation rather than a cell change with the atoms left behind.
core::Structure strained(const core::Structure& structure,
                         const std::array<double, 9>& epsilon)
{
    core::Structure deformed = structure;
    const auto apply = [&epsilon](const core::Vec3& v) {
        const double x[3] = {v.x, v.y, v.z};
        double out[3] = {0.0, 0.0, 0.0};
        for (int i = 0; i < 3; ++i) {
            out[i] = x[i];
            for (int j = 0; j < 3; ++j)
                out[i] += epsilon[static_cast<std::size_t>(i * 3 + j)] * x[j];
        }
        return core::Vec3{out[0], out[1], out[2]};
    };
    std::array<core::Vec3, 3> vectors = structure.cell().vectors();
    for (core::Vec3& v : vectors)
        v = apply(v);
    core::UnitCell cell = structure.cell();
    cell.setVectors(vectors);
    deformed.setCell(cell);
    for (core::Atom& atom : deformed.atoms())
        atom.position = apply(atom.position);
    return deformed;
}

} // namespace

StressTensor ForceCalculator::stress(const core::Structure& structure,
                                     double strain, bool verify) const
{
    StressTensor result;
    if (!structure.cell().isDefined()) {
        result.outcome = Outcome::invalid(
            "stress: a uniform strain is not defined without a periodic cell");
        return result;
    }
    if (!(strain > 0.0)) {
        result.outcome = Outcome::invalid("stress: the strain must be positive");
        return result;
    }
    const double volume = structure.cell().volume();
    if (!(volume > 0.0)) {
        result.outcome = Outcome::invalid("stress: the cell has no volume");
        return result;
    }

    CalangoDFTEngine engine(parameters_);
    const auto energyAt = [&engine](const core::Structure& s, bool& ok) {
        const CalangoDFTEngine::Result r = engine.run(s);
        ok = r.outcome.ok();
        return r.energy.total;
    };

    // The six independent components. A diagonal one perturbs ε_αα alone; an
    // off-diagonal one perturbs ε_αβ AND ε_βα together, so its derivative
    // carries a factor of two that has to come back out.
    const int pairs[6][2] = {{0, 0}, {1, 1}, {2, 2}, {1, 2}, {0, 2}, {0, 1}};
    for (const auto& pair : pairs) {
        const int a = pair[0];
        const int b = pair[1];
        const auto measure = [&](double amount, bool& ok) {
            std::array<double, 9> epsilon{};
            epsilon[static_cast<std::size_t>(a * 3 + b)] = amount;
            epsilon[static_cast<std::size_t>(b * 3 + a)] = amount;
            return energyAt(strained(structure, epsilon), ok);
        };
        bool okPlus = false;
        bool okMinus = false;
        const double plus = measure(strain, okPlus);
        const double minus = measure(-strain, okMinus);
        if (!okPlus || !okMinus) {
            result.outcome = {Status::NumericalFailure,
                              "stress: a strained configuration did not reach "
                              "self-consistency"};
            return result;
        }
        const double symmetry = (a == b) ? 1.0 : 2.0;
        const double value =
            (plus - minus) / (2.0 * strain) / (symmetry * volume);
        result.tensor[static_cast<std::size_t>(a * 3 + b)] = value;
        result.tensor[static_cast<std::size_t>(b * 3 + a)] = value;

        // The same doubled-step guard the forces carry: an exact derivative
        // barely moves, quadrature noise halves.
        if (!verify)
            continue;
        bool okPlus2 = false;
        bool okMinus2 = false;
        const double plus2 = measure(2.0 * strain, okPlus2);
        const double minus2 = measure(-2.0 * strain, okMinus2);
        if (okPlus2 && okMinus2) {
            const double doubled =
                (plus2 - minus2) / (4.0 * strain) / (symmetry * volume);
            result.noiseEstimateEvPerA3 = std::max(
                result.noiseEstimateEvPerA3, std::abs(value - doubled));
        }
    }

    constexpr double kGpaPerEvPerA3 = 160.21766208;
    result.pressureGpa = -(result.tensor[0] + result.tensor[4]
                           + result.tensor[8])
        / 3.0 * kGpaPerEvPerA3;
    result.outcome = Outcome::success();
    return result;
}

RelaxationResult ForceCalculator::relax(const core::Structure& structure,
                                        double forceToleranceEvPerA,
                                        int maxSteps) const
{
    RelaxationResult result;
    const std::size_t count = structure.size();
    if (count == 0) {
        result.outcome = Outcome::invalid("relaxation: no atoms");
        return result;
    }

    core::Structure current = structure;
    // FIRE: damped dynamics that turns the damping off while the system is
    // going downhill and freezes the velocity the moment it starts going up.
    // No line search, which is what makes it affordable here — a rejected
    // trial would throw away 6N self-consistency cycles.
    std::vector<std::array<double, 3>> velocity(count, {{0.0, 0.0, 0.0}});
    // The displacement of a step is timeStep² × force, so timeStep carries
    // units of √(Å²/(eV/Å)). These values give a first step of a few
    // hundredths of an ångström for the forces a stretched bond produces,
    // which is the right scale: large enough to move, small enough that the
    // energy surface is still quadratic over it.
    double timeStep = 0.05;
    double mixing = 0.1; // FIRE's α
    int uphill = 0;
    constexpr double kTimeStepMax = 0.2;
    constexpr double kMaxDisplacementA = 0.05;
    constexpr int kLatency = 5;

    for (int step = 0; step < maxSteps; ++step) {
        // Verified on the first step only: the check is about the ENERGY
        // SURFACE, and it does not become a different surface as the atoms
        // move a few hundredths of an angstrom along it.
        const AtomicForces forces = finiteDifference(current, 0.01, step == 0);
        if (!forces.outcome.ok()) {
            result.outcome = forces.outcome;
            return result;
        }
        CalangoDFTEngine engine(parameters_);
        const CalangoDFTEngine::Result energy = engine.run(current);
        if (!energy.outcome.ok()) {
            result.outcome = energy.outcome;
            return result;
        }
        result.history.push_back({energy.energy.total,
                                  forces.maxComponentEvPerA});
        result.steps = step + 1;
        result.finalMaxForceEvPerA = forces.maxComponentEvPerA;
        result.finalEnergyEv = energy.energy.total;
        if (forces.maxComponentEvPerA < forceToleranceEvPerA) {
            result.outcome = Outcome::success();
            break;
        }

        // Power: are we going downhill?
        double power = 0.0;
        double forceNorm = 0.0;
        double velocityNorm = 0.0;
        for (std::size_t atom = 0; atom < count; ++atom)
            for (int c = 0; c < 3; ++c) {
                const auto o = static_cast<std::size_t>(c);
                power += velocity[atom][o] * forces.total[atom][o];
                forceNorm += forces.total[atom][o] * forces.total[atom][o];
                velocityNorm += velocity[atom][o] * velocity[atom][o];
            }
        forceNorm = std::sqrt(forceNorm);
        velocityNorm = std::sqrt(velocityNorm);

        if (power > 0.0) {
            // Downhill: steer the velocity towards the force and, after a few
            // consecutive successes, lengthen the step.
            if (forceNorm > 0.0)
                for (std::size_t atom = 0; atom < count; ++atom)
                    for (int c = 0; c < 3; ++c) {
                        const auto o = static_cast<std::size_t>(c);
                        velocity[atom][o] = (1.0 - mixing) * velocity[atom][o]
                            + mixing * forces.total[atom][o] / forceNorm
                                * velocityNorm;
                    }
            if (++uphill > kLatency) {
                timeStep = std::min(timeStep * 1.1, kTimeStepMax);
                mixing *= 0.99;
            }
        } else {
            // Uphill: stop dead rather than overshoot further.
            uphill = 0;
            timeStep *= 0.5;
            mixing = 0.1;
            for (std::array<double, 3>& v : velocity)
                v = {{0.0, 0.0, 0.0}};
        }

        for (std::size_t atom = 0; atom < count; ++atom) {
            core::Vec3& position = current.atoms()[atom].position;
            for (int c = 0; c < 3; ++c) {
                const auto o = static_cast<std::size_t>(c);
                velocity[atom][o] += timeStep * forces.total[atom][o];
                const double move = std::clamp(timeStep * velocity[atom][o],
                                               -kMaxDisplacementA,
                                               kMaxDisplacementA);
                if (c == 0)
                    position.x += move;
                else if (c == 1)
                    position.y += move;
                else
                    position.z += move;
            }
        }
    }

    result.positions.reserve(count);
    for (const core::Atom& atom : current.atoms())
        result.positions.push_back(
            {{atom.position.x, atom.position.y, atom.position.z}});
    if (result.outcome.status != Status::Ok)
        result.outcome = {Status::NotConverged,
                          "the relaxation reached " + std::to_string(result.steps)
                              + " steps with a largest force component of "
                              + std::to_string(result.finalMaxForceEvPerA)
                              + " eV/A"};
    (void)component;
    return result;
}

} // namespace calango::dft
