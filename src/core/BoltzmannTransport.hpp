#pragma once

// Required on Linux: libstdc++ (GCC 13+) no longer pulls <cstdint> in
// transitively. Do not remove even if clangd reports it unused on macOS.
#include <cstdint>

#include "core/WannierHamiltonian.hpp"

#include <array>
#include <vector>

namespace calango::core {

/// Electronic and thermoelectric transport from a Wannier-interpolated band
/// structure, in the constant relaxation-time approximation.
///
/// NATIVE. BoltzWann and postw90 are the reference for which quantities are
/// worth reporting, the standard formulas and sensible defaults; nothing here
/// invokes them, links them, or requires a file they produce. The input is a
/// WannierHamiltonian, which Calango builds itself.
///
/// THE PHYSICS
///
/// Everything follows from one energy-resolved object, the transport
/// distribution function (also called the spectral conductivity):
///
///     Σ_αβ(ε) = (1/V N_k) Σ_{n,k} τ v_nα(k) v_nβ(k) δ(ε − ε_nk)
///
/// from which the three Onsager moments are
///
///     L^(m)_αβ = ∫ dε Σ_αβ(ε) (ε − μ)^m (−∂f/∂ε)
///
/// and then
///
///     σ   = e² L⁰
///     S   = (1/(e T)) (L⁰)⁻¹ L¹
///     κ_e = (1/(e² T)) [ L² − L¹ (L⁰)⁻¹ L¹ ]
///
/// κ_e is the ELECTRONIC thermal conductivity at zero electric current, which
/// is the quantity zT wants; the bare L² would be the zero-field value and is
/// larger. The difference is the Peltier heat carried by the compensating
/// current, and dropping it inflates zT.
///
/// THE RELAXATION TIME. τ enters σ and κ_e linearly and CANCELS EXACTLY in S,
/// because S is a ratio of two moments that both carry one factor of τ. So a
/// constant-τ Seebeck coefficient is a genuine prediction, while σ and κ_e are
/// only as good as the τ supplied. The interface takes τ as a scalar and the
/// integrals keep it inside the energy integral, so an energy-dependent
/// τ(ε) can be dropped in later without restructuring anything.
class BoltzmannTransport {
public:
    /// Physical constants, SI unless noted.
    static constexpr double kBoltzmann_eV = 8.617333262e-5;  ///< eV/K
    static constexpr double kElementaryCharge = 1.602176634e-19; ///< C
    /// Sommerfeld value π²k_B²/(3e²), in W·Ω/K². The Wiedemann-Franz limit
    /// κ_e/(σT) must approach this in a degenerate metal, which is one of the
    /// tests.
    static constexpr double kLorenzNumber = 2.44e-8;

    struct Options {
        /// Dense interpolation mesh. Transport integrals converge far more
        /// slowly than a band structure: the Fermi surface has to be resolved,
        /// not just crossed.
        std::array<int, 3> kmesh{20, 20, 20};
        /// Energy grid for Σ(ε), in eV, relative to the same zero as the
        /// Hamiltonian.
        double energyMin = -2.0;
        double energyMax = 2.0;
        int energyBins = 800;
        /// Gaussian width used to resolve δ(ε − ε_nk), in eV. Must exceed the
        /// level spacing of the mesh or Σ(ε) degenerates into isolated spikes;
        /// the module reports the ratio so that is visible rather than
        /// guessed at.
        double smearing = 0.02;
        /// Constant relaxation time in seconds. 10 fs is the usual order for a
        /// metal at room temperature and is a placeholder, not a prediction.
        double relaxationTime = 1.0e-14;
        /// Lattice thermal conductivity in W/(m·K), user input: it is a phonon
        /// quantity and nothing in an electronic structure determines it.
        double latticeThermalConductivity = 1.0;
        /// Spin degeneracy folded into the carrier count and the conductivity.
        /// 2 for a non-spin-polarised calculation.
        int spinDegeneracy = 2;
    };

    /// Everything reported at one (T, μ).
    struct Point {
        double temperature = 0.0;   ///< K
        double chemicalPotential = 0.0; ///< eV
        /// Conductivity tensor, S/m. Row-major 3x3.
        std::array<double, 9> sigma{};
        /// Seebeck tensor, V/K.
        std::array<double, 9> seebeck{};
        /// Electronic thermal conductivity at zero current, W/(m·K).
        std::array<double, 9> kappaElectronic{};
        /// Power factor S²σ, W/(m·K²). Trace-averaged scalar as well as the
        /// tensor, because that is what gets quoted.
        std::array<double, 9> powerFactor{};
        /// Figure of merit, dimensionless, using the supplied κ_L.
        std::array<double, 9> zT{};
        /// Carrier concentration in cm⁻³. Positive means electrons above the
        /// reference filling, negative means holes.
        double carrierConcentration = 0.0;
        /// Isotropic averages (one third of the trace) for quick reporting.
        double sigmaAvg = 0.0;
        double seebeckAvg = 0.0;
        double kappaAvg = 0.0;
        double powerFactorAvg = 0.0;
        double zTAvg = 0.0;
        /// κ_e/(σT), which must approach kLorenzNumber in a degenerate metal.
        double lorenzRatio = 0.0;
    };

    struct SpectralConductivity {
        std::vector<double> energies;            ///< eV
        /// Σ_αβ(ε), row-major 3x3 per energy, in SI (1/(Ω·m·s) × s).
        std::vector<std::array<double, 9>> sigma;
        /// Density of states per unit cell per eV, both spins, for reference.
        std::vector<double> dos;
        /// Mean level spacing of the mesh near the middle of the window, eV.
        /// Compared against the smearing to say whether the mesh is dense
        /// enough for the broadening chosen.
        double meanLevelSpacing = 0.0;
    };

    BoltzmannTransport(WannierHamiltonian hamiltonian, Options options);
    explicit BoltzmannTransport(WannierHamiltonian hamiltonian);

    /// Sweep the mesh once and build Σ(ε). Every (T, μ) afterwards is an
    /// integral over this, which is the whole reason the constant-τ
    /// approximation is cheap: the k-sum is done once, not per temperature.
    const SpectralConductivity& spectralConductivity() const;

    /// Transport at one temperature and chemical potential.
    Point evaluate(double temperature, double chemicalPotential) const;

    /// Electrons per unit cell below μ at temperature T, from the same
    /// interpolated bands. The reference for `carrierConcentration`.
    double electronCount(double temperature, double chemicalPotential) const;

    const Options& options() const { return options_; }
    const WannierHamiltonian& hamiltonian() const { return hamiltonian_; }

private:
    void build() const;

    WannierHamiltonian hamiltonian_;
    Options options_;
    /// Reference electron count, taken at the middle of the energy window, so
    /// carrier concentration is reported relative to a definite filling.
    mutable double referenceElectrons_ = 0.0;
    mutable SpectralConductivity spectral_;
    mutable bool built_ = false;
};

} // namespace calango::core
