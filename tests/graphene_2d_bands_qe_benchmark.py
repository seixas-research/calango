#!/usr/bin/env python3
"""2D Bands on Quantum ESPRESSO, RUN against real graphene.

Task 3 extended '2D Bands' from GPAW-only to also cover VASP, Quantum
ESPRESSO and SIESTA. Of those three, this is the one this machine can
actually run end to end without a license (VASP) or without hitting an
unrelated, pre-existing problem with the local `siesta` binary itself,
which SIGKILLs even on `siesta --version` — confirmed separately from this
script, and unrelated to anything the generator emits.

This is not a static check. It runs the ACTUAL GENERATED SCRIPT against a
real pw.x and a real carbon pseudopotential, on monolayer graphene — the
system Calango's own graphene_tetrahedron_benchmark.py uses for the same
reason: its physics is well known enough to sanity-check a result against.
Two real bugs were found this way and are now regression-tested here:

  * `K_POINTS crystal` needs an EXPLICIT (N, 4) array (kx, ky, kz, weight).
    An (N, 3) array is silently misread as a Monkhorst-Pack grid SHAPE by
    ase.io.espresso's writer instead of raising — the kind of mistake a
    static string check on the generated script cannot catch, because the
    generated line looks entirely reasonable either way.
  * A `calculation: "bands"` run populates every eigenvalue but reports no
    total energy — QE has nothing self-consistent left to converge — so
    the ordinary `atoms.get_potential_energy()` idiom every other backend
    uses to trigger its own calculation raises
    `PropertyNotImplementedError` here specifically, straight past a
    successful pw.x run, unless the generator catches it by name.

What is checked, beyond "it completed": the JSON schema every engine
shares, a sane Fermi level and energy scale, and every kept band actually
falling in the requested window around it.

Self-skips (exit 0) without pw.x, an ase-capable interpreter, or a carbon
pseudopotential — set CALANGO_QE_PWX / CALANGO_QE_PSEUDO_DIR /
CALANGO_ASE_PYTHON to point at them explicitly, or the test looks in a few
plausible places (mirrors xtb_integration_test.py's discovery pattern).

Usage:  graphene_2d_bands_qe_benchmark.py <calango_script_test binary>
"""
import json
import os
import pathlib
import re
import shutil
import subprocess
import sys
import tempfile

# Kept small deliberately: this is a generator/pipeline correctness check,
# not a converged DFT result — PROBE_EV below is not compared against
# anything physics-derived, only against the schema and the energy window
# the run itself was asked to keep.
GRID_SAMPLES = 6
ECUTWFC_RY = 20.0

failures = 0


def check(condition, what):
    global failures
    print(f"  {'ok  ' if condition else 'FAIL'} {what}")
    if not condition:
        failures += 1


def candidate_interpreters():
    explicit = os.environ.get("CALANGO_ASE_PYTHON")
    if explicit:
        yield explicit
    yield sys.executable
    home = pathlib.Path.home()
    roots = [home / "miniconda3" / "envs", home / "anaconda3" / "envs",
             home / "miniforge3" / "envs", home / "mambaforge" / "envs",
             pathlib.Path("/opt/miniconda3/envs"),
             pathlib.Path("/opt/anaconda3/envs")]
    prefix = os.environ.get("CONDA_PREFIX")
    if prefix:
        roots.insert(0, pathlib.Path(prefix).parent)
    for root in roots:
        if not root.is_dir():
            continue
        for env in sorted(root.iterdir()):
            exe = env / "bin" / "python"
            if exe.is_file():
                yield str(exe)


def find_ase_python():
    for exe in candidate_interpreters():
        try:
            done = subprocess.run(
                [exe, "-c", "import ase.io, ase.build, ase.calculators.espresso; "
                            "print('yes')"],
                capture_output=True, timeout=60)
        except (OSError, subprocess.SubprocessError):
            continue
        if done.returncode == 0 and b"yes" in done.stdout:
            return exe
    return None


def find_pw_x():
    explicit = os.environ.get("CALANGO_QE_PWX")
    if explicit and pathlib.Path(explicit).is_file():
        return explicit
    for candidate in (shutil.which("pw.x"),
                      os.path.expanduser(
                          "~/Codes/quantum_espresso/qe-7.5/install/bin/pw.x")):
        if candidate and pathlib.Path(candidate).is_file():
            return candidate
    return None


def find_carbon_pseudopotential():
    """(directory, filename) for a carbon UPF, or (None, None)."""
    explicit = os.environ.get("CALANGO_QE_PSEUDO_DIR")
    search_roots = [explicit] if explicit else [
        "~/Library/CloudStorage/Dropbox/Codes/QuantumESPRESSO/pseudos",
        "~/Codes/quantum_espresso/qe-7.5/pseudo",
    ]
    for root in search_roots:
        if not root:
            continue
        directory = pathlib.Path(os.path.expanduser(root))
        if not directory.is_dir():
            continue
        matches = sorted(directory.glob("C.pbe*.UPF")) or sorted(directory.glob("C.*UPF"))
        if matches:
            return str(directory), matches[0].name
    return None, None


def main():
    if len(sys.argv) < 2:
        raise SystemExit(
            "usage: graphene_2d_bands_qe_benchmark.py <calango_script_test>")
    binary = sys.argv[1]

    python = find_ase_python()
    pw_x = find_pw_x()
    pseudo_dir, pseudo_file = find_carbon_pseudopotential()
    if python is None or pw_x is None or pseudo_dir is None:
        print("2D Bands / QE: skipping — need an ase-capable interpreter, "
              "pw.x, and a carbon pseudopotential\n"
              f"  ase python:  {python or 'not found'}\n"
              f"  pw.x:        {pw_x or 'not found'}\n"
              f"  C pseudo:    {pseudo_dir + '/' + pseudo_file if pseudo_dir else 'not found'}\n"
              "Set CALANGO_ASE_PYTHON / CALANGO_QE_PWX / CALANGO_QE_PSEUDO_DIR "
              "to point at them explicitly.")
        return 0
    print(f"ase interpreter: {python}\npw.x: {pw_x}\n"
          f"pseudopotential: {pseudo_dir}/{pseudo_file}\n")

    with tempfile.TemporaryDirectory() as tmp:
        tmp = pathlib.Path(tmp)
        scripts = tmp / "scripts"
        scripts.mkdir()
        subprocess.run([binary, "--dump", str(scripts)], check=True,
                       stdout=subprocess.DEVNULL, timeout=300)

        job = tmp / "job"
        job.mkdir()
        build = subprocess.run(
            [python, "-c",
             "from ase.build import graphene\nfrom ase.io import write\n"
             f"write(r'{job / 'structure.extxyz'}', graphene(vacuum=15.0))"],
            capture_output=True, text=True, timeout=60)
        check(build.returncode == 0 and (job / "structure.extxyz").is_file(),
              f"fixture: monolayer graphene built ({build.stderr.strip()[-200:]})")
        if build.returncode != 0:
            print(f"\n{failures} check(s) FAILED.")
            return 1

        script = (scripts / "bands_2d_qe.py").read_text()
        script = script.replace(
            'command="mpirun -np 4 pw.x",          # EDIT ME',
            f'command={pw_x!r},')
        script = script.replace(
            'pseudo_dir="/path/to/pseudopotentials",  # EDIT ME',
            f'pseudo_dir={pseudo_dir!r},')
        script = script.replace(
            '    # "Si": "Si.pbe-n-rrkjus_psl.1.0.0.UPF",  # EDIT ME: one entry per element\n',
            f'    "C": {pseudo_file!r},\n')
        script = script.replace('_n = 24', f'_n = {GRID_SAMPLES}')
        script = re.sub(r'_system = \{"ecutwfc": [\d.]+\}',
                        f'_system = {{"ecutwfc": {ECUTWFC_RY}}}', script)
        check('_n = ' + str(GRID_SAMPLES) in script and
              f'"ecutwfc": {ECUTWFC_RY}' in script,
              "the cheap-run parameters actually landed in the script "
              "(a text substitution silently matching nothing would "
              "otherwise run the expensive default unnoticed)")
        (job / "run.py").write_text(script)

        done = subprocess.run([python, "run.py"], cwd=job,
                              capture_output=True, text=True, timeout=280)
        check(done.returncode == 0,
              f"the generated script ran pw.x to completion (exit "
              f"{done.returncode}): {done.stderr.strip()[-500:]}")
        check("CALANGO_DONE" in done.stdout, "and reported completion")

        result_path = job / "bands_2d.json"
        if done.returncode != 0 or not result_path.is_file():
            check(False, "bands_2d.json was written")
            print(f"\n{failures} check(s) FAILED.")
            return 1

        data = json.loads(result_path.read_text())
        check(all(k in data for k in
                  ("fermi_eV", "samples", "spins", "kx_per_A", "ky_per_A",
                   "special_points", "bands")),
              "the JSON has every key every 2D Bands engine's schema shares")
        check(data["samples"] == GRID_SAMPLES,
              f"the grid size round-trips ({data['samples']})")
        check(isinstance(data["fermi_eV"], (int, float))
                  and -30.0 < data["fermi_eV"] < 10.0,
              f"the Fermi level is a real, physically plausible number for "
              f"a light-element pi system ({data['fermi_eV']:.4f} eV)")
        check(set(data["special_points"]) >= {"G", "K", "M"},
              "the hexagonal lattice's high-symmetry points are labelled — "
              "G/K/M, the same labels the GPAW reference engine's own "
              "special-point derivation produces for the identical lattice")
        check(len(data["bands"]) > 0, "at least one band survived selection")
        ef = data["fermi_eV"]
        for band in data["bands"]:
            energies = [e for row in band["energies_eV"] for e in row]
            check(min(energies) == band["min_eV"] and max(energies) == band["max_eV"],
                  f"band {band['band']}: the reported min/max match the "
                  f"energies actually written")
        # Every band kept is either crossing E_F or the nearest one outside
        # the window on its side — never something far away the selection
        # logic should have dropped. bandsBelow/bandsAbove default to 4 in
        # the C++ config the dump used, so at most 4 bands per side of a
        # non-crossing set, plus however many genuinely cross.
        below_ef = sorted((b for b in data["bands"] if b["max_eV"] <= ef),
                          key=lambda b: -b["max_eV"])
        above_ef = sorted((b for b in data["bands"] if b["min_eV"] >= ef),
                          key=lambda b: b["min_eV"])
        check(len(below_ef) <= 4 and len(above_ef) <= 4,
              f"band selection kept at most 4 non-crossing bands per side "
              f"of E_F ({len(below_ef)} below, {len(above_ef)} above)")

    print("\n" + ("All 2D Bands / Quantum ESPRESSO checks passed."
                  if failures == 0 else f"{failures} check(s) FAILED."))
    return 0 if failures == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
