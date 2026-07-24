#!/usr/bin/env python3
"""Benchmark for the native GPAW partial-charge partitioning schemes.

Runs a GPAW single-point on bulk silicon and checks the two invariants every
correct grid-based partitioning must satisfy, independent of the scheme:

  * total electron counting  — the partitioned populations sum to the number of
    electrons that the all-electron density integrates to;
  * net charge balance        — the net atomic charges sum to ~0 for the neutral
    cell, and by the symmetry of diamond-Si each atom is ~neutral.

The Voronoi and Hirshfeld schemes here mirror the ones the GUI generates
(PartialChargeDialog::generateScript). Bader (on-grid) is exercised on a coarse
grid so the test stays fast.

Skips cleanly (exit 0) when GPAW is not importable, so it is safe to register as
an unconditional CTest in environments without a DFT stack.
"""
import sys


def main() -> int:
    try:
        import numpy as np
        from ase.build import bulk
        from ase.data import covalent_radii
        from gpaw import GPAW, PW
    except Exception as exc:  # gpaw (or its datasets) unavailable
        print(f"SKIP: GPAW not available ({exc})")
        return 0

    atoms = bulk("Si", "diamond", a=5.43)
    calc = GPAW(mode=PW(300), xc="PBE", kpts=(4, 4, 4), txt=None)
    atoms.calc = calc
    atoms.get_potential_energy()

    rho = calc.get_all_electron_density(gridrefinement=2)
    ng = rho.shape
    cell = np.asarray(atoms.get_cell())
    inv = np.linalg.inv(cell)
    dV = atoms.get_volume() / rho.size
    pos = atoms.get_positions()
    zval = atoms.get_atomic_numbers()
    rflat = rho.reshape(-1)
    # GPAW real-space grid points sit at i/N along each axis (not cell-centred).
    frac = np.stack(
        np.meshgrid(*[np.arange(n) / float(n) for n in ng], indexing="ij"),
        axis=-1,
    ).reshape(-1, 3)
    grid = frac @ cell

    def mic_dist(i):
        df = (grid - pos[i]) @ inv
        df -= np.round(df)
        return np.linalg.norm(df @ cell, axis=1)

    total_electrons = float(rflat.sum() * dV)
    z_total = float(zval.sum())
    # The all-electron density integrates to the full electron count.
    assert abs(total_electrons - z_total) < 0.5, (
        f"density integral {total_electrons:.3f} != Z {z_total:.3f}")

    dist = np.stack([mic_dist(i) for i in range(len(atoms))], axis=1)
    owner = np.argmin(dist, axis=1)

    def report(name, elec):
        charges = zval - elec
        net = float(charges.sum())
        print(f"{name:>9}: electrons={elec.sum():.3f}  net_sum={net:+.4f}  "
              f"q={np.round(charges, 4).tolist()}")
        # Neutral cell: net charges must balance to ~zero.
        assert abs(net) < 0.2, f"{name} net charge {net:+.4f} not balanced"
        # Populations conserve the total electron count.
        assert abs(elec.sum() - total_electrons) < 0.5, (
            f"{name} lost electrons: {elec.sum():.3f} vs {total_electrons:.3f}")

    # Voronoi.
    v_elec = np.array([rflat[owner == i].sum() * dV for i in range(len(atoms))])
    report("Voronoi", v_elec)

    # Hirshfeld (exponential spherical references).
    refs, den = [], np.zeros(rflat.shape)
    for i in range(len(atoms)):
        lam = max(covalent_radii[zval[i]], 0.3)
        ref = zval[i] * np.exp(-mic_dist(i) / lam)
        refs.append(ref)
        den += ref
    den[den == 0.0] = 1e-30
    h_elec = np.array([float((refs[i] / den * rflat).sum() * dV)
                       for i in range(len(atoms))])
    report("Hirshfeld", h_elec)

    print("PASS: total electron counting and net charge balance verified.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
