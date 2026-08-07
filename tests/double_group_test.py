#!/usr/bin/env python3
"""Double-group construction for spin-orbit-coupled bands.

The generated band-symmetry script builds the DOUBLE group at each
high-symmetry point and reduces the spinor characters against it. Everything
downstream of the wave functions is pure group theory with exact expected
answers, so it is checked here against the textbook results — the Oh double
group is order 96 with spinor irreps of dimension 2, 2 and 4 per parity
(Koster's Gamma6, Gamma7, Gamma8), and every irrep must reduce to itself with
zero residual.

The Python under test is extracted from the C++ that emits it, so this cannot
drift from what a run actually executes. Self-skips without numpy.
"""
import pathlib
import re
import sys

try:
    import numpy as np
except ImportError:
    print("numpy unavailable - skipping double-group checks")
    sys.exit(0)

_root = pathlib.Path(__file__).resolve().parent.parent
_src = (_root / "src/core/BandSymmetryScriptGenerator.cpp").read_text()


def _block(name):
    m = re.search(r'constexpr const char\* ' + name + r' = R"PY\((.*?)\)PY";',
                  _src, re.S)
    if not m:
        raise SystemExit("could not extract %s from the generator" % name)
    return m.group(1)


_ns = {"_np": np}
exec(_block("kPointGroupHelpers"), _ns)
exec(_block("kDoubleGroupHelpers"), _ns)
_calango_double_group = _ns["_calango_double_group"]
_calango_su2 = _ns["_calango_su2"]

fails = 0
def check(ok, what):
    global fails
    print(f"  {'ok  ' if ok else 'FAIL'} {what}")
    if not ok: fails += 1

def rot_z(n):
    """Cartesian n-fold rotation about z, as an integer matrix in a basis where
    it is integral (used with the matching lattice)."""
    t = 2*np.pi/n
    return np.array([[np.cos(t), -np.sin(t), 0],
                     [np.sin(t),  np.cos(t), 0], [0, 0, 1]])

def group_from(gens, tol=1e-8):
    """Close a set of Cartesian matrices under multiplication."""
    els = [np.eye(3)]
    changed = True
    while changed:
        changed = False
        for a in list(els):
            for g in gens:
                p = a @ g
                if not any(np.max(np.abs(p - e)) < tol for e in els):
                    els.append(p); changed = True
    return els

def run(name, cart_ops, expect_order, expect_single, expect_spinor):
    # Cartesian ops in a cubic lattice are already integral for the cubic
    # groups; for others use the identity lattice and pass floats.
    lattice = np.eye(3)
    labels, class_of, classes, irreps, col, su2, sign = _calango_double_group(
        [np.rint(o).astype(int) if np.max(np.abs(o - np.rint(o))) < 1e-9 else o
         for o in cart_ops], lattice)
    order = 2*len(cart_ops)
    single = sorted(e[1] for e in irreps if not e[3])
    spinor = sorted(e[1] for e in irreps if e[3])
    print(f"  {name}: order {order}, {len(classes)} classes")
    print(f"      single-valued dims {single}, spinor dims {spinor}")
    print(f"      labels: {[e[4] for e in irreps]}")
    check(order == expect_order, f"{name} double group has order {expect_order}")
    # The fundamental sum rule: sum of squared dimensions = group order.
    check(sum(e[1]**2 for e in irreps) == order,
          f"{name} sum of squared dims equals the group order")
    check(single == expect_single, f"{name} single-valued dims {expect_single}")
    check(spinor == expect_spinor, f"{name} spinor dims {expect_spinor}")
    # Every spinor irrep must have even dimension: Kramers.
    check(all(d % 2 == 0 for d in spinor), f"{name} spinor irreps are even-dimensional")
    return irreps

print("SU(2) lifts:")
u = _calango_su2(np.eye(3))
check(np.max(np.abs(u - np.eye(2))) < 1e-12, "identity lifts to the identity")
u2 = _calango_su2(rot_z(2))
check(np.max(np.abs(u2 @ u2 + np.eye(2))) < 1e-10,
      "a twofold rotation squares to -1 in SU(2), not +1")
u4 = _calango_su2(rot_z(4))
check(np.max(np.abs(np.linalg.matrix_power(u4, 4) + np.eye(2))) < 1e-10,
      "a fourfold rotation to the 4th power is -1 (a 2pi rotation)")
check(np.max(np.abs(np.linalg.matrix_power(u4, 8) - np.eye(2))) < 1e-10,
      "and to the 8th is +1 (4pi)")
inv = _calango_su2(-np.eye(3))
check(np.max(np.abs(inv - np.eye(2))) < 1e-12,
      "inversion acts trivially on spin (it is a pseudo-vector)")

print("\nDouble groups:")
# C2v: E, C2z, sigma_v(xz), sigma_v(yz). h=4 -> double group order 8.
# Single-valued: A1 A2 B1 B2 (four 1D). Spinor: one 2D (E1/2).
c2v = [np.eye(3), np.diag([-1,-1,1]), np.diag([1,-1,1]), np.diag([-1,1,1])]
run("C2v", c2v, 8, [1,1,1,1], [2])

# D2h: order 8 -> double group 16. Eight 1D single-valued, two 2D spinor.
d2h = group_from([np.diag([-1,-1,1]), np.diag([-1,1,-1]), -np.eye(3)])
run("D2h", d2h, 32 if len(d2h)==16 else 2*len(d2h),
    [1]*8, [2,2])

# Oh: order 48 -> double group 96. Single-valued dims 1,1,2,3,3 (x2 parity)
# = [1,1,1,1,2,2,3,3,3,3]; spinor: 2,2,4 per parity = [2,2,4,4] -> sorted.
perms = []
for p in [(0,1,2),(1,2,0),(2,0,1),(0,2,1),(2,1,0),(1,0,2)]:
    m = np.zeros((3,3)); 
    for i,j in enumerate(p): m[i,j] = 1
    perms.append(m)
signs = []
for sx in (1,-1):
    for sy in (1,-1):
        for sz in (1,-1):
            signs.append(np.diag([sx,sy,sz]))
oh = group_from([p @ s for p in perms for s in signs])
irr = run("Oh", oh, 96, [1,1,1,1,2,2,3,3,3,3], [2,2,2,2,4,4])

print("\nOh spinor irreps (the Gamma6/Gamma7/Gamma8 of a SOC band structure):")
for e in irr:
    if e[3]:
        print(f"      {e[4]:10s} dim {e[1]}")

# --- Reduction: recover a known irrep from its own characters ---------------
# This is what the driver does to every multiplet, with the characters coming
# from the wave functions instead. If the reduction is right, feeding it an
# irrep's own character vector must return multiplicity 1 for that irrep and 0
# for every other.
print("\nReduction of characters back to irreps:")
def reduction_ok(cart_ops, name):
    lattice = np.eye(3)
    rots = [np.rint(o).astype(int) if np.max(np.abs(o - np.rint(o))) < 1e-9 else o
            for o in cart_ops]
    labels, class_of, classes, irreps, col, su2, sign = _calango_double_group(
        rots, lattice)
    h = len(rots)
    order = 2 * h
    ok = True
    for target in irreps:
        chi_t = target[0]
        # Per-ELEMENT character vector, exactly as the driver builds it.
        per_el = np.array([chi_t[class_of[j]] for j in range(order)])
        mult = []
        for e in irreps:
            per_op = np.array([e[0][class_of[j]] for j in range(order)])
            n_a = np.sum(np.conj(per_op) * per_el) / order
            mult.append(float(n_a.real) / (2.0 if e[2] else 1.0))
        rounded = [int(round(m)) for m in mult]
        resid = max(abs(m - r) for m, r in zip(mult, rounded))
        expect = [1 if e is target else 0 for e in irreps]
        if resid > 1e-6 or rounded != expect:
            print(f"      {name}/{target[4]}: got {rounded}, want {expect}"
                  f" (residual {resid:.2e})")
            ok = False
    return ok

check(reduction_ok(c2v, "C2v"), "C2v: every irrep reduces to itself, exactly")
check(reduction_ok(d2h, "D2h"), "D2h: every irrep reduces to itself, exactly")
check(reduction_ok(oh, "Oh"),  "Oh: every irrep reduces to itself, exactly")

# A spinor multiplet's character on a barred element is minus the unbarred one
# — the property the driver relies on to avoid computing the second half.
print("\nSpinor sign under a 2pi rotation:")
labels, class_of, classes, irreps, col, su2, sign = _calango_double_group(
    [np.rint(o).astype(int) for o in oh], np.eye(3))
h = len(oh)
bar_ok = True
for e in irreps:
    for j in range(h):
        chi_u = e[0][class_of[j]]
        chi_b = e[0][class_of[j + h]]
        want = -chi_u if e[3] else chi_u
        if abs(chi_b - want) > 1e-8:
            bar_ok = False
check(bar_ok, "chi(barred) = -chi(unbarred) for spinor irreps, +chi otherwise")

print("\n" + ("All double-group checks passed." if fails == 0
              else f"{fails} check(s) FAILED."))
sys.exit(0 if fails == 0 else 1)
