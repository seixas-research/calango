#include "core/DatasetSplit.hpp"

#include <algorithm>
#include <numeric>
#include <random>

namespace calango::core {

DatasetSplit DatasetSplit::make(int count, double trainFraction,
                                double validationFraction, unsigned seed)
{
    DatasetSplit split;
    if (count <= 0)
        return split;

    trainFraction = std::clamp(trainFraction, 0.0, 1.0);
    validationFraction =
        std::clamp(validationFraction, 0.0, 1.0 - trainFraction);

    std::vector<int> indices(static_cast<std::size_t>(count));
    std::iota(indices.begin(), indices.end(), 0);
    std::mt19937 rng(seed);
    std::shuffle(indices.begin(), indices.end(), rng);

    const auto trainCount = static_cast<std::size_t>(
        std::llround(trainFraction * count));
    const auto validationCount = std::min(
        static_cast<std::size_t>(std::llround(validationFraction * count)),
        static_cast<std::size_t>(count) - trainCount);

    split.train.assign(indices.begin(), indices.begin() + trainCount);
    split.validation.assign(indices.begin() + trainCount,
                            indices.begin() + trainCount + validationCount);
    split.test.assign(indices.begin() + trainCount + validationCount,
                      indices.end());
    // Sorted subsets keep exported files in source order.
    std::sort(split.train.begin(), split.train.end());
    std::sort(split.validation.begin(), split.validation.end());
    std::sort(split.test.begin(), split.test.end());
    return split;
}

std::vector<int> bootstrapSample(const std::vector<int>& pool, unsigned seed)
{
    std::vector<int> sample;
    if (pool.empty())
        return sample;
    std::mt19937 rng(seed);
    std::uniform_int_distribution<std::size_t> pick(0, pool.size() - 1);
    sample.reserve(pool.size());
    for (std::size_t i = 0; i < pool.size(); ++i)
        sample.push_back(pool[pick(rng)]);
    std::sort(sample.begin(), sample.end());
    return sample;
}

} // namespace calango::core
