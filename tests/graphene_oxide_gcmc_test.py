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


def _gcmc_namespace(script_text):
    """exec the grand-canonical section of the generated script.

    Only the section: the rest of the file wants ase and a calculator. The
    two definitions this test is about — the stoichiometry table and the
    chemical-potential term — have no dependencies beyond the module
    globals the script sets above them, which are supplied here.
    """
    start = script_text.index("GCMC_STOICHIOMETRY = {")
    end = script_text.index("def _gcmc_insertable_kinds()")
    body = script_text[start:end]
    # Keep only the pieces that stand alone: the table, the delta helper and
    # the mu term. The insertion/deletion machinery needs the live system.
    # Split on top-level statements: a line that starts at column 0 and is
    # not a continuation. re.split on a lookahead would cut inside the
    # multi-line dict literal, whose closing brace IS at column 0.
    wanted = ("GCMC_STOICHIOMETRY", "GCMC_INSERTABLE",
              "def gcmc_delta_counts", "def gcmc_mu_term")
    keep, taking = [], False
    for line in body.splitlines():
        if line and not line[0].isspace() and not line.startswith(("}", ")")):
            taking = any(line.startswith(w) for w in wanted)
        if taking:
            keep.append(line)
        elif keep and line.startswith(("}", ")")):
            # The closing brace of a literal that was being kept.
            keep.append(line)
    namespace = {"mu_H": 0.0, "mu_O": 0.0}
    exec(compile("\n".join(keep), "<gcmc>", "exec"), namespace)
    return namespace


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

    print("\n" + (f"{failures} check(s) FAILED." if failures
                  else "All GO Grand Canonical MC invariant checks passed."))
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
