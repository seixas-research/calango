#pragma once

#include "dft/DftTypes.hpp"

#include <array>
#include <string>
#include <vector>

/// Calango's native Slater-Koster (SCC-)DFTB engine.
///
/// Reads the standard .skf parameter format distributed from dftb.org and
/// used by DFTB+, hotbit and every other Slater-Koster tight-binding code —
/// Calango parses these files with its own code, but does not implement,
/// link, or shell out to DFTB+ or any other tight-binding package. See
/// docs/sphinx/source/simulations/calculators.md for the "Native DFTB"
/// section and the license note on parameter sets (they are data, like a
/// Wannier Hamiltonian or a POTCAR — read, not bundled).
///
/// SPEC: "Format of the v1.0 Slater-Koster files", dftb.org,
/// https://www.dftb.org/fileadmin/DFTB/public/misc/slakoformat.pdf . Every
/// physical quantity in a .skf file is in ATOMIC UNITS (Hartree, Bohr)
/// unless stated otherwise in this header — the parser keeps that
/// convention exactly and Angstrom/eV conversion happens only at the
/// engine's own API boundary (mirroring calango::dft::CalangoDFTEngine).
namespace calango::dftb {

using calango::dft::Outcome;

/// One of the 10 two-center Slater-Koster integral channels a simple-format
/// (angular momentum up to d) .skf file tabulates, in the file's own column
/// order. Only Ppsigma/Ppi/Ss/Sp(sigma) are used by the s,p Hamiltonian this
/// engine builds; d-channel columns are parsed and kept (so a file with a
/// d-shell element still round-trips exactly) but are not yet assembled into
/// a Hamiltonian — see DftbBasis.hpp for the s,p-only scope statement.
enum class SkChannel {
    Ddsigma = 0,
    Ddpi,
    Dddelta,
    Pdsigma,
    Pdpi,
    Ppsigma,
    Pppi,
    Sdsigma,
    Spsigma,
    Sssigma,
    kCount,
};

/// On-site data for one angular-momentum shell (s or p; d is parsed but
/// unused, see above).
struct OnsiteShell {
    double energyHartree = 0.0;    ///< free-atom eigenvalue for this shell
    double hubbardUHartree = 0.0;  ///< the SCC gamma-functional on-site value
    double occupation = 0.0;       ///< neutral, ground-state shell occupation
};

/// One interval of the cubic-spline repulsive (format section 2.2).
struct RepulsiveSplineInterval {
    double startBohr = 0.0;
    double endBohr = 0.0;   ///< advisory only (DFTB+ does not interpret it
                             ///< either) — the NEXT interval's start is what
                             ///< actually bounds evaluation.
    /// c0..c5. c4 and c5 are 0 for every interval except the last, which is
    /// quintic (Eq. 7) rather than cubic (Eq. 6) — the file format's own
    /// asymmetry, not a simplification made here.
    std::array<double, 6> c{};
};

/// Parsed contents of one X-Y.skf file, giving the two-center interaction of
/// species X (first) with species Y (second) — heteronuclear pairs need BOTH
/// X-Y.skf and Y-X.skf, since the column convention is bond-direction
/// dependent (see DftbBasis.hpp for how the two are combined).
struct SlaterKosterFile {
    double gridDistanceBohr = 0.0;
    /// "nGridPoints" exactly as read from line 1. The table itself holds
    /// nGridPoints - 1 rows (see `table`'s own comment) — this is the file
    /// format's own off-by-one, not a bug here.
    int gridPointCount = 0;

    /// True for a same-species file (X == Y), which alone carries the
    /// on-site line.
    bool homonuclear = false;
    /// Indexed by AngularMomentum-as-int (0=s,1=p,2=d); only populated when
    /// `homonuclear`.
    std::array<OnsiteShell, 3> onsite{};

    /// Placeholder in the heteronuclear header, the real atomic mass (amu)
    /// in the homonuclear one.
    double massAmu = 0.0;
    /// Polynomial repulsive coefficients c2..c9 (Eq. 1), coefficients[0] is
    /// c2. Zero (with polyRcutBohr == 0) when the file uses a spline
    /// repulsive instead.
    std::array<double, 8> polyCoefficients{};
    double polyRcutBohr = 0.0;

    /// Row i (0-based) holds the 20 H/S values at distance
    /// (i + 1) * gridDistanceBohr — the table starts one grid step away from
    /// r = 0, per the format spec (the on-site line already gives r = 0).
    /// Column order within a row: SkChannel for H (10), then the same 10
    /// channels for S.
    std::vector<std::array<double, 20>> table;

    bool hasSpline = false;
    /// Short-range exponential repulsive (Eq. 5), valid below the first
    /// spline interval's start.
    double splineExpA1 = 0.0;
    double splineExpA2 = 0.0;
    double splineExpA3 = 0.0;
    double splineCutoffBohr = 0.0;
    std::vector<RepulsiveSplineInterval> splineIntervals;

    /// H or S value of `channel` at physical distance `rBohr`, LOCAL cubic
    /// (4-point Lagrange) interpolation over the nearest tabulated points —
    /// not a global spline. Matches the interpolation production DFTB codes
    /// use for this exact grid (a global natural spline would need an O(N)
    /// precompute this file's own callers have no reason to pay for). 0
    /// beyond the last tabulated point (the two-center interaction is
    /// defined to vanish there) and 0 for r below the first grid point
    /// (r < gridDistanceBohr; no .skf table extends that close, and the
    /// on-site line already covers r = 0 for the diagonal).
    double integral(bool isOverlap, SkChannel channel, double rBohr) const;
    /// d(integral)/dr at `rBohr` (Hartree/Bohr for H, 1/Bohr for S) — the
    /// analytic derivative of the SAME local cubic used by `integral`, so a
    /// force evaluator differentiates exactly what the energy evaluator
    /// reads, not a separately-fitted curve.
    double integralDerivative(bool isOverlap, SkChannel channel,
                               double rBohr) const;

    /// Repulsive-pair energy contribution at `rBohr` (Hartree): the spline
    /// piecewise form when `hasSpline`, else the polynomial of Eq. 1. Zero
    /// beyond the cutoff.
    double repulsiveEnergyHartree(double rBohr) const;
    /// dE_rep/dr (Hartree/Bohr) at `rBohr`, the analytic derivative of
    /// whichever form `repulsiveEnergyHartree` uses — needed by the force
    /// evaluator, not by the energy path.
    double repulsiveEnergyDerivativeHartree(double rBohr) const;
};

/// Parse the data block of a .skf file already read into memory.
///
/// `homonuclearHint` disambiguates the header format ONLY when the caller
/// cannot infer it from the file pair being loaded (SlaterKosterTable always
/// knows and does not need this); left at its default, the parser infers
/// homonuclear-ness from whether the second line parses as 10 on-site values
/// (Ed Ep Es SPE Ud Up Us fd fp fs — always well-formed as 10 numbers) versus
/// a repulsive header line (20 numbers, of which the trailing 10 are inert
/// placeholders) — the two are different widths, which is what a real
/// homonuclear/heteronuclear pair of files always is caught by, and a test
/// asserts both counts are exactly what the format specifies.
///
/// Extended (angular-momentum-up-to-f, '@'-prefixed) files are rejected with
/// a clear InvalidInput Outcome — see FUTURE.md. Every mainstream light- and
/// medium-element parameter set (mio, 3ob, pbc, matsci) ships the simple
/// (up-to-d) format this parser reads.
Outcome parseSlaterKosterFile(const std::string& text, SlaterKosterFile& out);

/// Read `path` and parse it (Outcome::invalid on a missing/unreadable file).
Outcome loadSlaterKosterFile(const std::string& path, SlaterKosterFile& out);

} // namespace calango::dftb
