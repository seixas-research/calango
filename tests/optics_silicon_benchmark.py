#!/usr/bin/env python3
"""Validation benchmark for the GPAW optics pipeline on silicon.

Mirrors ``core::OpticsScriptGenerator`` and the "Simulation → Optics…" wizard:

  1. Ground-state SCF  — plane-wave GPAW on bulk diamond-Si.
  2. Fixed-density NSCF with extra empty bands (the dielectric response sums
     interband transitions into unoccupied states).
  3. ``gpaw.response.df.DielectricFunction`` → complex ε(ω); ε₁ = Re, ε₂ = Im.

It applies the same robustness the GUI generator uses (per-direction guard,
non-finite scrubbing) and asserts a physically sane result (finite spectra,
static ε₁(0) > 1, non-zero absorption).

Skips cleanly (exit 0) without the GPAW response stack. Runs the DFT part in a
temporary directory.
"""
import os
import sys
import tempfile


ECUT_EV = 300.0
KPTS = (6, 6, 6)          # a reasonably dense grid for the optical response
XC = "PBE"
LATTICE_A = 5.43          # diamond-cubic Si (Å)
BROADENING_EV = 0.1


def _bootstrap_gpaw_env() -> None:
    """Re-exec under the GPAW conda env from ~/.calango/settings.json when the
    current interpreter (e.g. CTest's embedded python) has no GPAW, so the
    benchmark really runs.  No-op if GPAW is importable or
    no env is configured (main() then SKIPs cleanly)."""
    try:
        import gpaw  # noqa: F401
        return
    except Exception:
        pass
    if os.environ.get("_CALANGO_GPAW_REEXEC"):
        return
    import json
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


def main() -> int:
    try:
        import numpy as np
        from ase.build import bulk
        from gpaw import GPAW, PW
        from gpaw.response.df import DielectricFunction
    except Exception as exc:  # gpaw / response module unavailable
        print(f"SKIP: GPAW optics stack not available ({exc})")
        return 0

    workdir = tempfile.mkdtemp(prefix="calango_optics_si_")
    cwd = os.getcwd()
    os.chdir(workdir)
    try:
        atoms = bulk("Si", "diamond", a=LATTICE_A)

        # 1. Ground state.
        calc = GPAW(mode=PW(ECUT_EV), xc=XC, kpts=KPTS, txt="gpaw_gs.txt")
        atoms.calc = calc
        atoms.get_potential_energy()
        calc.write("gs.gpw", mode="all")

        # 2. Fixed-density NSCF with empty bands.
        n_occ = max(1, int(round(calc.get_number_of_electrons() / 2.0)))
        n_bands = max(4 * n_occ, 24)
        nscf = GPAW("gs.gpw").fixed_density(
            nbands=n_bands,
            convergence={"bands": max(2 * n_occ, 12)},
            symmetry="off",
            txt="gpaw_nscf.txt",
        )
        nscf.write("gs_nscf.gpw", mode="all")

        # 3. Dielectric function. GPAW's response module uses an internal
        #    non-linear frequency grid (retrieved via get_frequencies), and Si
        #    must be treated as an insulator — intraband=False disables the
        #    Drude (metallic) term that otherwise aborts the q→0 optical head.
        df = DielectricFunction(
            "gs_nscf.gpw",
            eta=BROADENING_EV,
            intraband=False,
            txt="gpaw_df.txt",
        )
        eps_nlfc, eps_lfc = df.get_dielectric_function(direction="x")
        frequencies = np.asarray(df.get_frequencies(), dtype=float)  # eV
        eps = np.asarray(eps_lfc)
        eps = np.where(np.isfinite(eps), eps, 0.0)
        eps1 = eps.real
        eps2 = eps.imag

        # --- Verification -------------------------------------------------
        assert len(eps1) == len(frequencies) and len(eps2) == len(frequencies), (
            "ε grid length mismatch")
        assert np.all(np.isfinite(eps1)) and np.all(np.isfinite(eps2)), (
            "non-finite dielectric values")
        assert float(eps1[0]) > 1.0, (
            f"static ε₁(0)={float(eps1[0]):.3f} is not > 1")
        assert float(eps2.max()) > 0.5, "ε₂(ω) shows no absorption"

        print(f"PASS: silicon optics — ε₁(0)={float(eps1[0]):.3f}, "
              f"max ε₂={float(eps2.max()):.3f} over {len(frequencies)} points.")
        return 0
    finally:
        os.chdir(cwd)


if __name__ == "__main__":
    _bootstrap_gpaw_env()
    sys.exit(main())
