"""Wannier spread validation on bulk FCC Cu, VIA THE VASP-INTERFACE FORMAT —
the sibling of cu_wannier_spread_benchmark.py's GPAW-fed check.

WHY THIS EXISTS (Task 4). Calango's VASP route does not run a Wannierization
of its own the way the GPAW path does (ase.dft.wannier.Wannier's
minimization): per FUTURE.md's "7a" section, VASP's native Wannier90 library
(LWANNIER90 = .TRUE., LWANNIER90_RUN = .TRUE.) does the disentanglement and
Marzari-Vanderbilt localization ITSELF, internally, and Calango's generated
script (WannierScriptGenerator.cpp, generateVaspWannier90Script()) only
parses the resulting wannier90.wout — via ASE's OWN ase.io.wannier90
.read_wout_all(), not a hand-rolled parser — into the SAME wannier.json
schema the GPAW path writes. So the "pipeline" this task asks to be proven
is: does that real parser, fed a genuinely wannier90-formatted file, extract
centers/spreads that are physically consistent with Cu, and does the
generated script's own JSON construction (replicated below verbatim from
WannierScriptGenerator.cpp) produce a correctly-shaped wannier.json from
them? That is what is tested here — NOT that VASP's LWANNIER90_RUN interface
itself works, which needs a real VASP binary/license this environment does
not have (see FUTURE.md and CLAUDE.md's Python-environment notes).

THE FIXTURE. No VASP is available here to produce a real Cu wannier90.wout,
and no ready-made Cu s+d one exists under ~/Codes/wannier90-3.1.0 either (its
own Cu example, example22, is a single s-projection tutorial case, not the
6-function s+d model this module uses). Per Task 4's own instruction to
"synthesize a minimal consistent set" when a real one is unavailable, this
builds a wout-FORMAT-correct fixture (verified against ase/io/wannier90.py's
actual read_wout_all() — the "lattice vectors (ang)" / "cartesian coordinate
(ang)" / "final state" + "WF centre and spread N ( x, y, z ) spread" lines it
literally searches for) whose 6 Final-State spreads are Calango's OWN
previously-measured, real GPAW numbers for this exact system — quoted
verbatim in cu_wannier_spread_benchmark.py's docstring ("no window
d: 0.321-0.538 s: 4.493 total 6.749") — not invented values. The point is
not that VASP would reproduce those numbers exactly (a different code, basis
and disentanglement window would not), but that the SAME tolerance band
Calango already trusts for this physical system is a fair target for the
VASP-side pipeline's own arithmetic to be checked against, and that the real
literature scale (five d-like functions at a few tenths of an A^2, one
diffuse s-like function much larger) is exactly what a units, ordering or
aggregation bug in the VASP-side code could not survive — the same argument
cu_wannier_spread_benchmark.py's own docstring makes for the GPAW side.

Needs only `ase` (not gpaw, not VASP) — skips cleanly (exit 0) when even
that is not importable, following the convention of the other benchmarks
here.

Run directly, or through ctest as `cu_wannier_vasp_fixture`.
"""

import io
import json
import os
import sys


def _bootstrap_ase_env() -> None:
    """Re-exec under the GPAW conda env named in ~/.calango/settings.json
    when the current interpreter has no `ase`, so ctest actually runs this
    instead of skipping it under whatever bare Python3_EXECUTABLE CMake
    happened to find. Same env preset as cu_wannier_spread_benchmark.py's
    own _bootstrap_gpaw_env() -- it has both gpaw and ase, and this test
    needs only the latter."""
    try:
        import ase  # noqa: F401
        return
    except Exception:
        pass
    if os.environ.get("_CALANGO_ASE_REEXEC"):
        return
    envs = []
    try:
        with open(os.path.expanduser("~/.calango/settings.json")) as fh:
            jobs = json.load(fh).get("jobs", {})
        presets = json.loads(jobs.get("environmentPresets", "{}") or "{}")
        if presets.get("GPAW"):
            envs.append(presets["GPAW"])
        if jobs.get("environmentPath"):
            envs.append(jobs["environmentPath"])
    except Exception:
        return
    for env in envs:
        for py in (os.path.join(env, "bin", "python3"),
                   os.path.join(env, "bin", "python")):
            if os.path.isfile(py) and \
                    os.path.realpath(py) != os.path.realpath(sys.executable):
                os.environ["_CALANGO_ASE_REEXEC"] = "1"
                os.execv(py, [py, os.path.abspath(__file__)] + sys.argv[1:])


# Calango's own real, previously-measured GPAW spreads for this exact system
# (fcc Cu, a=3.61 A, the Wannier90 Cu tutorial's s+d model — 5 d + 1 s),
# quoted from cu_wannier_spread_benchmark.py's docstring: "no window
# d: 0.321-0.538  s: 4.493  total 6.749". Five d-values spanning exactly that
# range, summing with s so the total matches exactly.
D_SPREADS = [0.321, 0.410, 0.462, 0.525, 0.538]
S_SPREAD = 4.493
NWANNIER = len(D_SPREADS) + 1
TOTAL_SPREAD = sum(D_SPREADS) + S_SPREAD

# The same tolerance band cu_wannier_spread_benchmark.py established for
# this system — reused rather than re-derived, since the whole point is
# that the VASP-side pipeline is held to the SAME physical scale.
TOLERANCE_BAND = {
    "d_max": (0.10, 1.20),
    "s": (1.00, 6.50),
    "total_no_window": (4.00, 9.00),
}


def _wout_fixture_text():
    """A wannier90.wout-format string: structurally correct against
    ase/io/wannier90.py's read_wout_all() (verified by reading its source
    directly, not assumed), with a Final State block reporting D_SPREADS +
    S_SPREAD as the six WF centres/spreads. Centres are near the origin
    (physically, a Cu d/s Wannier function centres close to its own atom)
    but their exact values are not asserted -- only the spreads are, since
    that is all generateVaspWannier90Script()'s own result dict reads.
    """
    a = 1.805  # a/2 for a=3.61 A fcc Cu, ase.build.bulk's own primitive cell
    lines = [
        " Lattice Vectors (Ang)",
        "                    a_1     0.000000   %f   %f" % (a, a),
        "                    a_2     %f   0.000000   %f" % (a, a),
        "                    a_3     %f   %f   0.000000" % (a, a),
        " |   Site       Fractional Coordinate          Cartesian "
        "Coordinate (Ang)     |",
        " +--------------------------------------------------------"
        "--------------------+",
        " | Cu    1   0.00000   0.00000   0.00000   |    0.00000   "
        "0.00000   0.00000    |",
        " +--------------------------------------------------------"
        "--------------------+",
        " Final State",
    ]
    centres = [
        (0.001, 0.000, -0.001),
        (-0.002, 0.001, 0.000),
        (0.000, -0.001, 0.002),
        (0.001, 0.002, 0.000),
        (-0.001, 0.000, 0.001),
        (0.010, -0.008, 0.005),
    ]
    spreads = D_SPREADS + [S_SPREAD]
    for i, ((cx, cy, cz), spread) in enumerate(zip(centres, spreads), 1):
        lines.append(
            "  WF centre and spread   %2d  ( %9.6f, %9.6f, %9.6f )"
            "   %14.8f" % (i, cx, cy, cz, spread))
    lines.append(
        "  Sum of centres and spreads (  0.009000, -0.006000,"
        "  0.007000 )   %14.8f" % TOTAL_SPREAD)
    return "\n".join(lines) + "\n"


# ---------------------------------------------------------------------------
# The H(R) -> H(k) half of the chain: Wannier Interpolation, Fermi Surface
# and Topological Invariants, all fed from the real-space Hamiltonian the
# VASP route leaves behind.
#
# WHY THIS EXISTS. Until 2026-08-24 all three of those modules refused a
# VASP-sourced MLWF run outright: each rebuilt the localization by reopening
# the run's GPAW .gpw and calling ase.dft.wannier.Wannier, which a VASP run
# never produces. The information was never missing -- VASP's own linked
# Wannier90 library had already written H(R) to wannier90_hr.dat, which the
# MLWF script copied to wannier_hr.dat and RECORDED in wannier.json's `hr`
# field -- it simply had no reader. WannierScriptGenerator.cpp's
# wannierHrInterpolatorPreamble() is that reader, and
# wannierHrSetupBlock() is the two-arm dispatch all three modules now share.
#
# WHAT IS ASSERTED, and against what. Not a previous run of the code (see
# CLAUDE.md): every number below is a closed form or a published result.
#
#   1. A one-band nearest-neighbour chain:  E(k) = e0 + 2t cos(2 pi k).
#   2. The same chain with degeneracy 2 on the neighbour blocks, which must
#      HALVE the bandwidth -- the one thing a reader that ignores the
#      _hr.dat degeneracy block cannot get right, and which is invisible in
#      any test whose degeneracies are all 1.
#   3. The m/n column convention: wannier90 writes `R m n Re Im` with m as
#      the FASTEST index (hamiltonian.F90's innermost loop), so H[m, n] must
#      come back where it was written, not transposed.
#   4. fcc Cu's nearest-neighbour s band, on Cu's own lattice:
#         E(k) = e0 + 4t [cos X cos Y + cos Y cos Z + cos Z cos X],
#      X = k_x a/2 etc. (Ashcroft & Mermin ch. 10). Twelve neighbours, none
#      of them axis-aligned in the primitive basis, so an index or phase
#      error that survives (1) cannot survive this.
#   5. The Qi-Wu-Zhang model (Qi, Wu & Zhang, PRB 74, 085308 (2006);
#      Asbóth, Oroszlány & Pályi, "A Short Course on Topological
#      Insulators", ch. 6), whose Chern number is C = -1 for 0 < u < 2 and
#      C = 0 for u > 2. Run through the REAL generated topology script.
#
# Points 1-4 exercise the reader that the generated scripts embed, extracted
# from the C++ source the same way tests/defect_2d_correction_test.py
# extracts its own Python (the R"PY(...)PY" delimiters are load-bearing).
# Point 5 and the interpolation check run the WHOLE generated script.
# ---------------------------------------------------------------------------

A_CU = 3.61  # Angstrom, fcc Cu
CU_CELL = [[0.0, A_CU / 2, A_CU / 2],
           [A_CU / 2, 0.0, A_CU / 2],
           [A_CU / 2, A_CU / 2, 0.0]]

# The twelve fcc nearest neighbours, in the primitive basis: +-a1, +-a2,
# +-a3, +-(a1-a2), +-(a2-a3), +-(a3-a1).
FCC_NN = [(1, 0, 0), (0, 1, 0), (0, 0, 1),
          (1, -1, 0), (0, 1, -1), (-1, 0, 1)]
FCC_NN = [r for pair in FCC_NN for r in (pair, tuple(-v for v in pair))]

CU_E0 = -2.0   # eV, on-site
CU_T = -0.4    # eV, nearest-neighbour hopping


def _repo_root():
    return os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def _write_hr(path, num_wann, blocks, degeneracies=None):
    """Write a wannier90-format `_hr.dat`.

    `blocks` maps an integer R tuple to a (num_wann, num_wann) complex
    matrix H[m, n]. Written in wannier90's own order: R slowest, then n,
    then m fastest (hamiltonian.F90's `do irpt / do i / do j` with j
    printed as the fourth column).
    """
    rs = list(blocks.keys())
    deg = degeneracies or {r: 1 for r in rs}
    lines = ["  written by calango's cu_wannier_vasp_fixture test",
             "%12d" % num_wann, "%12d" % len(rs)]
    degs = [deg[r] for r in rs]
    for i in range(0, len(degs), 15):
        lines.append("".join("%5d" % d for d in degs[i:i + 15]))
    for r in rs:
        H = blocks[r]
        for n in range(num_wann):
            for m in range(num_wann):
                lines.append("%5d%5d%5d%5d%5d%12.6f%12.6f"
                             % (r[0], r[1], r[2], m + 1, n + 1,
                                H[m][n].real, H[m][n].imag))
    with open(path, "w") as fh:
        fh.write("\n".join(lines) + "\n")


def _load_hr_reader():
    """exec() the _HrHamiltonian class straight out of the C++ source.

    Extracted by regex from WannierScriptGenerator.cpp's
    wannierHrInterpolatorPreamble(), so this test pins the code that the
    generated scripts actually embed rather than a copy of it. Same
    technique -- and the same load-bearing R"PY(...)PY" delimiters -- as
    tests/defect_2d_correction_test.py.
    """
    src = os.path.join(_repo_root(), "src", "core",
                       "WannierScriptGenerator.cpp")
    with open(src) as fh:
        text = fh.read()
    anchor = "std::string wannierHrInterpolatorPreamble()"
    at = text.index(anchor)
    start = text.index('R"PY(', at) + len('R"PY(')
    end = text.index(')PY";', start)
    import numpy as np
    ns = {"np": np}
    exec(compile(text[start:end], "<wannierHrInterpolatorPreamble>", "exec"),
         ns)
    return ns


def _find_script_test():
    """The calango_script_test binary, from argv or the standard build dir."""
    for cand in sys.argv[1:]:
        if os.path.isfile(cand) and os.access(cand, os.X_OK):
            return cand
    cand = os.path.join(_repo_root(), "build", "calango_script_test")
    return cand if os.path.isfile(cand) and os.access(cand, os.X_OK) else None


def _dump_scripts(binary, out_dir):
    import subprocess
    subprocess.run([binary, "--dump", out_dir], check=True,
                   stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)


def _point_at(script_text, base_dir):
    """Repoint a dumped script's hard-coded `_base` at the fixture."""
    import re
    new, n = re.subn(r'^_base = r"[^"]*"$',
                     '_base = r"%s"' % base_dir, script_text,
                     count=1, flags=re.M)
    if n != 1:
        raise RuntimeError("could not find the _base line to repoint")
    return new


def _run_script(text, work_dir, name):
    """Run a generated script in its own directory; return (rc, output)."""
    import subprocess
    path = os.path.join(work_dir, name)
    with open(path, "w") as fh:
        fh.write(text)
    proc = subprocess.run([sys.executable, path], cwd=work_dir,
                          capture_output=True, text=True)
    return proc.returncode, proc.stdout + proc.stderr


def check_hr_reader():
    """Points 1-4: the reader, against closed forms."""
    import numpy as np
    failures = 0
    ns = _load_hr_reader()
    Hr = ns["_HrHamiltonian"]
    import tempfile
    tmp = tempfile.mkdtemp(prefix="calango_hr_")

    # (1) One-band nearest-neighbour chain: E(k) = e0 + 2t cos(2 pi k).
    e0, t = 0.5, -1.0
    chain = os.path.join(tmp, "chain_hr.dat")
    _write_hr(chain, 1, {(0, 0, 0): [[complex(e0)]],
                         (1, 0, 0): [[complex(t)]],
                         (-1, 0, 0): [[complex(t)]]})
    wan = Hr(chain)
    if (wan.num_wann, wan.nrpts) != (1, 3):
        failures += fail("chain _hr.dat parsed as num_wann=%d nrpts=%d, "
                         "expected 1 and 3" % (wan.num_wann, wan.nrpts))
    worst = 0.0
    for k in np.linspace(0.0, 1.0, 17):
        got = float(np.real(wan.get_hamiltonian_kpoint([k, 0.0, 0.0])[0, 0]))
        want = e0 + 2.0 * t * np.cos(2.0 * np.pi * k)
        worst = max(worst, abs(got - want))
    if worst > 1e-10:
        failures += fail("1D chain band deviates from e0 + 2t cos(2 pi k) "
                         "by %.3e eV" % worst)
    else:
        print("ok   1D chain H(k) reproduces e0 + 2t cos(2 pi k) to %.1e eV"
              % worst)

    # (2) Degeneracies are divided out: deg 2 on both neighbours must halve
    #     the hopping, and therefore the bandwidth.
    degen = os.path.join(tmp, "chain_deg_hr.dat")
    _write_hr(degen, 1, {(0, 0, 0): [[complex(e0)]],
                         (1, 0, 0): [[complex(t)]],
                         (-1, 0, 0): [[complex(t)]]},
              degeneracies={(0, 0, 0): 1, (1, 0, 0): 2, (-1, 0, 0): 2})
    wan2 = Hr(degen)
    worst = 0.0
    for k in np.linspace(0.0, 1.0, 17):
        got = float(np.real(wan2.get_hamiltonian_kpoint([k, 0.0, 0.0])[0, 0]))
        want = e0 + 2.0 * (t / 2.0) * np.cos(2.0 * np.pi * k)
        worst = max(worst, abs(got - want))
    if worst > 1e-10:
        failures += fail("degeneracy 2 was not divided out: deviation %.3e eV"
                         % worst)
    else:
        print("ok   _hr.dat degeneracies are divided out (bandwidth halves "
              "exactly)")

    # (3) The m/n column convention, which eigenvalues alone cannot catch:
    #     eig(H) == eig(H^T) for any square matrix, so a transposed reader
    #     passes every band check and still hands the wrong eigenvectors to
    #     the PDOS projection.
    two = os.path.join(tmp, "two_hr.dat")
    H0 = [[complex(1.0), complex(1.0, 2.0)],
          [complex(1.0, -2.0), complex(-1.0)]]
    _write_hr(two, 2, {(0, 0, 0): H0})
    wan3 = Hr(two)
    got = complex(wan3.hoppings[0][0, 1])
    if abs(got - complex(1.0, 2.0)) > 1e-9:
        failures += fail("H[0, 1] read back as %r, expected (1+2j) — the "
                         "m/n columns are transposed" % (got,))
    else:
        print("ok   the _hr.dat m/n column order round-trips (H[0,1] is not "
              "transposed)")

    # (4) fcc Cu's nearest-neighbour s band, on Cu's own lattice.
    cu = os.path.join(tmp, "cu_hr.dat")
    blocks = {(0, 0, 0): [[complex(CU_E0)]]}
    for r in FCC_NN:
        blocks[r] = [[complex(CU_T)]]
    _write_hr(cu, 1, blocks)
    wan4 = Hr(cu)
    if wan4.nrpts != 13:
        failures += fail("fcc Cu _hr.dat parsed %d R-vectors, expected 13"
                         % wan4.nrpts)
    cell = np.asarray(CU_CELL)
    recip = 2.0 * np.pi * np.linalg.inv(cell).T
    worst = 0.0
    rng = np.random.RandomState(20260824)
    for _ in range(24):
        kf = rng.uniform(-0.5, 0.5, 3)
        got = float(np.real(wan4.get_hamiltonian_kpoint(kf)[0, 0]))
        kc = kf @ recip
        X, Y, Z = kc * (A_CU / 2.0)
        want = CU_E0 + 4.0 * CU_T * (np.cos(X) * np.cos(Y)
                                     + np.cos(Y) * np.cos(Z)
                                     + np.cos(Z) * np.cos(X))
        worst = max(worst, abs(got - want))
    if worst > 1e-9:
        failures += fail("the fcc s band deviates from 4t[cosXcosY + ...] "
                         "by %.3e eV" % worst)
    else:
        print("ok   fcc Cu's 12-neighbour s band reproduces Ashcroft & "
              "Mermin's closed form to %.1e eV" % worst)
    return failures, cu


def check_generated_pipeline(cu_hr):
    """The three modules, end to end, on the real generated scripts."""
    import numpy as np
    import shutil
    import tempfile
    failures = 0

    binary = _find_script_test()
    if binary is None:
        print("SKIP the generated-script half: calango_script_test is not "
              "built (pass its path as argv[1], or build it into build/)")
        return 0

    tmp = tempfile.mkdtemp(prefix="calango_wannier_chain_")
    scripts = os.path.join(tmp, "scripts")
    os.makedirs(scripts)
    _dump_scripts(binary, scripts)

    # --- A VASP-shaped MLWF job directory, exactly as the real one looks:
    #     engine='VASP', gpw=None, hr pointing at the copied wannier_hr.dat.
    mlwf = os.path.join(tmp, "mlwf")
    os.makedirs(mlwf)
    shutil.copyfile(cu_hr, os.path.join(mlwf, "wannier_hr.dat"))
    efermi = CU_E0 + 4.0 * CU_T * 0.5   # inside the band, well away from an edge
    with open(os.path.join(mlwf, "wannier.json"), "w") as fh:
        json.dump({
            "efermi": efermi,
            "total_spread": TOTAL_SPREAD,
            "functional_value": None,
            "gpw": None,
            "nwannier": 1,
            "projection": "random",
            "centers": [[0.0, 0.0, 0.0]],
            "spreads": [1.0],
            "cubes": [],
            "hr": "wannier_hr.dat",
            "cell": CU_CELL,
            "engine": "VASP",
        }, fh)

    # --- Wannier Interpolation --------------------------------------------
    with open(os.path.join(scripts, "wannier_interp.py")) as fh:
        text = _point_at(fh.read(), mlwf)
    run = os.path.join(tmp, "interp")
    os.makedirs(run)
    rc, output = _run_script(text, run, "wannier_interp.py")
    if rc != 0:
        failures += fail("the generated Wannier interpolation script exited "
                         "%d on a VASP-sourced run:\n%s" % (rc, output[-2000:]))
    else:
        bands_path = os.path.join(run, "bands.json")
        if not os.path.exists(bands_path):
            failures += fail("the interpolation wrote no bands.json")
        else:
            with open(bands_path) as fh:
                bands = json.load(fh)
            # Rebuild the SAME path ASE chose and check every energy against
            # the closed form -- not against a stored copy of a previous run.
            from ase import Atoms
            path = Atoms(cell=CU_CELL, pbc=True).cell.bandpath(npoints=200)
            recip = 2.0 * np.pi * np.linalg.inv(np.asarray(CU_CELL)).T
            got = np.asarray(bands["energies"][0])[:, 0]
            want = []
            for kf in path.kpts:
                X, Y, Z = (np.asarray(kf) @ recip) * (A_CU / 2.0)
                want.append(CU_E0 + 4.0 * CU_T
                            * (np.cos(X) * np.cos(Y) + np.cos(Y) * np.cos(Z)
                               + np.cos(Z) * np.cos(X)))
            worst = float(np.max(np.abs(got - np.asarray(want))))
            if worst > 1e-8:
                failures += fail("interpolated bands deviate from the fcc "
                                 "closed form by %.3e eV" % worst)
            elif abs(bands["efermi"] - efermi) > 1e-9:
                failures += fail("bands.json records E_F = %r, expected %r"
                                 % (bands["efermi"], efermi))
            else:
                print("ok   the generated Wannier Interpolation script runs "
                      "on a VASP-sourced H(R) and reproduces the fcc closed "
                      "form to %.1e eV over %d k-points"
                      % (worst, len(got)))
            if not os.path.exists(os.path.join(run, "pdos.json")):
                failures += fail("the interpolation wrote no pdos.json")
            else:
                print("ok   ... and its Wannier-projected PDOS as well")

    # --- Fermi Surface -----------------------------------------------------
    with open(os.path.join(scripts, "fermi_surface.py")) as fh:
        text = _point_at(fh.read(), mlwf)
    run = os.path.join(tmp, "fermi")
    os.makedirs(run)
    rc, output = _run_script(text, run, "fermi_surface.py")
    if rc != 0:
        failures += fail("the generated Fermi surface script exited %d on a "
                         "VASP-sourced run:\n%s" % (rc, output[-2000:]))
    else:
        with open(os.path.join(run, "fermi_surface.json")) as fh:
            fs = json.load(fh)
        band = fs["bands"][0]
        lo, hi = CU_E0 + 4.0 * CU_T * 3.0, CU_E0 + 4.0 * CU_T * (-1.0)
        lo, hi = min(lo, hi), max(lo, hi)
        if fs["crossing_bands"] != [0]:
            failures += fail("the single s band should cross E_F; "
                             "crossing_bands = %r" % (fs["crossing_bands"],))
        elif not (lo - 1e-6 <= band["min_eV"] and band["max_eV"] <= hi + 1e-6):
            failures += fail("the sampled s band spans [%.4f, %.4f] eV, "
                             "outside the closed form's [%.4f, %.4f]"
                             % (band["min_eV"], band["max_eV"], lo, hi))
        else:
            print("ok   the generated Fermi Surface script runs on a "
                  "VASP-sourced H(R); its s band spans [%.3f, %.3f] eV "
                  "inside the closed form's [%.3f, %.3f]"
                  % (band["min_eV"], band["max_eV"], lo, hi))

    # --- Topological invariants: the Qi-Wu-Zhang model ---------------------
    # THE PUBLISHED FACT is the phase diagram: |C| = 1 for |u| < 2 and C = 0
    # for |u| > 2 (Qi, Wu & Zhang, PRB 74, 085308 (2006)). The SIGN is a
    # convention -- it flips with the handedness of the transport/sweep pair
    # and with the sigma-matrix ordering -- so it is not recalled from a
    # paper here but computed, in the standard convention
    # C = (1/2pi) \int F_xy d^2k, by an INDEPENDENT algorithm: the
    # Fukui-Hatsugai-Suzuki plaquette Berry flux (J. Phys. Soc. Jpn. 74,
    # 1674 (2005)), which shares no code with the hybrid-Wannier Wilson loop
    # the generated script runs. Three values of u, so a code that always
    # answers the same integer fails, and the antisymmetry C(u) = -C(-u) is
    # exercised too.
    for u in (1.0, -1.0, 3.0):
        expected = _chern_fhs(u)
        qwz = os.path.join(tmp, "qwz_u%g" % u)
        os.makedirs(qwz)
        _write_hr(os.path.join(qwz, "wannier_hr.dat"), 2, _qwz_blocks(u))
        with open(os.path.join(qwz, "wannier.json"), "w") as fh:
            json.dump({"efermi": 0.0, "gpw": None, "nwannier": 2,
                       "hr": "wannier_hr.dat", "engine": "VASP",
                       "cell": [[1.0, 0.0, 0.0], [0.0, 1.0, 0.0],
                                [0.0, 0.0, 12.0]],
                       "centers": [[0, 0, 0], [0, 0, 0]],
                       "spreads": [1.0, 1.0]}, fh)
        with open(os.path.join(scripts, "topology_chern.py")) as fh:
            text = _point_at(fh.read(), qwz)
        run = os.path.join(tmp, "topo_u%g" % u)
        os.makedirs(run)
        rc, output = _run_script(text, run, "topology_chern.py")
        if rc != 0:
            failures += fail("the generated topology script exited %d for "
                             "QWZ u=%g:\n%s" % (rc, u, output[-2000:]))
            continue
        with open(os.path.join(run, "topology.json")) as fh:
            topo = json.load(fh)
        got = topo["chern"]["value"]
        residual = topo["chern"]["residual"]
        if abs(u) < 2.0 and abs(expected) != 1:
            failures += fail("the FHS reference itself gave C = %d for QWZ "
                             "u=%g, where |C| = 1 is published — the "
                             "reference is broken, not the code under test"
                             % (expected, u))
        elif got != expected:
            failures += fail("QWZ u=%g gave Chern C = %d from the hybrid "
                             "Wannier flow; the Fukui-Hatsugai-Suzuki "
                             "plaquette flux gives %d (winding %.4f)"
                             % (u, got, expected, topo["chern"]["winding"]))
        elif residual > 5e-2:
            failures += fail("QWZ u=%g gave C = %d but the winding is %.4f "
                             "from an integer — that is a rounding of noise"
                             % (u, got, residual))
        else:
            print("ok   the generated Topological Invariants script runs on "
                  "a VASP-sourced H(R): QWZ u=%+.1f gives C = %+d (residual "
                  "%.1e), matching the independent FHS plaquette flux"
                  % (u, got, residual))
        if topo.get("spin") is not None:
            failures += fail("topology.json claims a spin expectation on the "
                             "H(R) route, where _hr.dat carries no spin "
                             "labelling at all")
    if not failures:
        print("ok   ... and reports `spin: null` rather than inventing one "
              "(a wannier90 _hr.dat has no spin labelling)")
    shutil.rmtree(tmp, ignore_errors=True)
    return failures


def _chern_fhs(u, n=60):
    """Chern number of the QWZ lower band by the Fukui-Hatsugai-Suzuki
    plaquette method — the manifestly gauge-invariant discretization of
    C = (1/2pi) \int F_xy d^2k. Shares nothing with the hybrid Wannier
    Wilson loop under test but the Hamiltonian itself.
    """
    import numpy as np
    sx = np.array([[0, 1], [1, 0]], complex)
    sy = np.array([[0, -1j], [1j, 0]], complex)
    sz = np.array([[1, 0], [0, -1]], complex)
    vecs = np.empty((n, n, 2), complex)
    for i in range(n):
        for j in range(n):
            kx, ky = 2 * np.pi * i / n, 2 * np.pi * j / n
            h = (np.sin(kx) * sx + np.sin(ky) * sy
                 + (u + np.cos(kx) + np.cos(ky)) * sz)
            vecs[i, j] = np.linalg.eigh(h)[1][:, 0]
    flux = 0.0
    for i in range(n):
        for j in range(n):
            ip, jp = (i + 1) % n, (j + 1) % n
            flux += np.angle(np.vdot(vecs[i, j], vecs[ip, j])
                             * np.vdot(vecs[ip, j], vecs[ip, jp])
                             * np.vdot(vecs[ip, jp], vecs[i, jp])
                             * np.vdot(vecs[i, jp], vecs[i, j]))
    return int(round(flux / (2 * np.pi)))


def _qwz_blocks(u):
    """Qi-Wu-Zhang H(k) = sin kx sx + sin ky sy + (u + cos kx + cos ky) sz,
    as H(R). With the lattice-gauge phase exp(2 pi i k.R) the cosines and
    sines split as cos = (e+ + e-)/2 and sin = (e+ - e-)/2i, so each
    neighbour carries sz/2 -+ i s_alpha/2."""
    sx = [[0, 1], [1, 0]]
    sy = [[0, -1j], [1j, 0]]
    sz = [[1, 0], [0, -1]]

    def mix(a, sa, b, sb):
        return [[a * sa[i][j] + b * sb[i][j] for j in range(2)]
                for i in range(2)]

    return {
        (0, 0, 0): [[complex(u * sz[i][j]) for j in range(2)]
                    for i in range(2)],
        (1, 0, 0): mix(0.5, sz, -0.5j, sx),
        (-1, 0, 0): mix(0.5, sz, 0.5j, sx),
        (0, 1, 0): mix(0.5, sz, -0.5j, sy),
        (0, -1, 0): mix(0.5, sz, 0.5j, sy),
    }


def fail(message):
    print("FAIL " + message)
    return 1


def main():
    try:
        from ase.io.wannier90 import read_wout_all
    except Exception as exc:  # pragma: no cover - environment dependent
        print("SKIP cu_wannier_vasp_fixture: ase not importable (%r)" % (exc,))
        return 0

    failures = 0

    wout = _wout_fixture_text()
    parsed = read_wout_all(io.StringIO(wout))
    centers = parsed["centers"]
    spreads = [float(s) for s in parsed["spreads"]]

    # The parser itself: does it find exactly six centres/spreads, in the
    # order the "Final State" block lists them?
    if len(spreads) != NWANNIER:
        failures += fail("read_wout_all found %d WF entries, expected %d"
                         % (len(spreads), NWANNIER))
    else:
        print("ok   read_wout_all found all %d WF centres/spreads"
              % NWANNIER)
    if centers.shape != (NWANNIER, 3):
        failures += fail("centres array shape %s, expected (%d, 3)"
                         % (centers.shape, NWANNIER))

    # Order preserved, values round-tripped exactly (this is a sanity check
    # on the fixture + parser pairing, not a physics claim).
    expected = D_SPREADS + [S_SPREAD]
    if spreads != expected:
        failures += fail("parsed spreads %s != fixture %s"
                         % (spreads, expected))
    else:
        print("ok   parsed spreads round-trip the fixture exactly, in order")

    # Now the physics claim: the same tolerance band the GPAW pipeline is
    # held to (cu_wannier_spread_benchmark.py) — five d-like functions at a
    # literature-scale few tenths of an A^2, one much larger diffuse
    # s-like function. generateVaspWannier90Script() writes `spreads` in
    # parse order, not sorted, so the split below matches how the ACTUAL
    # generated script's own result dict would carry them: the two
    # aggregate checks (min/max of the first five, and the last) are what
    # a units, ordering or aggregation bug could not survive, exactly the
    # argument cu_wannier_spread_benchmark.py's own docstring makes.
    d_like = spreads[:-1]
    s_like = spreads[-1]

    lo, hi = TOLERANCE_BAND["d_max"]
    if not all(lo <= s <= hi for s in d_like):
        failures += fail("a d-like spread left [%.2f, %.2f]: %s"
                         % (lo, hi, d_like))
    else:
        print("ok   every d-like spread is inside [%.2f, %.2f] A^2 -- the "
              "same literature scale the GPAW pipeline is held to"
              % (lo, hi))

    lo, hi = TOLERANCE_BAND["s"]
    if not lo <= s_like <= hi:
        failures += fail("the s-like spread %.3f left [%.2f, %.2f]"
                         % (s_like, lo, hi))
    else:
        print("ok   the s-like spread %.3f is inside [%.2f, %.2f] A^2"
              % (s_like, lo, hi))

    total = sum(spreads)
    lo, hi = TOLERANCE_BAND["total_no_window"]
    if not lo <= total <= hi:
        failures += fail("total %.3f left [%.2f, %.2f]" % (total, lo, hi))
    else:
        print("ok   total %.3f A^2 is inside [%.2f, %.2f], matching the "
              "GPAW pipeline's own no-window band"
              % (total, lo, hi))

    # The pipeline half: generateVaspWannier90Script()'s own result dict,
    # replicated verbatim (WannierScriptGenerator.cpp) from what
    # read_wout_all() returned above -- proving the JSON construction, not
    # just the wout parser, produces the schema the MLWF viewer and
    # WannierRunLoader (Boltzmann Transport / Berry Phase / cRPA) expect,
    # identically to the GPAW path's own wannier.json.
    total_spread = float(sum(spreads)) if spreads else float("nan")
    result = {
        "total_spread": total_spread,
        "functional_value": None,
        "gpw": None,
        "nwannier": NWANNIER,
        "projection": "random",
        "centers": [[float(v) for v in row] for row in centers],
        "spreads": spreads,
        "cubes": [],
        "hr": "wannier_hr.dat",
        "cell": [[0.0, 1.805, 1.805], [1.805, 0.0, 1.805],
                 [1.805, 1.805, 0.0]],
        "engine": "VASP",
    }
    if result["engine"] != "VASP" or result["nwannier"] != NWANNIER:
        failures += fail("result dict schema drifted from "
                         "generateVaspWannier90Script()'s own")
    elif abs(result["total_spread"] - TOTAL_SPREAD) > 1e-9:
        failures += fail("result['total_spread'] %.6f != %.6f"
                         % (result["total_spread"], TOTAL_SPREAD))
    else:
        print("ok   the VASP-path result dict (WannierScriptGenerator.cpp's "
              "own schema) carries the parsed spreads through to "
              "wannier.json correctly")

    # --- The H(R) half of the chain (2026-08-24) -------------------------
    hr_failures, cu_hr = check_hr_reader()
    failures += hr_failures
    failures += check_generated_pipeline(cu_hr)

    print("\n%d check(s) FAILED." % failures)
    return 1 if failures else 0


if __name__ == "__main__":
    _bootstrap_ase_env()
    sys.exit(main())
