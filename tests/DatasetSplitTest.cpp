// core::DatasetSplit: the deterministic train/validation/test partitioner
// behind both the standalone Dataset Manager dialog and the Orchestration
// Dataset Manager node. makeStratified() is the addition this session made
// (pinning isolated-atom reference frames to train unconditionally) — the
// plain make() has run, unit-tested only indirectly through its callers,
// until now.

#include "core/DatasetSplit.hpp"

#include <cstdio>
#include <cstdlib>
#include <set>
#include <string>

using namespace calango::core;

namespace {

int failures = 0;

void check(bool ok, const std::string& what)
{
    std::printf("  %-4s %s\n", ok ? "ok" : "FAIL", what.c_str());
    if (!ok)
        ++failures;
}

std::set<int> asSet(const std::vector<int>& v)
{
    return {v.begin(), v.end()};
}

void testMakeBasics()
{
    std::printf("\nDatasetSplit::make: basic partition\n");
    const auto split = DatasetSplit::make(10, 0.6, 0.2, 42);
    check(split.train.size() == 6, "60% of 10 -> 6 in train");
    check(split.validation.size() == 2, "20% of 10 -> 2 in validation");
    check(split.test.size() == 2, "the remainder -> 2 in test");

    // Every index appears exactly once across the three subsets.
    std::set<int> all;
    for (int i : split.train) all.insert(i);
    for (int i : split.validation) all.insert(i);
    for (int i : split.test) all.insert(i);
    check(all.size() == 10, "every index 0..9 appears exactly once, total");
    bool covers0to9 = true;
    for (int i = 0; i < 10; ++i)
        covers0to9 = covers0to9 && all.count(i) == 1;
    check(covers0to9, "and they are exactly 0..9, nothing invented or dropped");
}

void testMakeDeterministic()
{
    std::printf("\nDatasetSplit::make: same (count, fractions, seed) -> same split\n");
    const auto a = DatasetSplit::make(20, 0.7, 0.15, 123);
    const auto b = DatasetSplit::make(20, 0.7, 0.15, 123);
    check(a.train == b.train && a.validation == b.validation && a.test == b.test,
          "identical inputs produce a byte-identical partition");
    const auto c = DatasetSplit::make(20, 0.7, 0.15, 124);
    check(a.train != c.train || a.validation != c.validation,
          "a different seed produces a (very likely) different partition");
}

void testMakeStratifiedPinning()
{
    std::printf("\nDatasetSplit::makeStratified: pinned indices always land in train\n");
    // 12 items; indices 0, 5, 11 are pinned (e.g. isolated-atom references).
    // trainFraction is small enough that, unstratified, most items would NOT
    // land in train -- so a pass here is a real assertion, not a coincidence
    // of a generous fraction.
    std::vector<bool> pinned(12, false);
    pinned[0] = pinned[5] = pinned[11] = true;
    const auto split = DatasetSplit::makeStratified(12, 0.2, 0.4, 7, pinned);

    const std::set<int> train = asSet(split.train);
    check(train.count(0) && train.count(5) && train.count(11),
          "all three pinned indices are in train");
    check(split.validation.empty() || (asSet(split.validation).count(0) == 0
              && asSet(split.validation).count(5) == 0
              && asSet(split.validation).count(11) == 0),
          "none of them leaked into validation");
    check(split.test.empty() || (asSet(split.test).count(0) == 0
              && asSet(split.test).count(5) == 0
              && asSet(split.test).count(11) == 0),
          "or into test");

    // The 9 unpinned items (12 - 3 pinned) split by the SAME fractions
    // make() would use on a 9-item pool: round(0.2*9)=2 to train,
    // round(0.4*9)=4 (capped at 9-2=7) to validation, remainder (3) to test.
    // Train therefore totals 3 pinned + 2 unpinned = 5.
    check(split.train.size() == 5,
          "train = 3 pinned + round(0.2 * 9 unpinned) = 3 + 2 = 5");
    check(split.validation.size() == 4,
          "validation = round(0.4 * 9 unpinned) = 4, none of it pinned");
    check(split.test.size() == 3, "test = the 9-item pool's remainder = 3");

    // Every index still appears exactly once, total.
    std::set<int> all = train;
    for (int i : split.validation) all.insert(i);
    for (int i : split.test) all.insert(i);
    check(all.size() == 12, "every one of the 12 indices still appears exactly once");
}

void testMakeStratifiedDeterministic()
{
    std::printf("\nDatasetSplit::makeStratified: deterministic, same as make() for it\n");
    std::vector<bool> pinned(8, false);
    pinned[2] = true;
    const auto a = DatasetSplit::makeStratified(8, 0.5, 0.25, 99, pinned);
    const auto b = DatasetSplit::makeStratified(8, 0.5, 0.25, 99, pinned);
    check(a.train == b.train && a.validation == b.validation && a.test == b.test,
          "identical (count, fractions, seed, pinning) -> byte-identical partition");
}

void testMakeStratifiedNoPinning()
{
    std::printf(
        "\nDatasetSplit::makeStratified: nothing pinned reduces to make()\n");
    const std::vector<bool> nothingPinned(15, false);
    const auto stratified = DatasetSplit::makeStratified(15, 0.6, 0.2, 55,
                                                          nothingPinned);
    const auto plain = DatasetSplit::make(15, 0.6, 0.2, 55);
    check(stratified.train == plain.train
              && stratified.validation == plain.validation
              && stratified.test == plain.test,
          "an all-false pinning vector produces the exact same partition as "
          "the unstratified make() with the same seed");
}

void testMakeStratifiedMismatchedSizeFallsBack()
{
    std::printf(
        "\nDatasetSplit::makeStratified: a mismatched pinning vector size "
        "falls back to make(), not a crash\n");
    const std::vector<bool> tooShort(3, true); // count is 10, not 3
    const auto stratified = DatasetSplit::makeStratified(10, 0.5, 0.3, 3,
                                                          tooShort);
    const auto plain = DatasetSplit::make(10, 0.5, 0.3, 3);
    check(stratified.train == plain.train
              && stratified.validation == plain.validation
              && stratified.test == plain.test,
          "a size mismatch is treated as \"nothing pinned\" (the documented "
          "fallback), not read out of bounds");
}

void testMakeStratifiedAllPinned()
{
    std::printf(
        "\nDatasetSplit::makeStratified: every item pinned -> everything in "
        "train\n");
    const std::vector<bool> allPinned(6, true);
    const auto split = DatasetSplit::makeStratified(6, 0.1, 0.1, 1, allPinned);
    check(split.train.size() == 6, "all 6 items in train");
    check(split.validation.empty() && split.test.empty(),
          "validation and test are empty -- there is no unpinned pool left "
          "for the fractions to apply to");
}

void testBootstrapSample()
{
    std::printf("\nbootstrapSample: same size as the pool, drawn with replacement\n");
    const std::vector<int> pool{10, 11, 12, 13, 14};
    const auto sample = bootstrapSample(pool, 42);
    check(sample.size() == pool.size(), "the resample is the same size as the pool");
    for (int v : sample)
        check(v >= 10 && v <= 14, "every drawn value actually came from the pool");
}

} // namespace

int main()
{
    std::printf("DatasetSplit - deterministic train/validation/test partitioning\n");
    testMakeBasics();
    testMakeDeterministic();
    testMakeStratifiedPinning();
    testMakeStratifiedDeterministic();
    testMakeStratifiedNoPinning();
    testMakeStratifiedMismatchedSizeFallsBack();
    testMakeStratifiedAllPinned();
    testBootstrapSample();

    std::printf("\n%d check(s) FAILED.\n", failures);
    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
