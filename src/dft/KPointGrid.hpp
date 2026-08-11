#pragma once

#include "dft/DftTypes.hpp"

#include <array>
#include <cstddef>
#include <vector>

namespace calango::dft {

/// One sampling point of the Brillouin zone.
struct KPoint {
    /// Fractional (reciprocal-lattice) coordinates in [0, 1).
    std::array<double, 3> fractional{{0.0, 0.0, 0.0}};
    /// Fraction of the full mesh this point stands for. Weights sum to 1.
    double weight = 0.0;
    /// How many points of the full mesh it represents.
    std::size_t orbitSize = 1;
};

/// A symmetry operation of the crystal, as an integer matrix in the basis of
/// the LATTICE VECTORS.
///
/// Integer because that is what "maps the lattice onto itself" means: a
/// rotation is a crystal symmetry exactly when its matrix in the lattice basis
/// has integer entries. Storing it that way rather than as a Cartesian
/// rotation makes every subsequent test exact — orbit membership, closure,
/// whether a k-point maps onto another mesh point — with no tolerance to tune.
struct SymmetryOperation {
    /// Row-major W, acting on FRACTIONAL DIRECT coordinates: p → W p (+ t).
    std::array<int, 9> matrix{{1, 0, 0, 0, 1, 0, 0, 0, 1}};
    /// True for the operations added by time reversal rather than found in
    /// the crystal, kept so the two reductions can be reported separately.
    bool fromTimeReversal = false;

    int at(int row, int column) const
    {
        return matrix[static_cast<std::size_t>(row * 3 + column)];
    }
};

/// Monkhorst-Pack sampling of the Brillouin zone, with reduction to the
/// irreducible wedge.
///
/// Why this matters more than its size suggests: for silicon the cohesive
/// energy moves by several electronvolts per atom between a Gamma-only and a
/// 2x2x2 mesh, while a whole extra basis tier moves it by half of one. Until
/// the sampling is converged, no other convergence question can be asked. A
/// dense mesh is only affordable if symmetry-equivalent points are computed
/// once, and for a face-centred cubic crystal that is a factor approaching 48.
///
/// THE ALGORITHM.
///
///   1. The LATTICE point group: every integer matrix W with WᵀGW = G, where
///      G is the metric of the lattice vectors. That condition is exactly the
///      statement that the corresponding Cartesian map is orthogonal and takes
///      the lattice to itself.
///   2. The CRYSTAL point group: those W for which some translation t makes
///      p → Wp + t map the set of atoms onto itself, species by species. A
///      lattice symmetry that the basis breaks is not a symmetry of the
///      crystal.
///   3. In reciprocal space a k-point transforms with the INVERSE TRANSPOSE,
///      k → W^{-T}k, which follows from requiring k·p invariant. W^{-T} is
///      again integer because det W = ±1.
///   4. Time reversal adds k → −k for a real Hamiltonian, which is what a
///      non-magnetic calculation with no spin-orbit coupling has.
///   5. Operations that do not map the mesh onto itself are DISCARDED. A
///      4x4x2 mesh does not have the symmetry of its own crystal, and using an
///      operation that moves a mesh point off the mesh would silently
///      mis-weight the sum.
///
/// WHAT REDUCTION IS SAFE, AND WHY THEY ARE SEPARATED.
///
/// Reducing the k-set changes the DENSITY, not just the band energy. Summing
/// w_k|ψ_k|² over a wedge does not reproduce the full-zone sum, because the
/// orbit members contribute |ψ_k(R⁻¹r)|² and not |ψ_k(r)|²; a point-group
/// reduction is exact only if the density is symmetrised afterwards.
///
/// Time reversal is the exception: |ψ_{−k}|² = |ψ_k*|² = |ψ_k|² pointwise for
/// a real Hamiltonian, so folding k and −k together needs no symmetrisation
/// and is exact as it stands. The two are therefore selectable separately,
/// and `dft_kpoints` measures what each does to a converged energy rather than
/// assuming.
class KPointGrid {
public:
    struct Atom {
        int atomicNumber = 0;
        std::array<double, 3> position{{0.0, 0.0, 0.0}}; ///< cartesian bohr
    };

    /// Which symmetries to fold the mesh with.
    enum class Symmetry {
        /// Every point of the full mesh, equal weights.
        None,
        /// k and −k identified. Exact for the density with no further work.
        TimeReversal,
        /// The full crystal point group and time reversal. Requires the
        /// density to be symmetrised to be exact — see the class comment.
        PointGroup,
    };

    /// Build the mesh for a periodic cell and reduce it.
    ///
    /// `lattice` empty (a finite system) yields the single point Gamma with
    /// weight 1, whatever the divisions say: there is no Brillouin zone to
    /// sample and it is not a choice.
    Outcome build(std::array<int, 3> divisions,
                  const std::vector<std::array<double, 3>>& lattice,
                  const std::vector<Atom>& atoms, Symmetry symmetry);

    const std::vector<KPoint>& points() const { return points_; }
    std::size_t size() const { return points_.size(); }
    /// Points in the unreduced mesh, so a caller can report the saving.
    std::size_t fullMeshSize() const { return fullMesh_; }
    /// The operations actually used to fold the mesh.
    const std::vector<SymmetryOperation>& operations() const
    {
        return operations_;
    }
    /// Operations of the crystal, before any were discarded for being
    /// incompatible with the chosen divisions. Its size is the order of the
    /// crystal point group — 48 for a face-centred cubic element.
    std::size_t crystalOperationCount() const { return crystalOperations_; }

    /// The crystal point group: integer matrices in the lattice basis.
    /// Exposed because it is independently checkable — the order of the group
    /// of a known structure is a published number.
    static std::vector<SymmetryOperation> crystalPointGroup(
        const std::vector<std::array<double, 3>>& lattice,
        const std::vector<Atom>& atoms, double tolerance = 1.0e-5);

    /// The point group of the LATTICE alone, ignoring what sits on it.
    /// Always a supergroup of the crystal's.
    static std::vector<SymmetryOperation> latticePointGroup(
        const std::vector<std::array<double, 3>>& lattice,
        double tolerance = 1.0e-5);

private:
    std::vector<KPoint> points_;
    std::vector<SymmetryOperation> operations_;
    std::size_t fullMesh_ = 0;
    std::size_t crystalOperations_ = 0;
};

} // namespace calango::dft
