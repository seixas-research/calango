#include "python_bridge/RamanAnalysis.hpp"

#include "python_bridge/AseBridge.hpp"

#include <pybind11/embed.h>
#include <pybind11/stl.h>

namespace py = pybind11;

namespace calango::pybridge {

namespace {

/// Factor-group analysis at Γ. Inputs: atoms, symprec. Output: `result`,
/// a dict with space-group info and per-irrep mode classification.
constexpr const char* kRamanScript = R"PY(
import numpy as np
import spglib

cell = (atoms.cell[:], atoms.get_scaled_positions(), atoms.numbers)
dataset = spglib.get_symmetry_dataset(cell, symprec=symprec)
if dataset is None:
    raise RuntimeError("spglib could not detect the symmetry of this cell")

def field(name):
    return dataset[name] if isinstance(dataset, dict) else getattr(dataset, name)

# Factor-group analysis must run on the primitive cell — centering
# translations of a conventional cell are pure translations, not factor
# group elements.
prim = spglib.standardize_cell(cell, to_primitive=True, no_idealize=True,
                               symprec=symprec)
if prim is None:
    raise RuntimeError("spglib could not derive a primitive cell")
plat, ppos, pnum = prim
psym = spglib.get_symmetry((plat, ppos, pnum), symprec=symprec)
rots = [np.array(r) for r in psym["rotations"]]
trans = [np.array(t) for t in psym["translations"]]
order = len(rots)

# --- abstract group structure (rotation parts = isogonal point group) ---
key = lambda m: tuple(int(x) for x in np.rint(m).flatten())
index = {key(r): i for i, r in enumerate(rots)}
if len(index) != order:
    raise RuntimeError("degenerate rotation parts — cell is not primitive?")
identity = index[key(np.eye(3))]
mult = [[index[key(rots[i] @ rots[j])] for j in range(order)]
        for i in range(order)]
inv = [mult[i].index(identity) for i in range(order)]

class_of = [-1] * order
classes = []
for i in range(order):
    if class_of[i] >= 0:
        continue
    members = sorted({mult[mult[g][i]][inv[g]] for g in range(order)})
    for m in members:
        class_of[m] = len(classes)
    classes.append(members)
nclasses = len(classes)

# --- character table from the class-sum algebra (Burnside) --------------
# Class sums obey C_i C_j = sum_l a_ijl C_l; in an irrep each C_i acts as
# the scalar lam_i = |C_i| chi_i / d, and the vectors lam are the common
# eigenvectors of the matrices (M_i)_{jl} = a_ijl.
a = np.zeros((nclasses, nclasses, nclasses))
for i, ci in enumerate(classes):
    for j, cj in enumerate(classes):
        for x in ci:
            for y in cj:
                a[i, j, class_of[mult[x][y]]] += 1.0
for l, cl in enumerate(classes):
    a[:, :, l] /= len(cl)

rng = np.random.default_rng(12345)
combo = np.tensordot(rng.random(nclasses), a, axes=(0, 0))  # sum_i w_i M_i
# The lambda vectors satisfy (M_i @ lam)_j = lam_i lam_j — they are RIGHT
# eigenvectors of the combination.
_, vectors = np.linalg.eig(combo)  # columns ~ lambda vectors

characters = []  # complex chi rows, one per irrep
for col in vectors.T:
    lam = col / col[class_of[identity]]
    dim = np.sqrt(order / np.sum(np.abs(lam) ** 2
                                 / [len(c) for c in classes]))
    chi = dim * lam / [len(c) for c in classes]
    characters.append(chi)
characters = np.array(characters)

# Pair complex-conjugate irreps into the physically real 2D combinations.
used = [False] * len(characters)
irreps = []  # (chi_real, dim, paired)
for i, chi in enumerate(characters):
    if used[i]:
        continue
    if np.max(np.abs(chi.imag)) < 1e-6:
        irreps.append((chi.real, int(round(chi[class_of[identity]].real)),
                       False))
        used[i] = True
        continue
    for j in range(i + 1, len(characters)):
        if not used[j] and np.max(np.abs(characters[j] - chi.conj())) < 1e-6:
            summed = (chi + characters[j]).real
            irreps.append((summed, int(round(summed[class_of[identity]])),
                           True))
            used[i] = used[j] = True
            break
    else:
        raise RuntimeError("unpaired complex irreducible representation")

# --- representation characters per operation ----------------------------
def unmoved(op):
    R, t = rots[op], trans[op]
    count = 0
    for x in ppos:
        d = R @ x + t - x
        if np.max(np.abs(d - np.rint(d))) < 1e-4:
            count += 1
    return count

chi_vec = np.array([np.trace(rots[i]) for i in range(order)], dtype=float)
chi_pol = np.array([(chi_vec[i] ** 2
                     + np.trace(rots[i] @ rots[i])) / 2.0
                    for i in range(order)])
chi_mech = np.array([chi_vec[i] * unmoved(i) for i in range(order)])

def reduce_rep(rep):
    counts = []
    for chi, _, paired in irreps:
        per_op = np.array([chi[class_of[i]] for i in range(order)])
        n = np.sum(rep * per_op) / order
        # A paired character is chi + conj(chi): the projector counts both
        # members, i.e. twice the multiplicity of the physical rep.
        counts.append(int(round(n / (2.0 if paired else 1.0))))
    return counts

n_mech = reduce_rep(chi_mech)
n_vec = reduce_rep(chi_vec)
n_pol = reduce_rep(chi_pol)

# --- Mulliken labels (heuristic) ----------------------------------------
# Axis geometry (perpendicularity, mirror normals) must be evaluated in
# Cartesian space — fractional-basis eigenvectors are not orthogonal.
basis = np.array(plat).T  # columns = lattice vectors
to_cart = np.linalg.inv(basis)
rot_cart = [basis @ np.array(rots[c[0]], dtype=float) @ to_cart
            for c in classes]
dets = [int(round(np.linalg.det(m))) for m in rot_cart]
traces = [float(np.trace(m)) for m in rot_cart]

def rotation_order(cls):
    if dets[cls] < 0:
        return 0
    cos = (traces[cls] - 1.0) / 2.0
    theta = np.arccos(np.clip(cos, -1.0, 1.0))
    return 1 if theta < 1e-6 else int(round(2 * np.pi / theta))

def axis_vector(matrix, eig):
    vals, vecs = np.linalg.eig(matrix.astype(float))
    for v, vec in zip(vals, vecs.T):
        if abs(v - eig) < 1e-6:
            return np.real(vec)
    return None

orders = [rotation_order(c) for c in range(nclasses)]
proper_max = max(orders)
principal = orders.index(proper_max)
principal_axis = axis_vector(rot_cart[principal], 1.0)

inversion = next((c for c in range(nclasses)
                  if np.allclose(rot_cart[c], -np.eye(3), atol=1e-6)), None)

def is_perp(u, v):
    return u is not None and v is not None and abs(np.dot(u, v)) < 1e-4 \
        * np.linalg.norm(u) * np.linalg.norm(v) + 1e-6

c2prime = next((c for c in range(nclasses)
                if orders[c] == 2 and c != principal
                and is_perp(axis_vector(rot_cart[c], 1.0),
                            principal_axis)), None)
sigma_h = None
sigma_v = None
for c in range(nclasses):
    if dets[c] < 0 and abs(traces[c] - 1.0) < 1e-6:  # a mirror
        normal = axis_vector(rot_cart[c], -1.0)
        if principal_axis is not None and normal is not None \
                and abs(abs(np.dot(normal, principal_axis))
                        - np.linalg.norm(normal)
                        * np.linalg.norm(principal_axis)) < 1e-4:
            sigma_h = c
        elif sigma_v is None:
            sigma_v = c

labels = []
# A class of more than two principal rotations means several equivalent
# principal axes — a cubic group, whose 1D irreps are all A by convention
# (no B labels exist in T..Oh).
cubic = len(classes[principal]) > 2
for chi, dim, _ in irreps:
    letter = {1: "A", 2: "E", 3: "T"}.get(dim, f"G{dim}")
    if dim == 1 and proper_max > 1 and not cubic and chi[principal] < -0.5:
        letter = "B"
    parity = ""
    if inversion is not None:
        parity = "g" if chi[inversion] > 0 else "u"
    prime = ""
    if inversion is None and sigma_h is not None:
        prime = "'" if chi[sigma_h] > 0 else "''"
    labels.append([letter, parity, prime, chi])

# Disambiguate identical letter/parity/prime groups with subscripts,
# ordered by the character under C2' (1D) or the principal class (>=2D).
from collections import defaultdict
groups = defaultdict(list)
for i, (letter, parity, prime, chi) in enumerate(labels):
    groups[(letter, parity, prime)].append(i)
final = [None] * len(labels)
for (letter, parity, prime), members in groups.items():
    if len(members) == 1:
        i = members[0]
        final[i] = letter + parity + prime
        continue
    def sort_key(i):
        chi = labels[i][3]
        aux = c2prime if irreps[i][1] == 1 else principal
        if aux is None:
            aux = sigma_v if sigma_v is not None else principal
        # The totally symmetric irrep is A1 by definition (sum over the
        # whole group is |G| for it, 0 for every other); after that, more
        # +1 characters rank earlier — the textbook subscript convention
        # for the C2'/σv distinctions, and a deterministic tiebreak.
        # Everything is rounded: the characters carry ~1e-15 numerical
        # noise from the eigendecomposition, and unrounded keys let that
        # noise outrank the real ±1 distinctions.
        symmetric = round(sum(len(classes[c]) * chi[c]
                              for c in range(nclasses)))
        return (-symmetric,
                -round(chi[aux], 6) if aux is not None else 0.0,
                tuple(-round(chi[c], 6) for c in range(nclasses)), i)
    for rank, i in enumerate(sorted(members, key=sort_key), start=1):
        final[i] = f"{letter}{rank}{parity}{prime}"

# --- character table (for display) ---------------------------------------
# Conjugacy classes named by their representative operation: proper
# rotations E/Cn, then i, mirrors (σh when the normal is the principal
# axis; the first remaining mirror class is σv, later ones σd) and improper
# rotations Sn from the rotation angle of the proper part
# (trace(S(θ)) = 2cosθ − 1). Prefixed with the class size.
# Identity first, the conventional leading column of every character table.
col_order = [class_of[identity]] + [c for c in range(nclasses)
                                    if c != class_of[identity]]
class_labels = []
_mirror_rank = 0
for c in col_order:
    if dets[c] > 0:
        k = orders[c]
        symbol = "E" if k == 1 else f"C{k}"
    elif inversion is not None and c == inversion:
        symbol = "i"
    elif abs(traces[c] - 1.0) < 1e-6:
        if c == sigma_h:
            symbol = "σh"
        else:
            symbol = "σv" if _mirror_rank == 0 else "σd"
            _mirror_rank += 1
    else:
        cos = np.clip((traces[c] + 1.0) / 2.0, -1.0, 1.0)
        symbol = f"S{int(round(2.0 * np.pi / np.arccos(cos)))}"
    n = len(classes[c])
    class_labels.append(symbol if n == 1 else f"{n}{symbol}")

# Rows ordered as tables print them: 1D irreps first, then E, then T. The
# paired complex-conjugate irreps appear as their physically real 2D sum,
# the spectroscopic convention.
row_order = sorted(range(len(irreps)), key=lambda i: (irreps[i][1], final[i]))
character_table = [{"label": final[i],
                    "chi": [float(irreps[i][0][c]) for c in col_order]}
                   for i in row_order]

# --- assemble -----------------------------------------------------------
modes = []
for i, (chi, dim, _) in enumerate(irreps):
    if n_mech[i] <= 0:
        continue
    modes.append({
        "label": final[i],
        "dim": dim,
        "total": n_mech[i],
        "acoustic": n_vec[i],
        "optical": n_mech[i] - n_vec[i],
        "raman": n_pol[i] > 0,
        "ir": n_vec[i] > 0,
    })
modes.sort(key=lambda m: (not m["raman"], m["label"]))

result = {
    "symbol": str(field("international")),
    "number": int(field("number")),
    "pointgroup": str(field("pointgroup")).strip(),
    "atoms_primitive": int(len(pnum)),
    "modes": modes,
    "classes": class_labels,
    "character_table": character_table,
}
)PY";

} // namespace

RamanAnalysis::Result RamanAnalysis::analyze(const core::Structure& structure,
                                             double symprec)
{
    Result result;
    if (!structure.cell().isDefined()) {
        result.error = "The structure has no unit cell — Γ-point factor "
                       "group analysis needs a periodic crystal.";
        return result;
    }

    try {
        py::dict scope;
        scope["atoms"] = AseBridge::toAtoms(structure);
        scope["symprec"] = symprec;
        // Single dict as globals AND locals (see the SqsBuilder note).
        py::exec(kRamanScript, scope, scope);

        const py::dict data = scope["result"].cast<py::dict>();
        result.spaceGroupSymbol = data["symbol"].cast<std::string>();
        result.spaceGroupNumber = data["number"].cast<int>();
        result.pointGroup = data["pointgroup"].cast<std::string>();
        result.atomsPrimitive = data["atoms_primitive"].cast<int>();
        for (const auto& entry : data["modes"].cast<py::list>()) {
            const py::dict m = entry.cast<py::dict>();
            Mode mode;
            mode.label = m["label"].cast<std::string>();
            mode.degeneracy = m["dim"].cast<int>();
            mode.totalCount = m["total"].cast<int>();
            mode.acousticCount = m["acoustic"].cast<int>();
            mode.opticalCount = m["optical"].cast<int>();
            mode.ramanActive = m["raman"].cast<bool>();
            mode.irActive = m["ir"].cast<bool>();
            result.modes.push_back(std::move(mode));
        }
        for (const auto& entry : data["classes"].cast<py::list>())
            result.classLabels.push_back(entry.cast<std::string>());
        for (const auto& entry : data["character_table"].cast<py::list>()) {
            const py::dict row = entry.cast<py::dict>();
            IrrepRow irrep;
            irrep.label = row["label"].cast<std::string>();
            for (const auto& value : row["chi"].cast<py::list>())
                irrep.characters.push_back(value.cast<double>());
            result.characterTable.push_back(std::move(irrep));
        }
    } catch (const py::error_already_set& e) {
        result.error = std::string("Raman analysis failed:\n") + e.what();
    }
    return result;
}

} // namespace calango::pybridge
