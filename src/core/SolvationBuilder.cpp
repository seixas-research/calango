#include "core/SolvationBuilder.hpp"

#include "core/Element.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <numeric>
#include <random>
#include <sstream>
#include <stdexcept>

namespace calango::core {

namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr double kDegree = kPi / 180.0;
/// 1 u/Å³ in g/cm³.
constexpr double kUPerA3ToGCm3 = 1.6605390666;

Vec3 centroidOf(const std::vector<Vec3>& points)
{
    if (points.empty())
        return {};
    Vec3 sum;
    for (const Vec3& p : points)
        sum += p;
    return sum / static_cast<double>(points.size());
}

void recentre(std::vector<Vec3>& points)
{
    const Vec3 centre = centroidOf(points);
    for (Vec3& p : points)
        p = p - centre;
}

// -- Rigid geometries --------------------------------------------------------
//
// Every one of these is FIXED BY SYMMETRY plus a bond length and (for the bent
// and pyramidal cases) one angle, so the coordinates below are exact rather
// than remembered. Species that need a real optimized geometry — the alcohols,
// the alkanes past methane, anything with a torsional degree of freedom — are
// deliberately absent from the library instead of being approximated: a
// molecule with plausible-looking but wrong internal coordinates produces a
// cell that is wrong in a way nobody notices until the first relaxation step
// tears it apart.

std::vector<Vec3> monatomic()
{
    return {{0.0, 0.0, 0.0}};
}

std::vector<Vec3> diatomic(double bond)
{
    return {{0.0, 0.0, -0.5 * bond}, {0.0, 0.0, 0.5 * bond}};
}

/// Linear XY2 (CO2): the central atom first.
std::vector<Vec3> linearTriatomic(double bond)
{
    return {{0.0, 0.0, 0.0}, {0.0, 0.0, bond}, {0.0, 0.0, -bond}};
}

/// Bent XY2 (H2O, H2S, SO2): the central atom first.
std::vector<Vec3> bentTriatomic(double bond, double angleDeg)
{
    const double half = 0.5 * angleDeg * kDegree;
    std::vector<Vec3> p = {
        {0.0, 0.0, 0.0},
        {bond * std::sin(half), bond * std::cos(half), 0.0},
        {-bond * std::sin(half), bond * std::cos(half), 0.0},
    };
    recentre(p);
    return p;
}

/// Trigonal pyramidal XY3 (NH3, H3O+): the central atom first.
///
/// With the three bonds at polar angle β from the C3 axis and 120° apart in
/// azimuth, the Y-X-Y angle θ obeys cos θ = 3/2 cos²β − 1/2, which inverts to
/// the cos β below. That is the whole geometry — no fitted coordinates.
std::vector<Vec3> pyramidal(double bond, double angleDeg)
{
    const double cosTheta = std::cos(angleDeg * kDegree);
    const double cosBeta = std::sqrt(std::max(0.0, (cosTheta + 0.5) / 1.5));
    const double sinBeta = std::sqrt(std::max(0.0, 1.0 - cosBeta * cosBeta));
    std::vector<Vec3> p = {{0.0, 0.0, 0.0}};
    for (int i = 0; i < 3; ++i) {
        const double phi = 2.0 * kPi * i / 3.0;
        p.push_back({bond * sinBeta * std::cos(phi),
                     bond * sinBeta * std::sin(phi), -bond * cosBeta});
    }
    recentre(p);
    return p;
}

/// Tetrahedral XY4 (CH4, NH4+, SO4²⁻, PO4³⁻): the central atom first.
std::vector<Vec3> tetrahedral(double bond)
{
    const double s = bond / std::sqrt(3.0);
    return {{0.0, 0.0, 0.0},
            {s, s, s},
            {s, -s, -s},
            {-s, s, -s},
            {-s, -s, s}};
}

/// Trigonal planar XY3 (NO3⁻, CO3²⁻): the central atom first.
std::vector<Vec3> trigonalPlanar(double bond)
{
    std::vector<Vec3> p = {{0.0, 0.0, 0.0}};
    for (int i = 0; i < 3; ++i) {
        const double phi = 2.0 * kPi * i / 3.0;
        p.push_back({bond * std::cos(phi), bond * std::sin(phi), 0.0});
    }
    return p;
}

using Species = SolvationBuilder::Species;
using Category = SolvationBuilder::Category;

Species make(std::string key, std::string name, std::string formula,
             Category category, double charge, double density, double radius,
             std::vector<int> numbers, std::vector<Vec3> positions)
{
    Species species;
    species.key = std::move(key);
    species.name = std::move(name);
    species.formula = std::move(formula);
    species.category = category;
    species.charge = charge;
    species.referenceDensity = density;
    species.contactRadius = radius;
    species.numbers = std::move(numbers);
    species.positions = std::move(positions);
    return species;
}

Species salt(std::string key, std::string name, std::string formula,
             std::vector<std::string> parts)
{
    Species species;
    species.key = std::move(key);
    species.name = std::move(name);
    species.formula = std::move(formula);
    species.category = Category::Salt;
    species.expandsTo = std::move(parts);
    return species;
}

/// A 3x3 inverse, used once per generate() to turn Cartesian separations into
/// fractional ones for the minimum-image test.
struct Matrix3 {
    double m[3][3]{};

    Vec3 operator*(const Vec3& v) const
    {
        return {m[0][0] * v.x + m[0][1] * v.y + m[0][2] * v.z,
                m[1][0] * v.x + m[1][1] * v.y + m[1][2] * v.z,
                m[2][0] * v.x + m[2][1] * v.y + m[2][2] * v.z};
    }
};

/// Inverse of the matrix whose COLUMNS are the three lattice vectors, i.e. the
/// Cartesian-to-fractional transform. Throws on a degenerate cell.
Matrix3 inverseOfColumns(const std::array<Vec3, 3>& cell)
{
    const double det = cell[0].dot(cell[1].cross(cell[2]));
    if (std::abs(det) < 1e-12)
        throw std::invalid_argument(
            "the structure's cell is degenerate — an interface region can only "
            "be opened in a cell with a non-zero volume");
    // Rows of the inverse are the reciprocal vectors (without the 2π).
    const Vec3 r0 = cell[1].cross(cell[2]) / det;
    const Vec3 r1 = cell[2].cross(cell[0]) / det;
    const Vec3 r2 = cell[0].cross(cell[1]) / det;
    Matrix3 inverse;
    inverse.m[0][0] = r0.x; inverse.m[0][1] = r0.y; inverse.m[0][2] = r0.z;
    inverse.m[1][0] = r1.x; inverse.m[1][1] = r1.y; inverse.m[1][2] = r1.z;
    inverse.m[2][0] = r2.x; inverse.m[2][1] = r2.y; inverse.m[2][2] = r2.z;
    return inverse;
}

/// Uniformly distributed rotation matrix (Shoemake's quaternion construction).
/// A liquid has no preferred molecular axis, and sampling Euler angles
/// uniformly does NOT give a uniform rotation — it clusters orientations near
/// the poles, which would show up as a spurious orientational order parameter
/// at the interface.
std::array<Vec3, 3> randomRotation(std::mt19937& rng)
{
    std::uniform_real_distribution<double> unit(0.0, 1.0);
    const double u1 = unit(rng);
    const double u2 = unit(rng);
    const double u3 = unit(rng);
    const double s1 = std::sqrt(1.0 - u1);
    const double s2 = std::sqrt(u1);
    const double x = s1 * std::sin(2.0 * kPi * u2);
    const double y = s1 * std::cos(2.0 * kPi * u2);
    const double z = s2 * std::sin(2.0 * kPi * u3);
    const double w = s2 * std::cos(2.0 * kPi * u3);
    return {Vec3{1 - 2 * (y * y + z * z), 2 * (x * y - z * w),
                 2 * (x * z + y * w)},
            Vec3{2 * (x * y + z * w), 1 - 2 * (x * x + z * z),
                 2 * (y * z - x * w)},
            Vec3{2 * (x * z - y * w), 2 * (y * z + x * w),
                 1 - 2 * (x * x + y * y)}};
}

Vec3 rotate(const std::array<Vec3, 3>& rotation, const Vec3& v)
{
    return {rotation[0].dot(v), rotation[1].dot(v), rotation[2].dot(v)};
}

/// Shortest tolerated separation (Å) between two atoms of DIFFERENT fluid
/// molecules.
///
/// The centre-to-centre exclusion alone is not enough: two waters at exactly
/// the O-O contact distance can still be oriented mouth to mouth, which puts
/// their hydrogens ~1 Å apart — a geometry no relaxation recovers from
/// gracefully. These are floors, not equilibrium distances: 1.70 Å for an
/// X···H pair sits just under a real hydrogen bond (H···O ≈ 1.8 Å), so the
/// packing may produce something on its way to being one without ever
/// producing an overlap.
double pairMinimum(int za, int zb)
{
    const bool firstIsHydrogen = za == 1;
    const bool secondIsHydrogen = zb == 1;
    if (firstIsHydrogen && secondIsHydrogen)
        return 1.80;
    if (firstIsHydrogen || secondIsHydrogen)
        return 1.70;
    return 2.40;
}

/// The largest pairMinimum, used to bound which placed molecules can possibly
/// have a close atom contact with a candidate.
constexpr double kMaxPairMinimum = 2.40;

std::string formatNumber(double value, int decimals)
{
    std::ostringstream out;
    out.setf(std::ios::fixed);
    out.precision(decimals);
    out << value;
    return out.str();
}

} // namespace

double SolvationBuilder::Species::molarMassU() const
{
    double mass = 0.0;
    for (int z : numbers)
        mass += Elements::atomicMass(z);
    return mass;
}

const std::vector<SolvationBuilder::Species>& SolvationBuilder::library()
{
    static const std::vector<Species> kLibrary = [] {
        std::vector<Species> all;

        // -- Liquids -------------------------------------------------------
        // Densities: water at 25 °C; the others at their normal boiling
        // point, which is the condition under which they are a liquid at all.
        all.push_back(make("water", "Water", "H2O", Category::Liquid, 0.0,
                           0.997, 1.40, {8, 1, 1},
                           bentTriatomic(0.9572, 104.52)));
        all.push_back(make("ammonia", "Ammonia", "NH3", Category::Liquid, 0.0,
                           0.682, 1.65, {7, 1, 1, 1},
                           pyramidal(1.012, 106.67)));
        all.push_back(make("hydrogen_fluoride", "Hydrogen fluoride", "HF",
                           Category::Liquid, 0.0, 0.99, 1.35, {9, 1},
                           diatomic(0.917)));
        all.push_back(make("hydrogen_sulfide", "Hydrogen sulfide", "H2S",
                           Category::Liquid, 0.0, 0.949, 1.85, {16, 1, 1},
                           bentTriatomic(1.336, 92.1)));

        // -- Gases ---------------------------------------------------------
        // Densities at 273.15 K and 1 bar. In a cell of interface size these
        // amount to a fraction of a molecule, which is physically correct and
        // is why the wizard switches to an explicit count for a gas: the
        // molecule count IS the partial pressure in a periodic cell.
        all.push_back(make("nitrogen", "Nitrogen", "N2", Category::Gas, 0.0,
                           1.234e-3, 1.65, {7, 7}, diatomic(1.098)));
        all.push_back(make("oxygen", "Oxygen", "O2", Category::Gas, 0.0,
                           1.409e-3, 1.60, {8, 8}, diatomic(1.208)));
        all.push_back(make("hydrogen", "Hydrogen", "H2", Category::Gas, 0.0,
                           8.88e-5, 1.35, {1, 1}, diatomic(0.741)));
        all.push_back(make("carbon_dioxide", "Carbon dioxide", "CO2",
                           Category::Gas, 0.0, 1.938e-3, 1.75, {6, 8, 8},
                           linearTriatomic(1.160)));
        all.push_back(make("carbon_monoxide", "Carbon monoxide", "CO",
                           Category::Gas, 0.0, 1.234e-3, 1.70, {6, 8},
                           diatomic(1.128)));
        all.push_back(make("methane", "Methane", "CH4", Category::Gas, 0.0,
                           7.07e-4, 1.90, {6, 1, 1, 1, 1},
                           tetrahedral(1.087)));
        all.push_back(make("ammonia_gas", "Ammonia (gas)", "NH3",
                           Category::Gas, 0.0, 7.51e-4, 1.65, {7, 1, 1, 1},
                           pyramidal(1.012, 106.67)));
        all.push_back(make("water_vapour", "Water vapour", "H2O",
                           Category::Gas, 0.0, 7.94e-4, 1.40, {8, 1, 1},
                           bentTriatomic(0.9572, 104.52)));
        all.push_back(make("nitric_oxide", "Nitric oxide", "NO", Category::Gas,
                           0.0, 1.322e-3, 1.65, {7, 8}, diatomic(1.154)));
        all.push_back(make("sulfur_dioxide", "Sulfur dioxide", "SO2",
                           Category::Gas, 0.0, 2.82e-3, 1.95, {16, 8, 8},
                           bentTriatomic(1.432, 119.5)));
        all.push_back(make("chlorine", "Chlorine", "Cl2", Category::Gas, 0.0,
                           3.12e-3, 2.00, {17, 17}, diatomic(1.988)));
        all.push_back(make("argon", "Argon", "Ar", Category::Gas, 0.0,
                           1.759e-3, 1.70, {18}, monatomic()));
        all.push_back(make("helium", "Helium", "He", Category::Gas, 0.0,
                           1.76e-4, 1.30, {2}, monatomic()));
        all.push_back(make("neon", "Neon", "Ne", Category::Gas, 0.0, 8.89e-4,
                           1.40, {10}, monatomic()));

        // -- Ions ----------------------------------------------------------
        // Contact radii are ionic/hydrated-shell scale rather than bare ionic
        // radii: the packer places bare ions, but they will be solvated the
        // moment the cell is equilibrated, and starting them at bare-ion
        // contact puts a solvent molecule inside the first hydration shell.
        all.push_back(make("li+", "Lithium", "Li⁺", Category::Ion, 1.0, 0.0,
                           1.05, {3}, monatomic()));
        all.push_back(make("na+", "Sodium", "Na⁺", Category::Ion, 1.0, 0.0,
                           1.25, {11}, monatomic()));
        all.push_back(make("k+", "Potassium", "K⁺", Category::Ion, 1.0, 0.0,
                           1.60, {19}, monatomic()));
        all.push_back(make("mg2+", "Magnesium", "Mg²⁺", Category::Ion, 2.0,
                           0.0, 1.05, {12}, monatomic()));
        all.push_back(make("ca2+", "Calcium", "Ca²⁺", Category::Ion, 2.0, 0.0,
                           1.35, {20}, monatomic()));
        all.push_back(make("nh4+", "Ammonium", "NH4⁺", Category::Ion, 1.0, 0.0,
                           1.60, {7, 1, 1, 1, 1}, tetrahedral(1.02)));
        all.push_back(make("h3o+", "Hydronium", "H3O⁺", Category::Ion, 1.0,
                           0.0, 1.45, {8, 1, 1, 1}, pyramidal(0.98, 113.0)));
        all.push_back(make("f-", "Fluoride", "F⁻", Category::Ion, -1.0, 0.0,
                           1.55, {9}, monatomic()));
        all.push_back(make("cl-", "Chloride", "Cl⁻", Category::Ion, -1.0, 0.0,
                           1.90, {17}, monatomic()));
        all.push_back(make("br-", "Bromide", "Br⁻", Category::Ion, -1.0, 0.0,
                           2.05, {35}, monatomic()));
        all.push_back(make("i-", "Iodide", "I⁻", Category::Ion, -1.0, 0.0,
                           2.25, {53}, monatomic()));
        all.push_back(make("oh-", "Hydroxide", "OH⁻", Category::Ion, -1.0, 0.0,
                           1.55, {8, 1}, diatomic(0.964)));
        all.push_back(make("no3-", "Nitrate", "NO3⁻", Category::Ion, -1.0, 0.0,
                           1.95, {7, 8, 8, 8}, trigonalPlanar(1.24)));
        all.push_back(make("co3-2", "Carbonate", "CO3²⁻", Category::Ion, -2.0,
                           0.0, 2.00, {6, 8, 8, 8}, trigonalPlanar(1.29)));
        all.push_back(make("so4-2", "Sulfate", "SO4²⁻", Category::Ion, -2.0,
                           0.0, 2.25, {16, 8, 8, 8, 8}, tetrahedral(1.49)));
        all.push_back(make("po4-3", "Phosphate", "PO4³⁻", Category::Ion, -3.0,
                           0.0, 2.35, {15, 8, 8, 8, 8}, tetrahedral(1.54)));

        // -- Salts ---------------------------------------------------------
        // A formula unit, so "3 units of (NH4)2SO4" inserts six ammonium and
        // three sulfate ions and the cell stays neutral by construction.
        all.push_back(salt("nacl", "Sodium chloride", "NaCl", {"na+", "cl-"}));
        all.push_back(salt("kcl", "Potassium chloride", "KCl", {"k+", "cl-"}));
        all.push_back(salt("licl", "Lithium chloride", "LiCl",
                           {"li+", "cl-"}));
        all.push_back(salt("nh4cl", "Ammonium chloride", "NH4Cl",
                           {"nh4+", "cl-"}));
        all.push_back(salt("cacl2", "Calcium chloride", "CaCl2",
                           {"ca2+", "cl-", "cl-"}));
        all.push_back(salt("mgcl2", "Magnesium chloride", "MgCl2",
                           {"mg2+", "cl-", "cl-"}));
        all.push_back(salt("na2so4", "Sodium sulfate", "Na2SO4",
                           {"na+", "na+", "so4-2"}));
        all.push_back(salt("nh42so4", "Ammonium sulfate", "(NH4)2SO4",
                           {"nh4+", "nh4+", "so4-2"}));
        all.push_back(salt("naoh", "Sodium hydroxide", "NaOH",
                           {"na+", "oh-"}));
        all.push_back(salt("nano3", "Sodium nitrate", "NaNO3",
                           {"na+", "no3-"}));
        return all;
    }();
    return kLibrary;
}

const SolvationBuilder::Species* SolvationBuilder::find(const std::string& key)
{
    for (const Species& species : library())
        if (species.key == key)
            return &species;
    return nullptr;
}

std::string SolvationBuilder::toString(Axis axis)
{
    switch (axis) {
    case Axis::A:
        return "a";
    case Axis::B:
        return "b";
    case Axis::C:
        break;
    }
    return "c";
}

SolvationBuilder::Result SolvationBuilder::generate(const Structure& substrate,
                                                    const Params& params)
{
    if (!substrate.cell().isDefined())
        throw std::invalid_argument(
            "the structure has no periodic cell. An interface region is opened "
            "along a lattice vector, so the substrate needs one — add a cell "
            "(Structure panel -> Add vacuum) or start from a slab.");
    if (params.regionThickness <= 0.0)
        throw std::invalid_argument("the region thickness must be positive");
    if (params.components.empty() && params.ions.empty())
        throw std::invalid_argument(
            "nothing to fill the region with — add at least one liquid, gas or "
            "ionic species");

    const int axis = static_cast<int>(params.axis);
    const int lateralA = (axis + 1) % 3;
    const int lateralB = (axis + 2) % 3;
    const int repeatA = std::max(1, params.lateral[0]);
    const int repeatB = std::max(1, params.lateral[1]);

    // -- Lateral supercell --------------------------------------------------
    std::array<Vec3, 3> cell = substrate.cell().vectors();
    std::vector<Atom> substrateAtoms;
    std::vector<std::size_t> sourceIndex; ///< which input atom each copy came from
    substrateAtoms.reserve(substrate.size()
                           * static_cast<std::size_t>(repeatA * repeatB));
    sourceIndex.reserve(substrateAtoms.capacity());
    for (int ia = 0; ia < repeatA; ++ia) {
        for (int ib = 0; ib < repeatB; ++ib) {
            const Vec3 shift =
                cell[lateralA] * ia + cell[lateralB] * ib;
            for (std::size_t index = 0; index < substrate.size(); ++index) {
                Atom atom = substrate.atoms()[index];
                atom.position = atom.position + shift;
                substrateAtoms.push_back(atom);
                sourceIndex.push_back(index);
            }
        }
    }
    cell[lateralA] = cell[lateralA] * repeatA;
    cell[lateralB] = cell[lateralB] * repeatB;

    // -- Region geometry ----------------------------------------------------
    //
    // The region is bounded by planes normal to a_lateralA x a_lateralB, not
    // by Cartesian planes: with a tilted cell those differ, and a region cut
    // on the Cartesian planes would not be commensurate with the periodicity.
    Vec3 normal = cell[lateralA].cross(cell[lateralB]);
    const double area = normal.norm();
    if (area < 1e-9)
        throw std::invalid_argument(
            "the two lattice vectors perpendicular to the chosen direction are "
            "collinear — the cell is degenerate");
    normal = normal / area;
    if (cell[axis].dot(normal) < 0.0)
        normal = normal * -1.0; // point the normal along the chosen axis

    const double heightBefore = cell[axis].dot(normal);
    if (heightBefore <= 1e-9)
        throw std::invalid_argument(
            "the chosen lattice vector lies in the plane of the other two");

    double lowest = 0.0;
    double highest = 0.0;
    if (!substrateAtoms.empty()) {
        lowest = highest = substrateAtoms.front().position.dot(normal);
        for (const Atom& atom : substrateAtoms) {
            const double projection = atom.position.dot(normal);
            lowest = std::min(lowest, projection);
            highest = std::max(highest, projection);
        }
    }
    const double slabThickness = highest - lowest;

    Result result;
    // Grow (or shrink) the cell so that the gap between the substrate's top
    // face and the bottom face of its own periodic image is EXACTLY the
    // requested thickness, whatever vacuum the input already carried. Asking
    // for a 20 Å water layer and getting 20 Å plus whatever the slab was built
    // with is the classic way to end up with an accidentally dilute interface.
    const double heightAfter = slabThickness + params.regionThickness;
    cell[axis] = cell[axis] * (heightAfter / heightBefore);
    if (heightAfter < heightBefore) {
        result.warnings.push_back(
            "the cell was SHORTENED along " + toString(params.axis)
            + " (from " + formatNumber(heightBefore, 2) + " to "
            + formatNumber(heightAfter, 2)
            + " Å): the structure already carried more vacuum than the "
              "requested region");
    }

    // Anchoring moves the substrate to the bottom of the cell so the region is
    // one contiguous block; without it the input coordinates are kept and the
    // region simply starts above the topmost atom, wrapping round the cell.
    double shiftAlongNormal = 0.0;
    if (params.anchorSubstrate)
        shiftAlongNormal = -lowest;
    const double regionBottom = highest + shiftAlongNormal
        + params.surfaceClearance;
    const double fillThickness =
        params.regionThickness - 2.0 * params.surfaceClearance;
    if (fillThickness <= 0.0)
        throw std::invalid_argument(
            "the surface clearance consumes the whole region — either widen "
            "the region or reduce the clearance");

    result.regionThickness = params.regionThickness;
    result.regionVolume = area * fillThickness;

    // -- Resolve what is to be placed --------------------------------------
    struct Request {
        const Species* species = nullptr;
        int count = 0;
    };
    std::vector<Request> ionRequests;
    double ionMass = 0.0;
    for (const IonicComponent& entry : params.ions) {
        if (entry.units <= 0)
            continue;
        const Species* species = find(entry.key);
        if (!species)
            throw std::invalid_argument("unknown ionic species '" + entry.key
                                        + "'");
        // A salt is a formula unit: expand it so the cell is neutral by
        // construction rather than by the user counting cations and anions.
        std::vector<std::string> parts = species->expandsTo;
        if (parts.empty())
            parts.push_back(species->key);
        for (const std::string& part : parts) {
            const Species* ion = find(part);
            if (!ion)
                throw std::invalid_argument("salt '" + entry.key
                                            + "' expands to unknown ion '"
                                            + part + "'");
            auto existing = std::find_if(
                ionRequests.begin(), ionRequests.end(),
                [ion](const Request& r) { return r.species == ion; });
            if (existing == ionRequests.end())
                ionRequests.push_back({ion, entry.units});
            else
                existing->count += entry.units;
            ionMass += ion->molarMassU() * entry.units;
            result.netCharge += ion->charge * entry.units;
        }
    }

    // Mole fractions, normalized so {3 water, 1 ammonia} and {0.75, 0.25} mean
    // the same mixture.
    std::vector<const Species*> solvents;
    std::vector<double> fractions;
    double fractionSum = 0.0;
    for (const Component& entry : params.components) {
        if (entry.fraction <= 0.0)
            continue;
        const Species* species = find(entry.key);
        if (!species)
            throw std::invalid_argument("unknown species '" + entry.key + "'");
        if (species->category == Category::Salt)
            throw std::invalid_argument(
                "'" + species->name
                + "' is a salt — add it under the ionic species, which are "
                  "inserted by formula unit rather than by mole fraction");
        solvents.push_back(species);
        fractions.push_back(entry.fraction);
        fractionSum += entry.fraction;
    }

    std::vector<Request> solventRequests;
    if (!solvents.empty() && fractionSum > 0.0) {
        double meanMass = 0.0;
        for (std::size_t i = 0; i < solvents.size(); ++i)
            meanMass += (fractions[i] / fractionSum) * solvents[i]->molarMassU();

        int total = 0;
        if (params.amount == Amount::Count) {
            total = std::max(0, params.moleculeCount);
        } else if (meanMass > 0.0) {
            // The ions already placed count against the density target, which
            // is what makes a brine come out at the density of brine rather
            // than at the density of water with salt added on top.
            const double targetMass =
                params.targetDensity * result.regionVolume / kUPerA3ToGCm3;
            const double remaining = targetMass - ionMass;
            if (remaining <= 0.0 && ionMass > 0.0) {
                result.warnings.push_back(
                    "the ions alone already reach the target density, so no "
                    "solvent was added — lower the ion count or raise the "
                    "density");
            }
            total = static_cast<int>(std::lround(std::max(0.0, remaining)
                                                 / meanMass));
        }

        // Largest-remainder apportionment, so the counts sum to the requested
        // total exactly instead of drifting by a molecule per component.
        std::vector<double> exact(solvents.size());
        std::vector<int> counts(solvents.size());
        int assigned = 0;
        for (std::size_t i = 0; i < solvents.size(); ++i) {
            exact[i] = (fractions[i] / fractionSum) * total;
            counts[i] = static_cast<int>(std::floor(exact[i]));
            assigned += counts[i];
        }
        std::vector<std::size_t> order(solvents.size());
        std::iota(order.begin(), order.end(), std::size_t{0});
        std::sort(order.begin(), order.end(),
                  [&exact](std::size_t a, std::size_t b) {
                      return (exact[a] - std::floor(exact[a]))
                          > (exact[b] - std::floor(exact[b]));
                  });
        for (std::size_t i = 0; assigned < total && i < order.size(); ++i) {
            ++counts[order[i]];
            ++assigned;
        }
        for (std::size_t i = 0; i < solvents.size(); ++i)
            if (counts[i] > 0)
                solventRequests.push_back({solvents[i], counts[i]});
    }

    if (ionRequests.empty() && solventRequests.empty())
        throw std::invalid_argument(
            "the request comes to zero molecules — check the density, the "
            "molecule count and the region size");

    // -- Packing ------------------------------------------------------------
    //
    // Ions FIRST, and within each group the largest species first. Ions are
    // the composition the user actually specified — dropping one because the
    // packing saturated would silently change the stoichiometry — and placing
    // large species while there is still room is what makes a mixture of very
    // different molecules pack at all.
    const auto byRadius = [](const Request& a, const Request& b) {
        return a.species->contactRadius > b.species->contactRadius;
    };
    std::stable_sort(ionRequests.begin(), ionRequests.end(), byRadius);
    std::stable_sort(solventRequests.begin(), solventRequests.end(), byRadius);
    std::vector<Request> plan = ionRequests;
    plan.insert(plan.end(), solventRequests.begin(), solventRequests.end());

    double largestRadius = 0.0;
    int requestedTotal = 0;
    for (const Request& request : plan) {
        largestRadius = std::max(largestRadius, request.species->contactRadius);
        requestedTotal += request.count;
        double extent = 0.0;
        for (const Vec3& p : request.species->positions)
            extent = std::max(extent, p.norm());
        largestRadius = std::max(largestRadius, extent);
    }

    const Matrix3 toFractional = inverseOfColumns(cell);
    // Minimum image, correct for the tilted lateral cells that slabs actually
    // have (a hexagonal surface cell's Wigner-Seitz region is a hexagon, not
    // the fractional box, so rounding alone finds the wrong nearest image for
    // separations near the corner). Rounding handles the axis direction, where
    // the cell is long and effectively orthogonal to the surface.
    const auto minimumImageSq = [&](const Vec3& a, const Vec3& b) {
        Vec3 delta = a - b;
        Vec3 fractional = toFractional * delta;
        double f[3] = {fractional.x, fractional.y, fractional.z};
        for (int k = 0; k < 3; ++k)
            f[k] -= std::round(f[k]);
        const Vec3 base = cell[0] * f[0] + cell[1] * f[1] + cell[2] * f[2];
        double best = base.dot(base);
        for (int da = -1; da <= 1; ++da) {
            for (int db = -1; db <= 1; ++db) {
                if (da == 0 && db == 0)
                    continue;
                const Vec3 image =
                    base + cell[lateralA] * da + cell[lateralB] * db;
                best = std::min(best, image.dot(image));
            }
        }
        return best;
    };

    // Only substrate atoms within reach of the region can ever be clashed
    // into: both the top face and — through the periodic image across the
    // region — the bottom one.
    const double reach = largestRadius + params.surfaceClearance;
    std::vector<Vec3> surfaceAtoms;
    for (const Atom& atom : substrateAtoms) {
        const double projection = atom.position.dot(normal);
        if (highest - projection <= reach || projection - lowest <= reach)
            surfaceAtoms.push_back(atom.position
                                   + normal * shiftAlongNormal);
    }
    const double clearanceSq =
        params.surfaceClearance * params.surfaceClearance;

    std::mt19937 rng(params.seed);
    std::uniform_real_distribution<double> unit(0.0, 1.0);

    struct Placed {
        Vec3 centre;
        double radius = 0.0;
        /// Distance from the centre to the outermost atom, used to bound the
        /// atom-level neighbour search.
        double extent = 0.0;
        std::size_t molecule = 0;
    };
    std::vector<Placed> placed;
    placed.reserve(static_cast<std::size_t>(requestedTotal));

    struct Molecule {
        const Species* species = nullptr;
        std::vector<Vec3> positions;
    };
    std::vector<Molecule> molecules;
    molecules.reserve(static_cast<std::size_t>(requestedTotal));

    // A global budget, not an unbounded retry: an over-dense request has to
    // come back as a short molecule count with a warning rather than hang.
    long budget = 600L * requestedTotal + 20000L;

    for (const Request& request : plan) {
        Placement placement;
        placement.key = request.species->key;
        placement.name = request.species->name;
        placement.requested = request.count;

        double extent = 0.0;
        for (const Vec3& local : request.species->positions)
            extent = std::max(extent, local.norm());

        for (int n = 0; n < request.count; ++n) {
            bool success = false;
            for (int attempt = 0; attempt < 3000 && budget > 0 && !success;
                 ++attempt) {
                --budget;
                // Uniform in the fractional lateral plane (so a tilted cell is
                // sampled evenly) and uniform in the perpendicular coordinate
                // across the fill slab.
                const Vec3 centre = cell[lateralA] * unit(rng)
                    + cell[lateralB] * unit(rng)
                    + normal * (regionBottom + unit(rng) * fillThickness);

                bool clash = false;
                for (const Placed& other : placed) {
                    const double contact =
                        other.radius + request.species->contactRadius
                        + params.packingTolerance;
                    if (minimumImageSq(centre, other.centre)
                        < contact * contact) {
                        clash = true;
                        break;
                    }
                }
                if (clash)
                    continue;

                const std::array<Vec3, 3> rotation = randomRotation(rng);
                std::vector<Vec3> atoms;
                atoms.reserve(request.species->positions.size());
                for (const Vec3& local : request.species->positions)
                    atoms.push_back(centre + rotate(rotation, local));

                // Atom-by-atom against the near neighbours that cleared the
                // centre test. Two molecules at exactly their contact distance
                // can still be oriented mouth to mouth, which puts their
                // hydrogens about 1 Å apart; the centre test cannot see that,
                // and a relaxation started from it does not end well.
                for (const Placed& other : placed) {
                    const double cutoff =
                        other.extent + extent + kMaxPairMinimum;
                    if (minimumImageSq(centre, other.centre) > cutoff * cutoff)
                        continue;
                    const Molecule& neighbour = molecules[other.molecule];
                    for (std::size_t i = 0; i < atoms.size() && !clash; ++i) {
                        for (std::size_t j = 0; j < neighbour.positions.size();
                             ++j) {
                            const double minimum = pairMinimum(
                                request.species->numbers[i],
                                neighbour.species->numbers[j]);
                            if (minimumImageSq(atoms[i], neighbour.positions[j])
                                < minimum * minimum) {
                                clash = true;
                                break;
                            }
                        }
                    }
                    if (clash)
                        break;
                }
                if (clash)
                    continue;

                // Atom-by-atom against the substrate: that surface is fixed,
                // and a molecule fused into it cannot relax out the way two
                // slightly close fluid molecules can.
                for (const Vec3& atom : atoms) {
                    for (const Vec3& surface : surfaceAtoms) {
                        if (minimumImageSq(atom, surface) < clearanceSq) {
                            clash = true;
                            break;
                        }
                    }
                    if (clash)
                        break;
                }
                if (clash)
                    continue;

                placed.push_back({centre, request.species->contactRadius,
                                  extent, molecules.size()});
                molecules.push_back({request.species, std::move(atoms)});
                ++placement.placed;
                success = true;
            }
            if (!success)
                break; // the region is saturated; report it below
        }
        result.placements.push_back(placement);
        result.totalMolecules += placement.placed;
    }

    for (const Placement& placement : result.placements) {
        if (placement.placed < placement.requested) {
            result.warnings.push_back(
                "only " + std::to_string(placement.placed) + " of "
                + std::to_string(placement.requested) + " "
                + placement.name
                + " molecules fit — the packing saturated. Lower the density, "
                  "widen the region, or reduce the packing tolerance.");
        }
    }

    // -- Assemble -----------------------------------------------------------
    Structure output;
    output.setCell(UnitCell(cell[0], cell[1], cell[2], {true, true, true}));
    for (const Atom& atom : substrateAtoms) {
        Atom moved = atom;
        moved.position = moved.position + normal * shiftAlongNormal;
        output.addAtom(moved);
    }
    double placedMass = 0.0;
    for (const Molecule& molecule : molecules) {
        placedMass += molecule.species->molarMassU();
        for (std::size_t i = 0; i < molecule.positions.size(); ++i) {
            Atom atom;
            atom.atomicNumber = molecule.species->numbers[i];
            atom.position = molecule.positions[i];
            output.addAtom(atom);
        }
    }

    // Carry the substrate's per-atom fields across the supercell. Losing the
    // initial magnetic moments of a magnetic slab the moment it is solvated
    // would be a silent change of the calculation being set up.
    const std::size_t fluidAtoms = output.size() - substrateAtoms.size();
    for (const auto& [name, values] : substrate.scalarFields()) {
        if (values.size() != substrate.size())
            continue;
        std::vector<double> expanded;
        expanded.reserve(output.size());
        for (std::size_t index : sourceIndex)
            expanded.push_back(values[index]);
        expanded.resize(expanded.size() + fluidAtoms, 0.0);
        output.setScalarField(name, std::move(expanded));
    }
    for (const auto& [name, values] : substrate.vectorFields()) {
        if (values.size() != substrate.size())
            continue;
        std::vector<Vec3> expanded;
        expanded.reserve(output.size());
        for (std::size_t index : sourceIndex)
            expanded.push_back(values[index]);
        expanded.resize(expanded.size() + fluidAtoms, Vec3{});
        output.setVectorField(name, std::move(expanded));
    }

    result.density = result.regionVolume > 0.0
        ? placedMass * kUPerA3ToGCm3 / result.regionVolume
        : 0.0;

    std::string composition;
    for (const Placement& placement : result.placements) {
        if (placement.placed <= 0)
            continue;
        if (!composition.empty())
            composition += " + ";
        const Species* species = find(placement.key);
        composition += std::to_string(placement.placed) + " "
            + (species ? species->formula : placement.key);
    }
    if (composition.empty())
        composition = "empty region";
    result.description = "Interface: " + composition + " in a "
        + formatNumber(params.regionThickness, 1) + " Å region along "
        + toString(params.axis)
        + " (random packing — equilibrate before use)";
    result.structure = std::move(output);
    return result;
}

} // namespace calango::core
