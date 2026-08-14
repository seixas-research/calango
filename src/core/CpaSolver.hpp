#pragma once

// Required on Linux: libstdc++ (GCC 13+) no longer pulls <cstdint> in
// transitively. Do not remove even if clangd reports it unused on macOS.
#include <cstdint>

#include <array>
#include <complex>
#include <string>
#include <vector>

namespace calango::core {

/// Single-site Coherent Potential Approximation for substitutionally
/// disordered alloys.
///
/// WHAT THIS IS. The CPA replaces a random alloy by an effective ordered
/// medium whose self-energy Σ(z) is chosen so that a single impurity embedded
/// in it produces, on average, no additional scattering. In multiple-scattering
/// (KKR) language that is
///
///     Σ_i c_i τ_i = τ_CPA,
///
/// with τ the scattering path operator. In the site-diagonal single-band
/// representation used here the scattering path operator reduces to the
/// site-diagonal Green's function, so the same condition reads
///
///     Σ_i c_i G_i(z) = G(z),
///
/// which is what solve() drives to zero. The two statements are the same
/// equation; the second is the one that can be iterated without assembling
/// structure constants.
///
/// WHAT THIS IS NOT. There is no all-electron KKR underneath: the medium's
/// band structure arrives as a bare density of states (or a k-resolved
/// dispersion for the Bloch spectral function), not from a radial Dirac solver
/// and real-space structure constants. Component scattering strength is a
/// single on-site level ε_i per spin rather than a full t-matrix. Everything
/// the CPA layer itself does — the self-consistency, the component
/// decomposition, the spectral function — is exact within that representation,
/// and the interface is shaped so a t-matrix backend can replace the on-site
/// levels without disturbing the solver.
///
/// CONVENTIONS. Energies in eV. The retarded Green's function is evaluated at
/// z = E + iη with η > 0, so Im G < 0 and the density of states is −Im G / π.
class CpaSolver {
public:
    /// One component of the substitutional alloy.
    struct Component {
        std::string symbol;
        /// Concentration c_i. The set must sum to 1 (checked).
        double concentration = 0.0;
        /// Spin-averaged on-site level ε_i, in eV, relative to the band centre.
        double onsiteEnergy = 0.0;
        /// Exchange splitting Δ_i in eV: ε_{i↑} = ε_i − Δ_i/2,
        /// ε_{i↓} = ε_i + Δ_i/2. Zero for a non-magnetic component.
        ///
        /// A rigid splitting rather than a self-consistent Stoner loop: it is
        /// what makes a moment appear at all, and it keeps the magnetic case
        /// falsifiable against the non-magnetic one (Δ = 0 must reproduce it
        /// bit for bit).
        double exchangeSplitting = 0.0;
    };

    /// The unperturbed medium, as a quadrature of its bare density of states.
    ///
    /// G(z) = Σ_j w_j / (z − Σ(z) − E_j), which is the quadrature of
    /// ∫ n₀(ε) dε / (z − Σ − ε). Supplying the bare DOS rather than a k-mesh
    /// keeps the Hilbert transform accurate near the real axis, where a sparse
    /// k-sample would show its individual poles.
    struct Lattice {
        /// Sample energies E_j of the bare band (eV).
        std::vector<double> energies;
        /// Quadrature weights w_j. Normalised to 1 by the solver.
        std::vector<double> weights;

        /// Semi-elliptic ("Bethe lattice") band of half-bandwidth W, centred
        /// at zero, on `samples` points. The one bare band whose Hilbert
        /// transform has a closed form, which is what the tests anchor on.
        static Lattice semicircular(double halfBandwidth, int samples = 4001);
        /// Flat band of half-width W — the rectangular DOS, whose Hilbert
        /// transform is an elementary logarithm.
        static Lattice rectangular(double halfWidth, int samples = 4001);
    };

    struct Options {
        /// Imaginary part η added to every real energy, in eV. Physical
        /// broadening as well as numerical regularisation: at η = 0 the
        /// Green's function of a discrete quadrature is a comb of poles.
        double broadening = 0.02;
        /// Convergence threshold on |Σ_new − Σ| in eV.
        double tolerance = 1e-10;
        int maxIterations = 500;
        /// Linear mixing factor for the self-energy update. The Newton-like
        /// update below converges unaided for weak scattering; the split-band
        /// regime needs damping, and 0.5 is the value that survives both.
        double mixing = 0.5;
    };

    /// The converged medium at one energy.
    struct Solution {
        std::complex<double> selfEnergy{0.0, 0.0};
        std::complex<double> greenFunction{0.0, 0.0};
        /// Component-resolved site-diagonal Green's functions G_i, same order
        /// as the components passed in.
        std::vector<std::complex<double>> componentGreen;
        int iterations = 0;
        bool converged = false;
        /// max_i |Σ_i c_i G_i − G| at exit: the CPA condition's own residual,
        /// which is the only honest measure of whether this energy is solved.
        double residual = 0.0;
    };

    // Two overloads rather than `Options options = {}`: a defaulted argument
    // of a nested type whose members have initializers is not yet complete at
    // the point the declaration is parsed, and the compiler says so.
    CpaSolver(std::vector<Component> components, Lattice lattice);
    CpaSolver(std::vector<Component> components, Lattice lattice,
              Options options);

    /// Solve the CPA condition at one complex energy, for one spin channel.
    /// `spin` is +1 (up) or −1 (down); it selects the sign of the exchange
    /// splitting. A non-magnetic alloy gives the same answer for both.
    Solution solve(double energy, int spin = +1) const;

    /// Total density of states −Im G / π at one energy, summed over the spins
    /// actually present (2 for non-magnetic, ↑+↓ for magnetic).
    double totalDos(double energy) const;

    /// Component-projected density of states −Im G_i / π, weighted by c_i so
    /// that Σ_i partialDos_i = totalDos exactly — that identity IS the CPA
    /// condition and the tests check it pointwise.
    std::vector<double> partialDos(double energy) const;

    /// Spin-resolved component DOS at one energy: [component][spin index],
    /// spin index 0 = up, 1 = down. Weighted by c_i as above.
    std::vector<std::array<double, 2>> partialDosBySpin(double energy) const;

    /// Bloch spectral function A(k, E) = −Im[1/(E + iη − ε_k − Σ(E))] / π.
    ///
    /// This is what replaces a band structure in a random alloy: the disorder
    /// gives Σ a finite imaginary part, so a sharp band becomes a Lorentzian
    /// of width −2 Im Σ. A vanishing Im Σ recovers a δ-function, i.e. the
    /// ordered limit, which is one of the tests.
    double blochSpectralFunction(double bandEnergy, double energy,
                                 int spin = +1) const;

    /// One sweep of the spectrum, from which every integral below is derived.
    ///
    /// Exists because the alternative is quadratic waste: locating E_F by
    /// bisecting on a quantity that itself costs a full sweep re-solves the
    /// CPA condition at every energy sixty times over. Solving once and
    /// integrating the cached result is the same physics at 1/60 the cost.
    struct DosGrid {
        std::vector<double> energies;
        /// Spin-summed total DOS at each energy.
        std::vector<double> total;
        /// [energy][component][spin], c_i-weighted. Spin 0 = up, 1 = down.
        std::vector<std::vector<std::array<double, 2>>> partial;
        /// Cumulative ∫ total dE from energies.front(), same length.
        std::vector<double> cumulative;
    };

    /// Sweep the spectrum on `samples` points between the bounds.
    DosGrid computeDosGrid(double lowerBound, double upperBound,
                           int samples = 801) const;

    /// Band filling ∫^{E_F} n(E) dE from a cached sweep, by interpolation.
    static double integratedDos(const DosGrid& grid, double fermiEnergy);

    /// The E_F at which the filling equals `electrons`, by inverting the
    /// cumulative integral of a cached sweep.
    static double findFermiEnergy(const DosGrid& grid, double electrons);

    /// Integrated component moment m_i = ∫^{E_F} (n_i↑ − n_i↓) dE, in μ_B per
    /// atom OF THAT COMPONENT (i.e. divided by c_i, which is what a
    /// per-species moment means).
    ///
    /// E_F is not determined here — the band filling that fixes it is an input
    /// to the alloy problem, not an output of the CPA condition.
    std::vector<double> componentMoments(const DosGrid& grid,
                                         double fermiEnergy) const;

    const std::vector<Component>& components() const { return components_; }

private:
    /// G(z) = Σ_j w_j / (z − ε_j) for the medium, given z already shifted by Σ.
    std::complex<double> latticeGreen(std::complex<double> zeta) const;
    /// ε_i for component i in spin channel `spin`.
    double onsite(const Component& c, int spin) const;
    /// True when any component carries an exchange splitting.
    bool magnetic() const;

    std::vector<Component> components_;
    Lattice lattice_;
    Options options_;
};

} // namespace calango::core
