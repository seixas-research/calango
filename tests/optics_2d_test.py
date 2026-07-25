#!/usr/bin/env python3
"""2D optical observables: units and vacuum-thickness invariance.

The 2D quantities exist for exactly one reason: a slab supercell's eps_3D is
NOT a property of the material — double the vacuum padding and it moves. Only
after the arbitrary thickness is divided back out do alpha_2D, sigma_2D and the
absorbance describe the sheet. This test checks that the generated code
actually delivers that, and that its unit conventions are the ones the labels
claim (a wrong 4*pi or hbar*c factor is invisible to review and to py_compile).

The reference is graphene's universal optical conductivity, sigma = e^2/(4 hbar)
= (pi/2) e^2/h, which gives an absorbance of exactly pi*alpha = 2.293% —
a landmark derived here independently of the code under test.

``twod_observables`` is extracted from a generated script BY NAME with ``ast``,
so this exercises the shipped code rather than a transcription of it.

Usage:  optics_2d_test.py <calango_script_test binary | generated .py>
"""
import ast
import subprocess
import sys
import tempfile
from pathlib import Path

import numpy as np

WANTED = {"twod_observables"}
CONSTANTS = {"hbar_c_eV_A", "alpha_fs"}

ALPHA = 1.0 / 137.035999084
HBAR_C_EV_A = 1973.269804

failures = 0


def check(condition, what):
    global failures
    print(f"  {'ok  ' if condition else 'FAIL'} {what}")
    if not condition:
        failures += 1


def close(a, b, tol=1e-9):
    return bool(np.all(np.abs(np.asarray(a) - np.asarray(b)) <= tol))


def load(script_path):
    tree = ast.parse(Path(script_path).read_text())
    picked = []
    for node in tree.body:
        if isinstance(node, ast.FunctionDef) and node.name in WANTED:
            picked.append(node)
        elif isinstance(node, ast.Assign) and any(
            isinstance(t, ast.Name) and t.id in CONSTANTS for t in node.targets
        ):
            picked.append(node)
    if not any(isinstance(n, ast.FunctionDef) for n in picked):
        raise SystemExit("generated script defines no twod_observables()")
    namespace = {"np": np}
    exec(compile(ast.Module(body=picked, type_ignores=[]), script_path, "exec"),
         namespace)
    return namespace


def resolve_script(argument, workdir):
    if argument.endswith(".py"):
        return argument
    dump = Path(workdir) / "scripts"
    dump.mkdir()
    subprocess.run([argument, "--dump", str(dump)], check=True,
                   stdout=subprocess.DEVNULL)
    script = dump / "optics_2d.py"
    if not script.exists():
        raise SystemExit(f"{argument} --dump produced no {script.name}")
    return str(script)


def graphene_eps2(omega_eV, L_z):
    """eps2 of a slab holding a sheet of graphene's universal conductivity.

    Derived independently of the code under test: a 2D conductivity sigma_2D
    spread over a slab of thickness L_z behaves as a 3D conductivity
    sigma_2D / L_z, and eps2 = 4 pi sigma_3D / omega. With sigma expressed in
    Gaussian sigma/c (dimensionless) and omega/(hbar c) in 1/Å,

        eps2 = 4 pi (sigma/c) / (k L_z),   k = omega / (hbar c).

    Graphene: sigma/c = alpha/4.
    """
    k = np.asarray(omega_eV, dtype=float) / HBAR_C_EV_A
    return 4.0 * np.pi * (ALPHA / 4.0) / (k * L_z)


def main():
    if len(sys.argv) < 2:
        raise SystemExit(__doc__)

    with tempfile.TemporaryDirectory() as tmp:
        ns = load(resolve_script(sys.argv[1], tmp))
    twod = ns["twod_observables"]

    omega = np.array([1.0, 2.0, 3.0])

    # -- Graphene: the absolute reference -----------------------------------
    print("Graphene's universal absorbance:")
    L_z = 20.0
    eps2 = graphene_eps2(omega, L_z)
    result = twod(omega, np.ones_like(omega), eps2, L_z)
    check(close(result["absorbance"], np.pi * ALPHA, 1e-12),
          f"A = pi*alpha = {np.pi * ALPHA:.6f} at every frequency")
    check(close(result["sigma_2D_re"], np.pi / 2.0, 1e-9),
          "Re[sigma_2D] = pi/2 in e^2/h, the universal value")

    # -- The whole point: none of it may depend on the vacuum ---------------
    print("Vacuum-thickness invariance:")
    thick = twod(omega, np.ones_like(omega), graphene_eps2(omega, 60.0), 60.0)
    for quantity in ("absorbance", "sigma_2D_re", "alpha_2D_im_A"):
        check(close(result[quantity], thick[quantity], 1e-9),
              f"{quantity} is unchanged when the vacuum triples")
    # eps_3D itself must NOT be invariant — if it were, there would be nothing
    # to correct and the whole 2D module would be pointless.
    check(not close(eps2, graphene_eps2(omega, 60.0), 1e-6),
          "eps_3D itself does depend on the vacuum (the reason for all this)")

    # -- Internal consistency of the reported set ---------------------------
    print("Relations among the reported quantities:")
    # A = 4 pi Re[sigma_2D/c]; with sigma reported in e^2/h that is A =
    # 2 alpha sigma[e^2/h]. Two separately computed arrays must agree.
    check(close(result["absorbance"],
                2.0 * ALPHA * np.asarray(result["sigma_2D_re"]), 1e-12),
          "A = 2 alpha sigma_2D[e^2/h]")
    # sigma_2D = -i omega alpha_2D: the real part comes from Im[alpha_2D] and
    # the imaginary part from Re[alpha_2D], with a sign flip.
    k = omega / HBAR_C_EV_A
    scale = 2.0 * np.pi / ALPHA
    check(close(result["sigma_2D_re"],
                k * np.asarray(result["alpha_2D_im_A"]) * scale, 1e-9),
          "Re[sigma_2D] = omega Im[alpha_2D]")
    check(close(result["sigma_2D_im"],
                -k * np.asarray(result["alpha_2D_re_A"]) * scale, 1e-9),
          "Im[sigma_2D] = -omega Re[alpha_2D]")

    # -- Vacuum contributes nothing ----------------------------------------
    print("Empty cell:")
    empty = twod(omega, np.ones_like(omega), np.zeros_like(omega), 25.0)
    check(close(empty["alpha_2D_re_A"], 0.0, 1e-12),
          "eps_3D = 1 gives alpha_2D = 0 (the -1 removes the vacuum)")
    check(close(empty["absorbance"], 0.0, 1e-12),
          "and nothing is absorbed")

    print("\nAll 2D optics checks passed." if failures == 0
          else f"\n{failures} check(s) FAILED.")
    return 0 if failures == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
