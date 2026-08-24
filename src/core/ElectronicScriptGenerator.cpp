#include "core/ElectronicScriptGenerator.hpp"

#include "core/AseScriptGenerator.hpp"
#include "core/BandSymmetryScriptGenerator.hpp"

#include <sstream>

namespace calango::core {

namespace {

/// `calculator` with its POTCAR directory guaranteed present -- see the
/// identical helper in TwoDBandsScriptGenerator.cpp. ElectronicConfig
/// mirrors the path in a field beside its CalculatorConfig, and the resolver
/// needs the directory AND the `xc` that picks the family inside it.
CalculatorConfig potcarConfig(CalculatorConfig calculator,
                              const std::string& mirroredPath)
{
    if (calculator.vaspPotcarPath.empty())
        calculator.vaspPotcarPath = mirroredPath;
    return calculator;
}

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

/// The vasprun.xml projected-DOS parser, as Python.
///
/// Shared by the two VASP routes that produce a PDOS, which reach it very
/// differently: the semilocal one runs a SEPARATE fixed-density pass
/// (ICHARG = 11) with LORBIT = 11 and parses what that writes, while the
/// hybrid one cannot — ICHARG = 11 is invalid for a hybrid — and instead
/// sets LORBIT = 11 on the single self-consistent run it already performs.
/// Same file, same block, same schema either way; only the run that produced
/// it differs, so the parser is written once rather than twice.
std::string vaspPdosParserBlock()
{
    std::ostringstream out;
    out <<
        "def _calango_shell(_field):\n"
        "    \"\"\"The angular-momentum shell a VASP partial-DOS field "
        "belongs to.\n"
        "\n"
        "    Almost every lm field name starts with its shell letter (py/pz/"
        "px,\n"
        "    dxy/dyz/dz2/dxz, fy3x2/.../fx3), and the non-lm LORBIT = 10 "
        "fields\n"
        "    are the bare letters. The exception is d(x2-y2), which VASP "
        "writes\n"
        "    as \"x2-y2\" with no leading d.\n"
        "    \"\"\"\n"
        "    if _field.startswith((\"x2\", \"dx2\")):\n"
        "        return \"d\"\n"
        "    return _field[0]\n"
        "\n"
        "try:\n"
        "    import xml.etree.ElementTree as _ET\n"
        "\n"
        "    _partial = _ET.parse(\"vasprun.xml\").getroot().find(\n"
        "        \".//dos/partial/array\")\n"
        "    if _partial is None:\n"
        "        raise RuntimeError(\n"
        "            \"vasprun.xml has no <dos><partial> block \"\n"
        "            \"(LORBIT did not take effect?)\")\n"
        "    _fields = [_f.text.strip()\n"
        "               for _f in _partial.findall(\"field\")]\n"
        "    _symbols = atoms.get_chemical_symbols()\n"
        "    _pdos_energies = None\n"
        "    _projections = {}\n"
        "    for _ion, _ion_set in enumerate(\n"
        "            _partial.find(\"set\").findall(\"set\")):\n"
        "        _symbol = (_symbols[_ion] if _ion < len(_symbols)\n"
        "                   else \"?\")\n"
        "        for _spin_set in _ion_set.findall(\"set\"):\n"
        "            _rows = [[float(_v) for _v in _r.text.split()]\n"
        "                     for _r in _spin_set.findall(\"r\")]\n"
        "            if _pdos_energies is None:\n"
        "                _pdos_energies = [_row[0] for _row in _rows]\n"
        "            for _col, _field in enumerate(_fields):\n"
        "                if _field == \"energy\":\n"
        "                    continue\n"
        "                # Summed over m and spin, matching the\n"
        "                # GPAW branch's per-element-per-shell\n"
        "                # aggregation. NOT _field[0]: VASP writes\n"
        "                # d(x2-y2) as bare \"x2-y2\", with no\n"
        "                # leading d, so the first letter puts a\n"
        "                # fifth of the d weight into a channel\n"
        "                # called \"x\" and leaves \"d\" short by\n"
        "                # exactly that much.\n"
        "                _key = f\"{_symbol} {_calango_shell(_field)}\"\n"
        "                _curve = _projections.setdefault(\n"
        "                    _key, [0.0] * len(_rows))\n"
        "                for _i, _row in enumerate(_rows):\n"
        "                    _curve[_i] += _row[_col]\n"
        "    if _pdos_energies and _projections:\n"
        "        _n = len(_pdos_energies)\n"
        "        _bin = ((_pdos_energies[-1] - _pdos_energies[0])\n"
        "                / (_n - 1)) if _n > 1 else 0.0\n"
        "        pdos = {\n"
        "            \"energies\": [float(_v) for _v in _pdos_energies],\n"
        "            \"efermi\": float(efermi),\n"
        "            # Already a finished curve on VASP's own\n"
        "            # ISMEAR/SIGMA-smeared grid, not a raw\n"
        "            # histogram Calango can re-bin -- so no sigma\n"
        "            # slider applies (BandPdosWindow.cpp reads\n"
        "            # \"broadened\", not the literal string\n"
        "            # \"vasp\", to decide that).\n"
        "            \"broadened\": True,\n"
        "            \"integration\": \"vasp\",\n"
        "            \"bin_width\": float(_bin),\n"
        "            \"projections\": {\n"
        "                _k: [float(_v) for _v in _v]\n"
        "                for _k, _v in _projections.items()},\n"
        "        }\n"
        "        print(f\"CALANGO_INFO pdos channels: \"\n"
        "              f\"{sorted(_projections)} ({_n} points, \"\n"
        "              f\"VASP's own DOSCAR/vasprun.xml grid)\",\n"
        "              flush=True)\n"
        "    else:\n"
        "        print(\"CALANGO_INFO no PDOS projections were "
        "produced\",\n"
        "              flush=True)\n"
        "except Exception as _e:\n"
        "    print(f\"CALANGO_WARN could not parse the projected "
        "DOS from \"\n"
        "          f\"vasprun.xml ({_e!r})\", flush=True)\n";
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
            //
            // MainWindow::stageJob() copies the baseline into this job's own
            // directory as "baseline.gpw" (for a remote run, whose only
            // filesystem is what got uploaded); prefer that staged copy over
            // the absolute path baked in at generation time, which resolves
            // only on the machine the script was generated on.
            out << "import os\n"
                   "# NSCF band structure from a fixed baseline charge density\n"
                   "# (produced by a prior Single-Point Calculation).\n"
                << "_baseline = (\"baseline.gpw\" if os.path.exists(\"baseline.gpw\")\n"
                << "             else r\"" << c.baselineDensityPath << "\")\n"
                   "calc = GPAW(_baseline, txt=\"gpaw_bands.txt\")\n"
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
                // Named rather than inlined into the branch: it is the one
                // setting that changes what the numbers in pdos.json MEAN, so
                // it should be visible at the top of the script review.
                << "pdos_tetrahedron = "
                << (c.dosIntegration == DosIntegration::Tetrahedron ? "True"
                                                                    : "False")
                << "  # linear tetrahedron interpolation (Bloechl)\n"
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
                   "# Which Brillouin-zone integration produced the numbers\n"
                   "# below. Written into pdos.json so the viewer knows whether\n"
                   "# it is holding a histogram it may broaden or a finished\n"
                   "# density of states it must not touch.\n"
                   "pdos_integration = \"sampling\"\n"
                   "if pdos_tetrahedron:\n"
                   "    if _dos is None:\n"
                   "        print(\"CALANGO_WARN tetrahedron integration needs \"\n"
                   "              \"gpaw.dos.DOSCalculator, which this GPAW does \"\n"
                   "              \"not provide; falling back to the sampled \"\n"
                   "              \"histogram\", flush=True)\n"
                   "    else:\n"
                   "        # Linear tetrahedron interpolation: the bands are\n"
                   "        # interpolated inside tetrahedra filling the BZ and\n"
                   "        # the DOS integrated analytically. width=0.0 is what\n"
                   "        # selects it (gpaw/dos.py: DOSCalculator.calculate).\n"
                   "        # The result is states/eV already — NOT a histogram —\n"
                   "        # so nothing downstream may broaden it again.\n"
                   "        try:\n"
                   "            for index, symbol in enumerate(\n"
                   "                    atoms.get_chemical_symbols()):\n"
                   "                for angular_index, angular in enumerate(\"spdf\"):\n"
                   "                    try:\n"
                   "                        _curve = _pdos_np.asarray(\n"
                   "                            _dos.raw_pdos(pdos_energies,\n"
                   "                                          a=index,\n"
                   "                                          l=angular_index,\n"
                   "                                          width=0.0))\n"
                   "                    except Exception:\n"
                   "                        continue  # shell absent on this species\n"
                   "                    if not _pdos_np.isfinite(_curve).all():\n"
                   "                        continue\n"
                   "                    if _curve.max() <= 1e-10:\n"
                   "                        continue\n"
                   "                    _key = f\"{symbol} {angular}\"\n"
                   "                    projections[_key] = (\n"
                   "                        projections.get(_key, 0.0) + _curve)\n"
                   "            if projections:\n"
                   "                pdos_integration = \"tetrahedron\"\n"
                   "            else:\n"
                   "                print(\"CALANGO_WARN tetrahedron integration \"\n"
                   "                      \"produced no channels; falling back to \"\n"
                   "                      \"the sampled histogram\", flush=True)\n"
                   "        except Exception as _e:\n"
                   "            # A Gamma-only or otherwise unusable mesh: the\n"
                   "            # interpolation has no tetrahedra to work with.\n"
                   "            # Said out loud, because a DOS that quietly\n"
                   "            # changed method is a DOS nobody can compare.\n"
                   "            projections = {}\n"
                   "            print(f\"CALANGO_WARN tetrahedron integration \"\n"
                   "                  f\"failed ({_e!r}); falling back to the \"\n"
                   "                  f\"sampled histogram. It needs a genuine \"\n"
                   "                  f\"Monkhorst-Pack mesh.\", flush=True)\n"
                   "\n"
                   "for index, symbol in enumerate(atoms.get_chemical_symbols()):\n"
                   "    if pdos_integration == \"tetrahedron\":\n"
                   "        break\n"
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
                   "            # which must not be broadened a second time. A\n"
                   "            # tetrahedron curve is finished in the same\n"
                   "            # sense: it is states/eV, not counts per bin.\n"
                   "            \"broadened\": pdos_integration == \"tetrahedron\",\n"
                   "            \"integration\": pdos_integration,\n"
                   "            \"bin_width\": float(_bin),\n"
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
               // A "bands" calculation runs pw.x and populates every
               // eigenvalue, but never reports a total energy — verified by
               // actually running this against real pw.x: QE has nothing
               // self-consistent left to converge, so get_potential_energy()
               // raises PropertyNotImplementedError even though the run
               // itself succeeded. It is still what TRIGGERS pw.x, so it is
               // called for that reason alone, with the one exception it is
               // known to raise afterward caught by name — not masked with
               // a bare except, which would also swallow a genuine failure.
               "from ase.calculators.calculator import "
               "PropertyNotImplementedError as _pni\n"
               "try:\n"
               "    atoms.get_potential_energy()\n"
               "except _pni:\n"
               "    pass\n"
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
               "#\n"
               "# `bandpath=`, not `kpts=` — ASE's Siesta.kpts is the SCF\n"
               "# Monkhorst-Pack grid dimensions ONLY (it indexes into\n"
               "# whatever is passed as kpts[0..2]); an explicit k-point set,\n"
               "# band path or otherwise, is a SEPARATE keyword that writes\n"
               "# %block BandPoints from path.kpts. Passing the BandPath as\n"
               "# kpts= crashes at write-input time (TypeError: len() of\n"
               "# unsized object) rather than at the queue, since Siesta's\n"
               "# kpts writer expects to index it like a 3-tuple.\n"
               "bands = Siesta(xc=\"PBE\", mesh_cutoff=200 * Ry,\n"
               "               energy_shift=0.01 * Ry, bandpath=bandpath)\n"
               "atoms.calc = bands\n"
               "atoms.get_potential_energy()\n"
               "bs = atoms.calc.band_structure()\n"
               "bs._reference = efermi\n"
               "_calango_progress(3, 4)\n";
        break;

    case ElectronicBackend::Vasp:
        // No explicit ispin= anywhere below, on either the fresh-SCF or the
        // baseline-reuse Vasp() call — verified against ASE's own
        // create_input.py (set_magmom()): it auto-sets ISPIN = 2 whenever
        // `atoms` carries nonzero initial magnetic moments and the caller did
        // not pass ispin itself. `atoms` here is `read("structure.extxyz")`
        // just below, unconditionally, so a spin-polarized structure stays
        // spin-polarized through both the SCF and the NSCF band pass without
        // Calango having to track or re-assert it — the same outcome GPAW
        // gets for free from a full .gpw restart, reached here by a different
        // route because CHGCAR alone carries no calculator state to restart.
        out << "# VASP workflow (requires VASP_PP_PATH + ASE_VASP_COMMAND).\n"
               "from ase.calculators.vasp import Vasp\n"
               "import os\n"
               "import shutil\n"
               "\n"
            << AseScriptGenerator::vaspPotcarResolutionSnippet(
                   potcarConfig(c.gpaw, c.vaspPotcarPath))
            << "kgrid = " << c.scfKpts << "\n";
        // PREC sets VASP's FFT grid density for a given ENCUT; every
        // Vasp() call below that reads a CHGCAR via ICHARG=11 (the
        // baseline-restart bands pass, the self-contained pair, and the
        // PDOS pass further down) MUST use the SAME prec as whichever run
        // wrote that CHGCAR, or VASP refuses it outright ("dimensions on
        // CHGCAR file are different") — the second bug found alongside
        // proc_4's POTCAR failure (Task 3, 2026-08-22). One shared value
        // for the whole case block: a baseline's CHGCAR always comes from
        // emitVasp() (AseScriptGenerator.cpp), which always sets this
        // explicitly, and the self-contained pair/PDOS pass share a CHGCAR
        // written earlier in this SAME script — so every restart in this
        // case block needs to agree with the ONE value the user configured.
        const std::string vaspPrec =
            AseScriptGenerator::vaspPrecString(c.gpaw.vaspPrec);
        // -- Hybrid functionals: KPOINTS_OPT, not ICHARG = 11 ---------------
        //
        // ICHARG = 11 is INVALID for a hybrid, and the VASP wiki says so in
        // as many words: "For hybrid functionals, the Hamiltonian cannot be
        // expressed in terms of the electronic charge density alone. […] The
        // electronic charge density must not be fixed for any hybrid
        // calculation, i.e., never set ICHARG=11!" (Band-structure
        // calculation using hybrid functionals). A hybrid needs the ORBITALS
        // on a regular mesh, not just the density, so there is no
        // fixed-density band pass to run.
        //
        // The documented route instead: ONE self-consistent hybrid run on a
        // uniform mesh, carrying the band path in a KPOINTS_OPT file —
        // "an optional input file to perform an additional one-shot
        // calculation after self-consistency is reached", read automatically
        // when present, available as of VASP 6.3.0 (wiki: KPOINTS_OPT).
        //
        // WHAT WAS VERIFIED BY RUNNING VASP 6.6.1, because the wiki does not
        // say it: the results land in vasprun.xml ONLY, under
        // <eigenvalues_kpoints_opt>. No EIGENVAL_OPT or DOSCAR_OPT text file
        // is written, and ASE cannot read that element at all — hence the
        // subprocess call and the parser below rather than
        // calc.band_structure().
        if (isHybrid(c.gpaw.vaspFunctional)) {
            out << "import subprocess\n"
                   "import xml.etree.ElementTree as _ET\n"
                   "import numpy as np\n"
                   "from ase.spectrum.band_structure import BandStructure\n"
                   "\n";

            // Wiki step 1: "Run a DFT SCF calculation to obtain a converged
            // WAVECAR file", then "restart from DFT WAVECAR". Optional, and
            // it is the ORBITALS that matter here, not the density.
            if (!c.baselineWavecarPath.empty()) {
                out << "# The wiki's step 1: restart from a converged "
                       "semilocal WAVECAR.\n"
                       "# A hybrid started from random orbitals converges "
                       "poorly and can\n"
                       "# land in a different local minimum; the ORBITALS "
                       "are what carry\n"
                       "# over, which is why this is a WAVECAR and not the "
                       "CHGCAR every\n"
                       "# other branch here stages.\n"
                    << "_wavecar = (\"baseline.WAVECAR\"\n"
                       "            if os.path.exists(\"baseline.WAVECAR\")\n"
                    << "            else r\"" << c.baselineWavecarPath
                    << "\")\n"
                       "if not os.path.exists(_wavecar):\n"
                       "    raise RuntimeError(\n"
                       "        'The baseline wavefunctions are gone: ' + "
                       "_wavecar + '\\n'\n"
                       "        'Re-run the semilocal Single-point "
                       "Calculation with '\n"
                       "        'LWAVE = .TRUE., or clear the baseline to let "
                       "this hybrid '\n"
                       "        'converge from scratch.')\n"
                       "if os.path.getsize(_wavecar) < 4096:\n"
                       "    raise RuntimeError(\n"
                       "        'The baseline WAVECAR is only %d bytes — it "
                       "holds no '\n"
                       "        'orbitals. VASP creates the file at startup "
                       "and fills it at '\n"
                       "        'the end, so a run that died leaves one "
                       "behind that exists '\n"
                       "        'and is empty.' % os.path.getsize(_wavecar))\n"
                       "if os.path.abspath(_wavecar) != "
                       "os.path.abspath('WAVECAR'):\n"
                       "    shutil.copyfile(_wavecar, 'WAVECAR')\n"
                       "print('CALANGO_INFO restarting the hybrid from ' + "
                       "_wavecar, flush=True)\n"
                       "\n";
            } else {
                out << "# No semilocal baseline was selected. The wiki "
                       "recommends starting a\n"
                       "# hybrid from a converged DFT WAVECAR "
                       "(https://vasp.at/wiki/NiO_HSE06);\n"
                       "# from scratch this converges more slowly and can "
                       "land elsewhere.\n"
                       "print('CALANGO_WARN this hybrid starts from scratch "
                       "— VASP recommends '\n"
                       "      'restarting from a converged semilocal WAVECAR "
                       "(select a '\n"
                       "      'Single-point baseline that ran with LWAVE = "
                       ".TRUE.)', flush=True)\n"
                       "\n";
            }

            out << "# ONE self-consistent hybrid run. KPOINTS holds the "
                   "UNIFORM mesh —\n"
                   "# the wiki is explicit that \"the KPOINTS file must "
                   "contain a uniform\n"
                   "# k mesh, when the KPOINTS_OPT file should be used "
                   "afterward\" — and\n"
                   "# the band path travels separately, below.\n"
                << "hybrid = Vasp(xc=\"PBE\", encut=" << c.ecutEv << ",\n"
                   "              kpts=(kgrid, kgrid, kgrid), gamma=True,\n"
                   "              ismear=0, sigma=0.05,\n"
                << "              prec=\"" << vaspPrec << "\",\n"
                << AseScriptGenerator::vaspHybridKeywords(c.gpaw,
                                                          "              ")
                // https://vasp.at/wiki/ALGO: the direct optimizers are the
                // supported ones for a hybrid; Normal/VeryFast are not.
                << "              algo=\"All\", time=0.4, precfock=\"Fast\",\n"
                   // https://vasp.at/wiki/HFRCUT, and the band-structure page
                   // recommends it specifically: "By default VASP uses
                   // auxiliary functions (HFALPHA) for the truncation of the
                   // Coulomb singularity, but this method leads to
                   // discontinuities in band-structure calculations. We
                   // recommend using the Coulomb truncation (HFRCUT)
                   // instead. In particular, HFRCUT=-1 converges best for
                   // systems with a band gap."
                   "              # HFRCUT = -1: Coulomb truncation rather "
                   "than VASP's default\n"
                   "              # auxiliary functions, which \"leads to "
                   "discontinuities in\n"
                   "              # band-structure calculations\" "
                   "(https://vasp.at/wiki/HFRCUT and\n"
                   "              # the hybrid band-structure page). Note it "
                   "converges best for\n"
                   "              # GAPPED systems; HFRCUT = 0 is the faster "
                   "choice for a metal.\n"
                   "              hfrcut=-1,\n"
                << AseScriptGenerator::vaspHubbardKeywords(c.gpaw,
                                                           "              ");
            if (c.pdos) {
                // LORBIT = 11 on the SCF run itself, not on a second pass.
                // The semilocal route runs a separate fixed-density job for
                // this; a hybrid cannot (ICHARG = 11 is invalid for one) and
                // does not need to -- it is already converging on a uniform
                // mesh, which is exactly what a DOS wants, and a hybrid SCF
                // is far too expensive to run twice.
                //
                // The consequence is worth knowing and is documented: the
                // PDOS mesh is the SCF mesh, so the wizard's own
                // "PDOS k-points" setting does not apply on this route.
                out << "              # LORBIT = 11: lm-decomposed PROCAR + "
                       "DOSCAR, on the SCF\n"
                       "              # mesh this run is already converging "
                       "on. The semilocal route\n"
                       "              # runs a second ICHARG = 11 pass for "
                       "this; a hybrid cannot,\n"
                       "              # and a second hybrid SCF would cost as "
                       "much as the first.\n"
                       "              lorbit=11,\n";
            }
            out << "              directory=\".\")\n"
                   "hybrid.write_input(atoms)\n"
                   "\n";

            out << "# The band path, as an EXPLICIT k-point list rather than "
                   "line mode.\n"
                   "#\n"
                   "# Line mode would make VASP interpolate its own points, "
                   "and the linear\n"
                   "# x-axis and special-point positions the viewer draws "
                   "would then have\n"
                   "# to be re-derived from whatever it chose. An explicit "
                   "list comes back\n"
                   "# EXACTLY as given — same count, same order, no symmetry "
                   "folding\n"
                   "# (verified against VASP 6.6.1) — so `bandpath` stays the "
                   "single\n"
                   "# source of truth for the geometry of the plot.\n"
                   "#\n"
                   "# The trailing weight is not load-bearing: KPOINTS_OPT "
                   "points never\n"
                   "# enter the SCF (the whole file is a one-shot AFTER "
                   "self-consistency),\n"
                   "# and VASP renormalizes them anyway.\n"
                   "with open('KPOINTS_OPT', 'w') as _fh:\n"
                   "    _fh.write('Calango band path (explicit list)\\n')\n"
                   "    _fh.write('%d\\n' % len(bandpath.kpts))\n"
                   "    _fh.write('Reciprocal\\n')\n"
                   "    for _k in bandpath.kpts:\n"
                   "        _fh.write('  %.10f  %.10f  %.10f  1\\n' % "
                   "tuple(_k))\n"
                   "print('CALANGO_INFO KPOINTS_OPT: %d band-path k-points' % "
                   "len(bandpath.kpts),\n"
                   "      flush=True)\n"
                   "_calango_progress(2, 4)\n"
                   "\n";

            out << "# ASE's own run() would parse OUTCAR afterwards and knows "
                   "nothing\n"
                   "# about <eigenvalues_kpoints_opt>, so the binary is "
                   "invoked directly\n"
                   "# and the result read below.\n"
                   "_command = (os.environ.get('ASE_VASP_COMMAND')\n"
                   "            or os.environ.get('VASP_COMMAND')\n"
                   "            or 'vasp_std')\n"
                   "print('CALANGO_INFO running: ' + _command, flush=True)\n"
                   "_proc = subprocess.run(_command, shell=True, "
                   "capture_output=True,\n"
                   "                       text=True)\n"
                   "with open('vasp.out', 'w') as _fh:\n"
                   "    _fh.write(_proc.stdout)\n"
                   "if _proc.stderr:\n"
                   "    with open('vasp.err', 'w') as _fh:\n"
                   "        _fh.write(_proc.stderr)\n"
                   "# VASP spells its warning banner with spaces between the "
                   "letters, so a\n"
                   "# plain grep for 'warning' finds nothing in a run that "
                   "emitted several.\n"
                   "for _line in _proc.stdout.splitlines():\n"
                   "    if 'W A R N I N G' in _line or 'internal error' in "
                   "_line.lower():\n"
                   "        print('CALANGO_WARN VASP: ' + _line.strip(), "
                   "flush=True)\n"
                   "if _proc.returncode != 0:\n"
                   "    raise RuntimeError(\n"
                   "        'VASP exited %d during the hybrid run. The last "
                   "lines of its '\n"
                   "        'output:\\n%s' % (_proc.returncode,\n"
                   "                          '\\n'.join("
                   "_proc.stdout.splitlines()[-20:]) or '(none)'))\n"
                   "_calango_progress(3, 4)\n"
                   "\n";

            out << R"PY(# --- Read the one-shot eigenvalues back ------------------------------
# vasprun.xml is the ONLY place they land: VASP 6.6.1 writes no
# EIGENVAL_OPT / DOSCAR_OPT text file (verified by running it), and ASE has
# no reader for this element.
_root = _ET.parse('vasprun.xml').getroot()
_node = _root.find('.//eigenvalues_kpoints_opt')
if _node is None:
    raise RuntimeError(
        'VASP finished but wrote no <eigenvalues_kpoints_opt> block to '
        'vasprun.xml, so the band path was never evaluated. KPOINTS_OPT is '
        'available as of VASP 6.3.0 - an older binary ignores the file '
        'silently. Check the VASP version in OUTCAR\'s first line, and that '
        'LKPOINTS_OPT was not set to .FALSE.')

_got = np.array([[float(_v) for _v in _e.text.split()]
                 for _e in _node.find("kpoints/varray[@name='kpointlist']")])
_want = np.asarray(bandpath.kpts, dtype=float)
# An explicit list comes back untouched. If it ever does not - a future VASP
# folding the optional list by symmetry, say - the x-axis below would silently
# describe different k-points than the energies beside it, so this is a hard
# error rather than a warning.
if _got.shape != _want.shape or not np.allclose(_got, _want, atol=1e-6):
    raise RuntimeError(
        'VASP returned %d k-points for the %d-point band path, or returned '
        'them in a different order. The linear axis and the energies would '
        'no longer describe the same path. Re-run with ISYM = 0.'
        % (len(_got), len(_want)))

# <set comment="spin N"> / <set comment="kpoint M"> / <r>eigenvalue</r>
_energies = np.array(
    [[[float(_r.text.split()[0]) for _r in _kset] for _kset in _sset]
     for _sset in _node.find('eigenvalues/array/set')], dtype=float)

# The Fermi level of the SELF-CONSISTENT run, which is the meaningful zero:
# the <dos> element with no comment. VASP also stamps one onto the
# kpoints_opt block and the two agreed exactly in every run checked, so
# either is usable and the SCF one is preferred on principle.
efermi = None
for _dos in _root.iter('dos'):
    _item = _dos.find("i[@name='efermi']")
    if _item is None:
        continue
    if _dos.get('comment') is None:
        efermi = float(_item.text)
        break
    if efermi is None:
        efermi = float(_item.text)
if efermi is None:
    raise RuntimeError('vasprun.xml records no Fermi level.')
efermi = float(efermi)

bs = BandStructure(path=bandpath, energies=_energies, reference=efermi)
print('CALANGO_INFO hybrid bands: %d spin(s) x %d k-points x %d bands'
      % _energies.shape, flush=True)
)PY";
            if (c.pdos) {
                out << "\n"
                       "# The projected DOS from the SAME vasprun.xml — "
                       "LORBIT = 11 was set\n"
                       "# on the hybrid run above, so this is the hybrid's "
                       "own projection on\n"
                       "# its SCF mesh (the wizard's PDOS k-point setting "
                       "does not apply\n"
                       "# here; there is no second pass to give it to).\n"
                    << vaspPdosParserBlock();
            }
            break;
        }

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
                   "#\n"
                   "# MainWindow::stageJob() copies the baseline into this\n"
                   "# job's own directory as \"baseline.CHGCAR\" (for a remote\n"
                   "# run, whose only filesystem is what got uploaded);\n"
                   "# prefer that staged copy over the absolute path baked in\n"
                   "# at generation time, which resolves only on the machine\n"
                   "# the script was generated on.\n"
                << "_baseline = (\"baseline.CHGCAR\" if os.path.exists("
                   "\"baseline.CHGCAR\")\n"
                << "             else r\"" << c.baselineDensityPath << "\")\n"
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
                   // PREC sets VASP's FFT grid density for a given ENCUT;
                   // an ICHARG=11 restart reading a CHGCAR written under a
                   // DIFFERENT prec gets a mismatched grid and VASP refuses
                   // it outright ("dimensions on CHGCAR file are
                   // different"). The baseline always comes from
                   // emitVasp() (AseScriptGenerator.cpp), which always sets
                   // this explicitly — matching it here, rather than
                   // leaving VASP's own PREC=Normal default in place, is
                   // what makes a CHGCAR baseline usable at all (Task 3,
                   // 2026-08-22 — the second bug found alongside proc_4's
                   // POTCAR failure).
                   "             prec=\""
                << AseScriptGenerator::vaspPrecString(c.gpaw.vaspPrec)
                << "\",\n"
                   // bandpath.kpts, not bandpath itself, and reciprocal=True:
                   // ASE's Vasp writer needs a plain array (a BandPath object
                   // has no __format__, so passing it raw crashes at
                   // write-input time with "unsupported format string passed
                   // to BandPath.__format__"), and without reciprocal=True
                   // the fractional coordinates it DOES accept would be
                   // written as if they were already Cartesian 1/A.
                << AseScriptGenerator::vaspHubbardKeywords(c.gpaw,
                                                           "             ")
                << "             directory=\".\", kpts=bandpath.kpts,\n"
                   "             reciprocal=True)\n"
                   "atoms.calc = bands\n"
                   "atoms.get_potential_energy()\n"
                   // The Fermi level of a run whose k-points are a 1D PATH
                   // (not a zone-filling mesh) is not a meaningful occupation
                   // count -- the VASP wiki's own ICHARG = 11 page recommends
                   // the prior self-consistent run's E_F instead. That run's
                   // single_point.json (AseScriptGenerator.cpp's single-point
                   // summary) sits beside the CHGCAR it wrote; prefer its
                   // "fermi_eV" and fall back to this pass's own (potentially
                   // unreliable) get_fermi_level() only when that sidecar is
                   // missing or predates this field (an older run, or a
                   // CHGCAR sourced from outside Calango).
                   "efermi = None\n"
                   "try:\n"
                   "    with open(os.path.join(os.path.dirname(_baseline) or "
                   "'.', 'single_point.json')) as _fh:\n"
                   "        efermi = json.load(_fh).get('fermi_eV')\n"
                   "except Exception:\n"
                   "    pass\n"
                   "if efermi is None:\n"
                   "    efermi = float(atoms.calc.get_fermi_level())\n"
                   "    print('CALANGO_INFO no single_point.json fermi_eV "
                   "beside the baseline; using this NSCF pass\\'s own "
                   "(less reliable) Fermi level', flush=True)\n"
                   "else:\n"
                   "    efermi = float(efermi)\n"
                   "bs = atoms.calc.band_structure()\n"
                   "bs._reference = efermi\n"
                   "_calango_progress(3, 4)\n";
        } else {
            out << "# No baseline was selected, so the SCF runs here first and\n"
                   "# the band pass reuses ITS density (ICHARG = 11).\n"
                << "scf = Vasp(xc=\"PBE\", encut=" << c.ecutEv << ",\n"
                   "           kpts=(kgrid, kgrid, kgrid), ismear=0, sigma=0.05,\n"
                << "           prec=\"" << vaspPrec << "\",\n"
                // DFT+U on BOTH passes, and it has to be both: an ICHARG = 11
                // band run reads the density the SCF wrote and then builds its
                // own Hamiltonian, so a band pass without the correction
                // diagonalizes plain PBE against a DFT+U density.
                << AseScriptGenerator::vaspHubbardKeywords(c.gpaw,
                                                           "           ")
                << "           lcharg=True, directory=\".\")\n"
                   "atoms.calc = scf\n"
                   "atoms.get_potential_energy()\n"
                   "efermi = float(atoms.calc.get_fermi_level())\n"
                   "_calango_progress(2, 4)\n"
                   "\n"
                   "# Non-self-consistent band run on the density just written.\n"
                << "bands = Vasp(xc=\"PBE\", encut=" << c.ecutEv
                << ", icharg=11,\n"
                   "             ismear=0, sigma=0.05,\n"
                << "             prec=\"" << vaspPrec << "\",\n"
                << AseScriptGenerator::vaspHubbardKeywords(c.gpaw,
                                                           "             ")
                << "             directory=\".\", kpts=bandpath.kpts,\n"
                   "             reciprocal=True)\n"
                   "atoms.calc = bands\n"
                   "atoms.get_potential_energy()\n"
                   "bs = atoms.calc.band_structure()\n"
                   "bs._reference = efermi\n"
                   "_calango_progress(3, 4)\n";
        }
        if (c.pdos) {
            // LORBIT = 11: lm-decomposed PROCAR + DOSCAR (VASP wiki,
            // LORBIT). RWIGS is deliberately absent -- it is IGNORED for
            // LORBIT >= 10, which projects onto the PAW projector functions
            // directly rather than a user-chosen sphere radius, so there is
            // no per-element setting to collect here at all.
            //
            // A separate pass, not a reuse of `bands`: DOSCAR/vasprun.xml's
            // partial DOS wants a proper zone-filling k-mesh (pdosKpts), not
            // the 1D band path `bands` was just diagonalized on. CHGCAR is
            // already on disk from whichever branch above ran -- ICHARG = 11
            // never rewrites it, so this reads the same converged density.
            out << "\n"
                   "# Projected DOS (LORBIT = 11), on the same fixed density.\n"
                << "dos_calc = Vasp(xc=\"PBE\", encut=" << c.ecutEv
                << ", icharg=11,\n"
                   "                ismear=0, sigma=0.05, lorbit=11,\n"
                << "                prec=\"" << vaspPrec << "\",\n"
                << "                kpts=(" << c.pdosKpts[0] << ", "
                << c.pdosKpts[1] << ", " << c.pdosKpts[2] << "),\n"
                   "                directory=\".\")\n"
                   "atoms.calc = dos_calc\n"
                   "atoms.get_potential_energy()\n"
                << vaspPdosParserBlock();
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
