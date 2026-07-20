#include "core/Noise.hpp"

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

} // namespace calango::core
