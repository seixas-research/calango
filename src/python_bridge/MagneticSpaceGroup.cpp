#include "python_bridge/MagneticSpaceGroup.hpp"

#include "python_bridge/AseBridge.hpp"

#include <pybind11/embed.h>
#include <pybind11/stl.h>

namespace py = pybind11;

namespace calango::pybridge {

namespace {

/// Magnetic space-group determination. Inputs: `atoms`, `moments` (an (N, 3)
/// list), `symprec`, `mag_symprec`. Output: `result`, a dict.
constexpr const char* kMagneticScript = R"PY(
import numpy as np
import spglib

lattice = atoms.cell[:]
positions = atoms.get_scaled_positions()
numbers = atoms.numbers
mag = np.asarray(moments, dtype=float).reshape(-1, 3)
if len(mag) != len(numbers):
    raise RuntimeError("one magnetic moment per atom is required")

# Collinear or not decides which shape spglib is handed, and that in turn
# decides which tensor rank it classifies. Feeding an (N, 3) array whose
# x and y components are zero is NOT equivalent to feeding the N scalars:
# spglib treats rank-1 moments as AXIAL vectors that rotate with the
# operations, so a collinear structure described that way is answered as a
# non-collinear one that happens to be aligned — a different, larger problem
# with a different tolerance behaviour.
transverse = float(np.max(np.abs(mag[:, :2]))) if len(mag) else 0.0
collinear = transverse <= max(mag_symprec, 1e-8)
if collinear:
    cell = (lattice, positions, numbers, mag[:, 2].copy())
else:
    cell = (lattice, positions, numbers, mag)

dataset = spglib.get_magnetic_symmetry_dataset(cell, symprec=symprec,
                                               mag_symprec=mag_symprec)
if dataset is None:
    raise RuntimeError("spglib could not determine the magnetic space group")


def field(name, default=None):
    if isinstance(dataset, dict):
        return dataset.get(name, default)
    return getattr(dataset, name, default)


uni_number = int(field("uni_number", 0))
msg_type = int(field("msg_type", 0))
rotations = np.asarray(field("rotations"), dtype=int)
translations = np.asarray(field("translations"), dtype=float)
time_reversals = np.asarray(field("time_reversals"), dtype=bool)

msg = spglib.get_magnetic_spacegroup_type(uni_number)
bns_number = str(getattr(msg, "bns_number", "")) if msg else ""
og_number = str(getattr(msg, "og_number", "")) if msg else ""
litvin = int(getattr(msg, "litvin_number", 0)) if msg else 0
parent_number = int(getattr(msg, "number", 0)) if msg else 0

parent_symbol = ""
if parent_number:
    try:
        # get_spacegroup_type_from_symmetry wants operations; the parent of the
        # BNS label is looked up by number instead, through its Hall entry.
        parent_symbol = str(spglib.get_spacegroup_type_from_symmetry(
            rotations[~time_reversals], translations[~time_reversals],
            lattice=lattice, symprec=symprec).international_short)
    except Exception:
        parent_symbol = ""

# --- crystallography with the moments IGNORED ---------------------------
# The contrast is the point: every operation here that is missing from the
# unitary subgroup below was broken by the magnetic order.
crystal = spglib.get_symmetry_dataset((lattice, positions, numbers),
                                      symprec=symprec)


def crystal_field(name, default=""):
    if crystal is None:
        return default
    if isinstance(crystal, dict):
        return crystal.get(name, default)
    return getattr(crystal, name, default)


unitary = rotations[~time_reversals]
try:
    unitary_pg = str(spglib.get_pointgroup(unitary)[0]).strip()
except Exception:
    unitary_pg = ""

# --- the anti-translation of a type-IV group ----------------------------
# Type IV is DEFINED by an antiunitary element whose spatial part is a pure
# translation. That translation is what doubles the magnetic cell, so it is
# worth naming rather than leaving the reader to infer it from the type.
#
# Guarded on the type rather than merely searched for: a type II grey group
# contains T on its own, so T times EVERY lattice translation is in it too,
# and an unguarded search finds one of those and calls it an anti-translation.
# In a grey group it carries no information — nothing is doubled, because the
# plain translation is a symmetry as well.
anti_translation = None
if msg_type == 4:
    identity = np.eye(3, dtype=int)
    for R, t, tr in zip(rotations, translations, time_reversals):
        if not tr or not np.array_equal(R, identity):
            continue
        folded = t - np.rint(t)
        if np.max(np.abs(folded)) > 1e-6:
            anti_translation = [float(v) for v in (t % 1.0)]
            break

# --- what the magnetic configuration IS ---------------------------------
net = np.linalg.norm(mag.sum(axis=0))
absolute = float(np.sum(np.linalg.norm(mag, axis=1)))
if absolute <= max(mag_symprec, 1e-6) * max(len(mag), 1):
    ordering = "Non-magnetic"
elif net <= max(mag_symprec, 1e-6) * max(len(mag), 1):
    ordering = "Antiferromagnetic"
elif abs(net - absolute) <= max(mag_symprec, 1e-6) * max(len(mag), 1):
    ordering = "Ferromagnetic"
else:
    ordering = "Ferrimagnetic"

equivalent = [int(v) for v in np.asarray(field("equivalent_atoms", []),
                                         dtype=int).ravel()]

result = {
    "bns_number": bns_number,
    "og_number": og_number,
    "uni_number": uni_number,
    "litvin_number": litvin,
    "type": msg_type,
    "parent_number": parent_number,
    "parent_symbol": parent_symbol,
    "crystal_symbol": str(crystal_field("international")),
    "crystal_number": int(crystal_field("number", 0) or 0),
    "crystal_pointgroup": str(crystal_field("pointgroup")).strip(),
    "unitary_pointgroup": unitary_pg,
    "operations": int(len(rotations)),
    "unitary_operations": int(np.sum(~time_reversals)),
    "antiunitary_operations": int(np.sum(time_reversals)),
    "rotations": [[[int(v) for v in row] for row in R] for R in rotations],
    "translations": [[float(v) for v in t] for t in translations],
    "time_reversals": [bool(v) for v in time_reversals],
    "anti_translation": anti_translation,
    "collinear": bool(collinear),
    "total_moment": float(net),
    "absolute_moment": absolute,
    "ordering": ordering,
    "equivalent_atoms": equivalent,
    "unique_sites": int(len(set(equivalent))) if equivalent else 0,
}
)PY";

} // namespace

std::vector<core::Vec3>
MagneticSpaceGroup::momentsFor(const core::Structure& structure,
                               MomentSource source, std::string* sourceName)
{
    const auto pick = [&](const std::string& name) -> std::vector<core::Vec3> {
        if (!structure.hasVectorData(name))
            return {};
        auto values = structure.resolvedVectorField(name);
        if (values.size() != structure.size())
            return {};
        if (sourceName)
            *sourceName = name;
        return values;
    };

    std::vector<core::Vec3> moments;
    switch (source) {
    case MomentSource::Computed:
        moments = pick("magmoms");
        break;
    case MomentSource::Initial:
        moments = pick("initial_magmoms");
        break;
    case MomentSource::Auto:
        // The converged result first: when a structure carries both, the guess
        // is what was asked for and the result is what was found, and it is the
        // result that has a magnetic space group.
        moments = pick("magmoms");
        if (moments.empty())
            moments = pick("initial_magmoms");
        break;
    }
    if (moments.empty()) {
        if (sourceName)
            *sourceName = "none";
        moments.assign(structure.size(), core::Vec3{});
    }
    return moments;
}

MagneticSpaceGroup::Result
MagneticSpaceGroup::analyze(const core::Structure& structure,
                            const std::vector<core::Vec3>& moments,
                            double symprec, double magSymprec)
{
    Result result;
    if (!structure.cell().isDefined()) {
        result.error = "The structure has no unit cell — a magnetic space "
                       "group is a property of a periodic crystal.";
        return result;
    }
    if (moments.size() != structure.size()) {
        result.error = "One magnetic moment per atom is required.";
        return result;
    }

    try {
        py::list momentList;
        for (const core::Vec3& m : moments)
            momentList.append(py::make_tuple(m.x, m.y, m.z));

        py::dict scope;
        scope["atoms"] = AseBridge::toAtoms(structure);
        scope["moments"] = momentList;
        scope["symprec"] = symprec;
        scope["mag_symprec"] = magSymprec;
        // Single dict as globals AND locals (see the SqsBuilder note).
        py::exec(kMagneticScript, scope, scope);

        const py::dict data = scope["result"].cast<py::dict>();
        result.bnsNumber = data["bns_number"].cast<std::string>();
        result.ogNumber = data["og_number"].cast<std::string>();
        result.uniNumber = data["uni_number"].cast<int>();
        result.litvinNumber = data["litvin_number"].cast<int>();
        result.type = data["type"].cast<int>();
        result.parentNumber = data["parent_number"].cast<int>();
        result.parentSymbol = data["parent_symbol"].cast<std::string>();
        result.crystalSpaceGroup = data["crystal_symbol"].cast<std::string>();
        result.crystalSpaceGroupNumber = data["crystal_number"].cast<int>();
        result.crystalPointGroup =
            data["crystal_pointgroup"].cast<std::string>();
        result.unitaryPointGroup =
            data["unitary_pointgroup"].cast<std::string>();
        result.operations = data["operations"].cast<int>();
        result.unitaryOperations = data["unitary_operations"].cast<int>();
        result.antiunitaryOperations =
            data["antiunitary_operations"].cast<int>();
        result.collinear = data["collinear"].cast<bool>();
        result.totalMoment = data["total_moment"].cast<double>();
        result.absoluteMoment = data["absolute_moment"].cast<double>();
        result.ordering = data["ordering"].cast<std::string>();
        result.equivalentAtoms =
            data["equivalent_atoms"].cast<std::vector<int>>();
        result.uniqueSites = data["unique_sites"].cast<int>();

        const auto rotations = data["rotations"].cast<py::list>();
        const auto translations = data["translations"].cast<py::list>();
        const auto reversals = data["time_reversals"].cast<py::list>();
        for (std::size_t i = 0; i < rotations.size(); ++i) {
            Operation op;
            const auto matrix = rotations[i].cast<py::list>();
            for (int r = 0; r < 3; ++r) {
                const auto row = matrix[static_cast<std::size_t>(r)]
                                     .cast<py::list>();
                for (int col = 0; col < 3; ++col)
                    op.rotation[r][col] =
                        row[static_cast<std::size_t>(col)].cast<int>();
            }
            const auto shift = translations[i].cast<py::list>();
            for (int axis = 0; axis < 3; ++axis)
                op.translation[axis] =
                    shift[static_cast<std::size_t>(axis)].cast<double>();
            op.timeReversal = reversals[i].cast<bool>();
            result.symmetryOperations.push_back(op);
        }

        if (!data["anti_translation"].is_none()) {
            const auto anti = data["anti_translation"].cast<py::list>();
            result.hasAntiTranslation = true;
            for (int axis = 0; axis < 3; ++axis)
                result.antiTranslation[axis] =
                    anti[static_cast<std::size_t>(axis)].cast<double>();
        }
    } catch (const py::error_already_set& e) {
        result.error =
            std::string("Magnetic space group determination failed:\n")
            + e.what();
    }
    return result;
}

} // namespace calango::pybridge
