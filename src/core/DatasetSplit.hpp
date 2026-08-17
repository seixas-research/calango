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

    /// Stratified variant: every index where `pinnedToTrain[i]` is true
    /// lands in `train` unconditionally (e.g. an isolated-atom reference
    /// frame, which MLIP training needs available at every stage) — the
    /// fractions below apply only to the REMAINING, unpinned indices, whose
    /// split is otherwise identical to make() (same shuffle-then-slice
    /// scheme, same determinism for a given seed). `pinnedToTrain.size()`
    /// must equal `count`; a mismatched size is treated as "nothing
    /// pinned" (falls back to the unstratified make()).
    static DatasetSplit makeStratified(int count, double trainFraction,
                                       double validationFraction,
                                       unsigned seed,
                                       const std::vector<bool>& pinnedToTrain);
};

/// Bootstrap resample of `pool` (same size, drawn with replacement) —
/// the classic committee construction for Query-by-Committee ensembles.
std::vector<int> bootstrapSample(const std::vector<int>& pool, unsigned seed);

} // namespace calango::core
