#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace calango::core {

/// A phase, reduced to the only thing a phase diagram needs from it: the
/// ability to answer "what is your molar Gibbs energy at this composition and
/// temperature".
///
/// A std::function rather than a model struct on purpose. The diagram code has
/// to work identically for a phase assessed here from DFT (Redlich-Kister
/// coefficients this module fitted) and for one read out of somebody else's
/// `.tdb` (an SGTE expression tree with LN and T^-1 terms), and those two have
/// nothing whatever in common except this one question.
struct GibbsPhase {
    std::string name;
    /// Molar Gibbs energy in J per mole of ATOMS. Per atom, not per formula
    /// unit: a diagram plots mole fraction, so two phases with different
    /// formula units must be compared on the same denominator or the common
    /// tangent is drawn between incompatible quantities.
    std::function<double(double moleFractionB, double temperatureK)> gibbs;
    /// The composition interval the phase exists over. A stoichiometric
    /// compound sets both to the same value and is sampled once.
    double minMoleFraction = 0.0;
    double maxMoleFraction = 1.0;
};

/// A two-phase field at one temperature: the horizontal tie-line joining the
/// two compositions in equilibrium.
struct BinaryTieLine {
    double xLeft = 0.0;
    double xRight = 0.0;
    int leftPhase = -1;  ///< index into BinaryPhaseDiagram::phaseNames
    int rightPhase = -1;
};

/// The equilibrium state of a binary system at one temperature.
struct BinarySection {
    double temperatureK = 0.0;
    /// Lower-hull vertices in ascending composition: the compositions at which
    /// some phase is the stable one.
    std::vector<double> vertexX;
    std::vector<int> vertexPhase;
    /// The gaps between consecutive vertices — the two-phase fields. A tie-line
    /// whose two ends carry the SAME phase index is a miscibility gap, which is
    /// as real a two-phase equilibrium as any other.
    std::vector<BinaryTieLine> tieLines;
};

struct BinaryPhaseDiagramOptions {
    double minTemperatureK = 300.0;
    double maxTemperatureK = 2000.0;
    int temperatureSteps = 150;
    /// Composition grid resolution. This IS the resolution of every phase
    /// boundary the diagram reports: a solubility limit can only be located to
    /// one grid step, because the construction is a convex hull over sampled
    /// points and not a root-find on the common-tangent condition. 401 steps
    /// puts that at 0.0025 in mole fraction, which is finer than the
    /// assessment underneath it.
    int compositionSteps = 401;
};

struct BinaryPhaseDiagram {
    std::vector<std::string> phaseNames;
    std::vector<BinarySection> sections; ///< ascending in temperature
};

/// Equilibrium at one temperature by the common-tangent construction.
///
/// The construction IS a lower convex hull: the stable state of a system at
/// composition x is the lowest Gibbs energy reachable by any mixture of
/// phases, which is exactly the lower convex envelope of all the phases'
/// G(x) curves taken together. Where the envelope follows one curve the system
/// is single-phase; where it cuts across as a straight line, that line is the
/// common tangent and its two ends are the two phases in equilibrium.
///
/// This is why the module needs no equilibrium solver and therefore no
/// pycalphad: there is no non-linear system to solve for a binary, only a
/// hull to build.
///
/// The hull here pops COLLINEAR points, unlike core::computeConvexHull which
/// deliberately keeps them. The two want opposite things and both are right:
/// a formation-energy diagram counts a configuration sitting exactly on a
/// tie-line as a stable ground state, while a phase diagram must treat the
/// sampled points along a tie-line as the two-phase mixture they are — keeping
/// them would shatter one two-phase field into four hundred single-phase ones.
BinarySection computeBinarySection(const std::vector<GibbsPhase>& phases,
                                   double temperatureK, int compositionSteps);

BinaryPhaseDiagram
computeBinaryPhaseDiagram(const std::vector<GibbsPhase>& phases,
                          const BinaryPhaseDiagramOptions& options);

/// The stable assemblage at composition `x` in `section`: one phase index for
/// a single-phase field, two for a two-phase field, empty when x is outside
/// every phase's range.
std::vector<int> binaryAssemblageAt(const BinarySection& section, double x);

// ---------------------------------------------------------------------------
// Ternary isothermal sections
// ---------------------------------------------------------------------------

/// A ternary phase. Composition is (x_B, x_C) with x_A = 1 − x_B − x_C.
struct TernaryGibbsPhase {
    std::string name;
    std::function<double(double xB, double xC, double temperatureK)> gibbs;
};

struct TernaryPoint {
    double xB = 0.0;
    double xC = 0.0;
    double gibbsJPerMol = 0.0;
    int phase = -1;
};

/// One triangle of the lower hull, by index into TernaryIsothermalSection::points.
///
/// The number of DISTINCT phases among its three vertices is the number of
/// phases in equilibrium there: one is a single-phase field, two a tie-line
/// (two-phase) region, three a three-phase triangle. That correspondence is
/// the whole content of an isothermal section, and it falls out of the hull
/// rather than being decided by a rule.
struct TernaryFacet {
    int vertex[3] = {-1, -1, -1};
};

struct TernaryIsothermalSection {
    bool ok = false;
    std::string note;
    double temperatureK = 0.0;
    std::vector<std::string> phaseNames;
    std::vector<TernaryPoint> points;
    std::vector<TernaryFacet> facets;
};

struct TernarySectionOptions {
    double temperatureK = 1000.0;
    /// Samples along each edge of the composition triangle. The grid is
    /// (gridSteps+1)(gridSteps+2)/2 points PER PHASE, and the hull is
    /// O(n·faces), so this is the knob that decides whether the section takes
    /// a millisecond or a minute.
    int gridSteps = 40;
};

/// Isothermal section of a ternary system.
///
/// Same principle as the binary, one dimension up: the equilibrium state is
/// the lower convex hull of the Gibbs surface, now a hull in 3D over the
/// composition triangle.
///
/// NUMERICAL NOTE, and it matters. The raw coordinates are mole fractions in
/// [0,1] against energies of order 10^4 J/mol, and a convex hull built on
/// coordinates with a 10^4 aspect ratio degenerates. Before hulling, the
/// energy has the plane through the three corner values subtracted and is then
/// scaled to unit range. Both operations are exactly lower-hull-preserving —
/// subtracting an affine function of (x_B, x_C) and scaling z by a positive
/// constant map the vertical direction to itself — so this is conditioning,
/// not approximation.
TernaryIsothermalSection
computeTernaryIsothermalSection(const std::vector<TernaryGibbsPhase>& phases,
                                const TernarySectionOptions& options);

/// Lower convex hull of a set of 3D points, as triangles.
///
/// Exposed rather than hidden because it is the part most likely to be wrong
/// and the part most easily checked in isolation: the projected areas of the
/// returned triangles must sum to the area of the 2D convex hull of the
/// inputs, which is a closed form for any grid a test cares to build.
///
/// Coplanar input is handled rather than refused — three pure components with
/// no solution phases produce exactly one triangle, and that is a real
/// three-phase field, not a degeneracy to reject.
std::vector<std::array<int, 3>>
lowerConvexHull3d(const std::vector<double>& x, const std::vector<double>& y,
                  const std::vector<double>& z);

} // namespace calango::core
