# MLIP training

{menuselection}`Modules --> MLIP` holds the two ends of the machine-learning-potential loop: the {guilabel}`Dataset Manager…` assembles and partitions training data from the trajectories you have generated, and the {guilabel}`Trainer…` builds and launches a MACE training run on it. The models that come out are what the MACE (and the other MLIP) engines in {doc}`/simulations/calculators` consume as custom checkpoints.

---

## The trainer

The Trainer is an interactive builder for MACE training configuration files (`mace_train.yaml`), with a live, editable YAML preview. **Every key it emits is one `mace.tools.arg_parser` accepts** — MACE loads the config through configargparse, which *aborts* on a key it does not recognize, so an invented setting is a failed run rather than an ignored line.

### Dataset and reference keys

- {guilabel}`Training file` / {guilabel}`Validation file` — extxyz sets, typically straight from the Dataset Manager's export.
- {guilabel}`Energy key` / {guilabel}`Forces key` — where the reference labels are stored in the training file. `energy` / `forces` (the defaults) are what ASE writes — and therefore what a Calango-exported dataset carries; `REF_energy` / `REF_forces` are MACE's own convention for sets prepared its way.

:::{warning}
**Getting the keys wrong does not fail the run.** MACE warns, sets the corresponding loss weight to zero, and trains anyway — on nothing. A model that trained suspiciously fast with a flat forces loss usually named a forces key its dataset does not contain.
:::

- {guilabel}`Isolated-atom energies (E0s)` — the one-atom reference energies MACE subtracts before fitting, so the model learns interactions rather than the huge constant offsets of the atomic totals. Three sources:
  - *Average* — least-squares regression out of the training set itself. The right default; needs nothing extra.
  - *From a JSON file* — `{"42": -5.0448, "16": -0.9036}` from your own isolated-atom runs. The most accurate option, and the one to use when several models must share a reference.
  - *Isolated atoms in the training file* — only if the set contains single-atom frames tagged `config_type=IsolatedAtom`, which a Calango-exported set never has.

  **There is no "leave it out"**: with none of these, MACE aborts before the first epoch with *"E0s not found in training file and not specified in command line"*.

### Architecture and optimization

| Setting | Default | Notes |
|---|---|---|
| {guilabel}`Model size` | medium | preset: small = 64 channels, max L 0; medium = 128, L 1; large = 192, L 2 |
| {guilabel}`Cutoff radius r_max` | 5.0 Å | range 2–12 Å |
| {guilabel}`Channels` / {guilabel}`Max L` | 128 / 1 | overridable after the preset |
| {guilabel}`Device` | — | `cpu`, `cuda`, `mps` |
| {guilabel}`Default dtype` | float32 | float64 for reference-quality fits |
| {guilabel}`Learning rate` | 0.01 | — |
| {guilabel}`Batch size` | 10 | — |
| {guilabel}`Epochs` | 200 | — |
| Loss weights | E 1.0 · F 100.0 · stress 0 · virials 0 | per-property |
| {guilabel}`Patience` | 50 | early-stopping epochs |
| {guilabel}`Eval interval` | 5 | validation cadence |
| {guilabel}`Seed` | 123 | — |

Two production settings are on by default: **stage-two SWA** (from epoch 150 — MACE raises the energy weight and drops the learning rate for the averaging phase) and **EMA** (exponential moving average of the weights, decay 0.99).

### Active learning

The optional Query-by-Committee group (off by default) trains an ensemble instead of one model: committee size default **3** (2–16), with an uncertainty threshold (default **0.05**) recorded for the selection step. The exported runner script is a standalone Python launcher that writes `mace_train.yaml` and invokes the MACE trainer **once per committee seed**, so the ensemble members differ by initialization. Pair it with the Dataset Manager's committee export below for members that also differ by data.

The dialog has its own execution-environment selector (a conda environment dropdown plus a free path field) — training wants the `mace-torch` CUDA stack, not the embedded interpreter. {guilabel}`Export YAML…` saves the config; the run buttons launch through the standard job machinery ({doc}`/simulations/jobs`, {doc}`/simulations/remote`).

% TODO screenshot: the MACE Trainer dialog with the dataset/architecture forms on the left and the live YAML preview on the right
```{figure} /_static/img/sim_mlip_trainer.png
:alt: The MACE trainer dialog with its editable YAML preview
:width: 92%
:figclass: screenshot

The Trainer: every form change re-renders the YAML; every emitted key is one MACE's own parser accepts.
```

---

## The dataset manager

{menuselection}`Modules --> MLIP --> Dataset Manager…` assembles training datasets from what you already computed — MD runs, noise ensembles, relaxation paths, cluster-expansion batches — and partitions them reproducibly.

**Loading.** {guilabel}`Add Files…` accepts multiple files of mixed provenance: `.extxyz`, `.traj`, `.cif`, VASP files, ASE databases. Each is listed with its frame count and the chemical formulas it contains, so a heterogeneous dataset stays legible; the summary line reports the total.

**Splitting.** Training default **80 %**, validation **10 %**, test the remainder (shown live); random seed default **42**. The split is deterministic — the same frame count, percentages and seed always produce exactly the same partition — and the three subsets are disjoint and cover every frame.

**Query-by-Committee ensembles.** Committee members default **1** (a single dataset); $N > 1$ adds `committee_01` … `committee_N` subdirectories, each with its own training set. Two ways to make the members differ:

- *Independent splits (seed + k)* — re-splits the non-test pool with a different seed per member, keeping one common test set.
- *Bootstrap resampling* — draws each member's training set with replacement: the classical bagging construction, which reuses roughly 63 % of the original frames per member.

**Export.**

- {guilabel}`Extended XYZ (MACE-ready)` — writes `train.extxyz`, `valid.extxyz` and `test.extxyz` (plus the committee subdirectories): exactly the layout the Trainer above, and most training scripts, expect.
- {guilabel}`ASE database (.db)` — one SQLite database per subset.

:::{note}
Export goes through `ase.io` directly rather than Calango's internal structure model, specifically so that **energies, forces and stresses attached to the frames survive** into the training files. A dataset exported from a finished MD run carries the labels a potential needs to train on — with the default `energy`/`forces` key names the Trainer expects.
:::

% TODO screenshot: the Dataset Manager with mixed files loaded, the 80/10/10 split and the committee/export controls
```{figure} /_static/img/sim_mlip_dataset.png
:alt: The dataset manager with loaded trajectory files and split controls
:width: 92%
:figclass: screenshot

The Dataset Manager: mixed-provenance loading, a deterministic split, and MACE-ready export.
```
