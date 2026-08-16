#include "core/StrainScriptHelpers.hpp"

#include "core/LocaleSafeNumber.hpp"

#include <sstream>

namespace calango::core {

std::string strainMatrixLiteral(const Matrix3& f)
{
    std::ostringstream out;
    out << "[[" << localeSafeFormat(f[0][0]) << ", " << localeSafeFormat(f[0][1]) << ", "
        << localeSafeFormat(f[0][2]) << "], "
        << "[" << localeSafeFormat(f[1][0]) << ", " << localeSafeFormat(f[1][1]) << ", "
        << localeSafeFormat(f[1][2]) << "], "
        << "[" << localeSafeFormat(f[2][0]) << ", " << localeSafeFormat(f[2][1]) << ", "
        << localeSafeFormat(f[2][2]) << "]]";
    return out.str();
}

std::vector<int> strainSampleMultiples(int pointsPerComponent)
{
    int half = pointsPerComponent / 2;
    if (half < 1)
        half = 1;
    std::vector<int> out;
    for (int m = half; m >= 1; --m)
        out.push_back(-m);
    for (int m = 1; m <= half; ++m)
        out.push_back(m);
    return out;
}

std::string strainPeriodicityGuardPython(const std::string& capabilitySubject)
{
    std::ostringstream out;
    out << "_pbc = list(atoms.pbc)\n"
           "if sum(1 for p in _pbc if p) < 2:\n"
           "    raise RuntimeError(\n"
        << "        '" << capabilitySubject
        << " needs periodicity along at least\\n'\n"
           "        'the two in-plane directions (three for a bulk "
           "crystal). This\\n'\n"
           "        'structure has pbc = ' + str(_pbc) + '.')\n"
           "\n";
    return out.str();
}

std::string strainVacuumAxisBlockPython(int vacuumAxis)
{
    const bool is2D = vacuumAxis >= 0 && vacuumAxis <= 2;
    std::ostringstream out;
    out << "VACUUM_AXIS = " << (is2D ? std::to_string(vacuumAxis) : "None")
        << "\n"
           "IS_2D = VACUUM_AXIS is not None\n"
           "if IS_2D:\n"
           "    # Confirm the vacuum axis Calango detected against the "
           "actual\n"
           "    # geometry — the fractional-coordinate gap along that axis, "
           "the\n"
           "    # same measure the wizard's own vacuum-axis guess used, "
           "re-derived\n"
           "    # here so a stale or hand-edited config shows up rather "
           "than being\n"
           "    # silently trusted.\n"
           "    _frac = atoms.get_scaled_positions(wrap=True)[:, "
           "VACUUM_AXIS]\n"
           "    _gap_frac = 1.0 - (float(_frac.max()) - float(_frac.min()))\n"
           "    _axis_length_A = float(atoms.cell.lengths()[VACUUM_AXIS])\n"
           "    _gap_A = _gap_frac * _axis_length_A\n"
           "    print(f'CALANGO_INFO 2D structure: vacuum axis "
           "{VACUUM_AXIS}, '\n"
           "          f'gap approx {_gap_A:.2f} A of {_axis_length_A:.2f} A "
           "total '\n"
           "          f'(pbc={_pbc})', flush=True)\n"
           "    if _gap_A < 6.0:\n"
           "        print(f'CALANGO_WARN the vacuum gap along axis "
           "{VACUUM_AXIS} is '\n"
           "              f'only {_gap_A:.2f} A -- periodic images may "
           "still interact; '\n"
           "              '10+ A is the usual minimum for a converged "
           "slab.',\n"
           "              flush=True)\n"
           "    # Best-effort — not every calculator config exposes 'kpts' "
           "as a\n"
           "    # {'size': ...} dict, so this warns when it can and stays "
           "quiet\n"
           "    # otherwise rather than fail a run over a diagnostic.\n"
           "    if '_baseline' in globals():\n"
           "        try:\n"
           "            _kpts_param = _baseline.parameters.get('kpts')\n"
           "            _size = (_kpts_param.get('size')\n"
           "                     if isinstance(_kpts_param, dict) else "
           "None)\n"
           "            if _size is not None and _size[VACUUM_AXIS] != 1:\n"
           "                print(\n"
           "                    f'CALANGO_WARN the baseline k-mesh has "
           "{_size[VACUUM_AXIS]} '\n"
           "                    'point(s) along the vacuum axis (index '\n"
           "                    f'{VACUUM_AXIS}); a 2D slab is normally "
           "sampled with '\n"
           "                    'exactly 1 there -- extra points along "
           "vacuum cost '\n"
           "                    'time for no physical benefit.', "
           "flush=True)\n"
           "        except Exception:\n"
           "            pass\n"
           "\n";
    return out.str();
}

std::string strainPointGroupDetectionPython(double symprec, const std::string& importErrorMessage)
{
    std::ostringstream out;
    out << "\n"
           "# --- Symmetry --------------------------------------------\n"
           "point_group = None\n"
           "point_group_ops_cartesian = []\n"
           "try:\n"
           "    import spglib\n"
        << "    _sym_symprec = " << localeSafeFormat(symprec) << "\n"
           "    _sym_cell = (atoms.cell[:], atoms.get_scaled_positions(),\n"
           "                 atoms.numbers)\n"
           "    _sym_data = spglib.get_symmetry_dataset(\n"
           "        _sym_cell, symprec=_sym_symprec)\n"
           "    _sym_ops = spglib.get_symmetry(_sym_cell, symprec=_sym_symprec)\n"
           "    if _sym_data is not None and _sym_ops is not None:\n"
           "        point_group = (_sym_data['pointgroup']\n"
           "                       if isinstance(_sym_data, dict)\n"
           "                       else getattr(_sym_data, 'pointgroup', None))\n"
           "        # Cartesian rotation from spglib's FRACTIONAL one.\n"
           "        # r_cart = cell^T . f_frac (cell rows are lattice\n"
           "        # vectors), and spglib's R acts as f' = R . f, so the\n"
           "        # matching Cartesian map is\n"
           "        #     R_cart = cell^T . R . (cell^T)^-1.\n"
           "        _cellT = np.array(atoms.cell[:]).T\n"
           "        _cellT_inv = np.linalg.inv(_cellT)\n"
           "        for _r_frac in _sym_ops['rotations']:\n"
           "            _r_cart = _cellT @ np.asarray(_r_frac, dtype=float) "
           "@ _cellT_inv\n"
           "            point_group_ops_cartesian.append(_r_cart)\n"
           "        print(f'CALANGO_INFO point group: {point_group}, '\n"
           "              f'{len(point_group_ops_cartesian)} operation(s)',\n"
           "              flush=True)\n"
           "except ImportError:\n"
        << "    print('CALANGO_WARN spglib not installed: " << importErrorMessage
        << "',\n"
           "          flush=True)\n";
    return out.str();
}

std::string applyStrainFunctionPython()
{
    return "def apply_strain(reference, f_matrix):\n"
           "    new_cell = np.array(reference.get_cell()[:]) @ np.array(f_matrix).T\n"
           "    strained = reference.copy()\n"
           "    # scale_atoms=True keeps the FRACTIONAL coordinates fixed — the\n"
           "    # CLAMPED-ION convention this base case computes.\n"
           "    strained.set_cell(new_cell, scale_atoms=True)\n"
           "    if IS_2D:\n"
           "        # An in-plane-only strain must not move atoms along the\n"
           "        # vacuum axis at all — assert it rather than trust the\n"
           "        # Voigt-component restriction silently: a bug in\n"
           "        # STRAIN_POINTS' F matrices would otherwise show up only\n"
           "        # as a subtly wrong tensor, never as a visible failure.\n"
           "        _before = reference.get_scaled_positions(wrap=False)[:, "
           "VACUUM_AXIS]\n"
           "        _after = strained.get_scaled_positions(wrap=False)[:, "
           "VACUUM_AXIS]\n"
           "        assert np.allclose(_before, _after, atol=1e-10), (\n"
           "            'a \"2D\" strain moved atoms along the vacuum axis — "
           "this is a '\n"
           "            'bug in the requested Voigt components, not a "
           "physical result')\n"
           "    return strained\n"
           "\n"
           "\n";
}

} // namespace calango::core
