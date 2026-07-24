#!/usr/bin/env python3
"""Validation benchmark for the GPAW linear-optics pipeline on diamond (C).

Mirrors the workflow that ``core::OpticsScriptGenerator`` emits and that the
"Simulation → Optics…" wizard runs:

  1. Ground-state SCF  — plane-wave cutoff 500 eV, 7×7×7 Monkhorst-Pack, PBE.
  2. Fixed-density NSCF with extra empty bands (the dielectric response sums
     transitions into unoccupied states).
  3. Non-self-consistent spectrum via ``gpaw.response.df.DielectricFunction``.

It then writes ``dielectric_function.csv`` / ``dielectric_function.json`` holding
the ε₁(ω) and ε₂(ω) spectra and asserts the pipeline produced a physically sane
result without runtime exceptions:

  * both files exist and parse, with equal-length energy / ε₁ / ε₂ columns;
  * all values are finite;
  * the static dielectric constant ε₁(ω→0) is > 1 (diamond's experimental value
    is ≈ 5.7; a coarse run lands in the same ballpark);
  * ε₂(ω) is essentially non-negative and shows absorption (a peak) above the
    gap, i.e. the imaginary part is not identically zero.

Skips cleanly (exit 0) when GPAW / its response module is not importable, so it
is safe to register as an unconditional CTest in environments without a DFT
stack. Runs entirely inside a temporary directory.
"""
import json
import os
import sys
import tempfile


# Ground-state parameters requested by the benchmark spec.
ECUT_EV = 500.0
KPTS = (7, 7, 7)
XC = "PBE"
# Diamond cubic lattice constant of carbon (Å).
LATTICE_A = 3.567
# Photon-energy grid (eV) — kept modest so the guarded run stays tractable.
OMEGA_MIN, OMEGA_MAX, NPTS = 0.0, 25.0, 200
BROADENING_EV = 0.1


def main() -> int:
    try:
        import numpy as np
        from ase.build import bulk
        from gpaw import GPAW, PW
        from gpaw.response.df import DielectricFunction
    except Exception as exc:  # gpaw / its response module unavailable
        print(f"SKIP: GPAW optics stack not available ({exc})")
        return 0

    workdir = tempfile.mkdtemp(prefix="calango_optics_")
    cwd = os.getcwd()
    os.chdir(workdir)
    try:
        atoms = bulk("C", "diamond", a=LATTICE_A)

        # 1. Ground-state SCF.
        calc = GPAW(mode=PW(ECUT_EV), xc=XC, kpts=KPTS, txt="gpaw_gs.txt")
        atoms.calc = calc
        atoms.get_potential_energy()
        calc.write("gs.gpw", mode="all")

        # 2. Fixed-density NSCF with extra empty bands.
        n_occ = max(1, int(round(calc.get_number_of_electrons() / 2.0)))
        n_bands = max(4 * n_occ, 16)
        nscf = GPAW("gs.gpw").fixed_density(
            nbands=n_bands,
            convergence={"bands": max(2 * n_occ, 8)},
            symmetry="off",
            txt="gpaw_nscf.txt",
        )
        nscf.write("gs_nscf.gpw", mode="all")

        # 3. Dielectric function (non-self-consistent).
        frequencies = np.linspace(OMEGA_MIN, OMEGA_MAX, NPTS)
        df = DielectricFunction(
            "gs_nscf.gpw",
            frequencies=frequencies,
            eta=BROADENING_EV,
            txt="gpaw_df.txt",
        )
        # (without, with) local-field corrections; use the LFC result.
        eps_nlfc, eps_lfc = df.get_dielectric_function(direction="x")
        eps = np.asarray(eps_lfc)
        eps1 = eps.real
        eps2 = eps.imag

        # Persist the spectra (both the CSV and JSON the spec asks for).
        with open("dielectric_function.csv", "w") as handle:
            handle.write("energy_eV,eps1,eps2\n")
            for w, e1, e2 in zip(frequencies, eps1, eps2):
                handle.write(f"{w:.6f},{e1:.6f},{e2:.6f}\n")
        with open("dielectric_function.json", "w") as handle:
            json.dump(
                {
                    "energy_eV": [float(w) for w in frequencies],
                    "eps1": [float(v) for v in eps1],
                    "eps2": [float(v) for v in eps2],
                },
                handle,
            )

        # --- Verification -------------------------------------------------
        assert os.path.exists("dielectric_function.csv"), "CSV not written"
        assert os.path.exists("dielectric_function.json"), "JSON not written"
        with open("dielectric_function.json") as handle:
            data = json.load(handle)
        n = len(data["energy_eV"])
        assert n == NPTS, f"energy grid length {n} != {NPTS}"
        assert len(data["eps1"]) == n and len(data["eps2"]) == n, (
            "eps1/eps2 columns do not match the energy grid")
        assert np.all(np.isfinite(eps1)) and np.all(np.isfinite(eps2)), (
            "non-finite values in the dielectric spectra")

        eps1_static = float(eps1[0])
        eps2_max = float(eps2.max())
        assert eps1_static > 1.0, (
            f"static dielectric constant ε₁(0)={eps1_static:.3f} is not > 1")
        # A tiny negative excursion from broadening is fine; a large one is not.
        assert eps2.min() > -0.5, f"ε₂ strongly negative ({eps2.min():.3f})"
        assert eps2_max > 0.5, "ε₂(ω) shows no absorption above the gap"

        print(
            f"PASS: diamond optics pipeline — ε₁(0)={eps1_static:.3f}, "
            f"max ε₂={eps2_max:.3f} over {n} points, no runtime exceptions.")
        return 0
    finally:
        os.chdir(cwd)


if __name__ == "__main__":
    sys.exit(main())
