#include "core/Noise.hpp"

#include <algorithm>
#include <array>
#include <random>
#include <vector>

namespace calango::core {

void applyRandomNoise(Structure& structure, const NoiseOptions& options)
{
    std::mt19937 generator(options.seed);
    std::normal_distribution<double> gaussian(0.0, options.amplitude);
    std::uniform_real_distribution<double> uniform(-options.amplitude, options.amplitude);

    const auto draw = [&]() {
        return options.distribution == NoiseOptions::Distribution::Gaussian
            ? gaussian(generator)
            : uniform(generator);
    };

    if (options.perturbCell && structure.cell().isDefined()) {
        // Remember fractional coordinates so atoms follow the cell strain.
        std::vector<Vec3> fractional;
        fractional.reserve(structure.size());
        for (const Atom& atom : structure.atoms())
            fractional.push_back(structure.cell().cartesianToFractional(atom.position));

        auto vectors = structure.cell().vectors();
        for (auto& vector : vectors)
            vector += Vec3{draw(), draw(), draw()};
        UnitCell cell = structure.cell();
        cell.setVectors(vectors);
        structure.setCell(cell);

        for (std::size_t i = 0; i < structure.size(); ++i)
            structure.atoms()[i].position =
                structure.cell().fractionalToCartesian(fractional[i]);
    }

    if (options.perturbPositions) {
        for (Atom& atom : structure.atoms())
            atom.position += Vec3{draw(), draw(), draw()};
    }
}

std::vector<std::shared_ptr<Structure>> buildNoiseEnsemble(
    const Structure& reference, const NoiseOptions& options, int count,
    bool cumulative, bool ramped)
{
    std::vector<std::shared_ptr<Structure>> frames;
    frames.reserve(static_cast<std::size_t>(std::max(count, 0)) + 1);
    // Frame 0 is the untouched reference: the statistics are a spread AROUND
    // something, and without it nothing has a centre to be a distribution
    // around. It is also, unmodified, exactly the ramp's zero-noise
    // endpoint.
    frames.push_back(std::make_shared<Structure>(reference));

    Structure walker = reference;
    for (int k = 1; k <= count; ++k) {
        NoiseOptions member = options;
        member.seed = options.seed + static_cast<unsigned int>(k);
        if (ramped)
            member.amplitude *= rampAmplitudeFactor(k, count);
        if (cumulative) {
            applyRandomNoise(walker, member);
            frames.push_back(std::make_shared<Structure>(walker));
        } else {
            Structure fresh = reference;
            applyRandomNoise(fresh, member);
            frames.push_back(std::make_shared<Structure>(std::move(fresh)));
        }
    }
    return frames;
}

} // namespace calango::core
