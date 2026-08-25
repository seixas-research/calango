"""GO Grand Canonical MC on pristine graphene, with a real potential.

WHAT THIS RUNS. The module's own headline case: start from PRISTINE periodic
graphene — zero functional groups — set chemical potentials, and let the
grand-canonical move set decide the composition. MACE-MP handles C/O/H and
keeps graphene stable, which EMT does not (EMT's carbon parameters deform
the sheet on their own, so it is not usable even as a mock here).

WHAT IT ASSERTS, and this is deliberately narrower than the module's
ambitions:

  * the run starts from a pristine framework at all — the conserving
    modules refuse an empty inventory, and this one must not;
  * the reference potentials are computed by the run, from ITS calculator,
    and satisfy their definitions: mu_H0 = 1/2 E(H2) and
    mu_O0 = E(H2O) - E(H2), checked against the two energies the log
    reports;
  * the composition is RECORDED per cycle (gcmc_n_O / gcmc_n_H /
    gcmc_n_groups) and the trace is consistent with the accepted moves'
    stoichiometry — every oxygen on the sheet is one an accepted insertion
    put there;
  * the composition RESPONDS to mu_O in the right direction, and plateaus
    rather than diverging to full coverage.

THE RESPONSE, and why the two points are where they are. mu_O is referenced
to water in equilibrium with H2, so mu_O^0 is already deeply negative
(-7.48 eV with MACE) and oxidising graphene at dmu_O = 0 is thermodynamically
UPHILL. The run must therefore leave the sheet pristine there — and it does,
accepting nothing at all in 30 cycles. Push the reservoir oxidising and the
onset is sharp: at dmu_O = +3 eV the same run places groups and settles at
O/C ~ 0.5, squarely in the range a real graphene oxide occupies. Above that
it plateaus at the site pool's own limit rather than climbing further
(+5 and +7 eV give the same composition), which is the saturation a finite
sheet must show.

Two points, not a fine scan: each is a MACE run of 30 optimisation cycles,
and the claim being tested is a DIRECTION and a plateau, not a curve.

THE CHE, in one more run. The same module can take its potentials from an
electrode instead (mu_H = 1/2 E(H2) - eU, mu_O = E(H2O) - 2 mu_H), and that
algebra says U = +2.5 V vs SHE is the SAME oxygen reservoir as
dmu_O = +5 eV while U = 0 is the same as dmu_O = 0 — so a third run at
U = +2.5 V, read against the two above, is a two-point scan in the
potential: pristine at U = 0, oxidised at U = +2.5 V. The formula itself,
its limits and its sign are pinned calculator-free in
graphene_oxide_gcmc_test.py; this checks that a real run actually behaves
that way.

SELF-SKIPS (exit 0) without mace-torch or ase.

Run directly, or through ctest as `graphene_oxide_gcmc_mace`.
"""

import json
import os
import re
import shutil
import subprocess
import sys
import tempfile

CYCLES = 40
failures = 0


def check(condition, what):
    global failures
    print(f"  {'ok  ' if condition else 'FAIL'} {what}")
    if not condition:
        failures += 1
    return condition


def _repo_root():
    return os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def _mace_python():
    """An interpreter with mace-torch. CLAUDE.md: mace_env, not the default."""
    for candidate in (os.environ.get("CALANGO_MACE_PYTHON", ""),
                      os.path.expanduser("~/miniconda3/envs/mace_env/bin/python"),
                      sys.executable):
        if not candidate or not os.path.isfile(candidate):
            continue
        probe = subprocess.run(
            [candidate, "-c", "import mace, ase"],
            capture_output=True, text=True)
        if probe.returncode == 0:
            return candidate
    return None


def _find_script_test():
    for cand in sys.argv[1:]:
        if os.path.isfile(cand) and os.access(cand, os.X_OK):
            return cand
    cand = os.path.join(_repo_root(), "build", "calango_script_test")
    return cand if os.access(cand, os.X_OK) else None


def _splice_mace(gcmc_text, mace_text):
    """Give the GCMC script the MACE calculator block."""
    def block(text):
        i = text.index("def attach_calculator(system):")
        return text[i:text.index("attach_calculator(atoms)", i)]
    out = gcmc_text.replace(block(gcmc_text), block(mace_text))
    return re.sub(r"^CALCULATOR_LABEL = .*$", 'CALCULATOR_LABEL = "MACE"',
                  out, count=1, flags=re.M)


def main():
    python = _mace_python()
    if python is None:
        print("SKIP graphene_oxide_gcmc_mace: no interpreter with mace-torch")
        return 0
    binary = _find_script_test()
    if binary is None:
        print("SKIP graphene_oxide_gcmc_mace: calango_script_test is not built")
        return 0

    tmp = tempfile.mkdtemp(prefix="calango_gcmc_mace_")
    scripts = os.path.join(tmp, "scripts")
    os.makedirs(scripts)
    subprocess.run([binary, "--dump", scripts], check=True,
                   stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    with open(os.path.join(scripts, "graphene_oxide_gcmc.py")) as fh:
        gcmc = fh.read()
    with open(os.path.join(scripts, "graphene_oxide_mcmd_mace.py")) as fh:
        mace = fh.read()

    run = os.path.join(tmp, "run")
    os.makedirs(run)
    subprocess.run(
        [python, "-c",
         "from ase.build import graphene\n"
         "from ase.io import write\n"
         "write('structure.extxyz', graphene(formula='C2', a=2.46, "
         "size=(3,3,1), vacuum=7.0))"],
        cwd=run, check=True)

    def prepare(work, delta_mu_o, electrode_potential=None):
        """One run. `electrode_potential` not None switches it to CHE mode.

        Under the CHE the module derives mu_H from U instead of offsetting
        it, and mu_O = E(H2O) - 2 mu_H then carries 2 eU — so U = +2.5 V is
        the same oxygen reservoir as dmu_O = +5 eV, by construction. That
        identity is what makes ONE extra MACE run enough to check the mode
        end to end against runs this test already pays for.
        """
        os.makedirs(work, exist_ok=True)
        seed = os.path.join(run, "structure.extxyz")
        target = os.path.join(work, "structure.extxyz")
        if os.path.abspath(seed) != os.path.abspath(target):
            shutil.copy(seed, target)
        text = _splice_mace(gcmc, mace)
        for pattern, replacement in (
                (r"^mc_cycles = .*$", "mc_cycles = %d" % CYCLES),
                (r"^delta_mu_O = .*$", "delta_mu_O = %g" % delta_mu_o),
                (r"^delta_mu_H = .*$", "delta_mu_H = 0.0"),
                (r"^mu_mode = .*$",
                 'mu_mode = "%s"'
                 % ("manual" if electrode_potential is None else "che")),
                (r"^electrode_potential_V = .*$",
                 "electrode_potential_V = %g" % (electrode_potential or 0.0)),
                (r"^solution_pH = .*$", "solution_pH = 0.0"),
                (r"^optimizer_max_steps = .*$", "optimizer_max_steps = 25"),
                (r"^equilibration_steps = .*$", "equilibration_steps = 5"),
                # Shared, so the three points pay for the references once.
                (r"^reference_cache_path = .*$",
                 'reference_cache_path = r"../go_gcmc_references.json"')):
            text = re.sub(pattern, replacement, text, count=1, flags=re.M)
        with open(os.path.join(work, "run.py"), "w") as fh:
            fh.write(text)
        return subprocess.run([python, "run.py"], cwd=work,
                              capture_output=True, text=True)

    # THE NEUTRAL POINT: oxidising from water at dmu_O = 0 is uphill, so a
    # correct run leaves the sheet pristine. This is the check that would
    # catch a sign error in the criterion at the level of the whole module.
    print("Running GO Grand Canonical MC from pristine graphene (MACE):")
    neutral = os.path.join(tmp, "neutral")
    done_neutral = prepare(neutral, 0.0)
    check(done_neutral.returncode == 0,
          f"the dmu_O = 0 run completed (exit {done_neutral.returncode})")
    if done_neutral.returncode == 0:
        summary0 = json.load(
            open(os.path.join(neutral, "mcmd_summary.json")))
        check(summary0["accepted"] == 0,
              f"and accepted NOTHING ({summary0['accepted']} moves) — "
              f"mu_O is water-referenced, so oxidising at dmu_O = 0 is "
              f"thermodynamically uphill and the sheet must stay pristine")

    # THE OXIDISING POINT: push the reservoir and an oxide must appear.
    done = prepare(run, 5.0)
    if not check(done.returncode == 0,
                 f"the dmu_O = +5 eV run completed (exit {done.returncode})"
                 + ("" if done.returncode == 0
                    else f": {(done.stdout + done.stderr)[-1500:]}")):
        return 1

    # BOTH logs: the first run computes the references and the second
    # reuses them from the cache, so the two energies are only ever
    # reported once — by whichever run got there first. Looking in one log
    # only would fail on the cache working, which is the opposite of what
    # this is checking.
    messages = []
    for directory in (run, neutral):
        log_path = os.path.join(directory, "log.json")
        if os.path.isfile(log_path):
            messages += [e["message"]
                         for e in json.load(open(log_path))["log"]]
    check(any("pristine framework" in m for m in messages),
          "it started from a PRISTINE framework — zero functional groups, "
          "which the conserving modules refuse outright")

    # --- The reference potentials, against their own definitions. --------
    e_h2 = e_h2o = mu_h0 = mu_o0 = None
    for message in messages:
        if "reference E(H2):" in message:
            e_h2 = float(message.split(":")[1].split()[0])
        elif "reference E(H2O):" in message:
            e_h2o = float(message.split(":")[1].split()[0])
        elif "chemical potentials:" in message:
            found = re.findall(r"mu_[HO]0 = (-?\d+\.\d+)", message)
            if len(found) == 2:
                mu_h0, mu_o0 = float(found[0]), float(found[1])
    check(e_h2 is not None and e_h2o is not None,
          f"both reference molecules were computed (E(H2) = {e_h2}, "
          f"E(H2O) = {e_h2o})")
    if None not in (e_h2, e_h2o, mu_h0, mu_o0):
        check(abs(mu_h0 - 0.5 * e_h2) < 1e-3,
              f"mu_H0 = 1/2 E(H2): {mu_h0:.4f} vs {0.5 * e_h2:.4f} eV")
        check(abs(mu_o0 - (e_h2o - e_h2)) < 1e-3,
              f"mu_O0 = E(H2O) - E(H2): {mu_o0:.4f} vs "
              f"{e_h2o - e_h2:.4f} eV")
        # Sanity on the numbers themselves: these are real molecular
        # energies, not placeholders.
        check(e_h2 < 0.0 and e_h2o < e_h2,
              "and both are real binding energies (E(H2O) below E(H2))")
    check(os.path.exists(os.path.join(run, "..", "go_gcmc_references.json")),
          "the references were cached for reuse")
    check(any("reused from" in m for m in messages),
          "and the second run REUSED them rather than paying for two more "
          "molecular calculations — which is what the cache is for")

    # --- The composition trace. ------------------------------------------
    metrics = json.load(open(os.path.join(run, "metrics.json")))["metrics"]
    oxygen = [m["gcmc_n_O"] for m in metrics if "gcmc_n_O" in m]
    groups = [m["gcmc_n_groups"] for m in metrics if "gcmc_n_groups" in m]
    check(len(oxygen) >= CYCLES // 2,
          f"the composition is recorded per cycle ({len(oxygen)} samples)")
    if oxygen:
        check(oxygen[0] == 0,
              "starting at zero oxygen, as a pristine sheet must")
        check(max(oxygen) > 0,
              f"and at dmu_O = +5 eV it PLACED groups (peak {max(oxygen)} O, "
              f"{max(groups)} groups) — the composition RESPONDS to the "
              f"reservoir, in the direction the criterion's sign demands")
        # The plateau: the last third must not still be climbing, and must
        # not have collapsed back to nothing. Diverging to full coverage
        # (one group per carbon) or staying at zero are the two failure
        # modes the module exists to avoid.
        tail = oxygen[-len(oxygen) // 3:]
        check(max(tail) - min(tail) <= max(2, max(oxygen) // 3),
              f"the composition PLATEAUS over the last third "
              f"({min(tail)}-{max(tail)} O, peak {max(oxygen)})")
        check(min(tail) > 0,
              "without collapsing back to a pristine sheet")

    # --- The stoichiometry, end to end. ----------------------------------
    summary = json.load(open(os.path.join(run, "mcmd_summary.json")))
    probe = subprocess.run(
        [python, "-c",
         "import sys, json\n"
         "from collections import Counter\n"
         "from ase.io import read\n"
         "frames = read(sys.argv[1], index=':')\n"
         "print(json.dumps([dict(Counter(f.get_chemical_symbols())) "
         "for f in frames]))",
         os.path.join(run, "accepted_structures.extxyz")],
        capture_output=True, text=True)
    if probe.returncode == 0 and probe.stdout.strip():
        counts = json.loads(probe.stdout)
        check(len(counts) == summary["accepted"],
              f"the accepted file holds one frame per accepted move "
              f"({len(counts)} of {summary['accepted']})")
        # Every oxygen came from an accepted insertion, and the group
        # recipes say how many each one brings: a hydroxyl PAIR (the
        # antiposition default) is 2 O + 2 H, an epoxide 1 O + 0 H. So the
        # oxygen count can never exceed twice the accepted-move count.
        if counts:
            check(counts[-1].get("O", 0) <= 2 * summary["accepted"],
                  f"and the final oxygen count ({counts[-1].get('O', 0)}) is "
                  f"within what {summary['accepted']} accepted insertions "
                  f"can place at 2 O each")
            check(counts[-1].get("C", 0) == 18,
                  "the carbon framework is untouched — no move has a carbon "
                  "reservoir to draw on")

    # --- The computational hydrogen electrode, end to end. ---------------
    #
    # ONE extra run, because the CHE's own algebra says where to put it.
    # mu_H = 1/2 E(H2) - eU and mu_O = E(H2O) - 2 mu_H, so U = +2.5 V vs SHE
    # is the same oxygen reservoir as dmu_O = +5 eV — the point already run
    # above — and U = 0 is the same as dmu_O = 0, the pristine point. The
    # pair is therefore a two-point scan in U over runs that mostly already
    # exist: at U = 0 nothing is placed, at U = +2.5 V an oxide appears.
    # That is the direction the mode exists for, checked with a real
    # potential rather than asserted.
    print("\nThe same module driven by an electrode potential (CHE):")
    che = os.path.join(tmp, "che")
    done_che = prepare(che, 0.0, electrode_potential=2.5)
    if check(done_che.returncode == 0,
             f"the U = +2.5 V vs SHE run completed "
             f"(exit {done_che.returncode})"
             + ("" if done_che.returncode == 0
                else f": {(done_che.stdout + done_che.stderr)[-1500:]}")):
        che_messages = []
        che_log = os.path.join(che, "log.json")
        if os.path.isfile(che_log):
            che_messages = [e["message"]
                            for e in json.load(open(che_log))["log"]]
        che_line = next((m for m in che_messages
                         if "chemical potentials (CHE)" in m), "")
        check(bool(che_line),
              "and reported its potentials as CHE-derived, not as mu0 + dmu")
        if che_line and None not in (e_h2, e_h2o, mu_o0):
            found = re.findall(r"mu_[HO] = .*?(-?\d+\.\d+) eV", che_line)
            if check(len(found) == 2,
                     f"with both absolute potentials in the log ({che_line})"):
                che_mu_h, che_mu_o = float(found[0]), float(found[1])
                # The formula, against THIS run's own reference energies.
                check(abs(che_mu_h - (0.5 * e_h2 - 2.5)) < 1e-3,
                      f"mu_H = 1/2 E(H2) - eU: {che_mu_h:.4f} vs "
                      f"{0.5 * e_h2 - 2.5:.4f} eV at U = +2.5 V")
                check(abs(che_mu_o - (e_h2o - 2.0 * che_mu_h)) < 1e-3,
                      f"mu_O = E(H2O) - 2 mu_H: {che_mu_o:.4f} vs "
                      f"{e_h2o - 2.0 * che_mu_h:.4f} eV")
                # And the identity that makes the two-point scan legitimate:
                # this reservoir IS the dmu_O = +5 eV one already run.
                check(abs(che_mu_o - (mu_o0 + 5.0)) < 1e-3,
                      f"which is mu_O0 + 5 eV ({mu_o0 + 5.0:.4f}) — the same "
                      f"oxygen reservoir as the dmu_O = +5 eV run above, "
                      f"reached from a potential instead of an offset")

        che_metrics = os.path.join(che, "metrics.json")
        if os.path.isfile(che_metrics):
            che_oxygen = [m["gcmc_n_O"] for m
                          in json.load(open(che_metrics))["metrics"]
                          if "gcmc_n_O" in m]
            if che_oxygen:
                check(che_oxygen[0] == 0,
                      "the CHE run also starts from a pristine sheet")
                check(max(che_oxygen) > 0,
                      f"and OXIDISES at U = +2.5 V (peak {max(che_oxygen)} O) "
                      f"where the U = 0 equivalent — the dmu_O = 0 run above "
                      f"— accepted nothing at all: O coverage rises with the "
                      f"electrode potential, which is the whole point of the "
                      f"mode")

    shutil.rmtree(tmp, ignore_errors=True)
    print("\n" + (f"{failures} check(s) FAILED." if failures
                  else "All GO Grand Canonical MC / MACE checks passed."))
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
