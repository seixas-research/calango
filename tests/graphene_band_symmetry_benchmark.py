#!/usr/bin/env python3
"""Band-symmetry classification validated on graphene.

The reference is E. Kogan and V. U. Nazarov, "Symmetry classification of energy
bands in graphene", Phys. Rev. B 85, 115418 (2012) — a first-principles study
whose entire content is the irreducible representation each graphene band
realizes at the high-symmetry points, which makes it a table of expected
answers rather than a number to reproduce approximately.

Graphene is space group P6/mmm (191). The little group is D_6h at Gamma, D_3h
at K, and C_2v along the Gamma-K line. This runs the SCRIPT THE GENERATOR
EMITS, so the little-group selection, the character evaluation, the numerically
generated character table and the JSON schema are all exercised as they ship.

WHAT THE PAPER SAYS, AND WHERE THIS DISAGREES WITH IT
-----------------------------------------------------
Reproduced exactly:

  * K, the two bands meeting at the Fermi level (the Dirac point) form ONE
    two-dimensional multiplet, E''.                     [paper, Sec. II + Eq. 12]
  * K, the three sigma valence bands are A1' + E'.      [paper, Eq. 11]
  * Gamma, the lowest valence band is A1g.              [paper, Sec. II]
  * Gamma, the pi* conduction band is B2g.              [paper, Sec. II]
  * Gamma-K line, the sigma bands run A1, B1, A1.       [paper, Sec. II]

Two Gamma-point labels in the paper are NOT reproduced, and the disagreement is
in the paper rather than here:

  * The pi valence band. The paper calls it A1u; it is A2u. The pi manifold at
    Gamma is spanned by p_z(A) +- p_z(B), and decomposing that two-dimensional
    space under D_6h gives A2u + B2g — A1u is not in it at all. The paper's own
    B2g for pi* fixes the other member: pi and pi* differ by the sublattice
    antisymmetrization, which carries B1u, and A2u (x) B1u = B2g, whereas
    A1u (x) B1u = B1g. A1u is inconsistent with the paper's own pi*.

  * The sigma doublet. The paper narrows it correctly to "either E1u or E2g",
    then picks E1u by "assuming that these two bands produce a bonding (that is
    symmetric) orbital". That assumption is what fails: for a p-sigma bond the
    BONDING combination is the sublattice-ANTIsymmetric one (the lobes have to
    meet head-on), which is even under inversion, hence E2g. E1u is the
    antibonding sigma* pair, which this calculation duly finds above the Fermi
    level.

Both corrected labels are what the paper's OWN compatibility relations
(its Table III) then require: with Gamma = {A1g, A2u, E2g} the Gamma-K line
must carry {A1, A1, B1, B2}, and that multiset is checked below.

Skips cleanly (exit 0) without GPAW or spglib.

Usage:  graphene_band_symmetry_benchmark.py <calango_script_test binary>
"""
import json
import os
import subprocess
import sys
import tempfile

# Cell / convergence parameters. The vacuum has to be generous: the paper used
# a 200 bohr separation precisely so the interlayer (image-potential) states
# would not crowd the low conduction bands, and identifying pi* is the one
# assertion here that competes with them. 12 A is enough once pi* is picked out
# by its p_z weight rather than by its energy.
VACUUM_A = 12.0
PW_CUTOFF_EV = 400.0
SCF_KPTS = 12
NBANDS = 16
CONVERGED_BANDS = 12


def _bootstrap_gpaw_env() -> None:
    """Re-exec under the GPAW conda env from ~/.calango/settings.json."""
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


failures = 0


def check(condition, what):
    global failures
    print(f"  {'ok  ' if condition else 'FAIL'} {what}", flush=True)
    if not condition:
        failures += 1


# --- Kogan & Nazarov, Table III -------------------------------------------
# Compatibility relations for the honeycomb lattice along the Gamma-K line:
# which C_2v representation each D_6h / D_3h representation restricts to. Read
# straight off the paper's table, and used below to check that the labels found
# at Gamma, on the line and at K are mutually consistent — a classification can
# be self-consistently wrong at one k-point, but not at three linked ones.
D6H_TO_C2V = {
    "A1g": "A1", "B1u": "A1", "E1u": ("A1", "B1"), "E2g": ("A1", "B1"),
    "A1u": "A2", "B1g": "A2", "E1g": ("A2", "B2"), "E2u": ("A2", "B2"),
    "A2g": "B1", "B2u": "B1",
    "A2u": "B2", "B2g": "B2",
}
D3H_TO_C2V = {
    "A1'": "A1", "A1''": "A2", "A2'": "B1", "A2''": "B2",
    "E'": ("A1", "B1"), "E''": ("A2", "B2"),
}


def build_baseline(workdir):
    """A converged graphene ground state, saved the way a Single Point does.

    The band count is set HERE rather than in the band script: the shipped
    workflow restarts from this file and inherits its parameters, which is
    exactly the inheritance the Electronic Structure wizard advertises.
    """
    from ase.build import graphene
    from gpaw import GPAW, PW

    atoms = graphene(vacuum=VACUUM_A / 2.0)
    calc = GPAW(mode=PW(PW_CUTOFF_EV), xc="PBE", kpts=(SCF_KPTS, SCF_KPTS, 1),
                occupations={"name": "fermi-dirac", "width": 0.02},
                nbands=NBANDS, convergence={"bands": CONVERGED_BANDS},
                symmetry={"tolerance": 1e-5},
                txt=os.path.join(workdir, "scf.txt"))
    atoms.calc = calc
    atoms.get_potential_energy()
    baseline = os.path.join(workdir, "single_point.gpw")
    calc.write(baseline, mode="all")
    return atoms, baseline


def point_by_label(symmetry, label):
    for point in symmetry["points"]:
        if point["label"] == label:
            return point
    return None


def multiplets(point):
    """Spin-0 multiplets, ordered by energy."""
    spins = point.get("spins") or []
    if not spins:
        return []
    return sorted(spins[0]["multiplets"], key=lambda m: m["energy_eV"])


def pz_weight(fatbands, kpoint_index, bands):
    """Summed C p_z weight of a multiplet."""
    for projection in fatbands["projections"]:
        if projection["label"] != "C p_z":
            continue
        weights = projection["weights"][0][kpoint_index]
        return sum(weights[b] for b in bands if b < len(weights))
    return 0.0


def main() -> int:
    if len(sys.argv) < 2:
        raise SystemExit(__doc__)
    binary = sys.argv[1]

    try:
        import numpy as np  # noqa: F401
        import spglib  # noqa: F401
        from gpaw import GPAW  # noqa: F401
    except Exception as exc:
        print(f"SKIP: GPAW/spglib unavailable ({exc})")
        return 0

    with tempfile.TemporaryDirectory() as workdir:
        dump = os.path.join(workdir, "scripts")
        os.mkdir(dump)
        subprocess.run([binary, "--dump", dump], check=True,
                       stdout=subprocess.DEVNULL)
        source = os.path.join(dump, "bands_graphene_symmetry.py")
        if not os.path.exists(source):
            print("FAIL: --dump produced no bands_graphene_symmetry.py")
            return 1

        print("Graphene ground state:", flush=True)
        atoms, baseline = build_baseline(workdir)
        check(os.path.exists(baseline), "baseline density written")

        job = os.path.join(workdir, "job")
        os.makedirs(job)
        with open(source) as handle:
            script = handle.read()
        # The ONLY substitution: where the baseline lives. Everything else in
        # the script — the path, the tolerances, the projections — is what the
        # generator emitted.
        script = script.replace("/jobs/proc_1/single_point.gpw", baseline)
        check('path_str = "GKMG"' in script,
              "the generated script walks Gamma-K-M-Gamma")
        run_py = os.path.join(job, "bands.py")
        with open(run_py, "w") as handle:
            handle.write(script)
        from ase.io import write as ase_write
        ase_write(os.path.join(job, "structure.extxyz"), atoms)

        print("Generated band-structure script:", flush=True)
        completed = subprocess.run([sys.executable, run_py], cwd=job,
                                   capture_output=True, text=True)
        if completed.returncode != 0:
            print(f"  FAIL exited {completed.returncode}")
            print(completed.stdout[-4000:])
            print(completed.stderr[-4000:])
            return 1
        check(True, "runs to completion")

        for name in ("bands.json", "band_symmetry.json", "fatbands.json"):
            check(os.path.exists(os.path.join(job, name)), f"{name} written")
        with open(os.path.join(job, "band_symmetry.json")) as handle:
            symmetry = json.load(handle)
        with open(os.path.join(job, "fatbands.json")) as handle:
            fatbands = json.load(handle)

        # --- the crystal it thinks it has ---------------------------------
        print("Crystallography:", flush=True)
        check(symmetry["space_group_number"] == 191,
              f"space group P6/mmm (191), got "
              f"{symmetry['space_group']} ({symmetry['space_group_number']})")
        # ase.build.graphene() puts an atom at the origin, where the site
        # symmetry is -6m2; the 6/mmm centre is the hexagon at (1/3, 2/3). The
        # classification has to find that itself or the K-point characters pick
        # up origin-dependent phases.
        check(symmetry["nonsymmorphic_residual"] < 1e-6,
              "the symmetry centre was located (P6/mmm is symmorphic)")
        for label in ("G", "K", "M"):
            check(point_by_label(symmetry, label) is not None,
                  f"{label} classified")
        check(not any(p["projective"] for p in symmetry["points"]),
              "no projective representations (as expected for a symmorphic "
              "group)")

        gamma = point_by_label(symmetry, "G")
        kpoint = point_by_label(symmetry, "K")
        efermi = symmetry["efermi"]

        # --- fatbands: p_z is the pi manifold ------------------------------
        # Asserted first because the symmetry checks below USE it to tell pi
        # from the interlayer states that share its energy range.
        print("Orbital projections:", flush=True)
        labels = [p["label"] for p in fatbands["projections"]]
        check("C p_z" in labels and "C s" in labels,
              f"both requested channels produced, got {labels}")
        gamma_states = multiplets(gamma)
        lowest = gamma_states[0]
        check(pz_weight(fatbands, gamma["kpoint_index"], lowest["bands"]) < 0.02,
              "the lowest sigma band at Gamma carries no p_z weight")
        pi_like = [m for m in gamma_states
                   if pz_weight(fatbands, gamma["kpoint_index"],
                                m["bands"]) > 0.30]
        check(len(pi_like) >= 2,
              f"a pi and a pi* band are found by p_z weight "
              f"({len(pi_like)} p_z-dominated multiplets at Gamma)")

        # --- Gamma ---------------------------------------------------------
        print("Gamma point (little group D_6h):", flush=True)
        occupied = [m for m in gamma_states if m["energy_eV"] < efermi]
        check(sum(m["degeneracy"] for m in occupied) == 4,
              f"four occupied bands, got "
              f"{sum(m['degeneracy'] for m in occupied)}")
        check(lowest["label"] == "A1g" and lowest["degeneracy"] == 1,
              f"lowest valence band is A1g [paper], got {lowest['label']}")

        doublets = [m for m in occupied if m["degeneracy"] == 2]
        check(len(doublets) == 1,
              f"one degenerate sigma pair among the occupied bands, got "
              f"{len(doublets)}")
        # The paper prints E1u here; see the module docstring for why E2g is
        # the correct label and why the paper's own pi* label requires it.
        check(bool(doublets) and doublets[0]["label"] == "E2g",
              f"the sigma doublet is E2g (paper prints E1u — its "
              f"'bonding = symmetric' assumption fails for a p-sigma bond), "
              f"got {doublets[0]['label'] if doublets else 'none'}")

        occupied_pi = [m for m in occupied
                       if pz_weight(fatbands, gamma["kpoint_index"],
                                    m["bands"]) > 0.30]
        check(len(occupied_pi) == 1,
              f"exactly one occupied pi band, got {len(occupied_pi)}")
        # The paper prints A1u; A1u is not in the p_z manifold's decomposition
        # at all. See the module docstring.
        check(bool(occupied_pi) and occupied_pi[0]["label"] == "A2u",
              f"the pi band is A2u (paper prints A1u, which is inconsistent "
              f"with its own B2g for pi*), got "
              f"{occupied_pi[0]['label'] if occupied_pi else 'none'}")

        empty_pi = [m for m in gamma_states
                    if m["energy_eV"] > efermi
                    and pz_weight(fatbands, gamma["kpoint_index"],
                                  m["bands"]) > 0.30]
        check(bool(empty_pi) and empty_pi[0]["label"] == "B2g",
              f"the pi* band is B2g [paper], got "
              f"{empty_pi[0]['label'] if empty_pi else 'none'}")

        # --- K -------------------------------------------------------------
        print("K point (little group D_3h):", flush=True)
        k_states = multiplets(kpoint)
        dirac = min(k_states, key=lambda m: abs(m["energy_eV"] - efermi))
        check(dirac["degeneracy"] == 2 and dirac["label"] == "E''",
              f"the two bands meeting at E_F form the doublet E'' [paper] — "
              f"this is the Dirac point; got {dirac['label']} "
              f"(degeneracy {dirac['degeneracy']})")
        check(abs(dirac["energy_eV"] - efermi) < 0.30,
              f"and it sits at the Fermi level "
              f"(E - E_F = {dirac['energy_eV'] - efermi:+.3f} eV)")
        check(pz_weight(fatbands, kpoint["kpoint_index"], dirac["bands"]) > 0.60,
              "and it is the pi manifold, by p_z weight")

        sigma_at_k = [m for m in k_states if m["energy_eV"] < dirac["energy_eV"]]
        sigma_labels = sorted(m["label"] for m in sigma_at_k)
        # Kogan & Nazarov Eq. (11): the sigma states at K realize A1' + E'.
        check(sigma_labels == ["A1'", "E'"],
              f"the sigma bands at K are A1' + E' [paper Eq. 11], got "
              f"{sigma_labels}")
        check(sum(m["degeneracy"] for m in sigma_at_k) == 3,
              "which is three states")

        # --- Gamma-K line and the compatibility relations -------------------
        print("Gamma-K line (little group C_2v) and Table III:", flush=True)
        line = point_by_label(symmetry, "G-K")
        check(line is not None, "the symmetry line was classified")
        if line is not None:
            line_states = multiplets(line)
            check(all(m["degeneracy"] == 1 for m in line_states[:4]),
                  "every state on the line is non-degenerate (C_2v is abelian)")

            # Table III applied to the Gamma labels found above: A1g -> A1,
            # A2u -> B2, E2g -> A1 + B1. Four bands, four labels.
            expected = []
            for multiplet in occupied:
                mapped = D6H_TO_C2V.get(multiplet["label"])
                if mapped is None:
                    continue
                expected += list(mapped) if isinstance(mapped, tuple) \
                    else [mapped]
            found = sorted(m["label"] for m in line_states[:4])
            check(sorted(expected) == found,
                  f"the four lowest line irreps are the Table III "
                  f"decomposition of the Gamma labels: expected "
                  f"{sorted(expected)}, got {found}")
            # The paper's own statement about the sigma bands along Gamma-K.
            sigma_line = sorted(m["label"] for m in line_states[:4]
                                if pz_weight(fatbands, line["kpoint_index"],
                                             m["bands"]) < 0.30)
            check(sigma_line == ["A1", "A1", "B1"],
                  f"the sigma bands along Gamma-K are A1, B1, A1 [paper], got "
                  f"{sigma_line}")

            # And from the other end: every line irrep must be compatible with
            # one of the K irreps, which is the same table read the other way.
            k_allowed = set()
            for multiplet in k_states[:3]:
                mapped = D3H_TO_C2V.get(multiplet["label"])
                if mapped is None:
                    continue
                k_allowed.update(mapped if isinstance(mapped, tuple)
                                 else [mapped])
            check(set(found) <= k_allowed,
                  f"and each is compatible with the K labels too: "
                  f"{sorted(set(found))} within {sorted(k_allowed)}")

    print("\nAll graphene band-symmetry checks passed." if failures == 0
          else f"\n{failures} check(s) FAILED.")
    return 0 if failures == 0 else 1


if __name__ == "__main__":
    _bootstrap_gpaw_env()
    sys.exit(main())
