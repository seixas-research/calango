#!/usr/bin/env python3
"""Validation benchmark for the Raman & IR Spectroscopy module on MgO.

Rock-salt MgO is the right system to validate this module against, for one
reason above all: it is CENTROSYMMETRIC (Fm-3m, point group O_h), and the
mutual exclusion rule then says its single triply-degenerate optical mode
(T_1u) is infrared-active and Raman-INACTIVE at first order.

That gives a two-sided test no fitted parameter can fake:

    IR    must be strong, at the known TO frequency;
    Raman must vanish, by symmetry, on the very same mode.

A module that computed IR correctly but had, say, a transposed index in the
polarizability contraction would light that mode up in the Raman spectrum and
be caught here. A module that returned zeros everywhere would fail the IR side.
Reference values:

    Z*(Mg) = +1.96, Z*(O) = -1.96   (Born charges; the famous ~2 % anomaly)
    TO(Gamma) ~ 401 cm^-1 (experiment), ~380-395 cm^-1 in PBE
    eps_inf = 2.96 (experiment), ~3.1 in PBE-RPA
    Raman activity of T_1u = 0 exactly, by inversion symmetry

The settings below are deliberately UNCONVERGED so the test runs in a couple of
minutes, and the TO window is loose to match. That is a speed choice, not the
module's accuracy — measured on this implementation:

    400 eV, 4x4x4, a=4.26   P = +60.0 GPa   TO = 348.7 cm^-1   (these settings)
    600 eV, 6x6x6, a=4.26   P =  +5.5 GPa   TO = 361.8 cm^-1
    600 eV, 6x6x6, a=4.22   P =  +1.2 GPa   TO = 388.8 cm^-1   (PBE minimum)

i.e. converged and at its own functional's lattice constant, the module lands
in the PBE literature range of 380-390 cm^-1. The 60 GPa residual pressure at
the benchmark's own settings is what softens it to 349. Widen the grid and
cutoff here if this is ever repurposed as an accuracy reference rather than a
regression guard.

What is actually exercised is the SHIPPED generated script: this dumps
``raman_ir.py`` from the C++ generator via ``calango_script_test --dump`` and
runs it with its three inherited-input constants redirected at the artefacts
produced here. Only those constants are rewritten -- every line of physics is
the generator's own -- so a regression in the generated code fails this test.

Skips cleanly (exit 0) without GPAW. Runs in a temporary directory.

Usage:  raman_ir_mgo_benchmark.py <calango_script_test binary>
"""
import json
import os
import re
import subprocess
import sys
import tempfile

# PBE relaxes rock-salt MgO to ~4.26 A (experiment 4.212 A); using the PBE
# value keeps the phonon near its own functional's minimum, so the frequency is
# not contaminated by a residual pressure.
LATTICE_A = 4.26
ECUT_EV = 400.0
KPTS = (4, 4, 4)
XC = "PBE"
DELTA = 0.01

# Windows, not point values: this is a real DFT calculation at a modest cutoff
# and k-grid, and the test exists to catch a broken module, not to referee the
# third decimal of a converged number.
TO_MIN_CM, TO_MAX_CM = 300.0, 460.0
ZSTAR_MIN, ZSTAR_MAX = 1.6, 2.3
# With the translational sum rule imposed the acoustic branch is zero to
# machine precision; the allowance here is for the residual asymmetry of a
# finite-difference Hessian, not for the tens of cm^-1 an uncorrected one shows.
ACOUSTIC_MAX_CM = 5.0
# The Raman activity of a mode forbidden by inversion symmetry is zero up to
# the finite-difference noise floor. Scaled against the IR intensity of the
# same mode so the bound means "negligible beside the signal that IS there",
# not an absolute number in units nothing else here uses.
RAMAN_LEAK_FRACTION = 0.05


def _bootstrap_gpaw_env() -> None:
    """Re-exec under the GPAW conda env from ~/.calango/settings.json when the
    current interpreter (e.g. CTest's embedded python) has no GPAW, so the
    benchmark really runs. No-op if GPAW is importable or no env is
    configured (main() then SKIPs cleanly)."""
    try:
        import gpaw  # noqa: F401
        return
    except Exception:
        pass
    if os.environ.get("_CALANGO_GPAW_REEXEC"):
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
                os.environ["_CALANGO_GPAW_REEXEC"] = "1"
                os.execv(py, [py, os.path.abspath(__file__)] + sys.argv[1:])


def dump_generated_scripts(binary, directory):
    """Write the generator's scripts out with `calango_script_test --dump`."""
    subprocess.run([binary, "--dump", directory], check=True,
                   stdout=subprocess.DEVNULL)


def configure_raman_script(path, baseline, born_charges, compute_raman):
    """Point the shipped script at real inputs.

    Only the four top-level constants the generator emits as plain assignments
    are rewritten. Everything below them -- the vibrational run, the Z*
    contraction, the response loop, the spectra -- is the generated code
    verbatim, which is the whole point of running this rather than a
    transcription of it.
    """
    source = open(path).read()
    substitutions = {
        "BASELINE": repr(baseline),
        "BORN_CHARGES": repr(born_charges),
        "COMPUTE_RAMAN": "True" if compute_raman else "False",
    }
    for name, value in substitutions.items():
        source, count = re.subn(rf"^{name} = .*$", f"{name} = {value}", source,
                                count=1, flags=re.MULTILINE)
        if count != 1:
            raise SystemExit(
                f"generated script has no top-level `{name} = ...` line — the "
                "generator's input block was restructured and this benchmark "
                "can no longer redirect it")
    open(path, "w").write(source)


def run_born_charges(atoms, workdir):
    """Z* by the same Berry-phase central difference the Born Charges module
    uses, writing the born_charges.json schema the Raman/IR script reads."""
    import numpy as np
    from gpaw import GPAW, PW
    try:
        from gpaw.berryphase import polarization_phase as phase_fn
    except ImportError:
        from gpaw.berryphase import get_polarization_phase as phase_fn

    cell = np.array(atoms.get_cell())
    reference = atoms.get_positions().copy()
    born = np.zeros((len(atoms), 3, 3))

    def phase(displaced, tag):
        calc = GPAW(mode=PW(ECUT_EV), xc=XC, kpts=KPTS, symmetry="off",
                    txt=f"gpaw_born_{tag}.txt")
        displaced.calc = calc
        displaced.get_potential_energy()
        result = phase_fn(calc=calc)
        if isinstance(result, dict):
            result = result["phase_c"]
        return np.asarray(result, dtype=float)

    for index in range(len(atoms)):
        for axis in range(3):
            phases = []
            for sign in (+1, -1):
                positions = reference.copy()
                positions[index, axis] += sign * DELTA
                moved = atoms.copy()
                moved.set_positions(positions)
                phases.append(
                    phase(moved, f'{index}{axis}{"p" if sign > 0 else "m"}'))
            # The Berry phase is defined modulo 2*pi; the two displaced runs
            # routinely land on different branches, and a raw subtraction then
            # gives a Z* out by an integer number of polarization quanta.
            dphi = (phases[0] - phases[1] + np.pi) % (2.0 * np.pi) - np.pi
            born[index, :, axis] = (dphi / (2.0 * np.pi)) @ cell / (2.0 * DELTA)

    born -= born.sum(axis=0) / len(atoms)      # acoustic sum rule
    symbols = atoms.get_chemical_symbols()
    summary = {
        "displacement_A": DELTA,
        "acoustic_sum_rule": True,
        "volume_A3": float(atoms.get_volume()),
        "atoms": [
            {
                "index": i,
                "symbol": symbols[i],
                "tensor": [[float(v) for v in row] for row in born[i]],
                "raw_tensor": [[float(v) for v in row] for row in born[i]],
                "isotropic": float(np.trace(born[i]) / 3.0),
                "eigenvalues": sorted(
                    float(v) for v in np.linalg.eigvals(
                        0.5 * (born[i] + born[i].T)).real),
            }
            for i in range(len(atoms))
        ],
    }
    path = os.path.join(workdir, "born_charges.json")
    with open(path, "w") as handle:
        json.dump(summary, handle, indent=2)
    return path, born


def main() -> int:
    if len(sys.argv) < 2:
        print("SKIP: no calango_script_test binary given")
        return 0
    # Absolute before the chdir below, or a relative path handed in from the
    # build tree stops resolving once the run moves into its temp directory.
    binary = os.path.abspath(sys.argv[1])
    if not os.path.isfile(binary):
        print(f"SKIP: {binary} does not exist (build calango_script_test first)")
        return 0
    try:
        import numpy as np
        from ase.build import bulk
        from gpaw import GPAW, PW
        from gpaw.response.df import DielectricFunction  # noqa: F401
    except Exception as exc:
        print(f"SKIP: GPAW stack not available ({exc})")
        return 0

    compute_raman = os.environ.get("CALANGO_MGO_SKIP_RAMAN") is None
    workdir = tempfile.mkdtemp(prefix="calango_raman_mgo_")
    cwd = os.getcwd()
    os.chdir(workdir)
    try:
        atoms = bulk("MgO", "rocksalt", a=LATTICE_A)
        assert len(atoms) == 2, "rock-salt MgO must have 2 atoms per primitive cell"

        # 1. Ground-state baseline — what the Single-Point wizard produces.
        print("[1/4] SCF baseline …", flush=True)
        calc = GPAW(mode=PW(ECUT_EV), xc=XC, kpts=KPTS, symmetry="off",
                    txt="gpaw_scf.txt")
        atoms.calc = calc
        energy = atoms.get_potential_energy()
        baseline = os.path.join(workdir, "single_point.gpw")
        calc.write(baseline, mode="all")
        print(f"      E = {energy:.4f} eV", flush=True)

        # 2. Born effective charges — what the Born Charges module produces.
        print("[2/4] Born effective charges (12 SCF runs) …", flush=True)
        born_path, born = run_born_charges(atoms, workdir)
        z_mg = float(np.trace(born[0]) / 3.0)
        z_o = float(np.trace(born[1]) / 3.0)
        print(f"      Z*(Mg) = {z_mg:+.3f} e   Z*(O) = {z_o:+.3f} e", flush=True)

        # 3. The module under test, run as shipped.
        print(f"[3/4] Raman/IR module (Raman {'on' if compute_raman else 'off'}) …",
              flush=True)
        dump_generated_scripts(binary, workdir)
        script = os.path.join(workdir, "raman_ir.py")
        configure_raman_script(script, baseline, born_path, compute_raman)
        # The generated script imports the staged logger module, which --dump
        # writes alongside it; running from workdir puts it on sys.path.
        subprocess.run([sys.executable, script], check=True, cwd=workdir)

        # 4. Verification against the known physics of rock-salt MgO.
        print("[4/4] Verification", flush=True)
        with open(os.path.join(workdir, "raman_ir.json")) as handle:
            results = json.load(handle)
        modes = results["modes"]
        assert len(modes) == 6, f"2 atoms must give 6 modes, got {len(modes)}"

        frequencies = np.array([m["frequency_cm"] for m in modes])
        ir = np.array([m["ir_intensity_D2_A2_amu"] for m in modes])
        raman = np.array([m["raman_activity_A4_amu"] for m in modes])
        order = np.argsort(frequencies)
        frequencies, ir, raman = frequencies[order], ir[order], raman[order]
        for f, a, r in zip(frequencies, ir, raman):
            print(f"      {f:8.2f} cm^-1   IR {a:10.4f}   Raman {r:10.4f}")

        # The acoustic branch is the three lowest modes BY CONSTRUCTION — a
        # 3N-mode spectrum always has exactly three of them — so they are
        # identified by rank and then checked to be where they belong, rather
        # than classified by a frequency cut that would silently reclassify the
        # very defect it should catch.
        acoustic = np.zeros(len(frequencies), dtype=bool)
        acoustic[np.argsort(np.abs(frequencies))[:3]] = True
        optical = ~acoustic
        assert np.all(np.abs(frequencies[acoustic]) < ACOUSTIC_MAX_CM), (
            f"the acoustic branch must sit at zero — a rigid translation costs "
            f"no energy — but the three lowest modes are "
            f"{np.round(frequencies[acoustic], 1)} cm^-1. The translational sum "
            f"rule is not being imposed on the Hessian.")

        # -- Born charges -----------------------------------------------------
        assert ZSTAR_MIN < abs(z_mg) < ZSTAR_MAX, (
            f"Z*(Mg) = {z_mg:+.3f} e is outside [{ZSTAR_MIN}, {ZSTAR_MAX}]")
        assert z_mg > 0 > z_o, "the cation must carry the positive Z*"
        assert abs(z_mg + z_o) < 0.1, (
            f"Z*(Mg) + Z*(O) = {z_mg + z_o:+.3f} e violates the acoustic sum rule")

        # -- TO frequency -----------------------------------------------------
        to_frequencies = frequencies[optical]
        assert np.all((TO_MIN_CM < to_frequencies) & (to_frequencies < TO_MAX_CM)), (
            f"optical modes {to_frequencies} cm^-1 outside the expected TO "
            f"window [{TO_MIN_CM}, {TO_MAX_CM}]")
        # O_h symmetry makes the three optical branches degenerate at Gamma.
        spread = float(to_frequencies.max() - to_frequencies.min())
        assert spread < 25.0, (
            f"the T_1u mode must be triply degenerate; spread is {spread:.1f} cm^-1")

        # -- IR activity ------------------------------------------------------
        assert np.all(ir[optical] > 1.0), (
            f"the T_1u mode is IR-active; got intensities {ir[optical]}")
        assert np.all(ir[acoustic] < 0.05 * ir[optical].max()), (
            f"rigid translations must be IR-dark; got {ir[acoustic]}")

        # -- Raman silence (the mutual exclusion rule) ------------------------
        if compute_raman:
            assert results["raman"]["computed"], "the Raman half did not run"
            leak = float(raman[optical].max())
            reference = float(ir[optical].max())
            assert leak < RAMAN_LEAK_FRACTION * reference, (
                f"rock-salt MgO is centrosymmetric, so its T_1u mode is "
                f"Raman-FORBIDDEN, but the module reports an activity of "
                f"{leak:.4f} A^4/amu against an IR intensity of {reference:.4f} "
                f"— above the {RAMAN_LEAK_FRACTION:.0%} noise allowance. "
                f"Something in the polarizability path is not respecting "
                f"inversion symmetry.")
            print(f"      Raman leakage {leak:.4g} vs IR {reference:.4g} "
                  f"({leak / reference:.2%} of it) — forbidden, as required")

        print(f"PASS: MgO rock-salt — Z*(Mg)={z_mg:+.2f} e, "
              f"TO={to_frequencies.mean():.1f} cm^-1 (3-fold degenerate), "
              f"IR-active and Raman-{'silent' if compute_raman else 'skipped'}.")
        return 0
    finally:
        os.chdir(cwd)


if __name__ == "__main__":
    _bootstrap_gpaw_env()
    sys.exit(main())
