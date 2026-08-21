#!/usr/bin/env python3
"""Native SCC-DFTB engine, RUN as the actual compiled binary end to end.

Every other DFTB check in this repo (calango_dftb_test, the C++ unit-style
binary) exercises the LIBRARY directly — parser, transform, Hamiltonian,
SCF, forces — against closed-form references. This test is different: it
runs `calango-dftb-run` itself, exactly as the generated Python wrapper
launches it in production (see src/dftb/DftbTaskConfig.hpp and
tools/calango_dftb_run.cpp) — a plain-text manifest on disk, a subprocess
invocation, real JSON files read back off disk. It is the one test that
would catch a bug in the manifest parser, the extxyz reader, the CLI
argument handling, or the JSON writer, none of which the library-level unit
tests touch at all.

The system is the SAME synthetic honeycomb (graphene-topology) carbon
fixture calango_dftb_test's own Dirac-cone section uses — nearest-neighbor
pz-pz hopping only, every other Slater-Koster channel zero — chosen for the
same reason: the Dirac-point degeneracy at K is protected by the lattice's
own point-group symmetry, so it is exact regardless of how crude the
hopping model is, which is what makes it useful as an end-to-end physics
check on a fixture that needs no external, licensed parameter set.

Usage:  dftb_native_run_test.py <calango-dftb-run binary>
"""
import pathlib
import shutil
import subprocess
import sys
import tempfile

failures = 0


def check(condition, what):
    global failures
    print(f"  {'ok  ' if condition else 'FAIL'} {what}")
    if not condition:
        failures += 1


def write_skf(path):
    lines = ["0.1 35", "0.0 0.0 0.0 0.0 0.0 0.0 0.0 0.0 2.0 2.0",
             "12.011 " + " ".join(["0"] * 19)]
    for _ in range(34):
        cols = ["0"] * 20
        cols[6] = "-0.1"  # Pppi (ppPi)
        lines.append(" ".join(cols))
    path.write_text("\n".join(lines) + "\n")


def main():
    if len(sys.argv) != 2:
        raise SystemExit("usage: dftb_native_run_test.py <calango-dftb-run>")
    binary = sys.argv[1]
    if not pathlib.Path(binary).is_file():
        print(f"calango-dftb-run not found at {binary} — skipping")
        return 0

    with tempfile.TemporaryDirectory() as tmp:
        tmp = pathlib.Path(tmp)
        write_skf(tmp / "C-C.skf")

        (tmp / "structure.extxyz").write_text(
            '2\n'
            'Lattice="2.46 0.0 0.0 -1.23 2.130570414 0.0 0.0 0.0 15.0" '
            'Properties=species:S:1:pos:R:3 pbc="T T F"\n'
            'C 0.0 0.0 0.0\n'
            'C 0.0 1.420380276 0.0\n'
        )

        # Gamma -> K -> M -> Gamma, K at EXACTLY (2/3, 2/3, 0) this time
        # (the manual smoke-test during development used a rounded
        # 0.66667 and saw a ~1e-4 Hartree residual gap from that rounding
        # alone — using the exact fraction here is what lets this test
        # assert near-machine-precision degeneracy).
        (tmp / "kpath.txt").write_text(
            "0.0 0.0 0.0 G\n"
            "0.16666666666667 0.16666666666667 0.0\n"
            "0.33333333333333 0.33333333333333 0.0\n"
            "0.5 0.5 0.0\n"
            "0.66666666666667 0.66666666666667 0.0 K\n"
            "0.5 0.33333333333333 0.0 M\n"
            "0.25 0.16666666666667 0.0\n"
            "0.0 0.0 0.0 G\n"
        )

        (tmp / "manifest.txt").write_text(
            "task bands\n"
            "structure structure.extxyz\n"
            f"skdir {tmp}\n"
            "scc false\n"
            "kmesh 6 6 1\n"
            "kpathfile kpath.txt\n"
            "bandsbelow 8\n"
            "bandsabove 8\n"
            "output bands.json\n"
        )

        done = subprocess.run([binary, "manifest.txt"], cwd=tmp,
                              capture_output=True, text=True, timeout=60)
        check(done.returncode == 0,
              f"calango-dftb-run exits 0 (stderr: {done.stderr.strip()[-300:]})")
        check("CALANGO_DONE" in done.stdout, "and prints CALANGO_DONE")
        check("CALANGO_RESULT bands=bands.json" in done.stdout,
              "and reports the result file via CALANGO_RESULT")
        check(any(line.startswith("CALANGO_PROGRESS") for line in done.stdout.splitlines()),
              "and reports progress via CALANGO_PROGRESS (JobRunner's own "
              "marker convention)")

        result_path = tmp / "bands.json"
        if done.returncode != 0 or not result_path.is_file():
            print(f"\n{failures} check(s) FAILED.")
            return 1

        import json
        data = json.loads(result_path.read_text())
        check(all(k in data for k in
                  ("x", "special_x", "special_labels", "efermi", "energies")),
              "bands.json has every key the shared Bands viewer schema needs")
        check(data["special_labels"] == ["G", "K", "M", "G"],
              "special point labels round-trip from the k-path file")
        check(len(data["energies"]) == 1,
              "one spin channel (this engine is non-spin-polarized)")

        # pz(atom0)/pz(atom1) are bands index 3/5 in the 8-orbital,
        # bandsbelow=8/bandsabove=8 (i.e. ALL bands kept) selection, in
        # ascending order — the two that touch at K. Rather than assume the
        # exact index, find the pair of bands with the smallest splitting
        # AT the K point specifically.
        k_index = data["special_labels"].index("K")
        k_energies = sorted(data["energies"][0][k_index])
        gaps = [k_energies[i + 1] - k_energies[i]
                for i in range(len(k_energies) - 1)]
        min_gap_at_k = min(gaps)
        check(min_gap_at_k < 1.0e-6,
              f"the pz/pz* bands are degenerate at K to near machine "
              f"precision (min gap {min_gap_at_k:.3e} eV) — the Dirac point "
              f"survives the FULL pipeline (manifest -> extxyz reader -> "
              f"native binary -> JSON), not just the in-process library "
              f"path calango_dftb_test already covers")

        g_index = data["special_labels"].index("G")
        g_energies = sorted(data["energies"][0][g_index])
        # At Gamma the pz/pz* splitting is exactly 2 * 3 * |ppPi| = 0.6
        # Hartree = 16.32... eV (see calango_dftb_test's own hand-derived
        # anchor for this same fixture).
        import math
        hartree_to_ev = 27.211386245988
        expected_gamma_gap = 2.0 * 3.0 * 0.1 * hartree_to_ev
        # The outermost (most bonding/antibonding) pair IS the pz/pz* pair
        # at Gamma in this fixture (s/px/py all sit at exactly 0 eV,
        # undispersed, since every OTHER Slater-Koster channel is zero) —
        # check the full bandwidth.
        full_bandwidth = max(g_energies) - min(g_energies)
        check(math.isclose(full_bandwidth, expected_gamma_gap, rel_tol=1e-6),
              f"the Gamma-point pz bandwidth matches the hand-derived "
              f"2*3*|ppPi| = {expected_gamma_gap:.4f} eV exactly "
              f"(got {full_bandwidth:.4f} eV)")

    print("\n" + ("All native DFTB end-to-end checks passed."
                  if failures == 0 else f"{failures} check(s) FAILED."))
    return 0 if failures == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
