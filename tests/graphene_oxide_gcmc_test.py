"""GO Grand Canonical MC: the algorithmic invariants, from the shipped code.

WHAT THIS PINS, and why it does it this way. A grand-canonical move is
accepted on

    ΔE − Σ_s Δn_s μ_s

so two things decide whether the module samples the right ensemble, and
neither of them is visible in a trajectory:

  1. **Δn_s must be exact.** Each group type's (n_C, n_O, n_H) comes from
     what collect_groups() actually puts in a group's `members` list, not
     from chemical intuition. The carboxyl entry is where the two differ:
     it brings a CARBON of its own ("−COOH replacing an edge H"), which a
     table written as "2 O + 1 H" leaves out. One miscounted atom biases
     every acceptance by one whole chemical potential.
  2. **μ must enter with the right SIGN.** It is SUBTRACTED, so a strongly
     negative Δμ makes insertion expensive and deletion cheap. Getting the
     sign backwards produces a run that looks healthy and oxidises when it
     should be reducing.

Both are read out of the GENERATED script rather than reimplemented here —
extracted the same way tests/defect_2d_correction_test.py extracts its
Python, so this tests the code that ships. Needs no calculator at all,
which is why it runs everywhere and in under a second.

The PHYSICAL scan (does a sensible oxide emerge, do the ratios track μ) is
a separate matter: it needs a potential that handles C/O/H and keeps
graphene stable, and it lives in graphene_oxide_gcmc_mace_test.py.

Run directly, or through ctest as `graphene_oxide_gcmc`.
"""

import math
import os
import re
import subprocess
import sys
import tempfile

failures = 0


def check(condition, what):
    global failures
    print(f"  {'ok  ' if condition else 'FAIL'} {what}")
    if not condition:
        failures += 1
    return condition


def _repo_root():
    return os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def _find_script_test():
    for cand in sys.argv[1:]:
        if os.path.isfile(cand) and os.access(cand, os.X_OK):
            return cand
    cand = os.path.join(_repo_root(), "build", "calango_script_test")
    return cand if os.access(cand, os.X_OK) else None


# k_B in eV/K, and the ONLY place this test states it. Every expected value
# below is computed from this constant rather than from a number typed out
# of a calculator, and the same object is injected into the extracted script
# as `units.kB` — so the test compares the script's ARITHMETIC against the
# formula, not two independently rounded transcriptions of a constant.
KB_EV_PER_K = 8.617333262e-5


class _Units:
    """Stand-in for `ase.units`, which the extracted helpers touch for kB."""
    kB = KB_EV_PER_K


def _gcmc_namespace(script_text):
    """exec the grand-canonical section of the generated script.

    Only the section: the rest of the file wants ase and a calculator. The
    definitions this test is about — the stoichiometry table, the
    chemical-potential term, and the two helpers that turn the reference
    energies into absolute μ — have no dependencies beyond the module
    globals the script sets above them, which are supplied here.
    """
    start = script_text.index("GCMC_STOICHIOMETRY = {")
    end = script_text.index("def _gcmc_insertable_kinds()")
    body = script_text[start:end]
    # Keep only the pieces that stand alone: the table, the delta helper, the
    # mu term and the two potential helpers. The insertion/deletion machinery
    # needs the live system.
    # Split on top-level statements: a line that starts at column 0 and is
    # not a continuation. re.split on a lookahead would cut inside the
    # multi-line dict literal, whose closing brace IS at column 0.
    wanted = ("GCMC_STOICHIOMETRY", "GCMC_INSERTABLE",
              "def gcmc_delta_counts", "def gcmc_mu_term",
              "def gcmc_potential_shift", "def gcmc_chemical_potentials")
    keep, taking = [], False
    for line in body.splitlines():
        if line and not line[0].isspace() and not line.startswith(("}", ")")):
            taking = any(line.startswith(w) for w in wanted)
        if taking:
            keep.append(line)
        elif keep and line.startswith(("}", ")")):
            # The closing brace of a literal that was being kept.
            keep.append(line)
    # The module globals the extracted helpers read. Seeded at the MANUAL
    # defaults; each CHE check below sets what it is about and leaves the
    # rest alone, so a value that stopped being read would show up as a
    # check that no longer moves.
    namespace = {
        "mu_H": 0.0, "mu_O": 0.0,
        "math": math, "units": _Units,
        "mu_mode": "manual",
        "delta_mu_H": 0.0, "delta_mu_O": 0.0,
        "electrode_potential_V": 0.0,
        "solution_pH": 0.0,
        "potential_temperature_K": 298.15,
    }
    exec(compile("\n".join(keep), "<gcmc>", "exec"), namespace)
    return namespace


def _check_computational_hydrogen_electrode(ns, delta, mu_term):
    """The CHE: the formula, its two limits, and the sign it enters with.

    Hand-computed throughout, from KB_EV_PER_K above — no number here is
    copied from a run of this code, which is this repository's standing rule
    for what a test may compare against.

    The scheme (Nørskov et al., J. Phys. Chem. B 108, 17886 (2004)):

        μ_H = ½E(H₂) − eU − k_B T ln(10)·pH      (U on the SHE scale)
        μ_O = E(H₂O) − 2μ_H + Δμ_O

    Two reference energies stand in for what a real run computes. Their
    VALUES are arbitrary and deliberately not round: every assertion below
    is a relation between μ and (U, pH, T), and a relation that only held
    for tidy inputs would not be one.
    """
    shift = ns["gcmc_potential_shift"]
    potentials = ns["gcmc_chemical_potentials"]
    e_h2, e_h2o = -6.7712, -14.2231

    def configure(mode="che", u=0.0, ph=0.0, temperature=298.15,
                  dmu_h=0.0, dmu_o=0.0):
        ns["mu_mode"] = mode
        ns["electrode_potential_V"] = u
        ns["solution_pH"] = ph
        ns["potential_temperature_K"] = temperature
        ns["delta_mu_H"] = dmu_h
        ns["delta_mu_O"] = dmu_o
        return potentials(e_h2, e_h2o)

    print("\nThe computational hydrogen electrode — the formula:")
    configure(u=0.0, ph=0.0)
    check(shift() == 0.0,
          "at U = 0 V vs SHE and pH 0 the electrochemical shift is exactly "
          "zero — that IS the standard hydrogen electrode's definition")
    configure(u=0.8, ph=0.0)
    check(abs(shift() - 0.8) < 1e-12,
          "e = 1 in these units, so U = +0.800 V shifts by exactly 0.800 eV")
    # k_B T ln(10) at 298.15 K — the textbook 59 meV per pH unit.
    per_ph = KB_EV_PER_K * 298.15 * math.log(10.0)
    configure(u=0.0, ph=1.0, temperature=298.15)
    check(abs(shift() - per_ph) < 1e-12,
          f"and one pH unit shifts by k_B T ln(10) = {per_ph * 1000:.1f} meV "
          f"at 298.15 K, the value every CHE paper quotes as ~59 meV")
    configure(u=0.0, ph=1.0, temperature=596.30)
    check(abs(shift() - 2.0 * per_ph) < 1e-12,
          "the pH term scales with T, and only the pH term does")

    print("\nThe two limits, which are what make the mode reviewable:")
    che_h, che_o = configure(u=0.0, ph=0.0)
    manual_h, manual_o = configure(mode="manual", dmu_h=0.0, dmu_o=0.0)
    check(abs(che_h - manual_h) < 1e-12 and abs(che_o - manual_o) < 1e-12,
          "at U = 0, pH 0 the CHE reproduces the manual references EXACTLY "
          "(μ_H = ½E(H₂), μ_O = E(H₂O) − E(H₂)) — the same numbers, from "
          "the same function, by two routes")
    che_h, che_o = configure(u=0.8, ph=2.0, temperature=298.15, dmu_o=-0.25)
    expected_shift = 0.8 + 2.0 * per_ph
    check(abs(che_h - (0.5 * e_h2 - expected_shift)) < 1e-12,
          "μ_H = ½E(H₂) − eU − k_B T ln(10)·pH, to the last digit")
    check(abs(che_o - (e_h2o - 2.0 * che_h - 0.25)) < 1e-12,
          "μ_O = E(H₂O) − 2μ_H + Δμ_O — oxygen from water in equilibrium "
          "with the ELECTRODE, which is what makes it U-dependent")
    loud_h, loud_o = configure(u=0.8, ph=2.0, temperature=298.15,
                               dmu_h=+9.0, dmu_o=-0.25)
    check(loud_h == che_h and loud_o == che_o,
          "and Δμ_H is not read at all in CHE mode — the potential fixes "
          "μ_H, so an additive knob on top would be a second, unlabelled "
          "potential axis")

    print("\nOxidation against the potential (the knob the mode exists for):")
    # The ladder. mu_O must rise by exactly 2 eV per volt: water releases
    # TWO proton-electron pairs per oxygen, so the oxygen reference carries
    # twice the electrode's shift. That factor of two is the whole
    # potential dependence of oxidation, and getting it wrong (or dropping
    # it to one) would halve every Pourbaix slope this module can produce.
    ladder = [0.0, 0.3, 0.6, 0.9, 1.2]
    mu_o_ladder = [configure(u=u, ph=0.0)[1] for u in ladder]
    check(all(b > a for a, b in zip(mu_o_ladder, mu_o_ladder[1:])),
          f"μ_O rises monotonically with U "
          f"({mu_o_ladder[0]:.3f} → {mu_o_ladder[-1]:.3f} eV over "
          f"0 → 1.2 V)")
    slopes = [(b - a) / (v - u) for a, b, u, v
              in zip(mu_o_ladder, mu_o_ladder[1:], ladder, ladder[1:])]
    check(all(abs(s - 2.0) < 1e-9 for s in slopes),
          f"at exactly 2 eV per volt ({slopes[0]:.6f}) — two "
          f"proton-electron pairs per oxygen, not one")
    mu_h_ladder = [configure(u=u, ph=0.0)[0] for u in ladder]
    check(all(b < a for a, b in zip(mu_h_ladder, mu_h_ladder[1:])),
          f"while μ_H falls with U ({mu_h_ladder[0]:.3f} → "
          f"{mu_h_ladder[-1]:.3f} eV) — a more oxidizing electrode makes "
          f"hydrogen cheaper to REMOVE, which is the same statement")

    # And now the part that matters: that this reaches the acceptance
    # criterion with the right sign. Insert one epoxide (+1 O, no H) at a
    # fixed, unfavourable dE, and read the grand-canonical exponent
    # dE - sum_s dn_s mu_s straight out of the shipped gcmc_mu_term().
    insert_epoxide = delta("epoxide", +1)
    kT = KB_EV_PER_K * 300.0
    # The SHEET's own energy change on gaining one oxygen — negative,
    # because an epoxide binds. Chosen so the ladder straddles the
    # acceptance threshold: with a ΔE far from it, every point on the ladder
    # would be accepted (or refused) outright and the monotonicity would be
    # a statement about nothing.
    trial_dE = -6.5

    def grand_delta(u):
        ns["mu_H"], ns["mu_O"] = configure(u=u, ph=0.0)
        return trial_dE - mu_term(insert_epoxide)

    exponents = [grand_delta(u) for u in ladder]
    check(all(b < a for a, b in zip(exponents, exponents[1:])),
          f"the grand-canonical ΔE − Σ Δn μ for inserting an epoxide falls "
          f"monotonically as U rises ({exponents[0]:+.3f} → "
          f"{exponents[-1]:+.3f} eV)")
    probabilities = [min(1.0, math.exp(-x / kT)) for x in exponents]
    check(all(b >= a for a, b in zip(probabilities, probabilities[1:])),
          "so the Metropolis acceptance probability for OXIDATION rises "
          "monotonically with the electrode potential — the equilibrium "
          "coverage can only follow")
    check(probabilities[0] < 1e-12 and probabilities[-1] == 1.0,
          f"and it spans the whole range over this ladder: {probabilities[0]:.1e} "
          f"at U = 0 V, accepted outright by U = +1.2 V")

    print("\npH, at fixed U vs SHE:")
    # At a FIXED potential vs. SHE, raising the pH is OXIDIZING — the same
    # direction as raising U, at 59 meV per pH unit. Fewer protons in
    # solution is a lower μ(H⁺ + e⁻), so the sheet gives one up more
    # readily; it is the −59 mV/pH slope every Pourbaix diagram draws its
    # oxide boundaries with. The intuition that "alkaline is reducing"
    # belongs to the RHE scale, where the two terms cancel — and it is
    # exactly why an RHE potential alongside a pH box would be one control
    # with no effect, which is why this module's U is SHE-referenced.
    alkaline = [configure(u=0.5, ph=ph, temperature=298.15)[1]
                for ph in (0.0, 3.0, 7.0, 14.0)]
    check(all(b > a for a, b in zip(alkaline, alkaline[1:])),
          f"μ_O rises monotonically as the solution is made alkaline "
          f"({alkaline[0]:.3f} → {alkaline[-1]:.3f} eV from pH 0 to 14) — "
          f"the same direction raising U moves it")
    rhe_equivalent = configure(u=0.5 + per_ph, ph=0.0)[1]
    check(abs(configure(u=0.5, ph=1.0, temperature=298.15)[1]
              - rhe_equivalent) < 1e-12,
          "and one pH unit is worth exactly one k_B T ln(10)/e of potential, "
          "which is the SHE↔RHE conversion — so the two scales agree, as "
          "they must")


def main():
    binary = _find_script_test()
    if binary is None:
        print("SKIP graphene_oxide_gcmc: calango_script_test is not built")
        return 0

    tmp = tempfile.mkdtemp(prefix="calango_gcmc_")
    subprocess.run([binary, "--dump", tmp], check=True,
                   stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    path = os.path.join(tmp, "graphene_oxide_gcmc.py")
    if not os.path.isfile(path):
        return check(False, "the GCMC script was dumped")
    with open(path) as fh:
        text = fh.read()

    print("The generated grand-canonical script:")
    check("grand_canonical = True" in text,
          "is generated with the grand-canonical switch on")
    check("delta_grand = delta - mu_work" in text,
          "accepts on dE - sum_s dn_s mu_s, not on dE")
    check("_mu_H0 = 0.5 * _e_h2" in text,
          "with mu_H0 = 1/2 E(H2)")
    check("_mu_O0 = _e_h2o - _e_h2" in text,
          "and mu_O0 = E(H2O) - E(H2) — water in equilibrium with hydrogen")
    check("gcmc_reference_potentials()" in text
          and "reference_cache_path" in text,
          "the two references are computed by the run and cached")
    check("attach_calculator(molecule)" in text,
          "through the SAME calculator every sheet energy uses — the "
          "consistency the criterion depends on")
    check('mu_mode = "manual"' in text,
          "and defaults to the MANUAL reference scheme")

    che_path = os.path.join(tmp, "graphene_oxide_gcmc_che.py")
    che_text = ""
    if check(os.path.isfile(che_path),
             "the computational-hydrogen-electrode variant is dumped too"):
        with open(che_path) as fh:
            che_text = fh.read()
        check('mu_mode = "che"' in che_text,
              "carrying the CHE mode tag")
        check("electrode_potential_V = 0.8" in che_text
              and "solution_pH = 2" in che_text
              and "potential_temperature_K = 298.15" in che_text,
              "with U, pH and the pH term's temperature all written out")

    print("\nStoichiometry, read off the group recipes:")
    ns = _gcmc_namespace(text)
    table = ns["GCMC_STOICHIOMETRY"]
    # collect_groups() builds these `members` lists:
    #   epoxide  [O]      hydroxyl [O, H]      carbonyl [O]
    #   carboxyl [C, O, O] + [H]
    expected = {
        "epoxide": {"C": 0, "O": 1, "H": 0},
        "hydroxyl": {"C": 0, "O": 1, "H": 1},
        "hydroxyl_pair": {"C": 0, "O": 2, "H": 2},
        "carbonyl": {"C": 0, "O": 1, "H": 0},
        "carboxyl": {"C": 1, "O": 2, "H": 1},
    }
    for kind, counts in expected.items():
        check(table.get(kind) == counts,
              f"{kind}: {counts} (script says {table.get(kind)})")
    check(table["carboxyl"]["C"] == 1,
          "and the carboxyl CARBON is counted — the one a table written "
          "from intuition leaves out")

    delta = ns["gcmc_delta_counts"]
    check(delta("hydroxyl", +1) == {"C": 0, "O": 1, "H": 1},
          "inserting a hydroxyl is +1 O, +1 H")
    check(delta("hydroxyl", -1) == {"C": 0, "O": -1, "H": -1},
          "and deleting one is exactly the negative of that")

    print("\nThe sign of mu in the acceptance criterion:")
    mu_term = ns["gcmc_mu_term"]
    kT = 8.617333262e-5 * 300.0

    def accept_exponent(dE, dn, mu_h, mu_o):
        ns["mu_H"], ns["mu_O"] = mu_h, mu_o
        return dE - mu_term(dn)

    insert_oh = delta("hydroxyl", +1)
    # Strongly NEGATIVE mu: the reservoir is starved, insertion must cost.
    starved = accept_exponent(0.0, insert_oh, -5.0, -5.0)
    check(starved > 0.0,
          f"at mu = -5 eV inserting a hydroxyl costs {starved:+.2f} eV even "
          f"at dE = 0 — the reservoir opposes it")
    check(math.exp(-starved / kT) < 1e-30,
          "so its Metropolis probability is essentially zero")
    # Strongly POSITIVE mu: insertion must be favourable.
    rich = accept_exponent(0.0, insert_oh, +5.0, +5.0)
    check(rich < 0.0,
          f"at mu = +5 eV the same insertion gains {rich:+.2f} eV — accepted "
          f"outright")
    # Deletion is the mirror image, exactly.
    delete_oh = delta("hydroxyl", -1)
    check(abs(accept_exponent(0.0, delete_oh, -5.0, -5.0) + starved) < 1e-12,
          "and deletion is the exact negative of insertion at the same mu")

    # A swap changes nothing, so the criterion must collapse to plain dE.
    check(mu_term({"C": 0, "O": 0, "H": 0}) == 0.0,
          "a swap has dn = 0 for every species, so the term vanishes and "
          "the criterion is the conserving one")

    # Carbon has no reservoir; a move that changed it must be refused
    # rather than silently priced at zero.
    try:
        mu_term({"C": 1, "O": 0, "H": 0})
        check(False, "a carbon-changing move is refused")
    except RuntimeError as exc:
        check("carbon reservoir" in str(exc),
              "a carbon-changing move is refused by name, not priced at 0")

    print("\nWhich kinds may be inserted:")
    insertable = ns["GCMC_INSERTABLE"]
    check(set(insertable) == {"epoxide", "hydroxyl"},
          f"the basal kinds only ({insertable}) — an edge insertion would "
          f"also have to remove the host's terminating hydrogen, a second "
          f"species entering the reservoir on the same move")
    check(all(table[k]["C"] == 0 for k in insertable),
          "and none of them carries a carbon, which is what makes the "
          "carbon refusal above unreachable in practice")

    _check_computational_hydrogen_electrode(ns, delta, mu_term)

    print("\n" + (f"{failures} check(s) FAILED." if failures
                  else "All GO Grand Canonical MC invariant checks passed."))
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
