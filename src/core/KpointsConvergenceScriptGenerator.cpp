#include "core/KpointsConvergenceScriptGenerator.hpp"

#include "core/AseScriptGenerator.hpp"
#include "core/ConvergenceSweepBlocks.hpp"

#include <sstream>

namespace calango::core {

namespace {

/// The ω_p helper the sweep calls once per mesh, emitted only when the metric
/// is on. Lives at module level in the generated script rather than inline in
/// the loop so the import cost and the docstring are paid once.
///
/// Two ladders are walked here, and both exist because of what they preserve:
///
/// 1. Ground-state access. The live calculator is used when it can be
///    (`get_gs_and_context` accepts one directly), and a `.gpw` is written
///    only if that path refuses — which it does under MPI, where the adapter
///    asserts `wfs.world.size == 1`. Writing unconditionally would add a
///    full mode="all" dump per mesh to a sweep whose whole point is to run
///    many of them.
///
/// 2. Integrator. Tetrahedron integration resolves the Fermi surface at T=0;
///    point integration weights it by −∂f/∂ε, a delta smeared to the
///    occupation width, and needs a far denser mesh for the same answer (Au:
///    within 2 % from 4³ against 24³). Tetrahedron is therefore tried first,
///    including the qsymmetry=False retry that lifts GPAW's IBZ-vertex
///    requirement, and point integration is the last resort rather than the
///    default. Whichever ran is recorded — two meshes measured by different
///    integrators are not comparable, and a curve that silently mixed them
///    would show convergence that is really a change of method.
std::string plasmaFrequencyHelper()
{
    return R"PY(

def plasma_frequency(calc, tag):
    """Intraband (Drude) plasma frequency omega_p, in eV.

    Returns (omega_p, tensor_eV2, integrationmode, qsymmetry, note). omega_p is
    None when the quantity does not exist for this system, with the reason in
    `note` — a gapped ground state has no partially occupied bands, so the
    intraband term is identically zero and plotting that as a converged 0.0
    would be a lie about a quantity that simply does not apply.
    """
    import numpy as np
    from ase.units import Hartree
    from gpaw.mpi import world
    from gpaw.response.chi0_drude import Chi0DrudeCalculator
    from gpaw.response.frequencies import FrequencyGridDescriptor
    from gpaw.response.pair import get_gs_and_context

    gpw_path = f"plasma_{tag}.gpw"
    _wrote_gpw = [False]

    def adapter():
        # The live calculator first; the .gpw fallback only if it refuses.
        try:
            return get_gs_and_context(calc, None, world, None)
        except Exception:
            if not _wrote_gpw[0]:
                calc.write(gpw_path, mode="all")
                _wrote_gpw[0] = True
            return get_gs_and_context(gpw_path, None, world, None)

    gs, _ = adapter()
    if not gs.metallic:
        return (None, None, None, None,
                "no partially occupied bands (gapped ground state): the "
                "intraband term is identically zero here")

    errors = []
    for mode, qsym in (("tetrahedron integration", True),
                       ("tetrahedron integration", False),
                       ("point integration", True)):
        try:
            gs, context = adapter()
            drude = Chi0DrudeCalculator(gs, context, qsymmetry=qsym,
                                        integrationmode=mode)
            # omega_p does not depend on this frequency grid — only the Drude
            # chi does — but `calculate` needs one, and the rate must be
            # non-zero for the upper-half-plane contour assertion inside it.
            data = drude.calculate(FrequencyGridDescriptor([0.0]), 0.1)
            # plasmafreq_vv holds omega_p^2 in Hartree^2, the 4*pi included.
            tensor = data.plasmafreq_vv.real * Hartree ** 2
            mean = float(np.diag(tensor).mean())
            if not np.isfinite(mean) or mean <= 0.0:
                errors.append(f"{mode} (qsymmetry={qsym}): non-positive trace")
                continue
            return (float(np.sqrt(mean)),
                    [[float(v) for v in row] for row in tensor],
                    mode, bool(qsym), None)
        except Exception as exc:
            errors.append(f"{mode} (qsymmetry={qsym}): "
                          f"{type(exc).__name__}: {exc}")
    return (None, None, None, None, "; ".join(errors))
)PY";
}

/// The per-mesh measurement, emitted inside the sweep loop's `try:`.
std::string plasmaMeasurementBlock()
{
    return
        "        # -- Intraband plasma frequency ---------------------------\n"
        "        # Measured per mesh like the others, but reported without a\n"
        "        # fallback: an omega_p that could not be computed is absent\n"
        "        # from the curve, never 0.0, which on this quantity would\n"
        "        # read as a perfectly converged free-carrier-free metal.\n"
        "        (_wp, _wp_tensor, _wp_mode,\n"
        "         _wp_qsym, _wp_note) = plasma_frequency(atoms.calc, label)\n"
        "        if _wp is not None:\n"
        "            record[\"plasma_frequency_eV\"] = _wp\n"
        "            record[\"plasma_frequency_tensor_eV2\"] = _wp_tensor\n"
        "            record[\"plasma_integration\"] = _wp_mode\n"
        "            record[\"plasma_qsymmetry\"] = _wp_qsym\n"
        "            _calango_metric(index, plasma_frequency=_wp)\n"
        "            print(f\"CALANGO_MEMBER kpts={label} \"\n"
        "                  f\"omega_p={_wp:.4f} eV ({_wp_mode})\", flush=True)\n"
        "        else:\n"
        "            record[\"plasma_frequency_note\"] = _wp_note\n"
        "            print(f\"CALANGO_WARN kpts={label} no plasma frequency: \"\n"
        "                  f\"{_wp_note}\", flush=True)\n";
}

/// Δω_p against the reference mesh, appended after the shared analysis block.
///
/// Separate from `convergence_sweep::analysisBlock` rather than folded into
/// it: that block is shared verbatim with the plane-wave cutoff sweep, and
/// this metric is offered only here. Editing the shared block to carry an
/// optional fourth quantity would put a k-point-only concern in the cutoff
/// sweep's generated script too.
std::string plasmaAnalysisBlock()
{
    return
        "\n"
        "# -- Plasma-frequency convergence ----------------------------------\n"
        "# The reference is the densest mesh, as for every other quantity. If\n"
        "# IT is the run that failed to produce an omega_p there is nothing to\n"
        "# difference against, and the absolute values still stand on their\n"
        "# own — so the deltas are simply omitted rather than silently\n"
        "# re-based onto a coarser mesh.\n"
        "_plasma_reference = reference.get(\"plasma_frequency_eV\")\n"
        "if _plasma_reference:\n"
        "    for p in converged:\n"
        "        if p.get(\"plasma_frequency_eV\") is not None:\n"
        "            p[\"delta_plasma_frequency_eV\"] = (\n"
        "                p[\"plasma_frequency_eV\"] - _plasma_reference)\n"
        "    print(f\"CALANGO_INFO reference omega_p = \"\n"
        "          f\"{_plasma_reference:.4f} eV\", flush=True)\n"
        "else:\n"
        "    print(\"CALANGO_WARN the reference mesh produced no plasma \"\n"
        "          \"frequency; absolute values are reported without deltas.\",\n"
        "          flush=True)\n";
}

} // namespace

std::string KpointsConvergenceScriptGenerator::generate(
    const KpointsConvergenceRunConfig& config)
{
    const bool vasp =
        config.calculator.calculator == CalculatorKind::Vasp;
    // GPAW's response module is what supplies the intraband term; there is no
    // equivalent path through the VASP branch of this generator, so the
    // request is dropped rather than emitted as code that cannot run.
    const bool plasma = config.plasmaFrequency && !vasp;
    CalculatorConfig calculator = config.calculator;
    if (vasp) {
        // Nothing restarts between points; one mesh's wavefunction and
        // density files must not leak into the next.
        calculator.vaspLwave = false;
        calculator.vaspLcharg = false;
    }

    std::ostringstream out;
    out << "#!/usr/bin/env python3\n"
           "# Generated by Calango " << CALANGO_VERSION
        << " — k-point convergence with " << (vasp ? "VASP" : "GPAW")
        << ".\n"
           "# One fixed-geometry SCF per Monkhorst-Pack mesh, on the same\n"
           "# structure. The densest mesh is the reference: every point is\n"
           "# recorded as its distance from that run in total energy per atom\n"
           "# and in the maximum force magnitude, which are the quantities a\n"
           "# production k-grid has to reproduce.\n"
           "# This is a plain ASE script: edit it freely or run it standalone.\n"
           "\n"
           "import json\n"
           "import math\n"
           "\n"
           "import numpy as np\n"
           "from ase.io import read\n"
           "\n"
        << AseScriptGenerator::jsonLoggerPreamble();
    if (!vasp)
        out << AseScriptGenerator::gpawImports(calculator);
    out << "\n"
           "# The sweep, ascending in density — the last entry is the\n"
           "# reference. K_PER_AXIS is the scalar each mesh was built from\n"
           "# (the swept axes' subdivision count); a pinned axis (a slab's\n"
           "# vacuum direction) stays at 1 in MESHES without disturbing it.\n"
           "MESHES = [";
    for (std::size_t i = 0; i < config.meshes.size(); ++i) {
        const auto& mesh = config.meshes[i];
        out << (i ? ", " : "") << "(" << mesh.kpts[0] << ", " << mesh.kpts[1]
            << ", " << mesh.kpts[2] << ")";
    }
    out << "]\n"
           "K_PER_AXIS = [";
    for (std::size_t i = 0; i < config.meshes.size(); ++i)
        out << (i ? ", " : "") << config.meshes[i].kPerAxis;
    out << "]\n"
           "\n"
           "results_path = r\""
        << config.resultsJson
        << "\"\n"
           "\n"
           "atoms = read(r\""
        << config.structureFile
        << "\")\n"
           "natoms = max(1, len(atoms))\n"
           "print(f\"CALANGO_INFO natoms={len(atoms)} \"\n"
           "      f\"meshes={len(MESHES)}\", flush=True)\n"
           "\n"
           "\n"
           "def attach_calculator(atoms, kpts):\n"
           "    \"\"\"Bind a fresh calculator at one k-point mesh.\n"
           "\n"
           "    Fresh per point rather than reused: a shared calculator would\n"
           "    carry the previous mesh's converged density into the next SCF\n"
           "    and quietly smooth out the very convergence behaviour the\n"
           "    sweep is trying to measure.\n"
           "    \"\"\"\n";
    if (vasp) {
        // The standard VASP calculator block (ENCUT, xc, INCAR extras), with
        // the mesh then overridden by the loop variable. One directory per
        // mesh; istart=0 forbids restarts across points. ASE's `gamma`
        // keyword is the Γ-centring switch for VASP's KPOINTS file.
        const std::string snippet =
            AseScriptGenerator::calculatorSnippet(calculator);
        std::istringstream lines(snippet);
        std::string line;
        while (std::getline(lines, line)) {
            if (line.empty())
                out << "\n";
            else
                out << "    " << line << "\n";
        }
        out << "    atoms.calc.set(kpts=tuple(kpts),\n"
            << "                   gamma="
            << (calculator.kptsGammaCentered ? "True" : "False")
            << ",  # Γ-centred KPOINTS, held across the sweep\n"
               "                   istart=0,\n"
               "                   directory="
               "f\"kpts_{kpts[0]}x{kpts[1]}x{kpts[2]}\")\n";
    } else {
        out << "    atoms.calc = GPAW(\n";
        if (calculator.kptsGammaCentered)
            out << "        # Γ-centred, as configured — held across the "
                   "sweep.\n"
                   "        kpts={\"size\": tuple(kpts), \"gamma\": True},\n";
        else
            out << "        kpts=tuple(kpts),  # the sweep variable\n";
        // The same keyword block every other generated GPAW script uses,
        // minus its kpts= line — that role is taken by the loop variable
        // above. Everything else (mode/cutoff, XC, eigensolver, mixer,
        // convergence targets, spin, Hubbard U, symmetry) is held fixed
        // across the sweep, as a convergence study requires.
        const std::string keywords = AseScriptGenerator::gpawKeywordArguments(
            calculator, "        ");
        std::istringstream lines(keywords);
        std::string line;
        while (std::getline(lines, line)) {
            if (line.find("kpts=") != std::string::npos)
                continue;
            out << line << "\n";
        }
        out << "        txt=f\"gpaw_kpts_{kpts[0]}x{kpts[1]}x{kpts[2]}"
               ".out\",\n"
               "    )\n";
    }
    if (plasma)
        out << plasmaFrequencyHelper();
    out << ""
           "\n"
           "\n"
           "points = []\n"
           "evaluated = []  # per successful point: record + forces + eigenvalues\n"
           "\n"
           "for index, kpts in enumerate(MESHES):\n"
           "    _calango_progress(index, len(MESHES))\n"
           "    record = {\"kpts\": list(kpts),\n"
           "              \"k_per_axis\": int(K_PER_AXIS[index])}\n"
           "    label = \"x\".join(str(k) for k in kpts)\n"
           "    try:\n"
           "        attach_calculator(atoms, kpts)\n"
        << convergence_sweep::measurementBlock(
               "kpts={label}",
               "        # Eigenvalues for the spectral-convergence metric:\n"
               "        # k-averaged band energies. Different meshes sample\n"
               "        # different k-points, so eigenvalues cannot be compared\n"
               "        # k-by-k across the sweep — but each band's BZ average\n"
               "        # can, and its drift with the mesh is exactly the\n"
               "        # sampling error the sweep measures. Weights are\n"
               "        # deliberately ignored: a fingerprint, not a DOS.\n")
        << (plasma ? plasmaMeasurementBlock() : std::string())
        << "    except Exception as error:\n";
    if (config.continueOnFailure) {
        out << "        # One diverging coarse-mesh SCF must not lose the\n"
               "        # rest of the curve.\n"
               "        record[\"error\"] = str(error)\n"
               "        _calango_event(\"error\",\n"
               "                       f\"kpts {label} failed: {error}\")\n"
               "        print(f\"CALANGO_WARN kpts={label} failed: {error}\",\n"
               "              flush=True)\n";
    } else {
        out << "        raise\n";
    }
    out << "    points.append(record)\n"
           "\n"
           "_calango_progress(len(MESHES), len(MESHES))\n"
           "\n"
        << convergence_sweep::analysisBlock(
               "# MESHES ascends in density, so the last successful point is the\n"
               "# densest mesh — the reference the sweep is judged against.\n",
               "mesh")
        << (plasma ? plasmaAnalysisBlock() : std::string())
        << "\n"
           "summary = {\n"
           "    \"formula\": atoms.get_chemical_formula(),\n"
           "    \"natoms\": int(len(atoms)),\n"
           "    \"meshes\": len(points),\n"
           "    \"evaluated\": len(converged),\n"
           "    \"failed\": len(points) - len(converged),\n"
           "    \"reference\": {\n"
           "        \"kpts\": reference[\"kpts\"],\n"
           "        \"k_per_axis\": reference[\"k_per_axis\"],\n"
           "        \"energy_eV\": reference[\"energy_eV\"],\n"
           "        \"energy_per_atom_eV\": reference[\"energy_per_atom_eV\"],\n"
           "        \"fmax_eV_per_A\": reference[\"fmax_eV_per_A\"],\n"
           "    },\n"
           "}\n"
        << (plasma
                ? "# .get, not [\"…\"]: the reference mesh is allowed to be the\n"
                  "# one that produced no omega_p, and the summary should say\n"
                  "# so (null) rather than abort a sweep that otherwise ran.\n"
                  "summary[\"reference\"][\"plasma_frequency_eV\"] = \\\n"
                  "    reference.get(\"plasma_frequency_eV\")\n"
                  "summary[\"plasma_frequency\"] = True\n"
                : "")
        << "\n"
           "with open(results_path, \"w\") as handle:\n"
           "    json.dump({\"summary\": summary, \"points\": points}, handle,\n"
           "              indent=2)\n"
           "print(f\"CALANGO_INFO wrote {results_path}\", flush=True)\n"
           "print(\"CALANGO_RESULT kpoints_convergence=\" + results_path,\n"
           "      flush=True)\n"
           "_calango_event(\"done\",\n"
           "               f\"{len(converged)} of {len(points)} meshes \"\n"
           "               f\"evaluated; reference \"\n"
           "               + \"x\".join(str(k) for k in reference[\"kpts\"]))\n";
    return out.str();
}

} // namespace calango::core
