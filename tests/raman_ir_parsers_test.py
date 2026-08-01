#!/usr/bin/env python3
"""Raman / IR: the VASP and Quantum ESPRESSO output parsers.

The GPAW route to a vibrational spectrum computes everything itself, so the
only thing between the physics and the answer is array algebra —
``raman_ir_math_test.py`` covers that. The other two routes are different in
kind: they read a Hessian, a Z* set and a Raman tensor out of somebody else's
text format, and neither VASP nor Quantum ESPRESSO is available here to produce
one.

Every failure mode in that reading is silent:

  * VASP prints dF/du, i.e. MINUS the Hessian. Get the sign wrong and every
    mode comes out imaginary — a complete, plausible spectrum drawn at negative
    wavenumbers.
  * VASP prints THREE dielectric tensors after a LEPSILON run (excluding local
    field effects, including them, and the ionic contribution). The Raman
    tensor differentiates the second. Matching the first — which appears
    earlier in the file — is a tens-of-percent error that lands whole in
    dalpha/du.
  * Quantum ESPRESSO's .dyn file stores force constants in Ry/bohr², unweighted
    by the masses. A missing unit conversion scales every frequency by 7.
  * ph.x writes fixed-point numbers for the dynamical matrix and exponential
    ones for the effective charges, in the same file.

So the parsers are exercised against realistic fragments whose right answers
are known by construction: an A–B spring pair whose force constants sum to zero
row-wise, and a Z* set that obeys the acoustic sum rule.

The functions are extracted BY NAME from a GENERATED script with ``ast``, so
this tests the code that ships rather than a transcription of it.

Usage:  raman_ir_parsers_test.py <calango_script_test binary>
"""
import ast
import subprocess
import sys
import tempfile
from pathlib import Path

import numpy as np
from ase.units import Bohr, Rydberg

VASP_WANTED = {"parse_vasp_hessian", "parse_vasp_born", "parse_vasp_dielectric"}
QE_WANTED = {"_blocks_of_three", "parse_qe_force_constants", "parse_qe_born",
             "parse_qe_raman"}

failures = 0


def check(condition, what):
    global failures
    print(f"  {'ok  ' if condition else 'FAIL'} {what}")
    if not condition:
        failures += 1


def load(script_path, wanted):
    """Compile just the named parsers out of a generated script.

    Nothing else comes along: no calculator construction, no subprocess call,
    no GPAW import. `re` and `np` are injected because the parsers lean on the
    script's own module-level imports, and Bohr/Rydberg because the Quantum
    ESPRESSO conversion does.
    """
    tree = ast.parse(Path(script_path).read_text())
    picked = [node for node in tree.body
              if isinstance(node, ast.FunctionDef) and node.name in wanted]
    missing = wanted - {node.name for node in picked}
    if missing:
        raise SystemExit(f"{script_path} defines no {sorted(missing)}")
    namespace = {"np": np, "re": __import__("re"),
                 "Bohr": Bohr, "Rydberg": Rydberg}
    exec(compile(ast.Module(body=picked, type_ignores=[]), script_path, "exec"),
         namespace)
    return namespace


def resolve(argument, workdir, name):
    """Path to a generated script, dumping a fresh set first if needed."""
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


# A two-atom cell whose force constants are a single spring: the diagonal is
# +k and the off-diagonal -k, so every row sums to zero and the acoustic modes
# are exactly at zero. VASP prints the NEGATIVE of that.
OUTCAR = """
 SECOND DERIVATIVES (NOT SYMMETRIZED)
 ------------------------------------
              1X          1Y          1Z          2X          2Y          2Z
 1X       -9.626170    0.000000    0.000000    9.626170    0.000000    0.000000
 1Y        0.000000   -9.626170    0.000000    0.000000    9.626170    0.000000
 1Z        0.000000    0.000000   -9.626170    0.000000    0.000000    9.626170
 2X        9.626170    0.000000    0.000000   -9.626170    0.000000    0.000000
 2Y        0.000000    9.626170    0.000000    0.000000   -9.626170    0.000000
 2Z        0.000000    0.000000    9.626170    0.000000    0.000000   -9.626170

 MACROSCOPIC STATIC DIELECTRIC TENSOR (excluding local field effects)
 ------------------------------------------------------
           3.500000     0.000000     0.000000
           0.000000     3.500000     0.000000
           0.000000     0.000000     3.500000
 ------------------------------------------------------

 MACROSCOPIC STATIC DIELECTRIC TENSOR (including local field effects in DFT)
 ------------------------------------------------------
           3.160000     0.000000    -0.000000
           0.000000     3.160000     0.000000
          -0.000000     0.000000     3.160000
 ------------------------------------------------------

 MACROSCOPIC STATIC DIELECTRIC TENSOR IONIC CONTRIBUTION
 ------------------------------------------------------
           6.000000     0.000000     0.000000
           0.000000     6.000000     0.000000
           0.000000     0.000000     6.000000
 ------------------------------------------------------

 BORN EFFECTIVE CHARGES (in e, cummulative output)
 ---------------------------------------------------------------------
 ion    1
    1     1.98000     0.00000     0.00000
    2     0.00000     1.98000     0.00000
    3     0.00000     0.00000     1.98000
 ion    2
    1    -1.98000     0.00000     0.00000
    2     0.00000    -1.98000     0.00000
    3     0.00000     0.00000    -1.98000
"""

# The same spring pair as a ph.x dynamical-matrix file: fixed-point for the
# matrix, exponential for the charges and the Raman tensor, which is what the
# real format does.
DYN = """Dynamical matrix file

MgO rock salt

  2    2    2   7.9700000   0.0000000   0.0000000   0.0000000   0.0000000   0.0000000
Basis vectors
     -0.500000000    0.000000000    0.500000000
      0.000000000    0.500000000    0.500000000
     -0.500000000    0.500000000    0.000000000
        1  'Mg '   22156.0000000000
        2  'O  '   14582.0000000000
    1    1      0.0000000000    0.0000000000    0.0000000000
    2    2      0.5000000000    0.5000000000    0.5000000000

     Dynamical  Matrix in cartesian axes

     q = (    0.000000000   0.000000000   0.000000000 )

    1    1
  0.20000000  0.00000000    0.00000000  0.00000000    0.00000000  0.00000000
  0.00000000  0.00000000    0.20000000  0.00000000    0.00000000  0.00000000
  0.00000000  0.00000000    0.00000000  0.00000000    0.20000000  0.00000000
    1    2
 -0.20000000  0.00000000    0.00000000  0.00000000    0.00000000  0.00000000
  0.00000000  0.00000000   -0.20000000  0.00000000    0.00000000  0.00000000
  0.00000000  0.00000000    0.00000000  0.00000000   -0.20000000  0.00000000
    2    1
 -0.20000000  0.00000000    0.00000000  0.00000000    0.00000000  0.00000000
  0.00000000  0.00000000   -0.20000000  0.00000000    0.00000000  0.00000000
  0.00000000  0.00000000    0.00000000  0.00000000   -0.20000000  0.00000000
    2    2
  0.20000000  0.00000000    0.00000000  0.00000000    0.00000000  0.00000000
  0.00000000  0.00000000    0.20000000  0.00000000    0.00000000  0.00000000
  0.00000000  0.00000000    0.00000000  0.00000000    0.20000000  0.00000000

     Dielectric Tensor:

      3.160000000000      0.000000000000      0.000000000000
      0.000000000000      3.160000000000      0.000000000000
      0.000000000000      0.000000000000      3.160000000000

     Effective Charges E-U: Z_{alpha}{s,beta}

     atom #    1
      1.980000000000E+00  0.000000000000E+00  0.000000000000E+00
      0.000000000000E+00  1.980000000000E+00  0.000000000000E+00
      0.000000000000E+00  0.000000000000E+00  1.980000000000E+00
     atom #    2
     -1.980000000000E+00  0.000000000000E+00  0.000000000000E+00
      0.000000000000E+00 -1.980000000000E+00  0.000000000000E+00
      0.000000000000E+00  0.000000000000E+00 -1.980000000000E+00

     Raman tensor (A^2)

     atom #    1    pol.  1
      0.500000000000E+00  0.100000000000E+00  0.000000000000E+00
      0.100000000000E+00  0.300000000000E+00  0.000000000000E+00
      0.000000000000E+00  0.000000000000E+00  0.300000000000E+00
     atom #    1    pol.  2
      0.300000000000E+00  0.100000000000E+00  0.000000000000E+00
      0.100000000000E+00  0.500000000000E+00  0.000000000000E+00
      0.000000000000E+00  0.000000000000E+00  0.300000000000E+00
     atom #    1    pol.  3
      0.300000000000E+00  0.000000000000E+00  0.000000000000E+00
      0.000000000000E+00  0.300000000000E+00  0.000000000000E+00
      0.000000000000E+00  0.000000000000E+00  0.500000000000E+00
     atom #    2    pol.  1
     -0.500000000000E+00  0.000000000000E+00  0.000000000000E+00
      0.000000000000E+00 -0.300000000000E+00  0.000000000000E+00
      0.000000000000E+00  0.000000000000E+00 -0.300000000000E+00
     atom #    2    pol.  2
     -0.300000000000E+00  0.000000000000E+00  0.000000000000E+00
      0.000000000000E+00 -0.500000000000E+00  0.000000000000E+00
      0.000000000000E+00  0.000000000000E+00 -0.300000000000E+00
     atom #    2    pol.  3
     -0.300000000000E+00  0.000000000000E+00  0.000000000000E+00
      0.000000000000E+00 -0.300000000000E+00  0.000000000000E+00
      0.000000000000E+00  0.000000000000E+00 -0.500000000000E+00

     Diagonalizing the dynamical matrix
"""


def main():
    if len(sys.argv) < 2:
        raise SystemExit(__doc__)

    with tempfile.TemporaryDirectory() as tmp:
        vasp = load(resolve(sys.argv[1], tmp, "raman_ir_vasp.py"), VASP_WANTED)
        print("VASP OUTCAR:")
        hessian = vasp["parse_vasp_hessian"](OUTCAR, 2)
        check(hessian.shape == (6, 6), "the whole 3N x 3N table is read")
        # The sign IS the test: VASP printed -9.626 on the diagonal.
        check(abs(hessian[0, 0] - 9.62617) < 1e-6,
              "dF/du is negated, so the diagonal comes out positive")
        check(abs(hessian[0, 3] + 9.62617) < 1e-6,
              "and the inter-atomic block negative")
        check(abs(hessian.sum()) < 1e-9,
              "rows sum to zero, as a translation-invariant Hessian must")

        born = vasp["parse_vasp_born"](OUTCAR, 2)
        check(born.shape == (2, 3, 3), "one Z* tensor per ion")
        check(abs(born[0, 0, 0] - 1.98) < 1e-9
              and abs(born[1, 2, 2] + 1.98) < 1e-9,
              "with the values and signs the file carries")
        check(abs(born.sum()) < 1e-9, "Z* obeys the acoustic sum rule")

        eps = vasp["parse_vasp_dielectric"](OUTCAR)
        # 3.5 appears FIRST in the file and 6.0 is the ionic block; only 3.16
        # is the clamped-ion electronic response the Raman tensor wants.
        check(abs(eps[0, 0] - 3.16) < 1e-9,
              f"the local-field-corrected DFT block is picked ({eps[0, 0]}), "
              "not the earlier excluding-LFE one or the ionic one")

        qe = load(resolve(sys.argv[1], tmp, "raman_ir_espresso.py"), QE_WANTED)
        print("Quantum ESPRESSO .dyn file:")
        force_constants = qe["parse_qe_force_constants"](DYN, 2)
        check(force_constants.shape == (6, 6),
              "every atom-pair block lands in the right place")
        expected = 0.20 * Rydberg / Bohr ** 2
        check(abs(force_constants[0, 0] - expected) < 1e-6,
              f"Ry/bohr^2 converted to eV/A^2 ({force_constants[0, 0]:.4f} "
              f"vs {expected:.4f})")
        check(abs(force_constants[0, 3] + expected) < 1e-6,
              "including the off-diagonal (1, 2) block")
        check(abs(force_constants.sum()) < 1e-9,
              "rows sum to zero, as for the VASP fragment")

        zstar = qe["parse_qe_born"](DYN[DYN.index("Effective Charges"):], 2)
        check(zstar.shape == (2, 3, 3), "one Z* tensor per atom")
        check(abs(zstar[0, 0, 0] - 1.98) < 1e-9
              and abs(zstar[1, 1, 1] + 1.98) < 1e-9,
              "read from exponential notation, unlike the matrix above")
        check(abs(zstar.sum()) < 1e-9, "Z* obeys the acoustic sum rule")

        raman = qe["parse_qe_raman"](DYN, 2)
        check(raman is not None and raman.shape == (2, 3, 3, 3),
              "the Raman tensor is [atom, polarization, i, j]")
        check(abs(raman[0, 0, 0, 0] - 0.5) < 1e-9
              and abs(raman[0, 0, 0, 1] - 0.1) < 1e-9,
              "with its off-diagonal elements intact")
        check(abs(raman[1, 2, 2, 2] + 0.5) < 1e-9,
              "and the last (atom, polarization) block in the right slot")
        # The Raman header is 'atom #  1  pol.  1' and the Z* header is
        # 'atom #  1'; a pattern that confused them would return 3N tensors
        # where N were expected, or vice versa.
        check(len(qe["_blocks_of_three"](
            DYN[DYN.index("Effective Charges"):DYN.index("Raman tensor")],
            r'atom\s+#\s*\d+\s*\n')) == 2,
              "the Z* header does not match the Raman one")
        check(qe["parse_qe_raman"](DYN[:DYN.index("Raman tensor")], 2) is None,
              "a run without lraman returns None, so the caller can explain "
              "the pseudopotential restriction instead of raising a KeyError")

    print("\nAll parser checks passed." if failures == 0
          else f"\n{failures} check(s) FAILED.")
    return 0 if failures == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
