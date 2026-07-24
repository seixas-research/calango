#!/usr/bin/env python3
"""Validation benchmark for the Wannier band/PDOS interpolation on silicon.

Mirrors ``core::generateWannierInterpolationScript`` and the MLWF viewer's
"Wannier Interpolation…" action:

  1. Ground-state SCF        — plane-wave GPAW on bulk diamond-Si, with valence
     + low-conduction bands so a disentangled Wannier basis can be built.
  2. Marzari-Vanderbilt localization via ``ase.dft.wannier.Wannier`` with a
     frozen energy window (``fixedenergy``) so the 8 Wannier functions span the
     valence manifold plus the lowest conduction states.
  3. Wannier interpolation   — the real-space Hamiltonian is Fourier
     transformed, H(k) = Σ_R e^{i k·R} H(R), via ``wan.get_hamiltonian_kpoint``,
     evaluated on a dense high-symmetry path Γ - X - L - Γ.

It then asserts a physically sane, artifact-free band structure:

  * every interpolated eigenvalue is finite;
  * the bands are smooth — no spurious jumps between adjacent dense k-points;
  * a fundamental gap opens between the occupied and unoccupied manifolds, with
    the valence-band maximum at Γ (Si is an indirect-gap semiconductor). PBE
    underestimates the gap (~0.6 eV vs the ~1.1 eV experimental value), so the
    assertion brackets that range generously rather than pinning 1.1 eV.

Skips cleanly (exit 0) when GPAW / ase.dft.wannier is not importable. Runs
entirely inside a temporary directory.
"""
import math
import os
import sys
import tempfile


# Ground-state parameters — modest so the guarded run stays tractable, but a
# 4×4×4 k-mesh and a few conduction bands are needed for a usable Wannier basis.
ECUT_EV = 200.0
KPTS = (4, 4, 4)
XC = "PBE"
LATTICE_A = 5.43        # diamond-cubic Si lattice constant (Å)
N_BANDS = 12            # valence (4) + conduction headroom
N_WANNIER = 8           # 4 bonding + 4 antibonding sp3 Wannier functions
N_OCCUPIED = 4          # 8 valence electrons / 2 → 4 filled bands
BAND_POINTS = 120       # dense samples along Γ-X-L-Γ


def _bootstrap_gpaw_env() -> None:
    """Re-exec under the GPAW conda env from ~/.calango/settings.json when the
    current interpreter (e.g. CTest's embedded python) has no GPAW, so the
    benchmark really runs.  No-op if GPAW is importable or no env is
    configured (main() then SKIPs cleanly)."""
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
        from ase.dft.wannier import Wannier
        from gpaw import GPAW, PW
    except Exception as exc:  # gpaw / ase.dft.wannier unavailable
        print(f"SKIP: GPAW / Wannier stack not available ({exc})")
        return 0

    workdir = tempfile.mkdtemp(prefix="calango_wannier_interp_")
    cwd = os.getcwd()
    os.chdir(workdir)
    try:
        atoms = bulk("Si", "diamond", a=LATTICE_A)

        # 1. Ground state. symmetry='off' — ASE's Wannier needs the full
        #    (unsymmetrized) Brillouin zone.
        calc = GPAW(
            mode=PW(ECUT_EV),
            xc=XC,
            kpts=KPTS,
            nbands=N_BANDS,
            symmetry="off",
            txt="gpaw_gs.txt",
        )
        atoms.calc = calc
        atoms.get_potential_energy()
        try:
            efermi = float(calc.get_fermi_level())
        except Exception:
            efermi = 0.0

        # 2. Localize a disentangled 8-function Wannier basis. Freezing exactly
        #    the four valence bands (fixedstates) keeps the occupied manifold
        #    identical to the DFT result while the remaining four Wannier
        #    functions disentangle from the conduction states — far more robust
        #    than an energy window, which occasionally fails to open the gap.
        try:
            wan = Wannier(nwannier=N_WANNIER, calc=calc,
                          fixedstates=N_OCCUPIED, initialwannier="orbitals")
        except TypeError:
            # Older ASE without fixedstates — fall back to the plain basis.
            wan = Wannier(nwannier=N_WANNIER, calc=calc,
                          initialwannier="orbitals")
        prev = None
        for _ in range(50):
            wan.localize(step=0.25, tolerance=1e-6)
            val = float(wan.get_functional_value())
            if prev is not None and abs(val - prev) < 1e-6:
                break
            prev = val

        # 3. Wannier interpolation H(R) -> H(k) along Γ - X - L - Γ.
        path = atoms.cell.bandpath("GXLG", npoints=BAND_POINTS)
        energies = []
        for kpt in path.kpts:
            H = np.asarray(wan.get_hamiltonian_kpoint(
                np.asarray(kpt, dtype=float)))
            eigs = np.linalg.eigvalsh(H)
            energies.append(np.sort(eigs.real))
        energies = np.asarray(energies)  # (nk, nbands=N_WANNIER)

        # --- Verification -------------------------------------------------
        assert energies.shape == (len(path.kpts), N_WANNIER), (
            f"unexpected band array shape {energies.shape}")
        assert np.all(np.isfinite(energies)), "non-finite interpolated energies"

        # Smoothness: no interpolation artifact should jump a band by many eV
        # between adjacent dense k-points.
        jumps = np.abs(np.diff(energies, axis=0))
        max_jump = float(jumps.max())
        assert max_jump < 6.0, (
            f"band structure not smooth — {max_jump:.2f} eV jump between "
            "adjacent k-points (interpolation artifact)")

        # Fundamental gap between the occupied (0..3) and unoccupied (4..7)
        # manifolds, referenced to E_F.
        energies -= efermi
        vbm = float(energies[:, N_OCCUPIED - 1].max())
        cbm = float(energies[:, N_OCCUPIED].min())
        gap = cbm - vbm
        # Γ is the first (and last) point of the path; the Si valence-band
        # maximum sits at Γ, where the top valence band is (near-)degenerate.
        vbm_at_gamma = float(energies[0, N_OCCUPIED - 1])

        assert gap > 0.1, (
            f"no fundamental gap resolved (E_g = {gap:.3f} eV); the Wannier "
            "basis did not separate valence from conduction")
        # PBE underestimates; bracket generously around ~0.6 eV (PBE) up to the
        # ~1.1 eV experimental value with margin.
        assert gap < 2.5, (
            f"gap {gap:.3f} eV is unphysically large — likely a disentanglement "
            "artifact")
        # The valence-band maximum coincides with (or is degenerate at) Γ.
        assert vbm - vbm_at_gamma < 0.1, (
            f"valence-band maximum not at Γ — VBM {vbm:.3f} eV vs Γ top "
            f"{vbm_at_gamma:.3f} eV")

        print(
            f"PASS: silicon Wannier interpolation — {N_WANNIER} WFs along "
            f"Γ-X-L-Γ, E_g = {gap:.3f} eV (VBM {vbm:.3f}, CBM {cbm:.3f} eV), "
            f"max adjacent-k jump {max_jump:.3f} eV, no artifacts.")
        return 0
    finally:
        os.chdir(cwd)


if __name__ == "__main__":
    _bootstrap_gpaw_env()
    sys.exit(main())
