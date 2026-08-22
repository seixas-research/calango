#include "core/HubbardScriptGenerator.hpp"

#include "core/AseScriptGenerator.hpp"

#include <sstream>

namespace calango::core {

namespace {

/// Python literal for a list of doubles.
std::string pyList(const std::vector<double>& values)
{
    std::ostringstream out;
    out << "[";
    for (std::size_t i = 0; i < values.size(); ++i)
        out << (i ? ", " : "") << values[i];
    out << "]";
    return out.str();
}

/// The per-site table the driver iterates over.
std::string pySiteTable(const std::vector<HubbardSite>& sites)
{
    std::ostringstream out;
    out << "[\n";
    for (const HubbardSite& site : sites) {
        out << "    {\"index\": " << site.atomIndex << ", \"symbol\": \""
            << site.element << "\", \"l\": " << static_cast<int>(site.shell)
            << ", \"shell\": \"" << toString(site.shell) << "\"},\n";
    }
    out << "]";
    return out.str();
}

/// VASP-specific half of the driver: how a perturbation is applied and how an
/// occupation is read back.
void emitVaspBackend(std::ostringstream& out, const HubbardRunConfig& c)
{
    out << R"PY(
# --- VASP backend ---------------------------------------------------------
#
# The perturbation is LDAUTYPE = 3. That mode does not apply a Hubbard
# correction at all: it adds a CONSTANT potential shift to the selected shell,
# separately for each spin channel, with LDAUU acting on spin up and LDAUJ on
# spin down. Setting both to alpha is therefore exactly the localized alpha of
# the linear-response method, and setting them to zero on every other species
# leaves those atoms untouched. (LDAUTYPE = 1 or 2 would add a real +U on top
# of the shift, which is a different calculation.)
#
# LDAU tags are PER SPECIES, not per atom, so the perturbed atom has to BE its
# own species. Relabelling it is not enough and not even possible — a POSCAR
# species is a chemical element. The mechanism is ASE's INTEGER-KEYED `setups`:
# `setups={0: 'Fe'}` marks atom 0 as a "special setup", and ASE then writes it
# first in the POSCAR as a species of its own with its own POTCAR, ahead of the
# ordinary per-element blocks. Verified ordering (ase 3.29,
# GenerateVaspInput._make_sort): special-setup atoms first, in the order given,
# then the remaining symbols in first-appearance order. `_species_order()`
# below reproduces exactly that, because the LDAU arrays are positional and an
# order that disagrees with the POSCAR silently perturbs the wrong species.
#
# LMAXMIX = 4 (d) or 6 (f) is not optional. It controls how many angular
# momenta of the one-centre occupancies are mixed, and with the default of 2
# the occupation matrix an LDAU run reports is not converged even when the
# energy is — the response then comes out wrong by tens of percent with no
# other symptom.
from ase.calculators.vasp import Vasp

# Atom index -> POTCAR name. The perturbed atoms keep their own element's
# dataset: they are the same atom as before, split out only so the tags can
# address them.
_SETUPS = {index: atoms[index].symbol for index in _TARGETS}


def _species_order():
    """POSCAR species order, mirroring ASE's own sort.

    Perturbed atoms first (one species each, in _TARGETS order), then the
    remaining elements in the order they first appear.
    """
    order = [atoms[index].symbol for index in _TARGETS]
    for i, atom in enumerate(atoms):
        if i in _TARGETS:
            continue
        if atom.symbol not in order[len(_TARGETS):]:
            order.append(atom.symbol)
    return order


def _make_calculator(directory, alpha_by_site, non_scf):
    # Positional, one entry per POSCAR species. The first len(_TARGETS)
    # entries are the perturbed atoms, in order.
    ldau_l, ldau_u, ldau_j = [], [], []
    for position, _symbol in enumerate(_species_order()):
        alpha = alpha_by_site.get(position) if position < len(_TARGETS) else None
        if position < len(_TARGETS):
            # Every measured site carries the manifold, whether or not it is
            # the one being perturbed this run: its occupation is what the
            # off-diagonal chi_ij is read from.
            ldau_l.append(_HUBBARD_L)
            ldau_u.append(alpha or 0.0)
            ldau_j.append(alpha or 0.0)
        else:
            # -1 switches the correction off for this species entirely.
            ldau_l.append(-1)
            ldau_u.append(0.0)
            ldau_j.append(0.0)

    kwargs = dict(_VASP_BASE)
    kwargs.update(
        directory=directory,
        setups=dict(_SETUPS),
        ldau=True,
        ldautype=3,
        ldaul=ldau_l,
        ldauu=ldau_u,
        ldauj=ldau_j,
        ldauprint=2,          # print the occupancy matrices to OUTCAR
        lmaxmix=6 if _HUBBARD_L == 3 else 4,
    )
    if non_scf:
        # chi_0: the response of the Kohn-Sham system at the UNPERTURBED
        # self-consistent potential. ICHARG = 11 reads CHGCAR and keeps the
        # density fixed, and NELM = 1 stops after the single diagonalization
        # that the perturbation has entered. Letting this converge would
        # measure chi a second time and give U = 0.
        kwargs.update(icharg=11, nelm=1)
    return Vasp(**kwargs)


def _read_occupations(directory, n_sites):
    """Trace of the on-site occupancy matrix for each measured atom.

    LDAUPRINT = 2 makes VASP write one 'onsite density matrix' block per
    LDAU atom per spin, in the order those atoms appear in the POSCAR — which
    is why the measured atoms are written first: their blocks then come first,
    in _TARGETS order, with nothing to disambiguate them from. The quantity
    the method needs is the total occupation of the manifold, i.e. the trace,
    summed over spins.
    """
    path = os.path.join(directory, "OUTCAR")
    with open(path, "r", errors="replace") as handle:
        lines = handle.readlines()

    # Only the LAST electronic step's matrices are the converged ones; VASP
    # prints a set per step when LDAUPRINT = 2.
    starts = [i for i, line in enumerate(lines)
              if "onsite density matrix" in line]
    if not starts:
        raise RuntimeError(
            "No on-site density matrix in %s. LDAUPRINT=2 and LDAU=.TRUE. are "
            "required; a run that stopped before the first electronic step "
            "writes none." % path)

    size = 2 * _HUBBARD_L + 1
    traces = []
    for start in starts[-n_sites * _N_SPIN:]:
        # The matrix follows after a blank line; read `size` rows of `size`
        # floats, skipping anything that is not a full numeric row.
        rows, i = [], start + 1
        while i < len(lines) and len(rows) < size:
            tokens = lines[i].split()
            if len(tokens) == size:
                try:
                    rows.append([float(t) for t in tokens])
                except ValueError:
                    pass
            i += 1
        if len(rows) < size:
            raise RuntimeError("Truncated occupancy matrix in %s" % path)
        traces.append(sum(rows[k][k] for k in range(size)))

    # Sum the spin channels back together: n = n_up + n_down.
    per_site = []
    for s in range(n_sites):
        per_site.append(sum(traces[s * _N_SPIN + spin]
                            for spin in range(_N_SPIN)))
    return per_site


def _prepare(scratch):
    """Nothing to do: VASP splits the species through `setups`, not the atoms."""
    return scratch


def _seed_from_reference(directory):
    """Give a chi_0 run the UNPERTURBED density to hold fixed.

    From the reference run, never from the perturbed self-consistent one —
    restarting from a screened density would make this a second measurement
    of chi, and U would come out near zero.
    """
    for name in ("CHGCAR", "WAVECAR"):
        source = os.path.join("alpha_reference", name)
        if os.path.exists(source):
            shutil.copy2(source, os.path.join(directory, name))
        elif name == "CHGCAR":
            raise FileNotFoundError(
                "alpha_reference/CHGCAR is missing — chi_0 has no density to "
                "hold fixed. LCHARG must be on for the reference run.")
)PY";
    (void)c;
}

/// Quantum ESPRESSO half of the driver.
void emitEspressoBackend(std::ostringstream& out, const HubbardRunConfig& c)
{
    out << R"PY(
# --- Quantum ESPRESSO backend ---------------------------------------------
#
# The perturbation is `Hubbard_alpha`, which is the alpha of the method by
# name — pw.x adds alpha * n to the energy of the selected manifold of one
# atomic TYPE. As in VASP the tag is per type, so the perturbed atom has to be
# a type of its own.
#
# The mechanism for splitting it is ASE's: when nspin = 2, two atoms of the
# same element with DIFFERENT initial magnetic moments are written as separate
# ATOMIC_SPECIES entries ("Fe", "Fe1", ...) sharing one pseudopotential. So
# each measured atom is given a moment offset by a negligible epsilon, which
# splits the type without changing the physics — it is a starting guess, not a
# constraint. Verified against ase 3.29's write_espresso_in.
#
# Two consequences worth knowing:
#   * The QE path is always spin-polarized. A non-magnetic system converges to
#     the non-magnetic solution anyway, at roughly twice the cost; there is no
#     way to split a type without it.
#   * The species INDEX that Hubbard_U(i) refers to is the order of first
#     appearance of each (symbol, moment) pair — NOT "perturbed first" as in
#     the VASP path. `_species_index()` reproduces that ordering; getting it
#     wrong perturbs a different sublattice and reports no error.
#
# A vanishingly small Hubbard_U (1e-8 eV) is set on every measured type. It is
# physically nothing, but it is what makes pw.x build the Hubbard projectors
# and report Tr[ns] at all; without it the occupations never appear in the
# output and there is nothing to differentiate.
#
# chi_0 comes from `electron_maxstep = 1` with the density restarted from the
# unperturbed run — the same single-diagonalization trick as the VASP path.
from ase.calculators.espresso import Espresso

# Epsilon large enough for ASE to see two distinct moments, small enough to be
# physically nothing. Each measured atom gets its own multiple so that two
# measured atoms of the same element also split from each other.
_MAGMOM_EPSILON = 1e-3


def _apply_split_moments(scratch):
    moments = list(scratch.get_initial_magnetic_moments())
    for n, index in enumerate(_TARGETS):
        moments[index] = moments[index] + (n + 1) * _MAGMOM_EPSILON
    scratch.set_initial_magnetic_moments(moments)
    return scratch


def _species_index(scratch):
    """1-based ATOMIC_SPECIES index of each measured atom.

    Mirrors ase.io.espresso.write_espresso_in: a new species is created for
    every (symbol, initial moment) pair, in the order the atoms appear.
    """
    seen, index_of = [], {}
    moments = scratch.get_initial_magnetic_moments()
    for i, atom in enumerate(scratch):
        key = (atom.symbol, moments[i])
        if key not in seen:
            seen.append(key)
        index_of[i] = seen.index(key) + 1
    return [index_of[index] for index in _TARGETS]


def _make_calculator(directory, alpha_by_site, non_scf, scratch):
    system = dict(_QE_SYSTEM)
    system["nspin"] = 2          # required: types split by moment
    system["lda_plus_u"] = True
    system["lda_plus_u_kind"] = 0
    for position, sidx in enumerate(_species_index(scratch)):
        # Written as indexed namelist keys rather than through a dict, so what
        # lands in &SYSTEM is exactly Hubbard_U(i) / Hubbard_alpha(i) with the
        # index this script computed.
        system[f"hubbard_u({sidx})"] = 1e-8
        system[f"hubbard_alpha({sidx})"] = alpha_by_site.get(position, 0.0)

    electrons = dict(_QE_ELECTRONS)
    if non_scf:
        electrons.update(electron_maxstep=1, startingpot="file",
                         scf_must_converge=False)
    control = dict(_QE_CONTROL, outdir=os.path.join(directory, "pwscf"))
    if non_scf:
        control["restart_mode"] = "restart"
    return Espresso(
        directory=directory,
        pseudopotentials=_PSEUDOPOTENTIALS,
        input_data={"control": control,
                    "system": system,
                    "electrons": electrons},
        kpts=_KPTS,
    )


def _read_occupations(directory, n_sites):
    """Tr[ns(na)] per perturbed atom, from the pw.x output.

    pw.x prints a Hubbard occupation block per SCF step; the last one is the
    converged occupation, and 'Tr[ns(na)]' carries the total occupation of the
    manifold, already summed over spin.
    """
    path = None
    for name in sorted(os.listdir(directory)):
        if name.endswith(".pwo") or name.endswith(".out"):
            path = os.path.join(directory, name)
    if path is None:
        raise RuntimeError("No pw.x output found in %s" % directory)
    with open(path, "r", errors="replace") as handle:
        text = handle.read()

    blocks = text.split("Tr[ns(")
    traces = []
    for block in blocks[1:]:
        # "Tr[ns(  1)] =   7.98765"
        try:
            atom = int(block.split(")")[0])
            value = float(block.split("=")[1].split()[0])
        except (IndexError, ValueError):
            continue
        traces.append((atom, value))
    if len(traces) < n_sites:
        raise RuntimeError(
            "pw.x reported %d Hubbard occupations, expected at least %d. A "
            "run without lda_plus_u reports none." % (len(traces), n_sites))
    # Last n_sites entries are the final SCF step's, in atom order.
    return [value for _, value in traces[-n_sites:]]


def _prepare(scratch):
    """Split the measured atoms into their own types (see above)."""
    return _apply_split_moments(scratch)


def _seed_from_reference(directory):
    """Copy the reference run's outdir so chi_0 restarts from its density."""
    source = os.path.join("alpha_reference", "pwscf")
    if not os.path.isdir(source):
        raise FileNotFoundError(
            "alpha_reference/pwscf is missing — chi_0 has no density to "
            "restart from.")
    target = os.path.join(directory, "pwscf")
    if os.path.isdir(target):
        shutil.rmtree(target)
    shutil.copytree(source, target)
)PY";
    (void)c;
}

} // namespace

bool hubbardSupportsCalculator(CalculatorKind kind)
{
    return kind == CalculatorKind::Vasp
        || kind == CalculatorKind::QuantumEspresso;
}

std::string toString(HubbardShell shell)
{
    switch (shell) {
    case HubbardShell::P:
        return "p";
    case HubbardShell::D:
        return "d";
    case HubbardShell::F:
        return "f";
    }
    return "d";
}

std::string generateHubbardScript(const HubbardRunConfig& config,
                                  const std::string& structureFile)
{
    const bool vasp = config.calculator.calculator == CalculatorKind::Vasp;
    std::ostringstream out;

    out << "#!/usr/bin/env python3\n"
           "# Linear-response Hubbard U — Cococcioni & de Gironcoli,\n"
           "# Phys. Rev. B 71, 035105 (2005).\n"
           "#\n"
           "# Generated by Calango. This is a plain ASE/Python script: edit it\n"
           "# freely or run it standalone wherever "
        << (vasp ? "VASP" : "Quantum ESPRESSO") << " is installed.\n"
           "#\n"
           "# WHAT IT COMPUTES\n"
           "#\n"
           "#   A localized potential alpha is added to the Hubbard manifold of\n"
           "#   one atom, and the occupation of that manifold is measured two\n"
           "#   ways: after a single diagonalization at the unperturbed\n"
           "#   self-consistent potential (the NON-INTERACTING response, chi_0)\n"
           "#   and after full self-consistency (the INTERACTING response, chi).\n"
           "#   The spurious curvature the +U term exists to cancel is the\n"
           "#   difference of the two inverse responses:\n"
           "#\n"
           "#       U_eff = (chi_0^-1 - chi^-1)_ii\n"
           "#\n"
           "#   chi_0 is not 'the response at U = 0'. It is the response BEFORE\n"
           "#   the other electrons have screened the perturbation. Converging\n"
           "#   it self-consistently measures chi twice and yields U = 0.\n"
           "#\n"
           "# WHAT TO CHECK BEFORE BELIEVING THE NUMBER\n"
           "#\n"
           "#   * The supercell. The perturbation is applied to every periodic\n"
           "#     image at once, so a cell that is too small measures the\n"
           "#     response to a LATTICE of perturbations. Repeat on a larger\n"
           "#     supercell; U is converged when it stops moving.\n"
           "#   * The linearity of the fit. hubbard_response.json carries every\n"
           "#     (alpha, n) point and the residual of each fit. A residual\n"
           "#     that is not small means alpha is too large.\n"
           "\n"
           "import json\n"
           "import os\n"
           "import shutil\n"
           "\n"
           "from ase.io import read, write\n"
           "\n";

    out << AseScriptGenerator::jsonLoggerPreamble() << "\n";

    // --- Inputs -----------------------------------------------------------
    out << "# --- Inputs "
           "--------------------------------------------------------------\n"
           "_SITES = "
        << pySiteTable(config.sites) << "\n"
        << "_ALPHAS = " << pyList(config.alphas) << "\n"
        << "_SUPERCELL = (" << config.supercell[0] << ", "
        << config.supercell[1] << ", " << config.supercell[2] << ")\n"
        << "# One l for the whole run: a single U matrix mixes sites of the\n"
           "# same manifold, and the engines take one LDAUL / Hubbard manifold\n"
           "# per species anyway.\n"
           "_HUBBARD_L = "
        << (config.sites.empty()
                ? 2
                : static_cast<int>(config.sites.front().shell))
        << "\n"
        << "_N_SPIN = "
        << (config.calculator.spinMode != SpinMode::Unpolarized
                    || config.calculator.spinPolarized
                ? 2
                : 1)
        << "\n"
        << "_WRITE_RESPONSE = "
        << (config.writeResponseData ? "True" : "False") << "\n\n";

    // --- Engine parameters ------------------------------------------------
    const CalculatorConfig& c = config.calculator;
    if (vasp) {
        out << "_VASP_BASE = dict(\n"
            << "    xc=\"" << c.vaspXc << "\",\n"
            << "    encut=" << c.planeWaveCutoffEv << ",\n"
            << "    kpts=(" << c.kpts[0] << ", " << c.kpts[1] << ", "
            << c.kpts[2] << "),\n"
            << "    gamma=" << (c.kptsGammaCentered ? "True" : "False") << ",\n"
            << "    prec=\"Accurate\",\n"
            << "    ediff=" << c.vaspEdiff << ",\n"
            << "    nelm=" << c.vaspNelm << ",\n"
            << "    ispin=" << (c.spinMode != SpinMode::Unpolarized
                                        || c.spinPolarized
                                    ? 2
                                    : 1)
            << ",\n"
            << "    ibrion=-1,\n"
            << "    nsw=0,\n"
            << "    # The unperturbed run has to leave a CHGCAR behind: every\n"
               "    # chi_0 step restarts the density from it.\n"
               "    lcharg=True,\n"
               "    lwave=True,\n"
            << ")\n\n";
    } else {
        out << "_QE_CONTROL = dict(calculation=\"scf\", tprnfor=True)\n"
            << "_QE_SYSTEM = dict(\n"
            << "    ecutwfc=" << c.planeWaveCutoffEv / 13.605693 << ",  # Ry\n"
            << "    ecutrho=" << 8.0 * c.planeWaveCutoffEv / 13.605693
            << ",\n"
            << "    occupations=\"smearing\",\n"
            << "    smearing=\"gaussian\",\n"
            << "    degauss=" << c.smearingWidthEv / 13.605693 << ",\n"
            << "    nspin=" << (c.spinMode != SpinMode::Unpolarized
                                        || c.spinPolarized
                                    ? 2
                                    : 1)
            << ",\n"
            << ")\n"
            << "_QE_ELECTRONS = dict(conv_thr=" << c.scfEnergyTolEv / 13.605693
            << ", electron_maxstep=" << c.scfMaxSteps << ")\n"
            << "_KPTS = (" << c.kpts[0] << ", " << c.kpts[1] << ", "
            << c.kpts[2] << ")\n"
            << "# One UPF per species. The split species share the element's\n"
               "# pseudopotential — they are the same atom, differently\n"
               "# labelled, and giving them different UPFs would make the\n"
               "# perturbed site a different material.\n"
               "_PSEUDOPOTENTIALS = {}\n\n";
    }

    // --- Structure & species splitting -----------------------------------
    out << "# --- Structure "
           "-----------------------------------------------------------\n"
           "_calango_event('info', 'Building the supercell')\n"
           "primitive = read(\""
        << structureFile << "\")\n"
        << "atoms = primitive.repeat(_SUPERCELL)\n"
        << (vasp ? AseScriptGenerator::vaspPotcarResolutionSnippet(
                       c.vaspPotcarPath)
                : std::string())
        << "_calango_event('info',\n"
           "               f'{len(primitive)} atoms -> {len(atoms)} in a '\n"
           "               f'{_SUPERCELL[0]}x{_SUPERCELL[1]}x{_SUPERCELL[2]} "
           "supercell')\n"
           "\n"
           "# The perturbed atoms are the images of the chosen primitive sites\n"
           "# in the FIRST copy of the cell. ase.Atoms.repeat() tiles the\n"
           "# primitive block-wise, so primitive index i is supercell index i.\n"
           "_TARGETS = [site[\"index\"] for site in _SITES]\n"
           "for index, site in zip(_TARGETS, _SITES):\n"
           "    if index >= len(primitive):\n"
           "        raise IndexError(\n"
           "            f'Site index {index} is outside the {len(primitive)}"
           "-atom cell')\n"
           "    if atoms[index].symbol != site[\"symbol\"]:\n"
           "        raise ValueError(\n"
           "            f'Atom {index} is {atoms[index].symbol}, not "
           "{site[\"symbol\"]}')\n"
           "\n"
           "# Both engines apply the Hubbard tags per SPECIES, so each measured\n"
           "# atom has to become a species of its own — otherwise the alpha\n"
           "# meant for one atom lands on every atom of that element in the\n"
           "# cell, perturbing the whole sublattice and measuring a different\n"
           "# response entirely. The two engines need different mechanisms for\n"
           "# that, so each backend below does it its own way.\n"
           "\n";

    if (vasp)
        emitVaspBackend(out, config);
    else
        emitEspressoBackend(out, config);

    // --- The pipeline -----------------------------------------------------
    out << R"PY(

# --- Pipeline -------------------------------------------------------------
_N_SITES = len(_SITES)
_TOTAL_RUNS = 1 + 2 * _N_SITES * len(_ALPHAS)
_done = 0


def _advance(message):
    global _done
    _done += 1
    _calango_progress(_done, _TOTAL_RUNS)
    _calango_event('info', message)


def _run(directory, alpha_by_site, non_scf):
    """One SCF (or one diagonalization) and the occupations it produced."""
    os.makedirs(directory, exist_ok=True)
    scratch = _prepare(atoms.copy())
    scratch.calc = _make_calculator(directory, alpha_by_site, non_scf, scratch)
    # A single point: the energy call is what drives the run. Its VALUE is not
    # used — the occupations are — but it is what makes the calculator execute
    # and it is what fails loudly if the run did not converge.
    energy = scratch.get_potential_energy()
    return energy, _read_occupations(directory, _N_SITES)


# Step 1: the unperturbed reference. Everything else is a difference from it,
# and its density is what every chi_0 step restarts from.
_calango_event('info', 'Unperturbed SCF (alpha = 0)')
_e0, _n0 = _run("alpha_reference", {}, non_scf=False)
_advance(f'Reference occupations: '
         + ', '.join(f'{n:.4f}' for n in _n0))
_calango_metric(0, energy=_e0)

# Steps 2..N: one perturbed pair per (site, alpha). The chi_0 run has to see
# the reference density, so its directory is seeded from the reference run.
_scf = {j: {} for j in range(_N_SITES)}   # chi   : n_i(alpha_j), self-consistent
_bare = {j: {} for j in range(_N_SITES)}  # chi_0 : n_i(alpha_j), one diagonalization

for j in range(_N_SITES):
    for alpha in _ALPHAS:
        tag = f'site{j + 1}_alpha{alpha:+.3f}'.replace('.', 'p')

        # chi: full self-consistency, the screened response.
        directory = f'scf_{tag}'
        _, n_scf = _run(directory, {j: alpha}, non_scf=False)
        _scf[j][alpha] = n_scf
        _advance(f'chi   site {j + 1}, alpha = {alpha:+.3f} eV -> '
                 + ', '.join(f'{n:.4f}' for n in n_scf))

        # chi_0: the same perturbation, one diagonalization, at the reference
        # potential. Seeded from the reference run rather than from the
        # perturbed one above — restarting from a screened density would make
        # this a second measurement of chi.
        directory = f'bare_{tag}'
        os.makedirs(directory, exist_ok=True)
        _seed_from_reference(directory)
        _, n_bare = _run(directory, {j: alpha}, non_scf=True)
        _bare[j][alpha] = n_bare
        _advance(f'chi_0 site {j + 1}, alpha = {alpha:+.3f} eV -> '
                 + ', '.join(f'{n:.4f}' for n in n_bare))


# --- Response matrices ----------------------------------------------------
#
# chi_ij = dn_i / dalpha_j, from a straight-line least-squares fit through the
# (alpha, n) points INCLUDING the unperturbed one at alpha = 0. A fit rather
# than a finite difference between the two extreme alphas: the fit uses every
# point, and its residual is the only warning you get that the response has
# left the linear regime.


def _slope_and_residual(xs, ys):
    """Least-squares slope of y = a + b x, and the RMS residual."""
    n = len(xs)
    mean_x = sum(xs) / n
    mean_y = sum(ys) / n
    sxx = sum((x - mean_x) ** 2 for x in xs)
    if sxx == 0.0:
        raise ZeroDivisionError('All alphas are identical')
    sxy = sum((x - mean_x) * (y - mean_y) for x, y in zip(xs, ys))
    slope = sxy / sxx
    intercept = mean_y - slope * mean_x
    residual = (sum((y - intercept - slope * x) ** 2
                    for x, y in zip(xs, ys)) / n) ** 0.5
    return slope, residual


def _response(table):
    """chi[i][j] = dn_i/dalpha_j over all measured sites."""
    matrix = [[0.0] * _N_SITES for _ in range(_N_SITES)]
    residuals = [[0.0] * _N_SITES for _ in range(_N_SITES)]
    for j in range(_N_SITES):
        xs = [0.0] + list(_ALPHAS)
        for i in range(_N_SITES):
            ys = [_n0[i]] + [table[j][alpha][i] for alpha in _ALPHAS]
            matrix[i][j], residuals[i][j] = _slope_and_residual(xs, ys)
    return matrix, residuals


def _invert(matrix):
    """Gauss-Jordan inverse. Small and dense — one entry per perturbed site."""
    n = len(matrix)
    a = [row[:] + [1.0 if i == j else 0.0 for j in range(n)]
         for i, row in enumerate(matrix)]
    for col in range(n):
        pivot = max(range(col, n), key=lambda r: abs(a[r][col]))
        if abs(a[pivot][col]) < 1e-14:
            raise ZeroDivisionError(
                'Singular response matrix: the occupation did not respond to '
                'the perturbation. Check that the Hubbard projectors are '
                'active and that alpha is large enough to move n at all.')
        a[col], a[pivot] = a[pivot], a[col]
        scale = a[col][col]
        a[col] = [v / scale for v in a[col]]
        for row in range(n):
            if row == col:
                continue
            factor = a[row][col]
            if factor:
                a[row] = [v - factor * w for v, w in zip(a[row], a[col])]
    return [row[n:] for row in a]


_chi, _chi_residual = _response(_scf)
_chi0, _chi0_residual = _response(_bare)
_chi_inv = _invert(_chi)
_chi0_inv = _invert(_chi0)

# U_eff = (chi_0^-1 - chi^-1)_ii. The diagonal: the off-diagonal elements of
# the difference are the inter-site V of the extended DFT+U+V functional, and
# are reported too rather than discarded.
_u_matrix = [[_chi0_inv[i][j] - _chi_inv[i][j] for j in range(_N_SITES)]
             for i in range(_N_SITES)]
_u_eff = [_u_matrix[i][i] for i in range(_N_SITES)]

_results = {
    "method": "linear response (Cococcioni & de Gironcoli, PRB 71, 035105)",
    "supercell": list(_SUPERCELL),
    "alphas_ev": list(_ALPHAS),
    "sites": [
        {
            "index": site["index"],
            "element": site["symbol"],
            "shell": site["shell"],
            "occupation_unperturbed": _n0[i],
            "U_eff_ev": _u_eff[i],
        }
        for i, site in enumerate(_SITES)
    ],
    "chi": _chi,
    "chi0": _chi0,
    "U_matrix_ev": _u_matrix,
}
with open("hubbard_u.json", "w") as handle:
    json.dump(_results, handle, indent=2)

if _WRITE_RESPONSE:
    with open("hubbard_response.json", "w") as handle:
        json.dump({
            "unperturbed": _n0,
            "chi_points": {str(a): _scf[j][a]
                           for j in range(_N_SITES) for a in _ALPHAS},
            "chi0_points": {str(a): _bare[j][a]
                            for j in range(_N_SITES) for a in _ALPHAS},
            "chi_fit_residual": _chi_residual,
            "chi0_fit_residual": _chi0_residual,
        }, handle, indent=2)

# A large residual means the response is not linear over the alphas used, and
# every number above is then a fit to a curve. Said out loud rather than left
# in the JSON, because it invalidates the result and nothing else reports it.
_worst = max(max(max(row) for row in _chi_residual),
             max(max(row) for row in _chi0_residual))
if _worst > 1e-3:
    _calango_event('warning',
                   f'Non-linear response (worst fit residual {_worst:.2e} '
                   f'electrons). Reduce the perturbation strengths.')

for i, site in enumerate(_SITES):
    print(f"CALANGO_RESULT U_eff({site['symbol']}-{site['shell']}, "
          f"site {site['index']}) = {_u_eff[i]:.4f} eV")
    _calango_metric(i + 1, energy=_u_eff[i])

_calango_event('done', 'Hubbard U written to hubbard_u.json')
print("CALANGO_DONE")
)PY";

    return out.str();
}

} // namespace calango::core
