#!/usr/bin/env python3
"""Effective Band Structure unfolding, RUN as the actual compiled binary,
end to end — the "task unfolding" counterpart to dftb_native_run_test.py's
"task bands" coverage (see that file's own docstring for why an end-to-end
run matters beyond the library-level unit tests in calango_dftb_test).

A 1D chain: a 1-atom primitive cell unfolded from its own EXACT 2x supercell.
Chosen (over a 2D honeycomb 2x1x1, the more "real" case) because its
dispersion is a single cosine with a closed form anyone can check by eye,
E(k) = 2*t*cos(2*pi*k) — s orbitals only, zero overlap, so there is no
S(k)-induced renormalization to account for either. calango_dftb_test's own
"1D chain, exact dispersion" section validates the SAME physics at the
library level; this test is what caught a REAL bug the library-level test
could not have (a hand-written extxyz's 6-decimal-digit position was just
imprecise enough to land a bond distance a floating-point epsilon on the
wrong side of the Slater-Koster table's first grid point, silently
returning a hopping of 0 instead of the tabulated value) — the fix
(SlaterKosterFile.cpp's boundary-tolerance snap) is regression-tested at
the library level too, but only an end-to-end run through the real extxyz
reader and the real manifest pipeline exercises the exact path that broke.

Usage:  dftb_unfolding_run_test.py <calango-dftb-run binary>
"""
import math
import pathlib
import subprocess
import sys
import tempfile

BOHR_PER_ANGSTROM = 1.8897261254578281
HARTREE_TO_EV = 27.211386245988

failures = 0


def check(condition, what):
    global failures
    print(f"  {'ok  ' if condition else 'FAIL'} {what}")
    if not condition:
        failures += 1


def write_skf(path):
    # s-only, orthogonal (Sss0 = 0): Hss0 = -0.2 hartree at r = 2.0 bohr
    # (the fixture's own gridDist), 0 beyond it.
    lines = ["2.0 3", "0.0 0.0 0.0 0.0 0.0 0.0 0.0 0.0 0.0 1.0",
             "1.008 " + " ".join(["0"] * 19)]
    lines.append(" ".join(["0"] * 9 + ["-0.2"] + ["0"] * 9 + ["0.0"]))
    lines.append(" ".join(["0"] * 20))
    path.write_text("\n".join(lines) + "\n")


def main():
    if len(sys.argv) != 2:
        raise SystemExit("usage: dftb_unfolding_run_test.py <calango-dftb-run>")
    binary = sys.argv[1]
    if not pathlib.Path(binary).is_file():
        print(f"calango-dftb-run not found at {binary} — skipping")
        return 0

    with tempfile.TemporaryDirectory() as tmp:
        tmp = pathlib.Path(tmp)
        write_skf(tmp / "H-H.skf")

        # period = 2.0 bohr, full double precision (repr()) throughout —
        # NOT truncated decimal text, which is exactly what triggered the
        # bug this test guards against.
        period_a = 2.0 / BOHR_PER_ANGSTROM
        (tmp / "primitive.extxyz").write_text(
            f"1\nLattice=\"{period_a!r} 0.0 0.0 0.0 15.0 0.0 0.0 0.0 15.0\" "
            f"Properties=species:S:1:pos:R:3 pbc=\"T F F\"\nH 0.0 0.0 0.0\n")
        (tmp / "supercell.extxyz").write_text(
            f"2\nLattice=\"{2 * period_a!r} 0.0 0.0 0.0 15.0 0.0 0.0 0.0 15.0\" "
            f"Properties=species:S:1:pos:R:3 pbc=\"T F F\"\n"
            f"H 0.0 0.0 0.0\nH {period_a!r} 0.0 0.0\n")

        (tmp / "kpath.txt").write_text(
            "0.0 0.0 0.0 G\n0.1 0.0 0.0\n0.2 0.0 0.0\n0.3 0.0 0.0\n"
            "0.4 0.0 0.0\n0.5 0.0 0.0 X\n")

        (tmp / "manifest.txt").write_text(
            "task unfolding\n"
            "structure supercell.extxyz\n"
            f"skdir {tmp}\n"
            "scc false\n"
            "kmesh 4 1 1\n"
            "kpathfile kpath.txt\n"
            "primitivestructure primitive.extxyz\n"
            "output effective_bands.json\n"
        )

        done = subprocess.run([binary, "manifest.txt"], cwd=tmp,
                              capture_output=True, text=True, timeout=60)
        check(done.returncode == 0,
              f"calango-dftb-run exits 0 (stderr: {done.stderr.strip()[-300:]})")
        check("CALANGO_RESULT effective_bands=effective_bands.json" in done.stdout,
              "and reports the result file via CALANGO_RESULT")

        result_path = tmp / "effective_bands.json"
        if done.returncode != 0 or not result_path.is_file():
            print(f"\n{failures} check(s) FAILED.")
            return 1

        import json
        data = json.loads(result_path.read_text())
        check(all(k in data for k in
                  ("efermi", "special_x", "special_labels", "energy_min",
                   "energy_max", "energy_bins", "sigma", "weight_threshold",
                   "columns")),
              "effective_bands.json has every key core::SpectralFunctionOptions "
              "/ the columns schema needs")
        check(data["special_labels"] == ["G", "X"],
              "special point labels round-trip from the k-path file")
        check(len(data["columns"]) == 6, "one column per k-path point")

        t = -0.2 * HARTREE_TO_EV  # hopping, eV
        for col in data["columns"]:
            energies = col["energies"]
            weights = col["weights"]
            check(len(energies) == 2 and len(weights) == 2,
                  "2 supercell states per column (2-atom cell)")
            # Partition identity, this time checked the OTHER way: each
            # column's own two weights (the two supercell states at ONE k)
            # must sum to 1 exactly regardless of how they split between
            # the states — a defect-free supercell partitions ALL its
            # spectral weight at the k requested between its states.
            total = sum(weights)
            check(math.isclose(total, 1.0, abs_tol=1e-6),
                  f"weights at x={col['path_coordinate']:.3f} sum to 1 "
                  f"(got {total:.6f})")
            # One state must be near-fully dominant (a pristine, defect-free
            # 2x supercell never actually MIXES primitive k-points).
            check(max(weights) > 0.99,
                  f"one state carries near-total weight at x="
                  f"{col['path_coordinate']:.3f} (max weight "
                  f"{max(weights):.4f}) — the precision bug this test "
                  f"guards against showed up as a smeared 0.25/0.25 split "
                  f"here instead")

        # Gamma (first column): E = +/- 2|t| exactly.
        gamma_energies = sorted(data["columns"][0]["energies"])
        check(math.isclose(gamma_energies[0], -2 * abs(t), rel_tol=1e-6)
                  and math.isclose(gamma_energies[1], 2 * abs(t), rel_tol=1e-6),
              f"Gamma-point energies are +/- 2|t| exactly (got {gamma_energies}, "
              f"expected +/-{2 * abs(t):.4f})")

    print("\n" + ("All native DFTB unfolding end-to-end checks passed."
                  if failures == 0 else f"{failures} check(s) FAILED."))
    return 0 if failures == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
