#pragma once

#include "core/Structure.hpp"

#include <vector>

namespace calango::core {

struct DistributionOptions {
    double cutoff = 3.0; ///< Å, neighbor cutoff defining "bonded" pairs
    int bins = 90;
    bool usePbc = true;  ///< enumerate periodic images within the cutoff
    int elementA = 0;    ///< atomic-number filter, 0 = any (pair/center)
    int elementB = 0;    ///< atomic-number filter for the partner atoms
};

struct HistogramResult {
    std::vector<double> x; ///< bin centers (Å for lengths, degrees for angles)
    std::vector<double> y; ///< counts per bin
};

/// Histogram of interatomic distances below the cutoff (each unordered
/// pair/image counted once). Element filters select the pair species
/// (A–B, order-insensitive; 0 matches anything). Periodic images are
/// enumerated exactly, as in the RDF.
HistogramResult computeBondLengthDistribution(const Structure& structure,
                                              const DistributionOptions& options);

/// Histogram of three-body angles j–i–k (0–180°): for every central atom
/// i (filtered by elementA), all unordered pairs of its neighbor sites
/// within the cutoff (neighbors filtered by elementB). Periodic images
/// count as distinct neighbor sites.
HistogramResult computeBondAngleDistribution(const Structure& structure,
                                             const DistributionOptions& options);

} // namespace calango::core
