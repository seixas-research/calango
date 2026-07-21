#include "python_bridge/SqsBuilder.hpp"

#include "python_bridge/AseBridge.hpp"

#include <pybind11/embed.h>

#include <stdexcept>

namespace py = pybind11;

namespace calango::pybridge {

namespace {

/// The generator, executed in a scratch namespace. Inputs: atoms (base
/// cell), repeat, replace_symbol, target ({symbol: fraction}), shells,
/// steps, seed. Outputs: result_atoms, method, objective.
constexpr const char* kSqsScript = R"PY(
import numpy as np

rng = np.random.default_rng(seed)
supercell = atoms.repeat(tuple(repeat))
symbols = np.array(supercell.get_chemical_symbols(), dtype=object)
sites = np.flatnonzero(symbols == replace_symbol)
if sites.size == 0:
    raise ValueError(f"no {replace_symbol} sites in the supercell")

# Target fractions -> integer site counts (largest remainder rounding).
species = sorted(target)
fractions = np.array([target[s] for s in species], dtype=float)
fractions = fractions / fractions.sum()
ideal = fractions * sites.size
counts = np.floor(ideal).astype(int)
for k in np.argsort(ideal - counts)[::-1][: sites.size - counts.sum()]:
    counts[k] += 1

decoration = np.repeat(np.arange(len(species)), counts)
rng.shuffle(decoration)

method = ""
result_atoms = None
objective = 0.0

try:
    from icet import ClusterSpace
    from icet.tools.structure_generation import generate_sqs

    chemical_symbols = [
        species if s == replace_symbol else [s]
        for s in atoms.get_chemical_symbols()
    ]
    cs = ClusterSpace(atoms, cutoffs=[max(shells)], chemical_symbols=chemical_symbols)
    conc = {s: float(f) for s, f in zip(species, fractions)}
    result_atoms = generate_sqs(
        cluster_space=cs,
        max_size=int(np.prod(repeat) * len(atoms)),
        target_concentrations=conc,
    )
    method = "icet"
except Exception:
    # icet missing (or its sublattice setup rejected this system) — the
    # internal annealer below handles it instead.
    result_atoms = None

if result_atoms is None:
    # Internal backend: simulated annealing that drives the Warren-Cowley
    # pair parameters of the chosen shells toward zero on the sublattice.
    from ase.neighborlist import neighbor_list

    rmax = max(shells)
    i_all, j_all, d_all = neighbor_list("ijd", supercell, rmax)
    on_lattice = np.zeros(len(supercell), dtype=bool)
    on_lattice[sites] = True
    site_rank = np.full(len(supercell), -1, dtype=int)
    site_rank[sites] = np.arange(sites.size)
    keep = on_lattice[i_all] & on_lattice[j_all]
    pi, pj, pd = site_rank[i_all[keep]], site_rank[j_all[keep]], d_all[keep]

    edges = [c for c in (min(shells), max(shells)) if c > 0]
    shell_pairs = []
    lower = 0.0
    for edge in edges:
        mask = (pd > lower) & (pd <= edge)
        if mask.any():
            shell_pairs.append((pi[mask], pj[mask]))
        lower = edge

    ns = len(species)
    conc = counts / counts.sum()

    def objective_of(labels):
        total = 0.0
        for si, sj in shell_pairs:
            pair_counts = np.zeros((ns, ns))
            np.add.at(pair_counts, (labels[si], labels[sj]), 1.0)
            row = pair_counts.sum(axis=1, keepdims=True)
            with np.errstate(divide="ignore", invalid="ignore"):
                alpha = 1.0 - (pair_counts / row) / conc[None, :]
            total += float(np.nansum(alpha * alpha))
        return total

    labels = decoration.copy()
    current = objective_of(labels)
    best, best_labels = current, labels.copy()
    t0, t1 = 0.5, 1e-3
    for step in range(int(steps)):
        a, b = rng.integers(0, sites.size, size=2)
        if labels[a] == labels[b]:
            continue
        labels[a], labels[b] = labels[b], labels[a]
        proposal = objective_of(labels)
        temperature = t0 * (t1 / t0) ** (step / max(1, steps - 1))
        if proposal <= current or rng.random() < np.exp(
            -(proposal - current) / temperature
        ):
            current = proposal
            if current < best:
                best, best_labels = current, labels.copy()
        else:
            labels[a], labels[b] = labels[b], labels[a]

    symbols[sites] = np.array(species, dtype=object)[best_labels]
    supercell.set_chemical_symbols(list(symbols))
    result_atoms = supercell
    method = "Monte Carlo (internal)"
    objective = best
)PY";

} // namespace

SqsBuilder::Result SqsBuilder::generate(const core::Structure& base,
                                        const Params& params)
{
    if (params.composition.size() < 2)
        throw std::runtime_error(
            "SQS needs at least two species in the target composition");

    try {
        py::dict scope;
        scope["atoms"] = AseBridge::toAtoms(base);
        scope["repeat"] = py::make_tuple(params.nx, params.ny, params.nz);
        scope["replace_symbol"] = params.replaceElement;
        py::dict target;
        for (const auto& [symbol, fraction] : params.composition)
            target[py::str(symbol)] = fraction;
        scope["target"] = target;
        scope["shells"] = py::make_tuple(params.shell1, params.shell2);
        scope["steps"] = params.steps;
        scope["seed"] = params.seed;

        py::exec(kSqsScript, py::globals(), scope);

        Result result;
        result.structure = AseBridge::fromAtoms(scope["result_atoms"]);
        result.method = scope["method"].cast<std::string>();
        result.objective = scope["objective"].cast<double>();
        return result;
    } catch (const py::error_already_set& e) {
        throw std::runtime_error(std::string("SQS generation failed:\n")
                                 + e.what());
    }
}

} // namespace calango::pybridge
