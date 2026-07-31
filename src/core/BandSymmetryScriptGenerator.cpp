#include "core/BandSymmetryScriptGenerator.hpp"

#include <sstream>

namespace calango::core {

namespace {

/// Conjugacy classes, character table and Mulliken labels for a finite point
/// group given by its integer rotation matrices in the fractional basis.
///
/// This is the same class-sum (Burnside) construction the Symmetry dialog uses
/// for the Γ-point factor-group analysis (python_bridge/RamanAnalysis.cpp), so
/// a band label and a phonon label out of this application are drawn from the
/// same table by the same rules. It is generated numerically rather than looked
/// up: a lookup table has to be keyed by a group NAME, and naming the group is
/// the step that would have to be got right first.
constexpr const char* kPointGroupHelpers = R"PY(
def _calango_point_group(rots, lattice):
    """(class_labels, class_of, classes, irreps) for the group `rots`.

    `rots` are integer 3x3 matrices in the fractional basis; `lattice` is the
    3x3 array of lattice vectors (rows) used only to evaluate the Cartesian
    axis geometry the Mulliken labels depend on — a fractional basis is not
    orthogonal, so perpendicularity and mirror normals cannot be read off it.

    Each irrep is (chi, dim, paired, label). Complex-conjugate irrep pairs are
    returned as their physically real 2D sum, the spectroscopic convention.
    """
    order = len(rots)
    rots = [_np.rint(_np.asarray(r)).astype(int) for r in rots]

    def _key(m):
        return tuple(int(x) for x in _np.rint(m).flatten())

    index = {_key(r): i for i, r in enumerate(rots)}
    if len(index) != order:
        raise RuntimeError("duplicate rotation parts — the cell is not "
                           "primitive")
    identity = index[_key(_np.eye(3))]
    mult = [[index[_key(rots[i] @ rots[j])] for j in range(order)]
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

    # --- character table from the class-sum algebra (Burnside) -------------
    # Class sums obey C_i C_j = sum_l a_ijl C_l; in an irrep each C_i acts as
    # the scalar lam_i = |C_i| chi_i / d, and the lam vectors are the common
    # eigenvectors of the matrices (M_i)_{jl} = a_ijl.
    a = _np.zeros((nclasses, nclasses, nclasses))
    for i, ci in enumerate(classes):
        for j, cj in enumerate(classes):
            for x in ci:
                for y in cj:
                    a[i, j, class_of[mult[x][y]]] += 1.0
    for l, cl in enumerate(classes):
        a[:, :, l] /= len(cl)

    rng = _np.random.default_rng(12345)
    combo = _np.tensordot(rng.random(nclasses), a, axes=(0, 0))
    _, vectors = _np.linalg.eig(combo)

    characters = []
    for col in vectors.T:
        lam = col / col[class_of[identity]]
        dim = _np.sqrt(order / _np.sum(_np.abs(lam) ** 2
                                       / [len(c) for c in classes]))
        characters.append(dim * lam / [len(c) for c in classes])
    characters = _np.array(characters)

    used = [False] * len(characters)
    irreps = []
    for i, chi in enumerate(characters):
        if used[i]:
            continue
        if _np.max(_np.abs(chi.imag)) < 1e-6:
            irreps.append([chi.real, int(round(chi[class_of[identity]].real)),
                           False])
            used[i] = True
            continue
        for j in range(i + 1, len(characters)):
            if not used[j] \
                    and _np.max(_np.abs(characters[j] - chi.conj())) < 1e-6:
                summed = (chi + characters[j]).real
                irreps.append([summed, int(round(summed[class_of[identity]])),
                               True])
                used[i] = used[j] = True
                break
        else:
            raise RuntimeError("unpaired complex irreducible representation")

    # --- Mulliken labels ---------------------------------------------------
    basis = _np.asarray(lattice, dtype=float).T   # columns = lattice vectors
    to_cart = _np.linalg.inv(basis)
    rot_cart = [basis @ rots[c[0]].astype(float) @ to_cart for c in classes]
    dets = [int(round(_np.linalg.det(m))) for m in rot_cart]
    traces = [float(_np.trace(m)) for m in rot_cart]

    def _order_of(cls):
        if dets[cls] < 0:
            return 0
        cos = (traces[cls] - 1.0) / 2.0
        theta = _np.arccos(_np.clip(cos, -1.0, 1.0))
        return 1 if theta < 1e-6 else int(round(2 * _np.pi / theta))

    def _axis(matrix, eig):
        vals, vecs = _np.linalg.eig(_np.asarray(matrix, dtype=float))
        for v, vec in zip(vals, vecs.T):
            if abs(v - eig) < 1e-6:
                return _np.real(vec)
        return None

    orders = [_order_of(c) for c in range(nclasses)]
    proper_max = max(orders)
    principal = orders.index(proper_max)
    principal_axis = _axis(rot_cart[principal], 1.0)

    inversion = next((c for c in range(nclasses)
                      if _np.allclose(rot_cart[c], -_np.eye(3), atol=1e-6)),
                     None)

    def _perp(u, v):
        if u is None or v is None:
            return False
        return abs(_np.dot(u, v)) < 1e-4 * _np.linalg.norm(u) \
            * _np.linalg.norm(v) + 1e-6

    c2prime = next((c for c in range(nclasses)
                    if orders[c] == 2 and c != principal
                    and _perp(_axis(rot_cart[c], 1.0), principal_axis)), None)
    sigma_h = None
    sigma_v = None
    for c in range(nclasses):
        if dets[c] < 0 and abs(traces[c] - 1.0) < 1e-6:   # a mirror
            normal = _axis(rot_cart[c], -1.0)
            if principal_axis is not None and normal is not None \
                    and abs(abs(_np.dot(normal, principal_axis))
                            - _np.linalg.norm(normal)
                            * _np.linalg.norm(principal_axis)) < 1e-4:
                sigma_h = c
            elif sigma_v is None:
                sigma_v = c

    labels = []
    # More than two principal rotations in one class means several equivalent
    # principal axes — a cubic group, whose 1D irreps are all A by convention.
    cubic = len(classes[principal]) > 2
    for chi, dim, _paired in irreps:
        letter = {1: "A", 2: "E", 3: "T"}.get(dim, "G%d" % dim)
        if dim == 1 and proper_max > 1 and not cubic and chi[principal] < -0.5:
            letter = "B"
        parity = ""
        if inversion is not None:
            parity = "g" if chi[inversion] > 0 else "u"
        prime = ""
        if inversion is None and sigma_h is not None:
            prime = "'" if chi[sigma_h] > 0 else "''"
        labels.append([letter, parity, prime, chi])

    from collections import defaultdict as _defaultdict
    groups = _defaultdict(list)
    for i, (letter, parity, prime, chi) in enumerate(labels):
        groups[(letter, parity, prime)].append(i)
    final = [None] * len(labels)
    for (letter, parity, prime), members in groups.items():
        if len(members) == 1:
            final[members[0]] = letter + parity + prime
            continue

        def _sort_key(i):
            chi = labels[i][3]
            aux = c2prime if irreps[i][1] == 1 else principal
            if aux is None:
                aux = sigma_v if sigma_v is not None else principal
            # The totally symmetric irrep is A1 by definition; after that more
            # +1 characters rank earlier. Rounded, because the eigendecom-
            # position carries ~1e-15 noise that would otherwise outrank the
            # real +-1 distinctions.
            symmetric = round(sum(len(classes[c]) * chi[c]
                                  for c in range(nclasses)))
            return (-symmetric,
                    -round(chi[aux], 6) if aux is not None else 0.0,
                    tuple(-round(chi[c], 6) for c in range(nclasses)), i)

        for rank, i in enumerate(sorted(members, key=_sort_key), start=1):
            final[i] = "%s%d%s%s" % (letter, rank, parity, prime)

    # --- class headers -----------------------------------------------------
    col_order = [class_of[identity]] + [c for c in range(nclasses)
                                        if c != class_of[identity]]
    class_labels = []
    _mirror_rank = 0
    for c in col_order:
        if dets[c] > 0:
            k = orders[c]
            symbol = "E" if k == 1 else "C%d" % k
        elif inversion is not None and c == inversion:
            symbol = "i"
        elif abs(traces[c] - 1.0) < 1e-6:
            if c == sigma_h:
                symbol = "sh"
            else:
                symbol = "sv" if _mirror_rank == 0 else "sd"
                _mirror_rank += 1
        else:
            cos = _np.clip((traces[c] + 1.0) / 2.0, -1.0, 1.0)
            symbol = "S%d" % int(round(2.0 * _np.pi / _np.arccos(cos)))
        n = len(classes[c])
        class_labels.append(symbol if n == 1 else "%d%s" % (n, symbol))

    for i, label in enumerate(final):
        irreps[i].append(label)
    return class_labels, class_of, classes, irreps, col_order
)PY";

/// The little-group character machinery: which operations leave k invariant,
/// and the trace of each degenerate multiplet under them.
constexpr const char* kLittleGroupHelpers = R"PY(
def _calango_wrap(v):
    """Fractional vector folded into (-1/2, 1/2]."""
    w = _np.asarray(v, dtype=float) - _np.rint(v)
    return w


def _calango_symmetry_origin(rots, taus):
    """Origin that makes the space group as symmorphic as it can be.

    A space-group operation is {R|t}, and t depends on where the origin was
    put: moving it to x0 replaces t by t - (1 - R)x0. For a SYMMORPHIC group
    there is an x0 that zeroes every t at once — the symmetry centre — and
    the little-group irrep labels at a zone-BOUNDARY point are only
    convention-free once the origin sits there. spglib reports the operations
    in the cell it was handed, which is wherever the user's file put them, and
    that is routinely not the centre: ase.build.graphene() puts an atom at the
    origin, where the site symmetry is -6m2, while the 6/mmm centre of the
    hexagon sits at (1/3, 2/3).

    The condition is linear but modulo the lattice, so there is no least-
    squares solution. It is however exhaustible: every crystallographic
    origin shift has denominator 1, 2, 3, 4 or 6, so a 1/12 grid contains all
    of them. 1728 candidates scored against every operation, vectorized —
    milliseconds, and no Wyckoff table needed.

    Returns (x0, residual). A residual that will not go to zero means the
    group is genuinely NONSYMMORPHIC and keeps its glide/screw parts.
    """
    eye = _np.eye(3)
    axis = _np.arange(12) / 12.0
    cand = _np.stack(_np.meshgrid(axis, axis, axis, indexing="ij"),
                     axis=-1).reshape(-1, 3)
    score = _np.zeros(len(cand))
    for R, t in zip(rots, taus):
        d = _np.asarray(t, dtype=float)[None, :] \
            - cand @ (eye - _np.asarray(R, dtype=float)).T
        score += _np.sum(_np.abs(d - _np.rint(d)), axis=1)
    best = int(_np.argmin(score))
    return cand[best], float(score[best])


def _calango_little_group(rots, kfrac):
    """Indices of the operations with R^-T k = k + G, and their G."""
    members, shifts = [], []
    for i, R in enumerate(rots):
        rinv_t = _np.rint(_np.linalg.inv(R)).astype(int).T
        g = rinv_t @ kfrac - kfrac
        if _np.max(_np.abs(g - _np.rint(g))) < 1e-6:
            members.append(i)
            shifts.append(_np.rint(g).astype(int))
    return members, shifts


def _calango_projective(shifts, taus):
    """Whether the little-group representations at this k are PROJECTIVE.

    The reduced operators multiply as D(R)D(R') = w(R, R') D(RR') with the
    factor system w(R, R') = exp(-2 pi i G0(R) . t_R'), where G0(R) = R^-T k - k.
    When every w is 1 the representations are ordinary and the point group's own
    character table labels them; when some w is not, no Mulliken symbol applies
    and the honest output is the raw characters.

    This is a property of the GROUP and the wave vector — not of the bands — so
    it is decided here rather than inferred from how nearly integral a
    reduction came out. Inferring it from the reduction conflates a genuine
    projective representation with an unconverged empty band, which is a
    different problem with a different fix.

    Both conditions are needed: a nonsymmorphic translation is harmless at a
    k-point where every G0 vanishes (Gamma, above all), and a nonzero G0 is
    harmless in a symmorphic group. Only a zone boundary of a nonsymmorphic
    group has both.
    """
    for g in shifts:
        if not _np.any(g):
            continue
        for t in taus:
            if abs(float(_np.dot(g, t)) - round(float(_np.dot(g, t)))) > 1e-6:
                return True
    return False


def _calango_character_operator(shape, rot, shift, tau, kfrac, origin):
    """(index map, phase array) implementing {R|tau} on FFT coefficients.

    Writing psi = sum_G c_G exp(2 pi i (k+G).x), the operation
    {R|tau} psi (x) = psi(R^-1 (x - tau)) sends the coefficient at G to
    G' = G0 + R^-T G and multiplies it by exp(-2 pi i (k+G').tau). That is a
    PERMUTATION of the coefficient array plus a phase — exact on any grid,
    for any translation, including nonsymmorphic ones. Doing it in real space
    instead would need psi sampled at R^-1(x - tau), which is only a grid
    point when the divisions happen to work out.
    """
    n = _np.asarray(shape, dtype=_np.int64)
    freqs = [_np.fft.fftfreq(int(ni), d=1.0 / int(ni)).astype(_np.int64)
             for ni in n]
    grid = _np.stack(_np.meshgrid(*freqs, indexing="ij"), axis=-1)
    rinv_t = _np.rint(_np.linalg.inv(rot)).astype(_np.int64).T
    # G' = G0 + R^-T G, for every bin at once.
    gp = grid @ rinv_t.T + _np.asarray(shift, dtype=_np.int64)
    index = tuple(gp[..., a] % n[a] for a in range(3))
    phase = _np.exp(-2j * _np.pi * ((gp + kfrac) @ tau))
    return index, phase


def _calango_coefficients(calc, band, kpt, spin, kfrac, origin):
    """Plane-wave coefficients c_G of one pseudo Kohn-Sham state.

    The origin shift is folded in here: measuring x from x0 multiplies c_G by
    exp(2 pi i (k+G).x0), which is one array multiply rather than a resampled
    wave function.
    """
    u = calc.get_pseudo_wave_function(band=band, kpt=kpt, spin=spin,
                                      periodic=True)
    c = _np.fft.fftn(_np.asarray(u)) / u.size
    if _np.max(_np.abs(origin)) > 1e-12:
        n = _np.asarray(c.shape, dtype=_np.int64)
        freqs = [_np.fft.fftfreq(int(ni), d=1.0 / int(ni)).astype(_np.int64)
                 for ni in n]
        grid = _np.stack(_np.meshgrid(*freqs, indexing="ij"), axis=-1)
        c = c * _np.exp(2j * _np.pi * ((grid + kfrac) @ origin))
    return c
)PY";

} // namespace

std::string generateBandSymmetryBlock(const BandSymmetryConfig& c)
{
    std::ostringstream out;
    out << "\n"
           "# --- Band symmetry classification ---------------------------\n"
           "# Irreducible representations of the little group at each\n"
           "# high-symmetry point of the path. The classification follows\n"
           "# Kogan & Nazarov, Phys. Rev. B 85, 115418 (2012): the little\n"
           "# group at k is the set of space-group operations with\n"
           "# R^-T k = k + G, the character of a degenerate multiplet is the\n"
           "# trace of those operations over it, and the multiplet is\n"
           "# labelled by reducing that character against the little\n"
           "# co-group's table.\n"
           "#\n"
           "# The characters come from the PSEUDO wave functions: the PAW\n"
           "# augmentation spheres are spherically symmetric about sites the\n"
           "# operations permute, so they contribute the same to bra and ket\n"
           "# and cancel out of the normalized trace.\n"
           "import numpy as _np\n"
           "\n"
        << kPointGroupHelpers << "\n"
        << kLittleGroupHelpers << "\n"
           "band_symmetry = None\n"
           "try:\n"
           "    import spglib as _spglib\n"
           "\n"
        << "    _sym_symprec = " << c.symprec << "\n"
        << "    _sym_degen = " << c.degeneracyEv << "\n"
        << "    _sym_window = " << c.windowEv << "\n"
           "\n"
           "    _sym_atoms = band_calc.get_atoms()\n"
           "    _sym_cell = (_sym_atoms.cell[:],\n"
           "                 _sym_atoms.get_scaled_positions(),\n"
           "                 _sym_atoms.numbers)\n"
           "    _sym_ops = _spglib.get_symmetry(_sym_cell,\n"
           "                                    symprec=_sym_symprec)\n"
           "    if _sym_ops is None:\n"
           "        raise RuntimeError('spglib found no symmetry operations')\n"
           "    _sym_rots = [_np.rint(r).astype(int)\n"
           "                 for r in _sym_ops['rotations']]\n"
           "    _sym_taus = [_np.asarray(t, dtype=float)\n"
           "                 for t in _sym_ops['translations']]\n"
           "    _sym_data = _spglib.get_symmetry_dataset(_sym_cell,\n"
           "                                             symprec=_sym_symprec)\n"
           "\n"
           "    def _sym_field(name, default=None):\n"
           "        if _sym_data is None:\n"
           "            return default\n"
           "        if isinstance(_sym_data, dict):\n"
           "            return _sym_data.get(name, default)\n"
           "        return getattr(_sym_data, name, default)\n"
           "\n"
           "    _sym_origin, _sym_residual = _calango_symmetry_origin(\n"
           "        _sym_rots, _sym_taus)\n"
           "    _sym_eye = _np.eye(3)\n"
           "    # Translations re-expressed about the symmetry centre: zero\n"
           "    # for every operation of a symmorphic group, the glide/screw\n"
           "    # part for a nonsymmorphic one.\n"
           "    _sym_taus0 = [_calango_wrap(t - (_sym_eye - R) @ _sym_origin)\n"
           "                  for R, t in zip(_sym_rots, _sym_taus)]\n"
           "    print(f'CALANGO_INFO band symmetry: {len(_sym_rots)} space-'\n"
           "          f'group operations, residual translation '\n"
           "          f'{_sym_residual:.4f}', flush=True)\n"
           "\n"
           "    _sym_x, _sym_special_x, _sym_special_labels = \\\n"
           "        bs.path.get_linear_kpoint_axis()\n"
           "    _sym_kpts = _np.asarray(bs.path.kpts, dtype=float)\n"
           "    _sym_energies = _np.asarray(bs.energies, dtype=float)\n"
           "    _sym_lattice = _np.asarray(_sym_atoms.cell[:], dtype=float)\n"
           "\n"
           "    # What gets classified: every high-symmetry POINT of the path,\n"
           "    # and — when line symmetry is on — the midpoint of every\n"
           "    # segment, which is a generic point of the symmetry LINE\n"
           "    # joining them. The line labels are what make the\n"
           "    # compatibility relations readable (Kogan & Nazarov's\n"
           "    # Table III): an irrep at a point must be compatible with the\n"
           "    # one on each line running out of it, and a classification\n"
           "    # that only ever looks at the points cannot show that.\n"
           "    _sym_targets = []\n"
           "    _sym_seen = set()\n"
           "    for _sym_i, _sym_label in enumerate(_sym_special_labels):\n"
           "        # A path break arrives as two labels at the same x; both\n"
           "        # name the same k-point, so classify it once.\n"
           "        _sym_kx = float(_sym_special_x[_sym_i])\n"
           "        _sym_ik = int(_np.argmin(_np.abs(_sym_x - _sym_kx)))\n"
           "        if _sym_ik in _sym_seen:\n"
           "            continue\n"
           "        _sym_seen.add(_sym_ik)\n"
           "        _sym_targets.append((str(_sym_label), _sym_kx, _sym_ik,\n"
           "                             'point'))\n"
        << "    if " << (c.classifyLines ? "True" : "False") << ":\n"
           "        for _sym_i in range(len(_sym_special_x) - 1):\n"
           "            _sym_lo = float(_sym_special_x[_sym_i])\n"
           "            _sym_hi = float(_sym_special_x[_sym_i + 1])\n"
           "            if _sym_hi - _sym_lo < 1e-9:\n"
           "                continue   # a path break, not a segment\n"
           "            _sym_ik = int(_np.argmin(_np.abs(\n"
           "                _sym_x - 0.5 * (_sym_lo + _sym_hi))))\n"
           "            if _sym_ik in _sym_seen:\n"
           "                continue\n"
           "            _sym_seen.add(_sym_ik)\n"
           "            _sym_targets.append((\n"
           "                '%s-%s' % (_sym_special_labels[_sym_i],\n"
           "                           _sym_special_labels[_sym_i + 1]),\n"
           "                float(_sym_x[_sym_ik]), _sym_ik, 'line'))\n"
           "\n"
           "    _sym_points = []\n"
           "    for _sym_label, _sym_kx, _sym_ik, _sym_kind in _sym_targets:\n"
           "        _sym_k = _sym_kpts[_sym_ik]\n"
           "\n"
           "        _sym_members, _sym_shifts = _calango_little_group(\n"
           "            _sym_rots, _sym_k)\n"
           "        _sym_little = [_sym_rots[m] for m in _sym_members]\n"
           "        try:\n"
           "            _sym_classes = _calango_point_group(_sym_little,\n"
           "                                                _sym_lattice)\n"
           "        except Exception as _sym_err:\n"
           "            print(f'CALANGO_WARN no character table at '\n"
           "                  f'{_sym_label}: {_sym_err}', flush=True)\n"
           "            continue\n"
           "        _sym_class_labels, _sym_class_of, _sym_class_members, \\\n"
           "            _sym_irreps, _sym_col_order = _sym_classes\n"
           "        _sym_order = len(_sym_little)\n"
           "        _sym_point_projective = _calango_projective(\n"
           "            _sym_shifts, [_sym_taus0[_m] for _m in _sym_members])\n"
           "\n"
           "        # One index map + phase array per operation, reused by\n"
           "        # every band at this k-point.\n"
           "        _sym_shape = None\n"
           "        _sym_operators = None\n"
           "\n"
           "        _sym_spins = []\n"
           "        for _sym_s in range(_sym_energies.shape[0]):\n"
           "            _sym_eig = _sym_energies[_sym_s][_sym_ik]\n"
           "            # Group the bands into degenerate multiplets: the\n"
           "            # trace of a multiplet is what carries the irrep, and\n"
           "            # a degenerate pair split by numerical noise would\n"
           "            # otherwise give two meaningless 1D characters.\n"
           "            _sym_groups = []\n"
           "            for _sym_n, _sym_e in enumerate(_sym_eig):\n"
           "                if abs(_sym_e - efermi) > _sym_window:\n"
           "                    continue\n"
           "                if _sym_groups and abs(\n"
           "                        _sym_e - _sym_eig[_sym_groups[-1][-1]]) \\\n"
           "                        < _sym_degen:\n"
           "                    _sym_groups[-1].append(_sym_n)\n"
           "                else:\n"
           "                    _sym_groups.append([_sym_n])\n"
           "\n"
           "            _sym_multiplets = []\n"
           "            for _sym_bands in _sym_groups:\n"
           "                _sym_chi = _np.zeros(_sym_order, dtype=complex)\n"
           "                for _sym_n in _sym_bands:\n"
           "                    _sym_c = _calango_coefficients(\n"
           "                        band_calc, _sym_n, _sym_ik, _sym_s,\n"
           "                        _sym_k, _sym_origin)\n"
           "                    if _sym_operators is None:\n"
           "                        _sym_shape = _sym_c.shape\n"
           "                        _sym_operators = [\n"
           "                            _calango_character_operator(\n"
           "                                _sym_shape, _sym_rots[_sym_m],\n"
           "                                _sym_g, _sym_taus0[_sym_m],\n"
           "                                _sym_k, _sym_origin)\n"
           "                            for _sym_m, _sym_g\n"
           "                            in zip(_sym_members, _sym_shifts)]\n"
           "                    _sym_norm = float(\n"
           "                        _np.sum(_np.abs(_sym_c) ** 2))\n"
           "                    if _sym_norm <= 0.0:\n"
           "                        continue\n"
           "                    for _sym_j, (_sym_idx, _sym_ph) \\\n"
           "                            in enumerate(_sym_operators):\n"
           "                        _sym_chi[_sym_j] += _np.sum(\n"
           "                            _np.conj(_sym_c[_sym_idx]) * _sym_c\n"
           "                            * _sym_ph) / _sym_norm\n"
           "\n"
           "                # Reduced character: the pure-translation phase of\n"
           "                # a nonsymmorphic operation divided out, so what\n"
           "                # remains can be matched against an ORDINARY\n"
           "                # point-group table. Identity when every t is 0.\n"
           "                _sym_chit = _np.array(\n"
           "                    [_sym_chi[_sym_j]\n"
           "                     * _np.exp(2j * _np.pi\n"
           "                               * float(_np.dot(_sym_k,\n"
           "                                               _sym_taus0[_sym_m])))\n"
           "                     for _sym_j, _sym_m\n"
           "                     in enumerate(_sym_members)])\n"
           "\n"
           "                _sym_mult = []\n"
           "                for _sym_chi_i, _sym_dim, _sym_paired, _sym_lab \\\n"
           "                        in _sym_irreps:\n"
           "                    _sym_per_op = _np.array(\n"
           "                        [_sym_chi_i[_sym_class_of[_sym_j]]\n"
           "                         for _sym_j in range(_sym_order)])\n"
           "                    _sym_n_a = _np.sum(_np.conj(_sym_per_op)\n"
           "                                       * _sym_chit) / _sym_order\n"
           "                    # A paired character is chi + conj(chi), so a\n"
           "                    # projector built from it counts both members.\n"
           "                    _sym_mult.append(\n"
           "                        float(_sym_n_a.real)\n"
           "                        / (2.0 if _sym_paired else 1.0))\n"
           "\n"
           "                _sym_round = [int(round(m)) for m in _sym_mult]\n"
           "                _sym_resid = float(max(\n"
           "                    [abs(m - r) for m, r\n"
           "                     in zip(_sym_mult, _sym_round)] or [0.0]))\n"
           "                _sym_names = []\n"
           "                for _sym_r, _sym_irrep in zip(_sym_round,\n"
           "                                              _sym_irreps):\n"
           "                    _sym_names += [_sym_irrep[3]] * max(_sym_r, 0)\n"
           "                # A reduction that will not come out in whole\n"
           "                # numbers means this multiplet is not a\n"
           "                # representation of the group as computed — an\n"
           "                # unconverged empty band, or a degeneracy the\n"
           "                # window cut in half. Withhold the label rather\n"
           "                # than print the nearest one; the characters are\n"
           "                # still reported for anyone who wants to look.\n"
           "                _sym_resolved = bool(_sym_resid <= 0.1\n"
           "                                     and not _sym_point_projective)\n"
           "                if not _sym_resolved:\n"
           "                    _sym_names = []\n"
           "                _sym_multiplets.append({\n"
           "                    'bands': [int(b) for b in _sym_bands],\n"
           "                    'degeneracy': len(_sym_bands),\n"
           "                    'energy_eV': float(_sym_eig[_sym_bands[0]]),\n"
           "                    'irreps': _sym_names,\n"
           "                    'resolved': _sym_resolved,\n"
           "                    'label': ' + '.join(_sym_names)\n"
           "                             if _sym_names else '?',\n"
           "                    'multiplicities': [float(round(m, 4))\n"
           "                                       for m in _sym_mult],\n"
           "                    'characters': [float(_np.round(v.real, 4))\n"
           "                                   for v in _sym_chit],\n"
           "                    'residual': round(_sym_resid, 4),\n"
           "                })\n"
           "            _sym_spins.append({'spin': int(_sym_s),\n"
           "                               'multiplets': _sym_multiplets})\n"
           "\n"
           "        _sym_worst = max([m['residual'] for _sp in _sym_spins\n"
           "                          for m in _sp['multiplets']] or [0.0])\n"
           "        if _sym_point_projective:\n"
           "            print(f'CALANGO_WARN {_sym_label}: the little-group '\n"
           "                  'representations are PROJECTIVE (a zone boundary '\n"
           "                  'of a nonsymmorphic group), so no ordinary '\n"
           "                  'Mulliken label applies; the characters are '\n"
           "                  'reported without one.', flush=True)\n"
           "        _sym_points.append({\n"
           "            'label': str(_sym_label),\n"
           "            'kind': _sym_kind,\n"
           "            'x': _sym_kx,\n"
           "            'kpoint_index': _sym_ik,\n"
           "            'kpoint': [float(v) for v in _sym_k],\n"
           "            'order': _sym_order,\n"
           "            'classes': list(_sym_class_labels),\n"
           "            'character_table': [\n"
           "                {'label': _sym_irrep[3],\n"
           "                 'dim': int(_sym_irrep[1]),\n"
           "                 'chi': [float(_sym_irrep[0][_sym_c])\n"
           "                         for _sym_c in _sym_col_order]}\n"
           "                for _sym_irrep in _sym_irreps],\n"
           "            'projective': bool(_sym_point_projective),\n"
           "            'max_residual': float(_sym_worst),\n"
           "            'spins': _sym_spins,\n"
           "        })\n"
           "\n"
           "    band_symmetry = {\n"
           "        'symprec': _sym_symprec,\n"
           "        'degeneracy_tol_eV': _sym_degen,\n"
           "        'window_eV': _sym_window,\n"
           "        'efermi': float(efermi),\n"
           "        'space_group': str(_sym_field('international', '')),\n"
           "        'space_group_number': int(_sym_field('number', 0) or 0),\n"
           "        'origin_shift': [float(v) for v in _sym_origin],\n"
           "        'nonsymmorphic_residual': float(_sym_residual),\n"
           "        'points': _sym_points,\n"
           "    }\n"
           "    with open('band_symmetry.json', 'w') as _handle:\n"
           "        json.dump(band_symmetry, _handle)\n"
           "    print('CALANGO_INFO band symmetry: '\n"
           "          + ', '.join(f\"{p['label']}=\"\n"
           "                      + '/'.join(m['label'] for m\n"
           "                                 in p['spins'][0]['multiplets'][:4])\n"
           "                      for p in _sym_points), flush=True)\n"
           "except Exception as _sym_exc:\n"
           "    # Never fatal: the band structure itself is complete and\n"
           "    # useful without the labels, and the classification needs\n"
           "    # spglib plus wave functions the backend may not expose.\n"
           "    import traceback as _sym_tb\n"
           "    print('CALANGO_WARN band symmetry classification failed: '\n"
           "          f'{_sym_exc}', flush=True)\n"
           "    _sym_tb.print_exc()\n";
    return out.str();
}

} // namespace calango::core
