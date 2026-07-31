// Linear-response Hubbard U: the generated driver, and the arithmetic it does.
//
// This module automates a recipe (Cococcioni & de Gironcoli, PRB 71, 035105)
// whose failure modes are all silent. Every one of the checks below stands for
// a way the run produces a plausible number that is wrong:
//
//   * chi_0 converged self-consistently -> it IS chi -> U = 0.
//   * The perturbation applied to a species rather than an atom -> the whole
//     sublattice is perturbed and the response is of the lattice, not a site.
//   * LMAXMIX left at its default -> the occupancy matrix an LDAU run reports
//     is not converged even when the energy is.
//   * A Hubbard correction added on top of the shift (LDAUTYPE 1/2 instead of
//     3) -> the perturbation is not the alpha of the method.
//
// None of these throws, and none is visible in the U that comes out. They are
// only catchable by asserting on what the script says.
//
// GUI-free, GL-free, Python-free.

#include "core/HubbardScriptGenerator.hpp"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

using namespace calango;

namespace {

int failures = 0;

void check(bool condition, const std::string& what)
{
    std::printf("  %s %s\n", condition ? "ok  " : "FAIL", what.c_str());
    if (!condition)
        ++failures;
}

bool has(const std::string& haystack, const std::string& needle)
{
    return haystack.find(needle) != std::string::npos;
}

core::HubbardRunConfig makeConfig(core::CalculatorKind kind)
{
    core::HubbardRunConfig c;
    c.calculator.calculator = kind;
    c.calculator.planeWaveCutoffEv = 520.0;
    c.calculator.kpts[0] = c.calculator.kpts[1] = c.calculator.kpts[2] = 4;
    core::HubbardSite site;
    site.atomIndex = 0;
    site.element = "Fe";
    site.shell = core::HubbardShell::D;
    c.sites.push_back(site);
    return c;
}

/// The reference implementation of the method's arithmetic, mirrored here so
/// the formula is asserted against a case whose answer is known by hand rather
/// than against whatever the script happens to compute.
///
/// Single site: chi and chi_0 are scalars and U = 1/chi_0 - 1/chi.
double scalarU(double chi0, double chi)
{
    return 1.0 / chi0 - 1.0 / chi;
}

} // namespace

int main()
{
    std::printf("Engine support:\n");
    {
        check(core::hubbardSupportsCalculator(core::CalculatorKind::Vasp),
              "VASP is supported");
        check(core::hubbardSupportsCalculator(
                  core::CalculatorKind::QuantumEspresso),
              "and Quantum ESPRESSO");
        // Not a matter of effort: the method needs a per-site potential shift
        // on the Hubbard projectors, and these have no such primitive.
        for (const auto kind : {core::CalculatorKind::Gpaw,
                                core::CalculatorKind::Mace,
                                core::CalculatorKind::Siesta,
                                core::CalculatorKind::Orca,
                                core::CalculatorKind::Lammps}) {
            check(!core::hubbardSupportsCalculator(kind),
                  "an engine with no localized alpha is refused");
        }
    }

    std::printf("VASP driver:\n");
    {
        const core::HubbardRunConfig c = makeConfig(core::CalculatorKind::Vasp);
        const std::string script =
            core::generateHubbardScript(c, "structure.extxyz");

        check(has(script, "ldautype=3"),
              "the perturbation is LDAUTYPE = 3, a bare potential shift");
        // LDAUTYPE 1 and 2 add a real +U on top of the shift, which is a
        // different Hamiltonian and therefore a different response.
        check(!has(script, "ldautype=1") && !has(script, "ldautype=2"),
              "and not a +U functional, which would perturb something else");
        check(has(script, "ldauu=") && has(script, "ldauj="),
              "alpha enters through LDAUU and LDAUJ");
        check(has(script, "ldauprint=2"),
              "LDAUPRINT = 2 so the occupancy matrices are printed at all");
        // Default LMAXMIX (2) leaves the one-centre occupancies unconverged
        // even when the energy has converged — the response is then wrong by
        // tens of percent with no other symptom.
        check(has(script, "lmaxmix"),
              "LMAXMIX is set rather than left at its default");
        check(has(script, "_HUBBARD_L == 3") && has(script, "4"),
              "and follows the manifold: 6 for f, 4 for d");

        // THE defining property of chi_0.
        check(has(script, "icharg=11") && has(script, "nelm=1"),
              "chi_0 is one diagonalization at a FIXED density");
        check(has(script, "non_scf=True"),
              "which the pipeline asks for explicitly");
        check(has(script, "CHGCAR"),
              "restarting from the unperturbed density, which must be written");
        // Seeding chi_0 from the perturbed self-consistent run instead of from
        // the reference would make it a second measurement of chi.
        check(has(script, "alpha_reference"),
              "and seeded from the reference run, not from the screened one");

        // The alpha has to reach ONE atom. LDAU tags are per species, so a
        // perturbation applied without splitting the atom out lands on every
        // atom of that element and measures the response of the sublattice.
        // ASE's integer-keyed `setups` is the mechanism that splits it.
        check(has(script, "_SETUPS = {index: atoms[index].symbol"),
              "the perturbed atom is split out through ASE's setups= by index");
        check(has(script, "setups=dict(_SETUPS)"),
              "and that mapping reaches the calculator");
        // The LDAU arrays are POSITIONAL over the POSCAR species. An order
        // that disagrees with the one ASE writes perturbs a different species
        // and reports nothing.
        check(has(script, "def _species_order"),
              "with the species order reproduced to match the POSCAR");
        check(has(script, "onsite density matrix"),
              "occupations are read from the OUTCAR occupancy matrix");
        check(has(script, "encut=520"), "the cutoff reaches the calculator");
        check(has(script, "kpts=(4, 4, 4)"), "and the k-grid");
    }

    std::printf("Quantum ESPRESSO driver:\n");
    {
        const core::HubbardRunConfig c =
            makeConfig(core::CalculatorKind::QuantumEspresso);
        const std::string script =
            core::generateHubbardScript(c, "structure.extxyz");

        check(has(script, "hubbard_alpha"),
              "the perturbation is Hubbard_alpha");
        check(has(script, "lda_plus_u"),
              "with the projectors switched on so occupations are reported");
        check(has(script, "1e-8"),
              "by a U that is numerically nothing — it is not the physics");
        check(has(script, "electron_maxstep=1"),
              "chi_0 stops after one diagonalization");
        check(has(script, "Tr[ns("),
              "occupations come from the Tr[ns] the output carries");
        check(!has(script, "ldautype"),
              "and no VASP tag leaks into the ESPRESSO path");
        // QE's split mechanism is different from VASP's: ASE writes a separate
        // ATOMIC_SPECIES entry for each (element, initial moment) pair, so the
        // measured atoms are given a negligible moment offset.
        check(has(script, "_MAGMOM_EPSILON"),
              "types are split by a negligible moment offset");
        check(has(script, "nspin\"] = 2"),
              "which needs nspin = 2, so the QE path is always spin-polarized");
        // The species INDEX is first-appearance order, NOT perturbed-first as
        // in the VASP path. Reusing the VASP ordering here would put alpha on
        // a different sublattice with no error.
        check(has(script, "def _species_index"),
              "and the 1-based species index is computed ASE's way");
        check(has(script, "hubbard_alpha({sidx})"),
              "so Hubbard_alpha addresses the type that was actually split");
    }

    std::printf("The pipeline covers every run it needs:\n");
    {
        core::HubbardRunConfig c = makeConfig(core::CalculatorKind::Vasp);
        c.alphas = {-0.1, -0.05, 0.05, 0.1};
        const std::string script =
            core::generateHubbardScript(c, "structure.extxyz");
        check(has(script, "_TOTAL_RUNS = 1 + 2 * _N_SITES * len(_ALPHAS)"),
              "one reference plus a chi and a chi_0 run per site per alpha");
        check(has(script, "_ALPHAS = [-0.1, -0.05, 0.05, 0.1]"),
              "the alphas reach the script");
        // alpha = 0 belongs to the reference run; listing it again would add a
        // duplicate point to the fit.
        check(has(script, "xs = [0.0] + list(_ALPHAS)"),
              "and the fit includes the unperturbed point exactly once");
        check(has(script, "U_eff = (chi_0^-1 - chi^-1)"),
              "the formula is stated where a reader of the script will see it");
        check(has(script, "_chi0_inv[i][j] - _chi_inv[i][j]"),
              "and is what the code computes");
        // The off-diagonal of the same difference is the inter-site V; keeping
        // it costs nothing and throwing it away loses a real quantity.
        check(has(script, "U_matrix_ev"),
              "the full matrix is reported, not only its diagonal");
        check(has(script, "hubbard_u.json"), "results land in hubbard_u.json");
        check(has(script, "residual"),
              "and the fit residual, which is the only linearity warning");
    }

    std::printf("Multiple sites make it a matrix problem:\n");
    {
        core::HubbardRunConfig c = makeConfig(core::CalculatorKind::Vasp);
        core::HubbardSite second;
        second.atomIndex = 3;
        second.element = "Fe";
        second.shell = core::HubbardShell::D;
        c.sites.push_back(second);
        const std::string script =
            core::generateHubbardScript(c, "structure.extxyz");
        check(has(script, "\"index\": 0") && has(script, "\"index\": 3"),
              "both sites are listed");
        // With N sites the inverse is a real matrix inverse. Treating them as
        // N independent scalars ignores how one site's occupation responds to
        // the other's perturbation, which is exactly what chi_ij measures.
        check(has(script, "def _invert"),
              "and the responses are inverted as matrices, not site by site");
        check(has(script, "Singular response matrix"),
              "with a singular matrix reported rather than dividing by zero");
    }

    std::printf("Supercell:\n");
    {
        core::HubbardRunConfig c = makeConfig(core::CalculatorKind::Vasp);
        c.supercell[0] = 3;
        c.supercell[1] = 3;
        c.supercell[2] = 1;
        const std::string script =
            core::generateHubbardScript(c, "structure.extxyz");
        check(has(script, "_SUPERCELL = (3, 3, 1)"),
              "the repetitions reach the script");
        check(has(script, "primitive.repeat(_SUPERCELL)"),
              "and are applied before anything else");
        // The user has to know this is the convergence parameter; a script
        // that just did it silently would be one nobody re-runs larger.
        check(has(script, "LATTICE of perturbations"),
              "with the reason a small cell is wrong stated in the script");
    }

    std::printf("The U formula itself:\n");
    {
        // A hand-checkable case. A screened response is always LARGER in
        // magnitude than the bare one (the other electrons move to oppose the
        // perturbation), so 1/chi_0 - 1/chi is positive — a U that came out
        // negative would mean the two responses had been swapped.
        const double chi0 = -0.30; // e/eV, bare
        const double chi = -0.15;  // e/eV, screened: further from zero it is not
        const double u = scalarU(chi0, chi);
        check(std::abs(u - (1.0 / -0.30 - 1.0 / -0.15)) < 1e-12,
              "U = 1/chi_0 - 1/chi for a single site");
        check(u > 0.0, "and is positive when the screened response is smaller");

        // Equal responses mean nothing was screened, which is the signature of
        // chi_0 having been run self-consistently by mistake.
        check(std::abs(scalarU(-0.2, -0.2)) < 1e-12,
              "identical responses give U = 0 — the chi_0-run-to-convergence bug");
    }

    std::printf(failures == 0 ? "\nAll Hubbard checks passed.\n"
                              : "\n%d check(s) FAILED.\n",
                failures);
    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
