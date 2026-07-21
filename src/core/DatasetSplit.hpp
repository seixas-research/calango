#pragma once

#include <vector>

namespace calango::core {

/// Deterministic train/validation/test partition of `count` items.
/// Fractions are clamped so train + validation <= 1; the remainder is
/// the test set. The same (count, fractions, seed) always yields the
/// same partition (std::mt19937 shuffle).
struct DatasetSplit {
    std::vector<int> train;
    std::vector<int> validation;
    std::vector<int> test;

    static DatasetSplit make(int count, double trainFraction,
                             double validationFraction, unsigned seed);
};

/// Bootstrap resample of `pool` (same size, drawn with replacement) —
/// the classic committee construction for Query-by-Committee ensembles.
std::vector<int> bootstrapSample(const std::vector<int>& pool, unsigned seed);

} // namespace calango::core
