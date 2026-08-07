#include "core/ElectronicScriptGenerator.hpp"

#include "core/AseScriptGenerator.hpp"
#include "core/BandSymmetryScriptGenerator.hpp"

#include <sstream>

namespace calango::core {

namespace {

/// A double-quoted Python string literal. The channel label and the element
/// filter are free text the user typed into the wizard, and a stray quote or
/// backslash in either would turn a generated script into a SyntaxError that
/// only surfaces when the job starts.
std::string pythonString(const std::string& text)
{
    std::string out = "\"";
    for (const char c : text) {
        switch (c) {
        case '"':  out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        case '\n': out += "\\n"; break;
        case '\r': out += "\\r"; break;
        default:   out += c; break;
        }
    }
    out += '"';
    return out;
}

/// Python literal for one fatband channel. Atom indices are baked in rather
/// than re-derived in the script: the user picked specific atoms in the
/// wizard, and re-deriving them from the element would silently widen a
/// "surface Ni" selection into "every Ni in the slab".
std::string fatbandChannelLiteral(const FatbandProjection& p)
{
    std::ostringstream out;
    out << "{'label': " << pythonString(p.label) << ", 'atoms': [";
    for (std::size_t i = 0; i < p.atoms.size(); ++i)
        out << (i ? ", " : "") << p.atoms[i];
    out << "], 'element': " << pythonString(p.element)
        << ", 'l': " << p.angularMomentum
        << ", 'm': " << p.magnetic << "}";
    return out.str();
}

/// Per-band, per-k orbital weights from the PAW projector overlaps.
///
/// The route is gpaw.dos.IBZWaveFunctions.pdos_weights, the same call the PDOS
/// above goes through — but taken BEFORE the energy integration, so what comes
/// back is |⟨p_i | ψ_nk⟩|² resolved by band and k-point instead of smeared into
/// a density of states. It is the portable entry point across both GPAW
/// engines, which get_orbital_ldos is not.
std::string fatbandBlock(const ElectronicConfig& c)
{
    std::ostringstream out;
    out << "\n"
           "# --- Orbital-projected bands (\"fatbands\") -------------------\n"
           "# |<phi_lm^a | psi_nk>|^2 per band and per k-point, so each band\n"
           "# can be drawn with a width or a colour proportional to the\n"
           "# weight of the orbitals selected in the wizard. A band structure\n"
           "# says where the states are; this says what they are made of.\n"
           "fatbands = None\n"
           "try:\n"
           "    import numpy as _fat_np\n"
           "    from gpaw.dos import DOSCalculator as _FatDOS\n"
           "    from gpaw.dos import get_projector_numbers as _fat_projectors\n"
           "\n"
           "    _fat_calc = _FatDOS.from_calculator(band_calc,\n"
           "                                        shift_fermi_level=False)\n"
           "    _fat_atoms = band_calc.get_atoms()\n"
           "    _fat_symbols = list(_fat_atoms.get_chemical_symbols())\n"
           "    _fat_shells = \"spdf\"\n"
           "\n";
    if (c.fatbandProjections.empty()) {
        out << "    # No explicit selection: one channel per element and per\n"
               "    # shell the setups actually carry projectors for. Asking\n"
               "    # for d on carbon would raise, not return zeros.\n"
               "    _fat_channels = []\n"
               "    for _fat_sym in sorted(set(_fat_symbols)):\n"
               "        _fat_idx = [_i for _i, _s in enumerate(_fat_symbols)\n"
               "                    if _s == _fat_sym]\n"
               "        for _fat_l in range(4):\n"
               "            if not _fat_projectors(_fat_calc.setups[_fat_idx[0]],\n"
               "                                   _fat_l):\n"
               "                continue\n"
               "            _fat_channels.append({\n"
               "                'label': f'{_fat_sym} {_fat_shells[_fat_l]}',\n"
               "                'atoms': list(_fat_idx), 'element': _fat_sym,\n"
               "                'l': _fat_l, 'm': -1})\n";
    } else {
        out << "    _fat_channels = [\n";
        for (const auto& p : c.fatbandProjections)
            out << "        " << fatbandChannelLiteral(p) << ",\n";
        out << "    ]\n"
               "    for _fat_ch in _fat_channels:\n"
               "        if not _fat_ch['atoms']:\n"
               "            _fat_ch['atoms'] = [\n"
               "                _i for _i, _s in enumerate(_fat_symbols)\n"
               "                if not _fat_ch['element']\n"
               "                or _s == _fat_ch['element']]\n";
    }
    out << "\n"
           "    _fat_out = []\n"
           "    for _fat_ch in _fat_channels:\n"
           "        _fat_total = None\n"
           "        for _fat_a in _fat_ch['atoms']:\n"
           "            if _fat_a < 0 or _fat_a >= len(_fat_symbols):\n"
           "                continue\n"
           "            _fat_ind = list(_fat_projectors(\n"
           "                _fat_calc.setups[_fat_a], _fat_ch['l']))\n"
           "            if not _fat_ind:\n"
           "                continue   # this species has no such shell\n"
           "            if _fat_ch['m'] >= 0:\n"
           "                # GPAW orders the 2l+1 partial waves of each\n"
           "                # projector set contiguously; stride past the\n"
           "                # radial repetitions to pick one m.\n"
           "                _fat_ind = _fat_ind[_fat_ch['m']::2 * _fat_ch['l'] + 1]\n"
           "                if not _fat_ind:\n"
           "                    continue\n"
           "            _fat_w = _fat_calc.wfs.pdos_weights(_fat_a, _fat_ind)\n"
           "            _fat_total = _fat_w if _fat_total is None \\\n"
           "                else _fat_total + _fat_w\n"
           "        if _fat_total is None:\n"
           "            print(f\"CALANGO_WARN no projectors for fatband \"\n"
           "                  f\"channel {_fat_ch['label']}\", flush=True)\n"
           "            continue\n"
           "        # pdos_weights is (nkpt, nband, nspin); the band energies\n"
           "        # are (nspin, nkpt, nband), so match them.\n"
           "        _fat_arr = _fat_np.asarray(_fat_total).transpose(2, 0, 1)\n"
           "        _fat_out.append({\n"
           "            'label': _fat_ch['label'],\n"
           "            'atoms': [int(_a) for _a in _fat_ch['atoms']],\n"
           "            'l': int(_fat_ch['l']),\n"
           "            'm': int(_fat_ch['m']),\n"
           "            'weights': [[[float(_v) for _v in _band]\n"
           "                         for _band in _spin] for _spin in _fat_arr],\n"
           "        })\n"
           "\n"
           "    if _fat_out:\n"
           "        fatbands = {'projections': _fat_out,\n"
           "                    'efermi': float(efermi),\n"
           "                    'max_weight': max(\n"
           "                        max(max(max(_b) for _b in _s)\n"
           "                            for _s in _p['weights'])\n"
           "                        for _p in _fat_out)}\n"
           "        with open('fatbands.json', 'w') as _handle:\n"
           "            json.dump(fatbands, _handle)\n"
           "        print('CALANGO_INFO fatband channels: '\n"
           "              + ', '.join(_p['label'] for _p in _fat_out),\n"
           "              flush=True)\n"
           "    else:\n"
           "        print('CALANGO_INFO no fatband projections were produced',\n"
           "              flush=True)\n"
           "except Exception as _fat_exc:\n"
           "    # Not fatal: the dispersion itself is complete without the\n"
           "    # weights, and the projection API is GPAW-specific.\n"
           "    print(f'CALANGO_WARN fatband projection failed: {_fat_exc}',\n"
           "          flush=True)\n";
    return out.str();
}

} // namespace

std::string generateElectronicScript(const ElectronicConfig& c)
{
    std::ostringstream out;
    out << "# Electronic band structure";
    if (c.pdos && c.backend == ElectronicBackend::Gpaw)
        out << " + PDOS";
    out << " — generated by Calango\n"
           "import json\n"
           "\n"
           "from ase.io import read\n"
           "\n"
        << AseScriptGenerator::jsonLoggerPreamble()
        << "atoms = read(\"structure.extxyz\")\n"
        << "npoints = " << c.npoints << "\n"
        << "path_str = " << (c.kpath.empty() ? "None" : "\"" + c.kpath + "\"")
        << "\n"
           "bandpath = atoms.cell.bandpath(path_str, npoints=npoints)\n"
           "print(f\"CALANGO_INFO k-path {bandpath.path}\", flush=True)\n"
           "_calango_progress(1, 4)\n"
           "\n"
           "pdos = None\n";

    switch (c.backend) {
    case ElectronicBackend::FreeElectrons:
        out << "# Empty-lattice (free-electron) reference bands — always\n"
               "# available, no electronic-structure package required.\n"
               "from ase.calculators.test import FreeElectrons\n"
               "\n"
            << "atoms.calc = FreeElectrons(nvalence=" << c.nvalence << ",\n"
               "                           kpts={\"path\": bandpath.path,\n"
               "                                 \"npoints\": npoints})\n"
               "atoms.get_potential_energy()\n"
               "_calango_progress(3, 4)\n"
               "bs = atoms.calc.band_structure()\n"
               "efermi = float(bs.reference)\n";
        break;

    case ElectronicBackend::Gpaw:
        out << AseScriptGenerator::gpawImports(c.gpaw) << "\n";
        if (!c.baselineDensityPath.empty()) {
            // Non-self-consistent (NSCF) run off a converged baseline density:
            // load the .gpw the Single-Point Calculation saved and evaluate the
            // band dispersion + PDOS at fixed charge density. No inline SCF.
            out << "# NSCF band structure from a fixed baseline charge density\n"
                   "# (produced by a prior Single-Point Calculation).\n"
                << "calc = GPAW(\"" << c.baselineDensityPath
                << "\", txt=\"gpaw_bands.txt\")\n"
                   "atoms = calc.get_atoms()\n"
                   "efermi = float(calc.get_fermi_level())\n"
                   "_calango_progress(2, 4)\n"
                   "\n"
                   "band_calc = calc.fixed_density(kpts=bandpath, "
                   "symmetry=\"off\",\n"
                   "                               txt=\"gpaw_bands.txt\")\n"
                   "bs = band_calc.band_structure()\n"
                   "_calango_progress(3, 4)\n";
        } else {
            // Legacy self-contained path: converge the SCF density inline, then
            // the NSCF band run reuses it via fixed_density.
            out << "calc = GPAW(\n"
                << AseScriptGenerator::gpawKeywordArguments(c.gpaw, "    ")
                << "    txt=\"gpaw_scf.txt\",\n"
                   ")\n"
                   "atoms.calc = calc\n"
                   "atoms.get_potential_energy()\n"
                   "efermi = float(calc.get_fermi_level())\n"
                   "_calango_progress(2, 4)\n"
                   "\n"
                   "band_calc = calc.fixed_density(kpts=bandpath, "
                   "symmetry=\"off\",\n"
                   "                               txt=\"gpaw_bands.txt\")\n"
                   "bs = band_calc.band_structure()\n"
                   "_calango_progress(3, 4)\n";
        }
        if (c.spinOrbit)
            // Non-perturbative SOC: the scalar-relativistic states along the
            // path are re-diagonalized in the spinor basis, so the result is
            // ONE spin channel of doubled, spin-mixed bands rather than the
            // two collinear channels. The Fermi level moves with them, which
            // is why it is re-read here rather than kept from the SCF.
            out << "\n"
                   "# --- Spin-orbit coupling -----------------------------------\n"
                   "# Re-diagonalizes the converged states in the spinor basis\n"
                   "# (gpaw.spinorbit). Lifts the degeneracies a scalar-\n"
                   "# relativistic run leaves: the Γ-point valence band of a\n"
                   "# III-V semiconductor, Rashba splitting, band inversion.\n"
                   "import numpy as _np\n"
                   "from ase.spectrum.band_structure import BandStructure\n"
                   "from gpaw.spinorbit import soc_eigenstates\n"
                   "\n"
                   "_soc = soc_eigenstates(band_calc)\n"
                   "_soc_energies = _np.asarray(_soc.eigenvalues())\n"
                   "efermi = float(_soc.fermi_level)\n"
                   "# (nkpt, nband) -> (1, nkpt, nband): spinor bands are a\n"
                   "# single channel, not a spin-up/spin-down pair.\n"
                   "bs = BandStructure(path=bs.path,\n"
                   "                   energies=_soc_energies[_np.newaxis],\n"
                   "                   reference=efermi)\n"
                   "print(f\"CALANGO_INFO spin-orbit coupling applied, \"\n"
                   "      f\"E_F = {efermi:.4f} eV\", flush=True)\n";
        if (c.pdos)
            out << "\n"
                   "# Element/orbital-projected DOS. The projection is re-sampled\n"
                   "# at a dedicated (typically denser) fixed-density k-mesh so\n"
                   "# the DOS is smooth independently of the SCF/band sampling.\n"
                << "pdos_npts = " << c.pdosPoints << "\n"
                << "pdos_width = " << c.pdosWidthEv << "\n"
                << "pdos_kpts = (" << c.pdosKpts[0] << ", " << c.pdosKpts[1]
                << ", " << c.pdosKpts[2] << ")\n"
                << "dos_calc = calc.fixed_density(kpts=pdos_kpts, symmetry=\"off\",\n"
                   "                              txt=\"gpaw_pdos.txt\")\n"
                   "\n"
                   "# Projection API. GPAW 26 made its NEW engine the default,\n"
                   "# and that engine dropped `get_orbital_ldos` — so on GPAW 26\n"
                   "# the old call raises AttributeError for every atom, every\n"
                   "# projection is skipped, and the run finishes with no PDOS\n"
                   "# and no error. gpaw.dos.DOSCalculator.raw_pdos has an\n"
                   "# identical signature in both engines and is the portable\n"
                   "# route; the legacy call is kept only for GPAW versions old\n"
                   "# enough to predate it.\n"
                   "#\n"
                   "# shift_fermi_level=False keeps the energies on the same\n"
                   "# absolute scale as `efermi` below, which is what the viewer\n"
                   "# plots the Fermi line against.\n"
                   "_dos = None\n"
                   "try:\n"
                   "    from gpaw.dos import DOSCalculator\n"
                   "\n"
                   "    _dos = DOSCalculator.from_calculator(dos_calc,\n"
                   "                                         shift_fermi_level=False)\n"
                   "except Exception as _e:\n"
                   "    print(f\"CALANGO_INFO DOSCalculator unavailable ({_e!r}); \"\n"
                   "          \"falling back to get_orbital_ldos\", flush=True)\n"
                   "\n"
                   "# --- RAW, unbroadened histogram --------------------------\n"
                   "# No Gaussian is applied here. What is written out is the\n"
                   "# projected weight falling in each narrow energy bin, and\n"
                   "# the viewer convolves that with whatever sigma the user\n"
                   "# dials. Two reasons, and neither is cosmetic:\n"
                   "#\n"
                   "#   * sigma is a PRESENTATION choice. Baking it in means a\n"
                   "#     peak that turns out to be a broadening artifact costs\n"
                   "#     another full SCF to re-examine.\n"
                   "#   * convolving a fine histogram with a Gaussian several\n"
                   "#     bins wide is numerically identical to broadening the\n"
                   "#     individual eigenvalues, and costs O(bins) instead of\n"
                   "#     O(kpoints x bands) — which is what makes the viewer's\n"
                   "#     slider redraw instantly rather than in seconds.\n"
                   "import numpy as _pdos_np\n"
                   "\n"
                   "_nspins = dos_calc.get_number_of_spins()\n"
                   "_nkpts = len(dos_calc.get_ibz_k_points())\n"
                   "_kweights = _pdos_np.asarray(dos_calc.get_k_point_weights())\n"
                   "_eig = [[_pdos_np.asarray(\n"
                   "             dos_calc.get_eigenvalues(kpt=_k, spin=_s))\n"
                   "         for _k in range(_nkpts)] for _s in range(_nspins)]\n"
                   "_all = _pdos_np.concatenate([_e for _s in _eig for _e in _s])\n"
                   "# Pad the window so the outermost states still have room for\n"
                   "# their tails once the viewer broadens them.\n"
                   "_lo = float(_all.min()) - 1.0\n"
                   "_hi = float(_all.max()) + 1.0\n"
                   "_nbins = max(2, int(pdos_npts))\n"
                   "_bin = (_hi - _lo) / (_nbins - 1)\n"
                   "pdos_energies = [_lo + _i * _bin for _i in range(_nbins)]\n"
                   "\n"
                   "projections = {}\n"
                   "for index, symbol in enumerate(atoms.get_chemical_symbols()):\n"
                   "    for angular_index, angular in enumerate(\"spdf\"):\n"
                   "        try:\n"
                   "            if _dos is not None:\n"
                   "                from gpaw.dos import get_projector_numbers\n"
                   "                _ind = list(get_projector_numbers(\n"
                   "                    _dos.setups[index], angular_index))\n"
                   "                if not _ind:\n"
                   "                    continue  # shell absent from this species\n"
                   "                # (nkpt, nband, nspin)\n"
                   "                _w = _pdos_np.asarray(\n"
                   "                    _dos.wfs.pdos_weights(index, _ind))\n"
                   "            else:\n"
                   "                # Pre-DOSCalculator GPAW: the legacy call can\n"
                   "                # only return a broadened curve, so ask it for\n"
                   "                # the narrowest one it will give and treat the\n"
                   "                # bins as already carrying that much width.\n"
                   "                _e, _d = dos_calc.get_orbital_ldos(\n"
                   "                    a=index, angular=angular, npts=_nbins,\n"
                   "                    width=max(_bin, 1e-3))\n"
                   "                _hist = _pdos_np.asarray(_d, dtype=float) * _bin\n"
                   "                _key = f\"{symbol} {angular}\"\n"
                   "                if _hist.max() > 1e-10:\n"
                   "                    projections[_key] = (\n"
                   "                        projections.get(_key, 0.0) + _hist)\n"
                   "                continue\n"
                   "        except Exception:\n"
                   "            continue\n"
                   "\n"
                   "        _hist = _pdos_np.zeros(_nbins)\n"
                   "        for _s in range(_nspins):\n"
                   "            for _k in range(_nkpts):\n"
                   "                _e = _eig[_s][_k]\n"
                   "                _idx = _pdos_np.clip(\n"
                   "                    ((_e - _lo) / _bin).astype(int), 0,\n"
                   "                    _nbins - 1)\n"
                   "                _hist += _pdos_np.bincount(\n"
                   "                    _idx, weights=_kweights[_k] * _w[_k, :, _s],\n"
                   "                    minlength=_nbins)\n"
                   "        if _hist.max() <= 1e-10:\n"
                   "            continue  # orbital not present on this species\n"
                   "        _key = f\"{symbol} {angular}\"\n"
                   "        projections[_key] = projections.get(_key, 0.0) + _hist\n"
                   "\n"
                   "if len(projections):\n"
                   "    pdos = {\"energies\": [float(v) for v in pdos_energies],\n"
                   "            \"efermi\": efermi,\n"
                   "            # The flag the viewer keys off. Its absence means\n"
                   "            # an older run whose curves are ALREADY broadened,\n"
                   "            # which must not be broadened a second time.\n"
                   "            \"broadened\": False,\n"
                   "            \"bin_width\": float(_bin),\n"
                   "            \"suggested_width\": float(pdos_width),\n"
                   "            \"projections\": {_k: [float(v) for v in _v]\n"
                   "                            for _k, _v in projections.items()}}\n"
                   "    print(f\"CALANGO_INFO pdos channels: \"\n"
                   "          f\"{sorted(projections)} \"\n"
                   "          f\"(raw, {_nbins} bins of {_bin:.4f} eV)\", flush=True)\n"
                   "else:\n"
                   "    # Loud, not silent: a missing PDOS used to look like a\n"
                   "    # successful run that simply had nothing to show.\n"
                   "    print(\"CALANGO_INFO no PDOS projections were produced\",\n"
                   "          flush=True)\n";
        // Both post-processes read the SCALAR-relativistic states held by
        // `band_calc`. With spin-orbit coupling on, `bs` no longer holds those
        // — it holds the spinor bands, in a different number, so a weight or a
        // character taken from `band_calc` would be attached to the wrong
        // band. Classifying spinor states needs the DOUBLE groups, which this
        // does not implement; saying so is better than labelling them wrongly.
        // Fatbands still cannot follow SOC: the orbital weights come from
        // `band_calc`'s SCALAR-relativistic states, and with SOC on `bs` holds
        // the spinor bands instead — a different set, in a different number, so
        // a weight would be attached to the wrong band. Band SYMMETRY can now:
        // it classifies the spinor states themselves, against the DOUBLE group.
        if (c.spinOrbit && c.fatbands) {
            out << "\n"
                   "print(\"CALANGO_WARN spin-orbit coupling is on, so the \"\n"
                   "      \"orbital projections were skipped: the spinor bands \"\n"
                   "      \"are a different set of states from the \"\n"
                   "      \"scalar-relativistic ones the projections \"\n"
                   "      \"describe.\", flush=True)\n";
        }
        if (c.bandSymmetry) {
            BandSymmetryConfig _symmetry = c.symmetry;
            // Which states are being classified decides the GROUP, so this is
            // not a preference: with SOC on, the single group cannot represent
            // them at all.
            _symmetry.spinorStates = c.spinOrbit;
            out << generateBandSymmetryBlock(_symmetry);
        }
        if (!c.spinOrbit && c.fatbands)
            out << fatbandBlock(c);
        break;

    case ElectronicBackend::Espresso:
        out << "# Quantum ESPRESSO workflow (pw.x must be installed).\n"
               "# EDIT ME: point pseudopotentials at your SSSP/pslibrary set.\n"
               "from ase.calculators.espresso import Espresso\n"
               "\n"
               "pseudopotentials = {s: f\"{s}.UPF\" for s in\n"
               "                    set(atoms.get_chemical_symbols())}\n"
            << "kgrid = " << c.scfKpts << "\n"
            << "scf = Espresso(pseudopotentials=pseudopotentials,\n"
               "               input_data={\"control\": {\"calculation\": \"scf\"},\n"
            << "                           \"system\": {\"ecutwfc\": "
            << c.ecutEv / 13.605693122994 << "}},\n"
               "               kpts=(kgrid, kgrid, kgrid))\n"
               "atoms.calc = scf\n"
               "atoms.get_potential_energy()\n"
               "efermi = float(atoms.calc.get_fermi_level())\n"
               "_calango_progress(2, 4)\n"
               "\n"
               "bands = Espresso(pseudopotentials=pseudopotentials,\n"
               "                 input_data={\"control\": {\"calculation\": \"bands\",\n"
               "                                         \"restart_mode\": \"restart\"},\n"
            << "                             \"system\": {\"ecutwfc\": "
            << c.ecutEv / 13.605693122994 << "}},\n"
               "                 kpts=bandpath)\n"
               "atoms.calc = bands\n"
               "atoms.get_potential_energy()\n"
               "bs = atoms.calc.band_structure()\n"
               "bs._reference = efermi\n"
               "_calango_progress(3, 4)\n";
        break;

    case ElectronicBackend::Siesta:
        out << "# SIESTA workflow (requires the siesta binary + SIESTA_PP_PATH).\n"
               "# EDIT ME: set the pseudopotential family / basis set.\n"
               "from ase.calculators.siesta import Siesta\n"
               "from ase.units import Ry\n"
               "\n"
            << "kgrid = " << c.scfKpts << "\n"
            << "scf = Siesta(xc=\"PBE\", mesh_cutoff=200 * Ry,\n"
               "             energy_shift=0.01 * Ry,\n"
               "             kpts=[kgrid, kgrid, kgrid])\n"
               "atoms.calc = scf\n"
               "atoms.get_potential_energy()\n"
               "efermi = float(atoms.calc.get_fermi_level())\n"
               "_calango_progress(2, 4)\n"
               "\n"
               "# Non-self-consistent band run along the path.\n"
               "bands = Siesta(xc=\"PBE\", mesh_cutoff=200 * Ry,\n"
               "               energy_shift=0.01 * Ry, kpts=bandpath)\n"
               "atoms.calc = bands\n"
               "atoms.get_potential_energy()\n"
               "bs = atoms.calc.band_structure()\n"
               "bs._reference = efermi\n"
               "_calango_progress(3, 4)\n";
        break;

    case ElectronicBackend::Vasp:
        out << "# VASP workflow (requires VASP_PP_PATH + ASE_VASP_COMMAND).\n"
               "from ase.calculators.vasp import Vasp\n"
               "import os\n"
               "import shutil\n"
               "\n"
            << "kgrid = " << c.scfKpts << "\n";
        if (!c.baselineDensityPath.empty()) {
            // The whole point of the baseline: no SCF here at all. VASP reads
            // the converged density from the CHGCAR of a prior single point
            // and diagonalizes along the band path at fixed density.
            //
            // The file has to be COPIED in rather than read from where it
            // lives: VASP has no path option for it — it opens 'CHGCAR' in the
            // working directory, full stop.
            out << "# Non-self-consistent bands on a converged density.\n"
                   "#\n"
                   "# ICHARG = 11 reads CHGCAR and never updates it, so no SCF\n"
                   "# runs here. VASP opens 'CHGCAR' in the working directory\n"
                   "# and takes no path for it, hence the copy.\n"
                << "_baseline = r\"" << c.baselineDensityPath
                << "\"\n"
                   "if not os.path.exists(_baseline):\n"
                   "    raise RuntimeError(\n"
                   "        f'The baseline charge density is gone: {_baseline}\\n'\n"
                   "        'ICHARG = 11 cannot converge one of its own — re-run "
                   "the '\n"
                   "        'Single-point Calculation that produced it.')\n"
                   "if os.path.abspath(_baseline) != os.path.abspath('CHGCAR'):\n"
                   "    shutil.copyfile(_baseline, 'CHGCAR')\n"
                   "print(f'CALANGO_INFO reusing the charge density from "
                   "{_baseline}',\n"
                   "      flush=True)\n"
                   "_calango_progress(2, 4)\n"
                   "\n"
                << "bands = Vasp(xc=\"PBE\", encut=" << c.ecutEv
                << ", icharg=11,\n"
                   "             ismear=0, sigma=0.05,\n"
                   "             directory=\".\", kpts=bandpath)\n"
                   "atoms.calc = bands\n"
                   "atoms.get_potential_energy()\n"
                   "efermi = float(atoms.calc.get_fermi_level())\n"
                   "bs = atoms.calc.band_structure()\n"
                   "bs._reference = efermi\n"
                   "_calango_progress(3, 4)\n";
        } else {
            out << "# No baseline was selected, so the SCF runs here first and\n"
                   "# the band pass reuses ITS density (ICHARG = 11).\n"
                << "scf = Vasp(xc=\"PBE\", encut=" << c.ecutEv << ",\n"
                   "           kpts=(kgrid, kgrid, kgrid), ismear=0, sigma=0.05,\n"
                   "           lcharg=True, directory=\".\")\n"
                   "atoms.calc = scf\n"
                   "atoms.get_potential_energy()\n"
                   "efermi = float(atoms.calc.get_fermi_level())\n"
                   "_calango_progress(2, 4)\n"
                   "\n"
                   "# Non-self-consistent band run on the density just written.\n"
                << "bands = Vasp(xc=\"PBE\", encut=" << c.ecutEv
                << ", icharg=11,\n"
                   "             ismear=0, sigma=0.05,\n"
                   "             directory=\".\", kpts=bandpath)\n"
                   "atoms.calc = bands\n"
                   "atoms.get_potential_energy()\n"
                   "bs = atoms.calc.band_structure()\n"
                   "bs._reference = efermi\n"
                   "_calango_progress(3, 4)\n";
        }
        break;
    }

    out << "\n"
           "x, special_x, special_labels = bs.path.get_linear_kpoint_axis()\n"
           "data = {\n"
           "    \"x\": list(map(float, x)),\n"
           "    \"special_x\": list(map(float, special_x)),\n"
           "    \"special_labels\": list(special_labels),\n"
           "    \"efermi\": float(efermi),\n"
           "    # energies[spin][kpoint][band] in eV\n"
           "    \"energies\": [[list(map(float, kpt)) for kpt in spin]\n"
           "                 for spin in bs.energies],\n"
           "}\n"
           "with open(\"bands.json\", \"w\") as handle:\n"
           "    json.dump(data, handle)\n"
           "if pdos is not None:\n"
           "    with open(\"pdos.json\", \"w\") as handle:\n"
           "        json.dump(pdos, handle)\n"
           "_calango_progress(4, 4)\n"
           "print(f\"CALANGO_RESULT bands={len(data['energies'][0][0])} \"\n"
           "      f\"kpts={len(data['x'])} efermi_eV={efermi:.4f}\", flush=True)\n";
    return out.str();
}

} // namespace calango::core
