#include "core/UnfoldingScriptGenerator.hpp"

#include "core/AseScriptGenerator.hpp"

#include <sstream>

namespace calango::core {

namespace {

/// The projection itself. Written once here rather than per backend: only the
/// calculator construction differs, and the Popescu-Zunger algebra is the
/// same whatever produced the wavefunctions.
constexpr const char* kProjection = R"PY(
def folding_offset(matrix, k_primitive, k_supercell):
    """The primitive-fractional shift the folding removed, k - K.

    THIS IS NOT k. The band path's k is reduced to K in SUPERCELL fractional
    coordinates, so what separates them is an integer vector of SUPERCELL
    reciprocal lattice vectors -- which is not an integer vector of primitive
    ones. Testing G against k instead of against this offset selects nothing
    at all except at the zone centre, and the whole map comes out empty.

    K is passed in rather than re-derived from round(k @ M.T), and that is not
    fussiness. Two k that fold to the same K can round to different integer
    vectors -- numpy rounds 0.5 to 0 and 1.5 to 2 -- and the offsets then
    differ by half a primitive reciprocal vector, which is a DIFFERENT mask.
    Taking K from the array the wavefunctions were actually computed at makes
    the offset consistent with them by construction.
    """
    import numpy as np

    inverse = np.linalg.inv(np.asarray(matrix, dtype=float))
    k_prim_of_K = np.asarray(k_supercell, dtype=float) @ inverse.T
    return np.asarray(k_primitive, dtype=float) - k_prim_of_K


def unfold_weights(calc, projection_cell, matrix, k_primitive, k_supercell,
                   k_supercell_index, spin):
    """Popescu-Zunger spectral weights P_Km(k) for one primitive wavevector.

    For each supercell band m at the folded wavevector K, sum the squared
    plane-wave coefficients whose G satisfies K + G - k in L*_primitive:

        P_Km(k) = sum_{G : G - (k - K) in L*_prim} |C_{Km}(G)|^2

    Only those components carry primitive Bloch character at k; the rest of
    the expansion belongs to the other |det M| - 1 primitive k that fold onto
    the same K. Summed over those, the weights are exactly 1 for every band --
    they partition the basis -- which is what check_partition() below asserts.
    """
    import numpy as np
    from ase.units import Bohr

    wfs = calc.wfs
    # BOTH arguments are required: get_rank_and_index(k, s). Called with the
    # k alone it raises TypeError, which is how a spin index that was never
    # threaded through this function announced itself.
    kpt_rank, kpt_local = wfs.kd.get_rank_and_index(k_supercell_index, spin)
    # Plane-wave G vectors at this K. GPAW returns them in ATOMIC UNITS
    # (1/Bohr) and already carrying the 2*pi; ASE's cell.reciprocal() is
    # 1/Angstrom WITHOUT it. Mixing the two scales every coordinate by
    # 1.8897, so nothing is ever an integer, the mask selects nothing, and
    # every weight is 0.0 -- a blank heatmap rather than an error.
    g_bohr = wfs.pd.get_reciprocal_vectors(q=kpt_local, add_q=False)
    g_primitive = ((g_bohr / Bohr) / (2.0 * np.pi)) @ np.linalg.inv(
        projection_cell.reciprocal())

    residual = g_primitive - folding_offset(matrix, k_primitive, k_supercell)
    residual -= np.round(residual)
    primitive_mask = np.all(np.abs(residual) < 1e-4, axis=1)

    psit_nG = wfs.kpt_u[kpt_local].psit_nG
    weights = []
    for band in range(wfs.bd.nbands):
        coefficients = psit_nG[band]
        total = np.vdot(coefficients, coefficients).real
        if total <= 0.0:
            weights.append(0.0)
            continue
        selected = coefficients[primitive_mask]
        weights.append(float(np.vdot(selected, selected).real / total))
    return weights


def check_partition(calc, projection_cell, matrix, k_primitive, k_supercell,
                    k_supercell_index, spin, bands=8):
    """The one identity here that does not depend on any physics.

    Every plane wave of a supercell state at K belongs to exactly ONE of the
    |det M| primitive wavevectors that fold onto K, so their weights sum to 1
    for every band -- pristine cell or defective, metal or insulator. It is a
    partition of the basis, and it catches a wrong unit, a wrong offset and a
    wrong transpose in one number.

    Only for a DIAGONAL M, where the partners are k + (i/m00, j/m11, l/m22);
    enumerating them for a general integer matrix needs its Smith normal form
    and is not worth carrying into a generated script.
    """
    import numpy as np
    from itertools import product

    m = np.asarray(matrix)
    if np.count_nonzero(m - np.diag(np.diag(m))):
        return None
    diagonal = [int(abs(d)) for d in np.diag(m)]
    if min(diagonal) < 1:
        return None

    # Every partner shares K -- adding shift/diag to k adds the integer vector
    # `shift` to k @ M.T -- so all of them are projections of the SAME
    # wavefunctions, which is what makes the sum an identity.
    total = None
    for shift in product(*(range(d) for d in diagonal)):
        partner = np.asarray(k_primitive, dtype=float) + np.asarray(
            shift, dtype=float) / np.asarray(diagonal, dtype=float)
        w = np.asarray(unfold_weights(calc, projection_cell, matrix, partner,
                                      k_supercell, k_supercell_index,
                                      spin)[:bands])
        total = w if total is None else total + w
    return float(np.abs(total - 1.0).max())
)PY";

} // namespace

std::string generateUnfoldingScript(const UnfoldingConfig& c)
{
    std::ostringstream out;
    out << "#!/usr/bin/env python3\n"
           "# Generated by Calango " << CALANGO_VERSION
        << " — effective band structure (Popescu-Zunger band unfolding).\n"
           "#\n"
           "# Converges the supercell, then projects each supercell eigenstate\n"
           "# back onto the primitive Bloch basis along the primitive cell's\n"
           "# high-symmetry path, writing effective_bands.json for the\n"
           "# Results heatmap. Reference: Popescu & Zunger, Phys. Rev. B 85,\n"
           "# 085201 (2012).\n"
           "# This is a plain ASE script: edit it freely or run it standalone.\n"
           "\n"
           "import json\n"
           "\n"
           "import numpy as np\n"
           "from ase.io import read\n"
           "\n"
        << AseScriptGenerator::jsonLoggerPreamble()
        << "atoms = read(r\"" << c.supercellFile << "\")\n"
        << "primitive = read(r\"" << c.primitiveFile << "\")\n"
           "\n"
           "# supercell = M . primitive, as deduced by the wizard.\n"
           "M = np.array([\n";
    for (int i = 0; i < 3; ++i) {
        out << "    [" << c.matrix.m[i][0] << ", " << c.matrix.m[i][1] << ", "
            << c.matrix.m[i][2] << "],\n";
    }
    out << "], dtype=int)\n"
           "\n"
           "# Commensurability guard: unfolding is only defined when the\n"
           "# supercell really is an integer multiple of the primitive cell.\n"
           "#\n"
           "# Measured RELATIVE to the supercell's own size. The absolute\n"
           "# residual this used to compare is a tight bound on a 4 Ang cell\n"
           "# and a meaningless one on a 40 Ang slab.\n"
           "residual = np.abs(atoms.cell[:] - M @ primitive.cell[:]).max()\n"
           "_scale = float(np.abs(atoms.cell[:]).max()) or 1.0\n"
        << "if residual / _scale > " << c.commensurateTolerance
        << ":\n"
           "    raise RuntimeError(\n"
           "        f\"Supercell and primitive cell are not commensurate \"\n"
           "        f\"(residual {residual:.4f} Ang, {100 * residual / _scale:.2f}% \"\n"
           "        f\"of the cell). Check the mapping matrix, raise the \"\n"
           "        f\"tolerance, or tick 'Force commensurability' — a relaxed \"\n"
           "        f\"supercell needs the last of those.\")\n"
           "if residual > 1e-9:\n"
           "    print(f\"CALANGO_WARN the cells are commensurate only to \"\n"
           "          f\"{100 * residual / _scale:.2f}%; the projection assumes \"\n"
           "          f\"an exact integer relation\", flush=True)\n"
           "print(f\"CALANGO_INFO primitive_cells={int(round(abs(np.linalg.det(M))))}\",\n"
           "      flush=True)\n"
           "\n"
           "# The lattice the PROJECTION uses, derived from the supercell\n"
           "# instead of read from the primitive file.\n"
           "#\n"
           "# These differ whenever the supercell was relaxed: the two cells\n"
           "# then agree only to the tolerance checked above. That residual is\n"
           "# harmless for the band PATH, which is a choice of where to look,\n"
           "# and fatal for the projection, which asks whether a coordinate is\n"
           "# an integer. The error grows with |G|, so at the plane-wave cutoff\n"
           "# a 0.05%% cell mismatch moves a coordinate by far more than the\n"
           "# 1e-4 acceptance window and quietly discards the high-G half of\n"
           "# every state. Taking primitive = M^-1 . supercell makes the\n"
           "# relation exact by construction.\n"
           "from ase.cell import Cell\n"
           "projection_cell = Cell(np.linalg.inv(M) @ atoms.cell[:])\n"
           "\n"
        << "energy_min = " << c.spectral.energyMin << "\n"
        << "energy_max = " << c.spectral.energyMax << "\n"
        << "energy_bins = " << c.spectral.energyBins << "\n"
        << "sigma = " << c.spectral.sigma << "\n"
        << "weight_threshold = " << c.spectral.weightThreshold << "\n"
        << "points_per_segment = " << c.pointsPerSegment << "\n"
        << "kpath_string = "
        << (c.kpath.empty() ? std::string("None")
                            : "\"" + c.kpath + "\"")
        << "\n"
           "\n"
           "# The path lives on the PRIMITIVE lattice — that is the zone the\n"
           "# effective band structure is drawn in.\n"
           "band_path = primitive.cell.bandpath(\n"
           "    kpath_string, npoints=None,\n"
           "    density=None) if kpath_string else primitive.cell.bandpath()\n"
           "kpts_primitive = band_path.kpts\n"
           "print(f\"CALANGO_INFO kpoints={len(kpts_primitive)}\", flush=True)\n"
           "\n"
           "# Each primitive k folds onto a supercell K: K_frac = M^T . k_frac,\n"
           "# reduced into the first zone. The supercell SCF only needs those.\n"
           "folded = (kpts_primitive @ M.T)\n"
           "folded -= np.round(folded)\n"
           "\n";

    switch (c.backend) {
    case UnfoldingBackend::Gpaw:
        out << "# GPAW in plane-wave mode: its wavefunction object exposes the\n"
               "# expansion coefficients the projection needs. FD and LCAO do\n"
               "# not, so PW is required here regardless of the Stage-3 choice.\n"
            << AseScriptGenerator::gpawImports(c.calculator)
            << "\n"
               "atoms.calc = GPAW(\n"
            << AseScriptGenerator::gpawKeywordArguments(c.calculator, "    ")
            << "    txt=\"unfold_scf.txt\",\n"
               ")\n"
               "atoms.get_potential_energy()\n"
               "efermi = float(atoms.calc.get_fermi_level())\n"
               "_calango_progress(1, 3)\n"
               "\n"
               "# Non-self-consistent pass at the folded wavevectors.\n"
               "band_calc = atoms.calc.fixed_density(\n"
               "    kpts=folded, symmetry=\"off\", txt=\"unfold_bands.txt\")\n"
               "_calango_progress(2, 3)\n"
            << kProjection
            << "\n"
               "# Both spin channels, summed into one spectral function.\n"
               "# A(k, E) is a sum over states and the heatmap has no spin\n"
               "# axis, so majority and minority states land in the same\n"
               "# column — correct for the map, and worth saying out loud,\n"
               "# because a defect level that appears in one channel only\n"
               "# (an NV centre's, for instance) is then indistinguishable\n"
               "# from one that appears in both.\n"
               "nspins = band_calc.get_number_of_spins()\n"
               "if nspins > 1:\n"
               "    print(\"CALANGO_WARN spin-polarized: the map sums both \"\n"
               "          \"channels, so majority and minority levels are not \"\n"
               "          \"distinguishable in it\", flush=True)\n"
               "\n"
               "# One self-test before the loop.\n"
               "#\n"
               "# The weights of the |det M| primitive wavevectors that fold\n"
               "# onto one K must sum to 1 for every band: they partition the\n"
               "# plane-wave basis. No physics enters, so a deviation is a bug\n"
               "# in the projection — a wrong unit, a wrong offset, a wrong\n"
               "# transpose — and not a property of the system. Checked here\n"
               "# rather than trusted, because each of those failures produces\n"
               "# a plausible-looking map rather than an error.\n"
               "#\n"
               "# THREE k-points, not one. The path starts at a high-symmetry\n"
               "# point often enough that a check run only there is close to\n"
               "# useless: at Gamma the folding offset is zero, so a projection\n"
               "# that ignores the offset entirely still partitions the basis\n"
               "# correctly and passes. It is the interior points, where the\n"
               "# offset is not a lattice vector, that discriminate.\n"
               "_samples = sorted({0, len(kpts_primitive) // 2,\n"
               "                   len(kpts_primitive) - 1})\n"
               "_checked = [check_partition(band_calc, projection_cell, M,\n"
               "                            kpts_primitive[i], folded[i], i, 0)\n"
               "            for i in _samples]\n"
               "_deviation = (None if any(v is None for v in _checked)\n"
               "              else max(_checked))\n"
               "if _deviation is None:\n"
               "    print(\"CALANGO_INFO partition check skipped (M is not \"\n"
               "          \"diagonal)\", flush=True)\n"
               "elif _deviation > 1e-3:\n"
               "    raise RuntimeError(\n"
               "        f\"The unfolding weights do not partition the basis: \"\n"
               "        f\"the {int(round(abs(np.linalg.det(M))))} primitive \"\n"
               "        f\"wavevectors folding onto one K sum to 1 only within \"\n"
               "        f\"{_deviation:.3e}. That is an identity, not a \"\n"
               "        f\"convergence criterion, so the projection is wrong \"\n"
               "        f\"and every weight below would be meaningless.\")\n"
               "else:\n"
               "    print(f\"CALANGO_INFO partition check ok \"\n"
               "          f\"(max |sum-1| = {_deviation:.2e})\", flush=True)\n"
               "\n"
               "columns = []\n"
               "for index, k in enumerate(kpts_primitive):\n"
               "    energies = []\n"
               "    weights = []\n"
               "    for spin in range(nspins):\n"
               "        energies.extend(\n"
               "            float(e)\n"
               "            for e in band_calc.get_eigenvalues(kpt=index, spin=spin))\n"
               "        weights.extend(unfold_weights(band_calc, projection_cell,\n"
               "                                      M, k, folded[index],\n"
               "                                      index, spin))\n"
               "    columns.append({\n"
               "        \"path_coordinate\": float(band_path.get_linear_kpoint_axis()[0][index]),\n"
               "        \"energies\": [float(e) for e in energies],\n"
               "        \"weights\": [float(w) for w in weights],\n"
               "    })\n"
               "    _calango_progress(2 + index / max(1, len(kpts_primitive)), 3)\n";
        break;
    case UnfoldingBackend::Espresso:
    case UnfoldingBackend::Siesta:
        out << "# EDIT ME: "
            << (c.backend == UnfoldingBackend::Espresso ? "Quantum ESPRESSO"
                                                        : "SIESTA")
            << " does not expose plane-wave\n"
               "# coefficients through its ASE calculator, so the projection\n"
               "# has to read the code's own wavefunction files. Converge the\n"
               "# supercell here, then fill in unfold_weights() against that\n"
               "# output (pw.x writes .save/wfc*.dat; SIESTA writes .WFSX).\n"
               "raise NotImplementedError(\n"
               "    \"Unfolding for this backend needs its wavefunction reader; \"\n"
               "    \"use GPAW, or complete this hook.\")\n"
               "\n"
               "columns = []\n"
               "efermi = 0.0\n";
        break;
    }

    out << "\n"
           "labels_x, labels = band_path.get_linear_kpoint_axis()[1:3]\n"
           "summary = {\n"
           "    \"efermi\": efermi,\n"
           "    \"special_x\": [float(x) for x in labels_x],\n"
           "    \"special_labels\": [str(l) for l in labels],\n"
           "    \"energy_min\": energy_min,\n"
           "    \"energy_max\": energy_max,\n"
           "    \"energy_bins\": energy_bins,\n"
           "    \"sigma\": sigma,\n"
           "    \"weight_threshold\": weight_threshold,\n"
           "    \"columns\": columns,\n"
           "}\n"
           "with open(\"effective_bands.json\", \"w\") as handle:\n"
           "    json.dump(summary, handle)\n"
           "print(\"CALANGO_INFO wrote effective_bands.json\", flush=True)\n"
           "_calango_progress(3, 3)\n"
           "\n"
           "print(f\"CALANGO_RESULT kpoints={len(columns)} efermi_eV={efermi:.6f}\",\n"
           "      flush=True)\n"
           "print(\"CALANGO_DONE\", flush=True)\n";
    return out.str();
}

} // namespace calango::core
