#include "core/OpticsScriptGenerator.hpp"

#include "core/AseScriptGenerator.hpp"

#include <algorithm>
#include <cctype>
#include <sstream>
#include <string>

namespace calango::core {

namespace {

/// True when the user asked for a response mesh on at least one axis.
bool wantsResponseKpts(const OpticsConfig& cfg)
{
    return cfg.responseKpts[0] > 0 || cfg.responseKpts[1] > 0
        || cfg.responseKpts[2] > 0;
}

/// `kpts=` line for the fixed-density step, or empty to inherit the baseline's
/// grid wholesale.
///
/// The mesh is resolved at RUNTIME (see responseKpointsBlock) because a 0 on
/// one axis means "keep the baseline's value there", and only the baseline
/// knows what that is. This used to require all three axes to be non-zero and
/// discarded the whole mesh otherwise — so "24, 24, auto", the natural entry
/// for a 2D sheet whose out-of-plane direction must stay at one k-point,
/// silently ran the baseline grid and produced a spectrum indistinguishable
/// from not having set anything.
std::string kpointsLine(const OpticsConfig& cfg)
{
    if (!wantsResponseKpts(cfg))
        return {};
    return "    kpts=_response_kpts,  # denser response mesh than the "
           "baseline SCF\n";
}

/// Resolves the requested mesh against the baseline's own, and says which is
/// which in the log. Emitted whether or not a mesh was asked for: reporting
/// the grid the response actually integrates over is what makes two runs
/// distinguishable in their output.
std::string responseKpointsBlock(const OpticsConfig& cfg)
{
    std::ostringstream out;
    out << "# --- Response k-mesh --------------------------------------------\n"
           "# The baseline's own Monkhorst-Pack dimensions, recovered from its\n"
           "# Brillouin-zone point set: counting the distinct fractional\n"
           "# coordinates along each axis gives the grid back without depending\n"
           "# on how the parameter happened to be spelled when it was set.\n"
           "_bz = np.asarray(gs.get_bz_k_points())\n"
           "_baseline_kpts = [int(len(np.unique(np.round(_bz[:, _i], 6))))\n"
           "                  for _i in range(3)]\n"
        << "_requested_kpts = (" << cfg.responseKpts[0] << ", "
        << cfg.responseKpts[1] << ", " << cfg.responseKpts[2]
        << ")  # 0 = inherit that axis\n"
           "_response_kpts = tuple(int(_r) if _r > 0 else _baseline_kpts[_i]\n"
           "                       for _i, _r in enumerate(_requested_kpts))\n"
           "print(f\"CALANGO_INFO baseline k-mesh={tuple(_baseline_kpts)} \"\n"
           "      f\"requested={_requested_kpts} \"\n"
           "      f\"response k-mesh={_response_kpts}\", flush=True)\n"
           "\n";
    return out.str();
}

/// `symmetry=` line. Off samples the full zone; omitting the keyword lets GPAW
/// reduce to the irreducible wedge and weight each point by its degeneracy,
/// which is what "Include IBZ points" asks for.
std::string symmetryLine(const OpticsConfig& cfg)
{
    return cfg.includeIbzPoints
        ? std::string("    # symmetry left ON: GPAW reduces the mesh to the "
                      "irreducible\n    # Brillouin zone and weights each point "
                      "by its degeneracy.\n")
        : std::string("    symmetry=\"off\",  # sample the full Brillouin "
                      "zone\n");
}

/// The 2D-observables block appended to both engine scripts. It only assumes
/// `atoms`, `omega_eV`, `_ok` (the direction keys that produced spectra) and
/// `results["eps_<key>"]` exist — which both scripts guarantee — so the sheet
/// physics is written once and cannot drift between engines.
std::string twoDObservablesBlock(int vacuumAxis)
{
    std::ostringstream out;
    // A supercell calculation of a sheet reports a dielectric function
    // diluted by whatever vacuum was used, so eps_3D is NOT a property of
    // the material — double the vacuum and it moves. Dividing the vacuum
    // thickness back out gives quantities that do not: the sheet
    // polarizability, the 2D conductivity and the absorbance.
    out << "\n"
           "# --- 2D observables (vacuum truncation) "
           "-------------------------\n"
        << "L_z = float(atoms.cell.lengths()[" << vacuumAxis << "])"
           "  # vacuum-direction cell length, Å\n"
           "results[\"vacuum_axis\"] = "
        << vacuumAxis << "\n"
           "results[\"L_z_A\"] = L_z\n"
           "\n"
           "# Physical constants in the units used here: ħc in eV·Å and the\n"
           "# fine-structure constant (dimensionless).\n"
           "hbar_c_eV_A = 1973.269804\n"
           "alpha_fs = 1.0 / 137.035999084\n"
           "\n"
           "\n"
           "def twod_observables(omega_eV, eps1, eps2, L_z):\n"
           "    \"\"\"Sheet observables from a slab's eps_3D.\n"
           "\n"
           "    omega_eV: photon energies (eV); eps1/eps2: the SUPERCELL\n"
           "    dielectric function; L_z: vacuum-direction cell length (Å).\n"
           "\n"
           "    Every quantity returned is independent of L_z — that is the\n"
           "    point. eps_3D itself is not: double the vacuum and it moves,\n"
           "    because the sheet's polarization is diluted over more cell.\n"
           "    \"\"\"\n"
           "    omega_eV = np.asarray(omega_eV, dtype=float)\n"
           "    eps1 = np.asarray(eps1, dtype=float)\n"
           "    eps2 = np.asarray(eps2, dtype=float)\n"
           "    # k = omega / (hbar c), in 1/Å.\n"
           "    k = omega_eV / hbar_c_eV_A\n"
           "    # alpha_2D(omega) = L_z / (4 pi) * (eps_3D - 1)   [Å]\n"
           "    # The -1 removes the vacuum's own contribution, which is\n"
           "    # what makes the result a property of the SHEET.\n"
           "    alpha2d_re = L_z / (4.0 * np.pi) * (eps1 - 1.0)\n"
           "    alpha2d_im = L_z / (4.0 * np.pi) * eps2\n"
           "    # A(omega) = (omega L_z / c) Im[eps_3D]. For graphene this\n"
           "    # returns the universal pi*alpha = 2.29%.\n"
           "    absorbance = k * L_z * eps2\n"
           "    # sigma_2D = -i omega alpha_2D, i.e.\n"
           "    #   Re[sigma_2D] = omega Im[alpha_2D]\n"
           "    #   Im[sigma_2D] = -omega Re[alpha_2D]\n"
           "    # Those are Gaussian sigma/c (dimensionless). The literature\n"
           "    # quotes 2D conductivity in e^2/h, where graphene's universal\n"
           "    # value is pi/2 ~ 1.5708, so convert: sigma[e^2/h] =\n"
           "    # (sigma/c) * 2 pi / alpha. Equivalently A / (2 alpha).\n"
           "    to_e2_over_h = 2.0 * np.pi / alpha_fs\n"
           "    sigma_re = k * alpha2d_im * to_e2_over_h\n"
           "    sigma_im = -k * alpha2d_re * to_e2_over_h\n"
           "    return {\n"
           "        \"alpha_2D_re_A\": [float(v) for v in alpha2d_re],\n"
           "        \"alpha_2D_im_A\": [float(v) for v in alpha2d_im],\n"
           "        \"absorbance\": [float(v) for v in absorbance],\n"
           "        \"sigma_2D_re\": [float(v) for v in sigma_re],\n"
           "        \"sigma_2D_im\": [float(v) for v in sigma_im],\n"
           "    }\n"
           "\n"
           "\n"
           "for key in list(_ok):\n"
           "    twod = twod_observables(omega_eV,\n"
           "                            results[\"eps_\" + key][\"eps1\"],\n"
           "                            results[\"eps_\" + key][\"eps2\"],\n"
           "                            L_z)\n"
           "    results[\"twod_\" + key] = twod\n"
           "    absorbance = np.asarray(twod[\"absorbance\"], dtype=float)\n"
           "    _peak = int(np.argmax(absorbance)) if absorbance.size else 0\n"
           "    print(f\"CALANGO_RESULT twod_{key} peak_absorbance=\"\n"
           "          f\"{absorbance[_peak]:.4f} at {omega_eV[_peak]:.3f} eV\","
           " flush=True)\n";
    return out.str();
}

/// The directions literal both scripts iterate: `("xx", "x"), …`, filtered to
/// the user's selection, never empty.
std::string directionsLiteral(const OpticsConfig& cfg)
{
    std::string directions;
    const auto addDirection = [&directions](bool enabled, const char* key,
                                            const char* axis) {
        if (!enabled)
            return;
        if (!directions.empty())
            directions += ", ";
        directions += std::string("(\"") + key + "\", \"" + axis + "\")";
    };
    addDirection(cfg.dirX, "xx", "x");
    addDirection(cfg.dirY, "yy", "y");
    addDirection(cfg.dirZ, "zz", "z");
    if (directions.empty())
        directions = "(\"xx\", \"x\")";
    return directions;
}

/// True when the configured XC functional carries exact exchange. LOPTICS
/// after a hybrid ground state must diagonalize with ALGO=Eigenval — the
/// semilocal recipe's ALGO=Exact path does not apply the exact-exchange
/// operator to the empty states it generates.
bool usesExactExchange(const OpticsConfig& cfg)
{
    std::string xc = cfg.calculator.vaspXc;
    std::transform(xc.begin(), xc.end(), xc.begin(),
                   [](unsigned char c) { return std::tolower(c); });
    return xc == "hse06" || xc == "hse03" || xc == "pbe0" || xc == "b3lyp"
        || xc == "hf";
}

/// VASP linear optics: the two-step protocol from the VASP wiki ("Dielectric
/// properties" / LOPTICS): a self-consistent run that leaves CHGCAR+WAVECAR,
/// then an exact-diagonalization restart at fixed density (ICHARG=11) with
/// LOPTICS=.TRUE., an enlarged NBANDS, CSHIFT as the broadening and NEDOS as
/// the frequency-grid density. ε(ω) is read back from vasprun.xml's
/// <dielectricfunction> block and written to the same optics.json schema the
/// GPAW path produces, so one results window serves both engines.
std::string generateVaspOpticsScript(const OpticsConfig& cfg)
{
    // Step-2 k-mesh: 0 on an axis inherits the SCF grid — resolvable here at
    // generation time, since for VASP the SCF grid is the config's own.
    const bool densify = wantsResponseKpts(cfg);
    int responseKpts[3];
    for (int axis = 0; axis < 3; ++axis)
        responseKpts[axis] = cfg.responseKpts[axis] > 0
            ? cfg.responseKpts[axis]
            : cfg.calculator.kpts[axis];

    const char* algo = usesExactExchange(cfg) ? "Eigenval" : "Exact";

    std::ostringstream out;
    out << "# Optical properties (VASP, frequency-dependent dielectric "
           "function) — generated by Calango\n"
           "import json\n"
           "import xml.etree.ElementTree as ET\n"
           "\n"
           "import numpy as np\n"
           "from ase.io import read\n"
           "from ase.calculators.vasp import Vasp\n"
           "\n"
        << AseScriptGenerator::jsonLoggerPreamble()
        << "atoms = read(\"structure.extxyz\")\n"
           "_calango_log.progress(0, 4)\n"
           "\n"
           "# --- Step 1: self-consistent ground state ------------------------\n"
           "# Standard VASP optics is a TWO-step protocol: a normal SCF that\n"
           "# writes CHGCAR and WAVECAR, then an exact-diagonalization restart\n"
           "# with LOPTICS on the frozen density. One combined run would\n"
           "# diagonalize the enlarged band set self-consistently — slower and\n"
           "# not what the reference recipe measures.\n";
    {
        // The same VASP calculator block every generated script uses (ENCUT,
        // KPTS, xc, INCAR extras) — the ground state is defined once, by the
        // shared config, not re-spelled here.
        out << AseScriptGenerator::calculatorSnippet(cfg.calculator);
    }
    out << "# The restart needs the density and wavefunctions on disk,\n"
           "# whatever the calculator's own output defaults say.\n"
           "atoms.calc.set(lwave=True, lcharg=True)\n"
           "energy = float(atoms.get_potential_energy())\n"
           "print(f\"CALANGO_INFO scf_energy_eV={energy:.6f}\", flush=True)\n"
           "try:\n"
           "    nbands_scf = int(atoms.calc.get_number_of_bands())\n"
           "except Exception:\n"
           "    nbands_scf = 0\n"
           "_calango_log.progress(1, 4)\n"
           "\n"
           "# --- Step 2: LOPTICS at fixed density ----------------------------\n"
           "params = dict(atoms.calc.parameters)\n"
           "params.update(\n"
           "    icharg=11,   # non-selfconsistent: fix the step-1 density\n"
        << "    algo=\"" << algo << "\",  "
        << (usesExactExchange(cfg)
                ? "# hybrid functional: Eigenval applies the exact-exchange\n"
                  "                        # operator to the new empty states"
                : "# exact diagonalization over the enlarged band set")
        << "\n"
           "    nelm=1,      # one diagonalization pass — nothing to converge\n"
           "    loptics=True,   # frequency-dependent dielectric function\n"
        << "    cshift=" << cfg.broadeningEv
        << ",  # complex shift — the Lorentzian broadening of ε(ω)\n"
        << "    nedos=" << (cfg.npoints > 100 ? cfg.npoints : 100)
        << ",  # frequency-grid density of the ε(ω) output\n"
           "    lwave=False,\n"
           "    lcharg=False,\n"
           ")\n"
           "if nbands_scf > 0:\n"
        << "    # LOPTICS sums transitions into empty states, and VASP's\n"
           "    # default NBANDS barely covers occupation.\n"
        << "    params[\"nbands\"] = int(round(" << cfg.vaspNbandsFactor
        << " * nbands_scf))\n"
           "else:\n"
           "    print(\"CALANGO_WARN could not read the SCF band count — \"\n"
           "          \"running LOPTICS with VASP's default NBANDS; the \"\n"
           "          \"high-energy tail of the spectrum will be \"\n"
           "          \"underconverged.\", flush=True)\n";
    if (densify)
        out << "# Denser optics k-mesh than the SCF (legitimate under\n"
               "# ICHARG=11 — the density is fixed, only the sampling of the\n"
               "# transitions changes).\n"
               "params[\"kpts\"] = ("
            << responseKpts[0] << ", " << responseKpts[1] << ", "
            << responseKpts[2] << ")\n";
    out << "atoms.calc = Vasp(**params)\n"
           "atoms.get_potential_energy()  # triggers the LOPTICS run\n"
           "_calango_log.progress(2, 4)\n"
           "\n"
           "# --- ε(ω) from vasprun.xml ---------------------------------------\n"
           "tree = ET.parse(\"vasprun.xml\")\n"
           "nodes = tree.getroot().findall(\".//dielectricfunction\")\n"
           "if not nodes:\n"
           "    raise RuntimeError(\n"
           "        \"vasprun.xml holds no <dielectricfunction> — the LOPTICS \"\n"
           "        \"step produced no optics output (check OUTCAR).\")\n"
           "# VASP 6 writes density-density AND current-current responses; the\n"
           "# density-density block is the standard IPA spectrum.\n"
           "node = nodes[0]\n"
           "for candidate in nodes:\n"
           "    if \"density\" in (candidate.get(\"comment\") or \"\"):\n"
           "        node = candidate\n"
           "        break\n"
           "\n"
           "\n"
           "def _rows(section):\n"
           "    return np.asarray([[float(x) for x in r.text.split()]\n"
           "                       for r in section.findall(\".//r\")],\n"
           "                      dtype=float)\n"
           "\n"
           "\n"
           "imag = _rows(node.find(\"imag\"))\n"
           "real = _rows(node.find(\"real\"))\n"
           "omega_all = real[:, 0]\n"
           "# Columns per row: energy, xx, yy, zz, xy, yz, zx.\n"
           "_component = {\"xx\": 1, \"yy\": 2, \"zz\": 3}\n"
           "_calango_log.progress(3, 4)\n"
           "\n"
        << "window = (omega_all >= " << cfg.omegaMinEv
        << ") & (omega_all <= " << cfg.omegaMaxEv
        << ")\n"
           "if not np.any(window):\n"
           "    window = np.ones_like(omega_all, dtype=bool)\n"
           "    print(\"CALANGO_WARN the requested photon-energy window lies \"\n"
           "          \"outside VASP's ε(ω) grid; writing the full grid \"\n"
           "          \"instead.\", flush=True)\n"
           "omega_eV = omega_all[window]\n"
           "\n"
           "# ħc = 197.3269804 eV·nm = 197.3269804e-7 eV·cm. With ħω in eV the\n"
           "# absorption coefficient α = 2 (ω / ħc) k then comes out in cm^-1.\n"
           "hbar_c_eV_cm = 197.3269804e-7\n"
           "\n"
           "\n"
           "def derived_spectra(omega_eV, eps):\n"
           "    \"\"\"The seven per-direction spectra from complex ε(ω) — the\n"
           "    same formulas the GPAW path uses, so the two engines are\n"
           "    comparable panel by panel.\"\"\"\n"
           "    eps = np.where(np.isfinite(eps), eps, 0.0)\n"
           "    eps1 = eps.real\n"
           "    eps2 = eps.imag\n"
           "    refractive = np.sqrt(eps.astype(complex))  # N = n + i k\n"
           "    n = refractive.real\n"
           "    k = refractive.imag\n"
           "    absorption = 2.0 * (omega_eV / hbar_c_eV_cm) * k  # cm^-1\n"
           "    reflectivity = ((n - 1.0) ** 2 + k ** 2) / \\\n"
           "        ((n + 1.0) ** 2 + k ** 2)\n"
           "    _denom = np.where(np.abs(eps) > 1e-12, eps, 1e-12)\n"
           "    loss = (-1.0 / _denom).imag  # energy-loss function -Im(1/ε)\n"
           "    return {\n"
           "        \"eps1\": [float(v) for v in eps1],\n"
           "        \"eps2\": [float(v) for v in eps2],\n"
           "        \"absorption\": [float(v) for v in absorption],\n"
           "        \"reflectivity\": [float(v) for v in reflectivity],\n"
           "        \"n\": [float(v) for v in n],\n"
           "        \"k\": [float(v) for v in k],\n"
           "        \"loss\": [float(v) for v in loss],\n"
           "    }\n"
           "\n"
           "\n"
           "results = {\"energy_eV\": [float(w) for w in omega_eV]}\n"
           "results[\"engine\"] = \"VASP\"\n"
           "results[\"sampling\"] = {\n"
        << "    \"nedos\": " << (cfg.npoints > 100 ? cfg.npoints : 100)
        << ",\n"
        << "    \"cshift_eV\": " << cfg.broadeningEv << ",\n"
        << "    \"algo\": \"" << algo << "\",\n"
        << "    \"nbands_factor\": " << cfg.vaspNbandsFactor << ",\n"
        << "    \"response_kpts\": [" << responseKpts[0] << ", "
        << responseKpts[1] << ", " << responseKpts[2] << "],\n"
           "    \"npoints\": int(len(omega_eV)),\n"
           "    \"omega_min_eV\": float(omega_eV[0]) if len(omega_eV) else 0.0,\n"
           "    \"omega_max_eV\": float(omega_eV[-1]) if len(omega_eV) else 0.0,\n"
           "}\n"
           "\n"
        << "directions = [" << directionsLiteral(cfg) << "]\n"
        << "_ok = []\n"
           "for key, _axis in directions:\n"
           "    column = _component[key]\n"
           "    eps = real[window, column] + 1j * imag[window, column]\n"
           "    results[key] = derived_spectra(omega_eV, eps)\n"
           "    results[\"eps_\" + key] = {\n"
           "        \"eps1\": results[key][\"eps1\"],\n"
           "        \"eps2\": results[key][\"eps2\"],\n"
           "    }\n"
           "    _ok.append(key)\n";
    if (cfg.vacuumAxis >= 0 && cfg.vacuumAxis <= 2)
        out << twoDObservablesBlock(cfg.vacuumAxis);
    out << "\n"
           "with open(\"optics.json\", \"w\") as handle:\n"
           "    json.dump(results, handle)\n"
           "_calango_log.progress(4, 4)\n"
           "print(\"CALANGO_RESULT optics=optics.json\", flush=True)\n";
    return out.str();
}

} // namespace

std::string generateOpticsScript(const OpticsConfig& cfg)
{
    // Engine dispatch: VASP is self-contained (SCF + LOPTICS in one job);
    // everything else runs the GPAW response workflow below.
    if (cfg.calculator.calculator == CalculatorKind::Vasp)
        return generateVaspOpticsScript(cfg);

    // Note: cfg.calculator is deliberately NOT consulted. Every ground-state
    // parameter (mode, cutoff, xc, k-grid, smearing) comes from the inherited
    // .gpw, which GPAW restores on restart. Emitting them here would let a
    // wizard-side value silently disagree with the baseline it claims to use.

    // The list of (json-key, GPAW axis) pairs the response loop iterates over,
    // filtered to the directions the user asked for. Guard against an empty
    // selection so the script always produces at least one spectrum.
    const std::string directions = directionsLiteral(cfg);

    std::ostringstream out;
    out << "# Optical properties (linear dielectric response) — generated by "
           "Calango\n"
           "import json\n"
           "\n"
           "import numpy as np\n"
           "from ase.io import read\n"
           "\n"
        << AseScriptGenerator::jsonLoggerPreamble()
        << "atoms = read(\"structure.extxyz\")\n"
           "_calango_log.progress(0, 4)\n"
           "\n"
           "# --- Baseline ground state (inherited, NOT re-converged) "
           "---------\n"
           "# The SCF was done by the Single-Point Calculation this run "
           "inherits\n"
           "# from; re-converging it here would silently produce a spectrum "
           "from a\n"
           "# different ground state than the one that was inspected.\n"
           "from gpaw import GPAW\n"
           "\n"
        << "gs = GPAW(r\"" << cfg.baselineDensityPath << "\", txt=None)\n"
           "_calango_log.progress(1, 4)\n"
           "\n"
        << responseKpointsBlock(cfg)
        << "# --- Fixed-density NSCF with extra empty bands "
           "----------------------\n"
           "# The dielectric response sums transitions into unoccupied states, so\n"
           "# a generous number of empty bands improves the spectrum. They are\n"
           "# converged at the FIXED baseline density — no self-consistency.\n"
           "n_occ = max(1, int(round(gs.get_number_of_electrons() / 2.0)))\n"
           "n_bands = max(4 * n_occ, 24)  # occupied + empty bands\n"
           "nscf = gs.fixed_density(\n"
           "    nbands=n_bands,\n"
           "    convergence={\"bands\": max(2 * n_occ, 12)},\n"
        << kpointsLine(cfg)
        << symmetryLine(cfg)
        << "    txt=\"gpaw_nscf.txt\",\n"
           ")\n"
           "# What the response will actually integrate over. With symmetry\n"
           "# reduction on, the count is the IRREDUCIBLE set and each point\n"
           "# carries its degeneracy weight; the weights sum to 1 either way.\n"
           "_ibz = nscf.get_ibz_k_points()\n"
           "print(f\"CALANGO_INFO response k-points={len(_ibz)} \"\n"
           "      f\"weight_sum={float(sum(nscf.get_k_point_weights())):.4f}\",\n"
           "      flush=True)\n"
           "nscf.write(\"gs_nscf.gpw\", mode=\"all\")\n"
           "_calango_log.progress(2, 4)\n"
           "\n"
           "# --- Dielectric function via GPAW's response module "
           "-----------------\n"
           "try:\n"
           "    from gpaw.response.df import DielectricFunction\n"
           "except Exception as exc:  # pragma: no cover\n"
           "    raise RuntimeError(\n"
           "        \"GPAW's response module (gpaw.response.df) is required for \"\n"
           "        \"the optical-properties workflow but could not be imported: \"\n"
           "        f\"{exc}\"\n"
           "    )\n"
           "\n"
        << "integrationmode = \""
        << (cfg.tetrahedronIntegration ? "tetrahedron integration"
                                       : "point integration")
        << "\"\n"
           "results_meta = {\"integrationmode\": integrationmode}\n"
           "\n"
           "# --- Photon-energy grid ------------------------------------------\n"
           "# The window and sample count the wizard collected, handed to GPAW\n"
           "# explicitly. They used to be collected and then dropped: the\n"
           "# response module was left to build its own default non-linear grid\n"
           "# and that grid was read back, so changing \"Number of points\" or\n"
           "# either energy bound changed nothing whatsoever in the output.\n"
           "#\n"
           "# hilbert=False is REQUIRED alongside an explicit frequency list.\n"
           "# GPAW's default path builds a spectral function on its own\n"
           "# non-linear grid and Hilbert-transforms it, and asserts that\n"
           "# descriptor's type; turning the transform off evaluates exactly the\n"
           "# frequencies requested. It costs more — the work is now linear in\n"
           "# the number of points rather than amortized over the transform —\n"
           "# which is the honest price of the grid actually being the one that\n"
           "# was asked for.\n"
        << "frequencies_eV = np.linspace(" << cfg.omegaMinEv << ", "
        << cfg.omegaMaxEv << ", " << (cfg.npoints > 1 ? cfg.npoints : 2)
        << ")\n"
           "print(f\"CALANGO_INFO photon-energy grid \"\n"
           "      f\"{frequencies_eV[0]:.3f}..{frequencies_eV[-1]:.3f} eV \"\n"
           "      f\"in {len(frequencies_eV)} points\", flush=True)\n"
           "\n"
           "try:\n"
           "    df = DielectricFunction(\n"
           "        \"gs_nscf.gpw\",\n"
           "        frequencies=frequencies_eV,\n"
           "        hilbert=False,  # required by an explicit frequency list\n"
        << "        eta=" << cfg.broadeningEv
        << ",  # Lorentzian broadening η, eV\n"
           "        intraband=False,  # semiconductor: no Drude/intraband term\n"
           "        integrationmode=integrationmode,\n"
           "        txt=\"gpaw_df.txt\",\n"
           "    )\n"
           "    # The k-grid check happens lazily, inside the first response\n"
           "    # evaluation, so it is triggered here rather than left to\n"
           "    # surface halfway through the direction loop.\n"
           "    _ = df.get_frequencies()\n"
           "except ValueError as exc:\n"
           "    if \"vertices of the IBZ\" not in str(exc):\n"
           "        raise\n"
           "    # Tetrahedron integration needs every vertex of the irreducible\n"
           "    # BZ present in the ground-state k-grid. Falling back to point\n"
           "    # integration here would return a spectrum computed by a\n"
           "    # different method than the one requested, labeled as if it\n"
           "    # were not — so this stops instead.\n"
           "    raise RuntimeError(\n"
           "        \"Tetrahedron integration requires a ground-state k-grid \"\n"
           "        \"that contains all vertices of the irreducible Brillouin \"\n"
           "        \"zone, and the inherited baseline's grid does not.\\n\\n\"\n"
           "        \"Either re-run the Single-Point baseline with a grid from \"\n"
           "        \"gpaw.bztools.find_high_symmetry_monkhorst_pack(), or \"\n"
           "        \"turn off tetrahedron integration in the Optics wizard to \"\n"
           "        \"use point integration with the eta broadening.\\n\\n\"\n"
           "        f\"GPAW reported: {exc}\"\n"
           "    ) from exc\n"
           "# Read back rather than reused: this is the grid the response\n"
           "# module actually integrated on, and asserting it against the one\n"
           "# requested is what would catch a future GPAW quietly substituting\n"
           "# its own.\n"
           "frequencies = np.asarray(df.get_frequencies(), dtype=float)\n"
           "if len(frequencies) != len(frequencies_eV):\n"
           "    print(f\"CALANGO_WARN GPAW evaluated {len(frequencies)} \"\n"
           "          f\"frequencies where {len(frequencies_eV)} were \"\n"
           "          f\"requested; the spectra follow the grid it used.\",\n"
           "          flush=True)\n"
           "_calango_log.progress(3, 4)\n"
           "\n"
           "# ħc = 197.3269804 eV·nm = 197.3269804e-7 eV·cm. With ħω in eV the\n"
           "# absorption coefficient α = 2 (ω / ħc) k then comes out in cm^-1.\n"
           "hbar_c_eV_cm = 197.3269804e-7\n"
           "omega_eV = frequencies\n"
           "\n"
           "# The integrator is recorded alongside the spectra: two runs of the\n"
           "# same system can differ visibly in peak shape purely by this\n"
           "# choice, so a spectrum that does not say which was used is not\n"
           "# reproducible from its own output.\n"
           "results = {\"energy_eV\": [float(w) for w in frequencies]}\n"
           "results.update(results_meta)\n"
           "# The sampling that produced these numbers, recorded alongside\n"
           "# them: two spectra that differ only in k-mesh or grid density are\n"
           "# otherwise indistinguishable from their own output files.\n"
           "results[\"sampling\"] = {\n"
           "    \"response_kpts\": list(_response_kpts),\n"
           "    \"baseline_kpts\": list(_baseline_kpts),\n"
           "    \"requested_kpts\": list(_requested_kpts),\n"
           "    \"npoints\": int(len(frequencies)),\n"
           "    \"omega_min_eV\": float(frequencies[0]),\n"
           "    \"omega_max_eV\": float(frequencies[-1]),\n"
        << "    \"eta_eV\": " << cfg.broadeningEv << ",\n"
           "    \"ibz_points\": int(len(_ibz)),\n"
           "}\n"
           "\n"
        << "directions = [" << directions << "]\n"
           "_ok = []\n"
           "for key, axis in directions:\n"
           "    # (nlfc, lfc): without and with local-field corrections; the\n"
           "    # local-field-corrected result is the physically complete one.\n"
           "    # Guard each direction: a near-singular optical-limit head for\n"
           "    # one polarization must not abort the whole tensor evaluation.\n"
           "    try:\n"
           "        eps_nlfc, eps_lfc = df.get_dielectric_function(direction=axis)\n"
           "    except Exception as _e:\n"
           "        _calango_log.event('warning',\n"
           "                           'direction %s failed: %r' % (axis, _e))\n"
           "        continue\n"
           "    eps = np.asarray(eps_lfc)\n"
           "    # Drop any non-finite frequency points (missing grid entries /\n"
           "    # singular matrix rows) rather than propagating NaNs downstream.\n"
           "    eps = np.where(np.isfinite(eps), eps, 0.0)\n"
           "    eps1 = eps.real\n"
           "    eps2 = eps.imag\n"
           "    refractive = np.sqrt(eps.astype(complex))  # N = n + i k\n"
           "    n = refractive.real\n"
           "    k = refractive.imag\n"
           "    absorption = 2.0 * (omega_eV / hbar_c_eV_cm) * k  # cm^-1\n"
           "    reflectivity = ((n - 1.0) ** 2 + k ** 2) / "
           "((n + 1.0) ** 2 + k ** 2)\n"
           "    _denom = np.where(np.abs(eps) > 1e-12, eps, 1e-12)\n"
           "    loss = (-1.0 / _denom).imag  # energy-loss function -Im(1/ε)\n"
           "    results[key] = {\n"
           "        \"eps1\": [float(v) for v in eps1],\n"
           "        \"eps2\": [float(v) for v in eps2],\n"
           "        \"absorption\": [float(v) for v in absorption],\n"
           "        \"reflectivity\": [float(v) for v in reflectivity],\n"
           "        \"n\": [float(v) for v in n],\n"
           "        \"k\": [float(v) for v in k],\n"
           "        \"loss\": [float(v) for v in loss],\n"
           "    }\n"
           "    # Convenience top-level duplicate of the diagonal ε components.\n"
           "    results[\"eps_\" + key] = {\n"
           "        \"eps1\": [float(v) for v in eps1],\n"
           "        \"eps2\": [float(v) for v in eps2],\n"
           "    }\n"
           "    _ok.append(key)\n"
           "\n"
           "if not _ok:\n"
           "    raise RuntimeError('DielectricFunction produced no valid "
           "direction — check the k-point sampling and empty-band count.')\n";

    if (cfg.vacuumAxis >= 0 && cfg.vacuumAxis <= 2)
        out << twoDObservablesBlock(cfg.vacuumAxis);

    out << "with open(\"optics.json\", \"w\") as handle:\n"
           "    json.dump(results, handle)\n"
           "_calango_log.progress(4, 4)\n"
           "print(\"CALANGO_RESULT optics=optics.json\", flush=True)\n";
    return out.str();
}

} // namespace calango::core
