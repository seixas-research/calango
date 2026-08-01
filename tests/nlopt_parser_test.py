#!/usr/bin/env python3
"""Nonlinear optics: the gpaw.nlopt call surface and the results it parses.

Two halves, both of which fail silently in production:

1. THE CALL SURFACE. `make_nlodata`, `get_shg`, `get_shift` and
   `get_chi_tensor` are functions in somebody else's package, reached only
   after a ground state has been converged. A misspelled keyword surfaces as a
   TypeError an hour into the job. This checks the emitted calls against the
   signatures GPAW actually declares.

2. THE UNIT CONVERSIONS. Everything gpaw.nlopt returns is in SI base units —
   chi^(2) in m/V, the shift current in A/V^2 — and nobody quotes those: the
   literature uses pm/V for bulk chi^(2) and nm^2/V for the sheet value of a
   monolayer. A factor of 1e6 either way produces a plot that looks entirely
   reasonable and is wrong by six orders of magnitude. There is no way to catch
   that by reading, so the shipped functions are driven with arrays of exactly
   the shape GPAW hands back and the numbers are checked.

Plus the inversion-symmetry test, which is the module's one piece of physics
that runs before anything expensive: chi^(2) and the shift current vanish
IDENTICALLY in a centrosymmetric crystal, and what a finite k-mesh returns
there looks like a spectrum.

Everything under test is extracted BY NAME from a GENERATED script with
``ast``, so this exercises the code that ships. No GPAW required.

Usage:  nlopt_parser_test.py <calango_script_test binary>
"""
import ast
import re
import subprocess
import sys
import tempfile
from pathlib import Path

import numpy as np

WANTED = {"sheet_thickness_m", "shg_observables", "shift_observables",
          "linear_observables", "has_inversion_symmetry", "spectrum_peak"}

failures = 0


def check(condition, what):
    global failures
    print(f"  {'ok  ' if condition else 'FAIL'} {what}")
    if not condition:
        failures += 1


def load(script_path):
    """Compile just the post-processing out of a generated script."""
    tree = ast.parse(Path(script_path).read_text())
    picked = [node for node in tree.body
              if isinstance(node, ast.FunctionDef) and node.name in WANTED]
    missing = WANTED - {node.name for node in picked}
    if missing:
        raise SystemExit(f"{script_path} defines no {sorted(missing)}")
    namespace = {"np": np}
    exec(compile(ast.Module(body=picked, type_ignores=[]), script_path, "exec"),
         namespace)
    return namespace


def resolve(argument, workdir, name):
    if argument.endswith(".py"):
        return argument
    dump = Path(workdir) / "scripts"
    if not dump.exists():
        dump.mkdir()
        subprocess.run([argument, "--dump", str(dump)], check=True,
                       stdout=subprocess.DEVNULL)
    script = dump / name
    if not script.exists():
        raise SystemExit(f"{argument} --dump produced no {name}")
    return str(script)


def call_args(source, name):
    """The argument text of the first real CALL to `name`, or None.

    Skips empty-parenthesis matches, because the docstrings in the generated
    script refer to these functions as "get_shg()" — and a regex that took the
    first `name(...)` it saw would happily check a prose mention and report
    every keyword missing.
    """
    for match in re.finditer(re.escape(name) + r"\((.*?)\)", source, re.S):
        if match.group(1).strip():
            return match.group(1)
    return None


def check_call_surface(source):
    """The emitted gpaw.nlopt calls, against the signatures GPAW declares.

    Keyword names are read out of the script text rather than asserted as one
    long string, so a reordering is fine and a rename is not — which is the
    distinction that matters when GPAW moves.
    """
    print("gpaw.nlopt call surface:")
    check("from gpaw.nlopt.matrixel import make_nlodata" in source,
          "make_nlodata comes from gpaw.nlopt.matrixel")
    check("from gpaw.nlopt.shg import get_shg" in source,
          "get_shg from gpaw.nlopt.shg")
    check("from gpaw.nlopt.shift import get_shift" in source,
          "get_shift from gpaw.nlopt.shift")
    check("from gpaw.nlopt.linear import get_chi_tensor" in source,
          "get_chi_tensor from gpaw.nlopt.linear")

    # make_nlodata(calc, spin_string='all', ni=None, nf=None) -> NLOData
    check(re.search(r"make_nlodata\(\s*'gs\.gpw'", source) is not None,
          "make_nlodata takes the .gpw path as its first positional argument")
    check("'ni'" in source and "'nf'" in source,
          "the band window uses GPAW's own ni / nf names")
    check(".write('mml.npz')" in source,
          "NLOData.write() saves the matrix elements for reuse")

    # get_shg(nlodata, freqs, eta, pol, eshift, gauge, ftol, Etol, band_n,
    #         out_name)
    args = call_args(source, "get_shg")
    check(args is not None, "get_shg is called")
    if args:
        for keyword in ("freqs=", "eta=", "pol=", "eshift=", "gauge=",
                        "out_name="):
            check(keyword in args, f"get_shg receives {keyword[:-1]}")
        check(args.lstrip().startswith("nlodata"),
              "with the NLOData object first and positional")
    args = call_args(source, "get_shift")
    check(args is not None, "get_shift is called")
    if args:
        for keyword in ("freqs=", "eta=", "pol=", "eshift=", "out_name="):
            check(keyword in args, f"get_shift receives {keyword[:-1]}")
        # get_shift has no gauge argument at all; passing one is a TypeError.
        check("gauge=" not in args,
              "and NOT a gauge, which get_shift does not accept")
    args = call_args(source, "get_chi_tensor")
    check(args is not None, "get_chi_tensor is called")
    if args:
        for keyword in ("freqs=", "eta=", "eshift=", "out_name="):
            check(keyword in args, f"get_chi_tensor receives {keyword[:-1]}")
        check("pol=" not in args,
              "and NOT a pol, which it does not take — it returns the tensor")

    # The ground state make_nlodata will accept.
    check("'point_group': False" in source or
          '"point_group": False' in source,
          "the ground state turns the point group off, as make_nlodata asserts")
    check("parallel={'domain': 1}" in source,
          "and keeps the real-space domain on one rank for the gather")


def main():
    if len(sys.argv) < 2:
        raise SystemExit(__doc__)

    with tempfile.TemporaryDirectory() as tmp:
        script = resolve(sys.argv[1], tmp, "nlopt_all.py")
        check_call_surface(Path(script).read_text())
        ns = load(script)

        # -- Units ------------------------------------------------------------
        # get_shg returns np.vstack((freqs, chi_l)): row 0 the grid, row 1 the
        # complex susceptibility in m/V. Feed it a value whose conversions are
        # exact in decimal so a wrong power of ten cannot hide in rounding.
        print("chi^(2) unit conversion:")
        freqs = np.linspace(0.0, 4.0, 5)
        chi = np.full(5, 1e-12 + 2e-12j)        # 1 + 2i pm/V, exactly
        shg = np.vstack((freqs, chi))
        out = ns["shg_observables"](shg[0], shg[1])
        check(np.allclose(out["chi2_re_pm_V"], 1.0),
              "1e-12 m/V is 1 pm/V")
        check(np.allclose(out["chi2_im_pm_V"], 2.0),
              "and the imaginary part converts with it")
        check(np.allclose(out["chi2_abs_pm_V"], np.sqrt(5.0)),
              "|chi| is the modulus of the two, not their sum")
        check("chi2_sheet_re_nm2_V" not in out,
              "a bulk run emits no sheet columns at all")

        # A 10 A vacuum axis: 1e-12 m/V * 1e-9 m = 1e-21 m^2/V = 1e-3 nm^2/V.
        thickness = ns["sheet_thickness_m"]([3.16, 3.16, 10.0], 2)
        check(abs(thickness - 1e-9) < 1e-18,
              "the vacuum length converts from Angstrom to metres")
        check(ns["sheet_thickness_m"]([3.16, 3.16, 10.0], -1) is None,
              "and a bulk cell has no thickness to divide out")
        sheet = ns["shg_observables"](shg[0], shg[1], thickness)
        check(np.allclose(sheet["chi2_sheet_re_nm2_V"], 1e-3),
              "chi * L in m^2/V converts to nm^2/V")
        check(np.allclose(sheet["chi2_re_pm_V"], 1.0),
              "and the bulk columns survive alongside it")

        # get_shift returns a REAL conductivity in A/V^2.
        print("Shift-current unit conversion:")
        sigma = np.vstack((freqs, np.full(5, 3.0)))
        shift = ns["shift_observables"](sigma[0], sigma[1], thickness)
        check(np.allclose(shift["sigma_A_V2"], 3.0),
              "A/V^2 is reported as-is")
        check(np.allclose(shift["sigma_sheet_A_nm_V2"], 3.0),
              "and sigma * L (1e-9 m) reported in A*nm/V^2 is the same number")
        check(len(shift["energy_eV"]) == 5,
              "the energy grid rides along with the spectrum")

        # get_chi_tensor returns (3, 3, nw) complex.
        print("Linear tensor:")
        chi_vvl = np.zeros((3, 3, 5), dtype=complex)
        chi_vvl[0, 0] = 1.5 + 0.5j
        chi_vvl[2, 2] = 4.0 + 1.0j
        linear = ns["linear_observables"](freqs, chi_vvl)
        check(np.allclose(linear["chi1_xx_re"], 1.5), "chi^(1) components pass "
              "through unconverted (they are dimensionless)")
        check(np.allclose(linear["eps_xx_1"], 2.5),
              "eps_1 = 1 + Re chi, in GPAW's 4pi/eps_0 convention")
        check(np.allclose(linear["eps_zz_2"], 1.0),
              "eps_2 = Im chi")
        check(np.allclose(linear["chi1_xy_re"], 0.0),
              "and an off-diagonal that GPAW returned as zero stays zero")

        # -- Inversion symmetry ----------------------------------------------
        # The one check that decides whether the whole run means anything.
        print("Inversion symmetry:")
        inversion = ns["has_inversion_symmetry"]
        cubic = np.eye(3) * 4.0
        # Rock salt: an inversion centre on every atom. chi^(2) == 0.
        rocksalt_scaled = np.array([[0.0, 0.0, 0.0], [0.5, 0.5, 0.5]])
        check(inversion(cubic, rocksalt_scaled, np.array([12, 8])),
              "a rock-salt cell is recognized as centrosymmetric")
        # Zinc blende: the same two positions, but the two SPECIES break it.
        # Geometry alone cannot tell these apart — the atomic numbers must be
        # part of the test, and this is the pair that proves they are.
        zincblende_scaled = np.array([[0.0, 0.0, 0.0], [0.25, 0.25, 0.25]])
        check(not inversion(cubic, zincblende_scaled, np.array([31, 33])),
              "a zinc-blende cell is not, which is why GaAs has an SHG "
              "spectrum and MgO does not")
        # Same positions, one species: diamond IS centrosymmetric.
        check(inversion(cubic, zincblende_scaled, np.array([6, 6])),
              "and diamond, at the identical coordinates, is — so the species "
              "decide, not the geometry")
        # A displaced atom must break it — but only once there are enough
        # atoms for that to be possible. Any TWO atoms of one species are
        # centrosymmetric about their own midpoint whatever their separation,
        # so the case that actually tests the search is a third atom placed
        # where no centre can map the set onto itself.
        broken = np.array([[0.0, 0.0, 0.0],
                           [0.25, 0.25, 0.25],
                           [0.5, 0.1, 0.0]])
        check(not inversion(cubic, broken, np.array([6, 6, 6])),
              "a third atom off every candidate centre breaks the symmetry")
        # The complement of that: the same three atoms placed symmetrically
        # about the middle one. If this came back False the search would be
        # missing centres that do not sit at the origin.
        symmetric = np.array([[0.1, 0.0, 0.0],
                              [0.4, 0.0, 0.0],
                              [0.7, 0.0, 0.0]])
        check(inversion(cubic, symmetric, np.array([6, 6, 6])),
              "and a centre away from the origin is still found")
        # And the check must survive an origin shift, or it would report
        # "centrosymmetric" only for cells that happen to be centred.
        shifted = (rocksalt_scaled + 0.137) % 1.0
        check(inversion(cubic, shifted, np.array([12, 8])),
              "the centre is found wherever the origin happens to sit")

        print("Peak reporting:")
        peak = ns["spectrum_peak"]([1.0, 2.0, 3.0], [0.1, -5.0, 2.0])
        check(peak == (2.0, 5.0),
              "the largest |value| wins, sign included — a strongly negative "
              "chi^(2) is a peak, not a trough to skip")
        check(ns["spectrum_peak"]([], []) == (0.0, 0.0),
              "an empty spectrum reports nothing rather than raising")

    print("\nAll nonlinear-optics checks passed." if failures == 0
          else f"\n{failures} check(s) FAILED.")
    return 0 if failures == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
