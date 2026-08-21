#!/usr/bin/env python3
"""2D Bands on SIESTA, RUN against real graphene.

Task 3 extended '2D Bands' from GPAW-only to also cover VASP, Quantum
ESPRESSO and SIESTA. This is the SIESTA half of that real verification —
see graphene_2d_bands_qe_benchmark.py for Quantum ESPRESSO and its own
docstring for why VASP (licensing) is fixture-verified only.

This is not a static check. It runs the ACTUAL GENERATED SCRIPT against a
real `siesta` binary and a real carbon pseudopotential, on monolayer
graphene — the same system and the same reasoning as
graphene_tetrahedron_benchmark.py. One real bug was found this way and is
now regression-tested here:

  * ASE's `Siesta.get_eigenvalues()` / `.results['eigenvalues']` is
    populated from the ORDINARY (SCF) eigenvalue output, not from the
    `%block BandPoints` pass this generator asks for — even though the
    calculator object that ran WAS the one with `bandpath=` set. Reading it
    the generic way every other engine here supports raised "IndexError:
    index 1 is out of bounds for axis 1 with size 1": only the SCF's own
    (tiny) k-point count was actually present. `Siesta.band_structure()`
    overrides the generic `Calculator` method and returns
    `self.results['bandstructure']` — built from the BandPoints pass
    specifically — which is the one that actually holds every requested
    point. A static read of the generated script cannot catch this: the
    line that was wrong (`get_eigenvalues(kpt=_k, ...)`) is exactly the
    same generic call every other engine's branch uses correctly.

Self-skips (exit 0) without a working `siesta` binary, an ase-capable
interpreter, or a carbon pseudopotential — set CALANGO_SIESTA_BIN /
CALANGO_SIESTA_PSEUDO_DIR / CALANGO_ASE_PYTHON to point at them explicitly,
or the test looks in a few plausible places, including the `siesta_env`
Conda environment this was verified against.

Usage:  graphene_2d_bands_siesta_benchmark.py <calango_script_test binary>
"""
import json
import os
import pathlib
import shutil
import subprocess
import sys
import tempfile

GRID_SAMPLES = 4

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
                [exe, "-c", "import ase.io, ase.build, ase.calculators.siesta, "
                            "ase.dft.kpoints; print('yes')"],
                capture_output=True, timeout=60)
        except (OSError, subprocess.SubprocessError):
            continue
        if done.returncode == 0 and b"yes" in done.stdout:
            return exe
    return None


def find_siesta_bin():
    """A `siesta` binary that actually runs, not merely one that exists —
    some installs on a given machine can be broken/killed unconditionally
    while a Conda one works fine, which is exactly what motivated checking
    `siesta --version` here rather than only `shutil.which`."""
    explicit = os.environ.get("CALANGO_SIESTA_BIN")
    candidates = [explicit] if explicit else []
    candidates.append(str(pathlib.Path.home()
                          / "miniconda3/envs/siesta_env/bin/siesta"))
    which = shutil.which("siesta")
    if which:
        candidates.append(which)
    for candidate in candidates:
        if not candidate or not pathlib.Path(candidate).is_file():
            continue
        try:
            done = subprocess.run([candidate, "--version"],
                                  capture_output=True, timeout=30)
        except (OSError, subprocess.SubprocessError):
            continue
        if done.returncode == 0:
            return candidate
    return None


def find_carbon_pseudopotential():
    """(directory, filename) for a carbon .psml/.psf, or (None, None)."""
    explicit = os.environ.get("CALANGO_SIESTA_PSEUDO_DIR")
    search_roots = [explicit] if explicit else [
        "~/Library/CloudStorage/Dropbox/Codes/SIESTA/PseudoDojo/nc-sr-04_pbe_stringent",
        "~/Codes/siesta_5.4.1/Tests/Pseudos",
        "~/Simulations/siesta/pseudos",
    ]
    for root in search_roots:
        if not root:
            continue
        directory = pathlib.Path(os.path.expanduser(root))
        if not directory.is_dir():
            continue
        matches = sorted(directory.glob("C.psml")) or sorted(directory.glob("C.psf"))
        if matches:
            return str(directory), matches[0].name
    return None, None


def main():
    if len(sys.argv) < 2:
        raise SystemExit(
            "usage: graphene_2d_bands_siesta_benchmark.py <calango_script_test>")
    binary = sys.argv[1]

    python = find_ase_python()
    siesta = find_siesta_bin()
    pseudo_dir, pseudo_file = find_carbon_pseudopotential()
    if python is None or siesta is None or pseudo_dir is None:
        print("2D Bands / SIESTA: skipping — need an ase-capable "
              "interpreter, a working siesta binary, and a carbon "
              "pseudopotential\n"
              f"  ase python: {python or 'not found'}\n"
              f"  siesta:     {siesta or 'not found'}\n"
              f"  C pseudo:   {pseudo_dir + '/' + pseudo_file if pseudo_dir else 'not found'}\n"
              "Set CALANGO_ASE_PYTHON / CALANGO_SIESTA_BIN / "
              "CALANGO_SIESTA_PSEUDO_DIR to point at them explicitly.")
        return 0
    print(f"ase interpreter: {python}\nsiesta: {siesta}\n"
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

        script = (scripts / "bands_2d_siesta.py").read_text()
        script = script.replace('_n = 24', f'_n = {GRID_SAMPLES}')
        check(f'_n = {GRID_SAMPLES}' in script,
              "the cheap-run grid size actually landed in the script")
        shutil.copy(pathlib.Path(pseudo_dir) / pseudo_file, job / pseudo_file)
        (job / "run.py").write_text(script)

        env = dict(os.environ)
        env["SIESTA_PP_PATH"] = str(job)
        env["ASE_SIESTA_COMMAND"] = f"{siesta} < PREFIX.fdf > PREFIX.out"
        done = subprocess.run([python, "run.py"], cwd=job, env=env,
                              capture_output=True, text=True, timeout=280)
        check(done.returncode == 0,
              f"the generated script ran siesta to completion (exit "
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
              "the same G/K/M labels every other engine's identical-lattice "
              "run produces")
        check(len(data["bands"]) > 0, "at least one band survived selection")
        for band in data["bands"]:
            energies = [e for row in band["energies_eV"] for e in row]
            check(min(energies) == band["min_eV"] and max(energies) == band["max_eV"],
                  f"band {band['band']}: the reported min/max match the "
                  f"energies actually written — the exact invariant the "
                  f"IndexError this test guards against broke before "
                  f"bands_2d.json was ever reached")

    print("\n" + ("All 2D Bands / SIESTA checks passed."
                  if failures == 0 else f"{failures} check(s) FAILED."))
    return 0 if failures == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
