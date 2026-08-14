"""Wannier spread validation on bulk FCC Cu — the canonical reference case.

WHY THIS EXISTS. The total Wannier spread Calango reports was suspected of
being systematically too high. Measuring it against Cu settles which half of
the pipeline is responsible, because the individual d-like spreads have a
well-known scale (a few tenths of an Angstrom squared) that a units or
b-vector/weight error cannot survive: any such error multiplies EVERY spread
by the same wrong factor, so five d functions landing at 0.3-0.6 A^2 rules it
out on its own.

WHAT IT FOUND. The functional is fine; the total is a SUM dominated by one
diffuse s-like function, and that function is what a frozen (disentanglement)
window fixes. Measured here on an 8x8x8 mesh with 20 bands:

    no window                d: 0.321-0.538   s: 4.493   total 6.749
    frozen window E_F + 2 eV d: 0.335-0.466   s: 2.669   total 4.736

so the window removes 30% of the total while barely moving the d manifold.

Skips cleanly (exit 0) when GPAW is not importable, following the convention
of the other benchmarks here.

Run directly, or through ctest as `cu_wannier_spread`.
"""

import json
import os
import sys


def _bootstrap_gpaw_env() -> None:
    """Re-exec under the GPAW conda env from ~/.calango/settings.json when the
    current interpreter has no GPAW, so the benchmark really runs."""
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


TOLERANCE_BAND = {
    # (low, high) in A^2. Bands rather than point values: the numbers depend
    # on the mesh, the cutoff and the pseudopotential, and pinning them
    # tightly would test those settings rather than the wannierization. They
    # are wide enough to survive a GPAW version bump and narrow enough that
    # a factor-of-two units error or a failed minimization falls outside.
    "d_max": (0.10, 1.20),      # every d-like function, literature <= ~1 A^2
    "s": (1.00, 6.50),          # the one diffuse function, no window
    "total_no_window": (4.00, 9.00),
    "total_window": (2.50, 6.50),
}


def fail(message):
    print("FAIL " + message)
    return 1


def main():
    try:
        import numpy as np
        from ase.build import bulk
        from ase.dft.wannier import Wannier
        from gpaw import GPAW, PW
    except Exception as exc:  # pragma: no cover - environment dependent
        print("SKIP cu_wannier_spread: GPAW/ASE not importable (%r)" % (exc,))
        return 0

    failures = 0

    atoms = bulk("Cu", "fcc", a=3.61)
    calc = GPAW(
        mode=PW(400),
        xc="PBE",
        kpts=(8, 8, 8),
        nbands=20,
        convergence={"bands": 18},
        # Wannier needs the full BZ: ase.dft.wannier refuses an IBZ-only
        # calculation, and with symmetry on GPAW stores only the wedge.
        symmetry={"point_group": False, "time_reversal": False},
        txt=None,
    )
    atoms.calc = calc
    atoms.get_potential_energy()
    nbands = calc.get_number_of_bands()
    print("Cu FCC: nbands=%d  E_F=%.3f eV  nkpts=%d"
          % (nbands, calc.get_fermi_level(), len(calc.get_bz_k_points())))

    # 6 Wannier functions: the five Cu 3d plus one s-like. The setup the
    # Wannier90 Cu tutorial uses for an s+d model.
    nwannier = 6
    if nwannier > nbands:
        return fail("nwannier %d exceeds nbands %d" % (nwannier, nbands))

    def wannierize(**kwargs):
        wan = Wannier(nwannier=nwannier, calc=calc,
                      initialwannier="orbitals", **kwargs)
        initial = float(np.sum(wan.get_spreads()))
        previous = None
        for _ in range(60):
            wan.localize(step=0.25, tolerance=1e-8)
            value = float(wan.get_functional_value())
            if previous is not None and abs(value - previous) < 1e-8:
                break
            previous = value
        spreads = np.sort(np.asarray(wan.get_spreads(), dtype=float))
        return initial, spreads

    initial, spreads = wannierize()
    total = float(spreads.sum())
    d_like = spreads[:-1]
    s_like = float(spreads[-1])
    print("no window   : initial %.3f -> total %.3f A^2" % (initial, total))
    print("              d-like %s  s-like %.3f"
          % (np.array2string(d_like, precision=3), s_like))

    # The minimization must actually move. A reported spread equal to the
    # initial projection value is the signature of a wannierization that never
    # iterated, which is one of the ways this number goes wrong silently.
    if not initial > 1.5 * total:
        failures += fail(
            "minimization barely moved: initial %.3f vs final %.3f"
            % (initial, total))
    else:
        print("ok   the minimization reduced the spread by %.1fx"
              % (initial / total))

    lo, hi = TOLERANCE_BAND["d_max"]
    if not all(lo <= float(s) <= hi for s in d_like):
        failures += fail("a d-like spread left [%.2f, %.2f]: %s"
                         % (lo, hi, d_like))
    else:
        print("ok   every d-like spread is inside [%.2f, %.2f] A^2 — the "
              "literature scale, which rules out a units or weight error"
              % (lo, hi))

    lo, hi = TOLERANCE_BAND["s"]
    if not lo <= s_like <= hi:
        failures += fail("the s-like spread %.3f left [%.2f, %.2f]"
                         % (s_like, lo, hi))
    else:
        print("ok   the s-like spread %.3f is inside [%.2f, %.2f] A^2"
              % (s_like, lo, hi))

    lo, hi = TOLERANCE_BAND["total_no_window"]
    if not lo <= total <= hi:
        failures += fail("total %.3f left [%.2f, %.2f]" % (total, lo, hi))
    else:
        print("ok   total %.3f A^2 is inside [%.2f, %.2f]" % (total, lo, hi))

    # And the frozen window, which is the actionable half of the finding.
    _, windowed = wannierize(fixedenergy=2.0)
    windowedTotal = float(windowed.sum())
    print("frozen +2eV : total %.3f A^2  %s"
          % (windowedTotal, np.array2string(windowed, precision=3)))

    lo, hi = TOLERANCE_BAND["total_window"]
    if not lo <= windowedTotal <= hi:
        failures += fail("windowed total %.3f left [%.2f, %.2f]"
                         % (windowedTotal, lo, hi))
    else:
        print("ok   windowed total %.3f A^2 is inside [%.2f, %.2f]"
              % (windowedTotal, lo, hi))

    # The relation that makes the recommendation worth making.
    if not windowedTotal < total:
        failures += fail("the frozen window did not reduce the total spread")
    else:
        print("ok   the frozen window removes %.0f%% of the total spread"
              % (100.0 * (total - windowedTotal) / total))

    print("\n%d check(s) FAILED." % failures)
    return 1 if failures else 0


if __name__ == "__main__":
    # Re-exec under the GPAW env named in ~/.calango/settings.json when
    # the current interpreter has none, so ctest runs this instead of
    # skipping it under whatever Python CMake happened to find.
    _bootstrap_gpaw_env()
    sys.exit(main())
