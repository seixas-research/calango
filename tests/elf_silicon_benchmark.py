#!/usr/bin/env python3
"""Validation benchmark for the Electron Localization Function (ELF) on silicon.

Mirrors the ELF branch of the Single-point wizard's density exports (see
``gpawDensityExportBlock`` in AseScriptGenerator): a plane-wave GPAW ground
state on bulk diamond-Si, then the ELF evaluated on the real-space grid. There
is no longer a separate ELF module — the field comes out of the same SCF as the
other five density exports and renders in the Volumetric Data dock.

GPAW 25.x replaced the old ``gpaw.elf.ELF`` class with
``gpaw.elf.elf_from_dft_calculation(calc)``, and that routine needs the kinetic
energy density that only the *new* GPAW engine tracks — so ``GPAW_NEW=1`` is set
before importing gpaw. The returned ``UGArray`` is gathered to a contiguous
float64 NumPy grid (this is exactly the array-shape / boundary / memory-layout
handling the GUI script performs).

It asserts the ELF is a finite 3D field with η ∈ [0, 1].

Skips cleanly (exit 0) when GPAW is not importable. Runs the DFT part inside a
temporary directory.
"""
import os
import sys
import tempfile

# The ELF / kinetic-energy-density path only exists in GPAW's new engine.
os.environ.setdefault("GPAW_NEW", "1")


ECUT_EV = 340.0
KPTS = (4, 4, 4)
XC = "PBE"
LATTICE_A = 5.43  # diamond-cubic Si (Å)


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
        from gpaw.elf import elf_from_dft_calculation
    except Exception as exc:  # gpaw / gpaw.elf unavailable
        print(f"SKIP: GPAW / ELF stack not available ({exc})")
        return 0

    workdir = tempfile.mkdtemp(prefix="calango_elf_")
    cwd = os.getcwd()
    os.chdir(workdir)
    try:
        atoms = bulk("Si", "diamond", a=LATTICE_A)
        calc = GPAW(mode=PW(ECUT_EV), xc=XC, kpts=KPTS, txt="gpaw_gs.txt")
        atoms.calc = calc
        atoms.get_potential_energy()

        # ELF on the real-space grid. The new API returns a (possibly domain-
        # distributed) UGArray — gather it to rank 0 and take a contiguous
        # float64 view; collapse a leading spin axis if present.
        elf_R = elf_from_dft_calculation(calc)
        gathered = elf_R.gather() if hasattr(elf_R, "gather") else elf_R
        grid = np.asarray(getattr(gathered, "data", gathered), dtype=float)
        if grid.ndim == 4:  # (spin, nx, ny, nz)
            grid = grid[0]
        grid = np.ascontiguousarray(grid, dtype=float)

        # --- Verification -------------------------------------------------
        assert grid.ndim == 3, f"ELF grid is not 3D: shape {grid.shape}"
        assert np.all(np.isfinite(grid)), "non-finite ELF values"
        assert grid.min() >= -1e-4 and grid.max() <= 1.0 + 1e-3, (
            f"ELF outside [0,1]: [{grid.min():.4f}, {grid.max():.4f}]")

        print(f"PASS: silicon ELF — grid {grid.shape}, "
              f"eta in [{grid.min():.3f}, {grid.max():.3f}], no exceptions.")
        return 0
    finally:
        os.chdir(cwd)


if __name__ == "__main__":
    _bootstrap_gpaw_env()
    sys.exit(main())
