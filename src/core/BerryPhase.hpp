#pragma once

// Required on Linux: libstdc++ (GCC 13+) no longer pulls <cstdint> in
// transitively. Do not remove even if clangd reports it unused on macOS.
#include <cstdint>

#include "core/WannierHamiltonian.hpp"

#include <array>
#include <vector>

namespace calango::core {

/// Berry-phase quantities from a Wannier representation.
///
/// NATIVE. Wang, Yates, Souza and Vanderbilt (PRB 74, 195118) is the reference
/// for the interpolation formulas and postw90 for the feature scope; nothing
/// here calls, links or reads those codes. The input is a WannierHamiltonian.
///
/// GAUGE. This is the whole difficulty of the subject, so the choices are
/// stated once here.
///
///  * Berry PHASES are computed as Wilson loops — the product of overlap
///    matrices between neighbouring k-points, phase extracted from the
///    determinant. That discretisation is gauge covariant by construction:
///    an arbitrary phase on |u_n(k_j)⟩ appears once as a bra and once as a
///    ket and cancels, so the answer does not depend on what the eigensolver
///    happened to return. Computing a phase as a sum of ⟨u|∂u⟩ instead would
///    not have that property.
///
///  * Berry CURVATURE uses the Kubo form, a sum over band pairs of
///    ⟨n|∂_αH|m⟩⟨m|∂_βH|n⟩/(ε_n − ε_m)². Every factor is a matrix element
///    between eigenstates, so the arbitrary phases cancel pairwise; the
///    formula never differentiates an eigenvector and so never needs them to
///    vary smoothly with k.
///
///  * PERIODIC GAUGE. The Hamiltonian is built in convention I,
///    H(k) = Σ_R e^{ik·R} H(R), with the orbital positions NOT in the phase.
///    In that convention H(k+G) = H(k) exactly, so |u(k+G)⟩ = |u(k)⟩ and a
///    Wilson loop closes on itself with no correction factor. Convention II
///    (positions in the phase) would need an explicit e^{-iG·τ} on the closing
///    overlap; mixing the two is the classic way to get a Zak phase wrong by
///    a constant.
class BerryPhase {
public:
    /// e²/ħ in SI, for the anomalous Hall conductivity.
    static constexpr double kEsquaredOverHbar = 2.4341348e-4; ///< S (=A/V)
    /// Conductance quantum e²/h, S. A 2D Chern insulator has
    /// σ_xy = C e²/h exactly.
    static constexpr double kConductanceQuantum = 3.8740458e-5;

    struct Options {
        /// Bands included as "occupied" for the loop / polarization / AHC.
        /// Empty means "every band below `fermiLevel`".
        std::vector<int> occupiedBands;
        double fermiLevel = 0.0;
        /// Points per Wilson loop. The loop is exact in the limit of many
        /// points; too few and the discretisation shows up as a phase that is
        /// not quantised where it should be.
        int loopPoints = 64;
        /// Mesh for Brillouin-zone integrals (AHC, Chern number).
        std::array<int, 3> kmesh{48, 48, 1};
    };

    /// One Wilson loop.
    struct LoopResult {
        /// Total Berry phase of the occupied manifold, in radians, wrapped to
        /// (−π, π].
        double berryPhase = 0.0;
        /// Individual Wilson-loop eigenphases — the hybrid Wannier centres of
        /// the loop, in units of the loop period. Their sum is the total phase
        /// modulo 2π, and their INDIVIDUAL flow is what distinguishes a
        /// topological band from a trivial one.
        std::vector<double> wannierCentres;
    };

    BerryPhase(WannierHamiltonian hamiltonian, Options options);
    explicit BerryPhase(WannierHamiltonian hamiltonian);

    /// Berry phase around a closed path given as fractional k-points. The path
    /// must close by a reciprocal lattice vector; the last point is NOT
    /// repeated (the loop closes back onto the first internally).
    LoopResult wilsonLoop(const std::vector<std::array<double, 3>>& path) const;

    /// Wilson loop along reciprocal direction `axis`, at fixed values of the
    /// other two. The building block of hybrid Wannier centres, polarization
    /// and the Z2/Chern flow.
    LoopResult wilsonLoopAlong(int axis,
                               const std::array<double, 3>& base) const;

    /// Berry curvature Ω_n^{αβ}(k) of one band, in Å².
    ///
    /// Kubo form. The (ε_n − ε_m)² denominator diverges at a degeneracy — that
    /// is physical, curvature really does diverge at a band touching — so
    /// pairs closer than `degeneracyCutoff` are skipped and reported rather
    /// than silently producing an enormous number.
    double bandCurvature(const std::array<double, 3>& kFractional, int band,
                         int alpha, int beta,
                         double degeneracyCutoff = 1e-5) const;

    /// Summed curvature of the occupied manifold at one k, Å².
    double totalCurvature(const std::array<double, 3>& kFractional, int alpha,
                          int beta) const;

    /// Curvature sampled on a 2D plane, for a colour map.
    struct CurvatureMap {
        std::vector<double> axis1; ///< fractional coordinate along `dir1`
        std::vector<double> axis2;
        /// [i1][i2] in Å².
        std::vector<std::vector<double>> values;
        double minimum = 0.0;
        double maximum = 0.0;
    };
    CurvatureMap curvaturePlane(int dir1, int dir2, int samples1, int samples2,
                                double fixedCoordinate, int alpha,
                                int beta) const;

    /// Anomalous Hall conductivity σ_αβ from the BZ integral of the curvature.
    ///
    /// Returns SI (S/m) for a 3D cell. `chernNumber` is the same integral in
    /// the dimensionless normalisation (1/2π)∫Ω d²k, which is an integer for a
    /// gapped 2D system and is the sharper thing to test.
    struct HallResult {
        double sigmaSI = 0.0;      ///< S/m
        double chernNumber = 0.0;  ///< (1/2π) ∫ Ω d²k over the 2D BZ
        /// Conductance per layer in units of e²/h — for a 2D model this is
        /// the Chern number and is what a quantised Hall plateau reports.
        double sigmaInConductanceQuanta = 0.0;
        int degeneraciesSkipped = 0;
    };
    HallResult anomalousHall(int alpha, int beta) const;

    /// Electric polarization along `axis` from the Berry phase, in e·Å per
    /// unit cell, and in C/m². Defined only modulo the quantum eR/V, which is
    /// reported alongside — a polarization quoted without its quantum is not
    /// a number, it is one branch of one.
    struct Polarization {
        double phaseRadians = 0.0;
        double dipolePerCell = 0.0;  ///< e·Å
        double siValue = 0.0;        ///< C/m²
        double quantumSI = 0.0;      ///< the modulo, C/m²
    };
    Polarization polarization(int axis, int transverseSamples = 16) const;

    /// Hybrid Wannier centre flow: the Wilson-loop eigenphases along `axis`
    /// as a function of the transverse coordinate. This is the same object the
    /// existing topological-invariants feature plots; it is exposed here so a
    /// Chern number can be read off the winding without leaving the module.
    struct CentreFlow {
        std::vector<double> transverse;               ///< fractional
        std::vector<std::vector<double>> centres;     ///< [step][band]
        /// Net winding of the summed centres over the full period — the Chern
        /// number, as an integer when the manifold is gapped.
        double winding = 0.0;
    };
    CentreFlow wannierCentreFlow(int loopAxis, int transverseAxis,
                                 int steps) const;

    const Options& options() const { return options_; }

private:
    std::vector<int> occupiedAt(const std::vector<double>& energies) const;

    WannierHamiltonian hamiltonian_;
    Options options_;
};

} // namespace calango::core
