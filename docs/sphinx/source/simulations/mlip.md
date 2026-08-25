# MLIP training

{menuselection}`Modules --> MLIP` holds the two ends of the machine-learning-potential loop: the {guilabel}`Dataset Manager…` assembles and partitions training data from the trajectories you have generated, and the {guilabel}`Trainer…` builds and launches a training run on it. The models that come out are what the MACE (and the other MLIP) engines in {doc}`/simulations/calculators` consume as custom checkpoints.

The same two steps also exist as {doc}`/simulations/orchestration` canvas nodes — {ref}`dataset-manager` and {ref}`mace-trainer` — for chaining them directly onto the calculations that produce the training data: `Structure Container → Single-point fan-out → Dataset Manager → MACE Trainer` is one graph, run end to end, rather than an export-then-reopen-a-dialog round trip. The node versions reuse this page's own dialogs (the Trainer's setup dialog is literally the same one), so everything below applies to both; the pipeline-specific parts (multi-parent merging, the manifest hand-off between the two nodes) are documented on the Orchestration page instead of duplicated here.

---

## The trainer

The Trainer is a **wizard**, one decision per page:

| Step | Page | What it decides |
|---|---|---|
| 1 | {guilabel}`Framework` | which model type to train |
| 2 | {guilabel}`Dataset` | where the reference data is, and how it is named |
| 3 | {guilabel}`Model` | what is being fitted |
| 4 | {guilabel}`Training` | how it is fitted |
| 5 | {guilabel}`Config and launch` | the generated config file — editable — plus the interpreter and the Run buttons |

It used to be one dialog with every MACE parameter on it at once, two columns
and five group boxes, and it had grown past the point where it could be read —
let alone adjusted — without scrolling past the control you came for. The
wizard is a fixed height and every parameter page scrolls **inside** it, so no
future addition can push it off a laptop screen again; a regression test walks
every page and asserts exactly that.

### Step 1 — which framework

Every machine-learning potential Calango knows how to *run* is listed, and
exactly one has an implemented trainer:

| Framework | Trainer | Config | Entry point |
|---|---|---|---|
| **MACE** | **implemented** | YAML (`mace_train.yaml`) | `mace.cli.run_train` |
| DeePMD-kit | not yet supported | JSON (`deepmd_input.json`) | `dp train` |
| NequIP | not yet supported | YAML (`nequip_train.yaml`) | `nequip-train` |
| Allegro | not yet supported | YAML (`allegro_train.yaml`) | `nequip-train` |
| CHGNet | not yet supported | Python (no CLI) | `chgnet.trainer.Trainer` |
| MatterSim | not yet supported | CLI flags | `finetune_mattersim` |
| FAIRChem / OCP | not yet supported | YAML fragments | OCP `main.py` |

The unsupported six are **listed rather than hidden**: hiding them answers
"can Calango train a NequIP model?" with silence, and silence reads as *look
harder*. Selecting one shows what its trainer reads and runs and what a
backend for it would need — they are not interchangeable, which is exactly why
none of them is half-built — and leaves {guilabel}`Next` **disabled**. The
gate is the button, not the row, so the reason can be read. The per-framework
notes are also in `FUTURE.md`.

The list is not typed out anywhere: it is the calculator library's own
`MachineLearning` family, so an MLIP added to Calango appears here
automatically, marked unsupported until somebody writes its backend. Adding
that backend is one subclass of `MlipTrainerBackend` plus one line — the
backend owns its own parameter pages, so nothing in the wizard is
framework-specific.

**Every key the MACE backend emits is one `mace.tools.arg_parser` accepts** —
MACE loads the config through configargparse, which *aborts* on a key it does
not recognize, so an invented setting is a failed run rather than an ignored
line. That is checked live against whatever `mace-torch` is installed, not
against a list written in Calango.

### Step 2 — dataset and reference keys

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

### Steps 3 and 4 — model and training

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
| {guilabel}`Interaction layers` | 2 | *Advanced* — message-passing depth; the receptive field is this × r_max |
| {guilabel}`Correlation order` | 3 | *Advanced* — body order of the product basis; what MACE is named for |

The two pages follow the same **Basic / Advanced** split the rest of the
application's settings pages use: the controls a run normally touches are
prominent, and the long tail sits in an {guilabel}`Advanced` group under
them. On the {guilabel}`Model` page that tail is the two architecture
constants above — which the old dialog wrote into *every* config with no
control at all, so "I need three message-passing layers" used to mean
hand-editing the YAML. On the {guilabel}`Training` page it is the schedule
(patience, validation interval, precision), the two-phase loss and the
active-learning committee.

Two production settings are on by default: **stage-two SWA** (from epoch 150 — MACE raises the energy weight and drops the learning rate for the averaging phase) and **EMA** (exponential moving average of the weights, decay 0.99).

#### Active learning

The optional Query-by-Committee group (off by default) trains an ensemble instead of one model: committee size default **3** (2–16), with an uncertainty threshold (default **0.05**) recorded for the selection step. The exported runner script is a standalone Python launcher that writes `mace_train.yaml` and invokes the MACE trainer **once per committee seed**, so the ensemble members differ by initialization. Pair it with the Dataset Manager's committee export below for members that also differ by data.

### Step 5 — the config, and it has the last word

The final page shows the generated config file in a **monospaced, editable**
view. Whatever is in that editor is what gets written and what runs —
verbatim, hand edits included. It replaces the old free-form "extra keys"
override box and subsumes it: a whole editable file is strictly more powerful
than an append-only override, and it has the property the override never did,
which is that **what you see is what runs**.

- A change on an earlier page **regenerates** the text — unless you have
  edited it, in which case the page says the settings moved under it and
  leaves your text alone. Silently replacing what somebody wrote is exactly
  what an editable config must not do.
- {guilabel}`Regenerate from settings` rebuilds it from the pages, and asks
  first, because that discards the edits.
- Re-opening a saved Orchestration {guilabel}`MACE Trainer` node restores the
  config it was saved with, verbatim — the widgets keep their own defaults,
  because only the text is stored, and nothing overwrites it.
- The version-keyed generation is unchanged: the `mace-torch` version from the
  last successful environment check is recorded as a comment at the top.

The page also carries the execution-environment selector (a conda environment
dropdown plus a free path field) — training wants the `mace-torch` CUDA/MPS
stack, not the embedded interpreter (which never ships `mace-torch`; it is not
vendored or hard-depended-on by Calango at all). {guilabel}`Export Config…`
saves the file; {guilabel}`Run (Local)` and {guilabel}`Run (Remote)` launch
through the standard job machinery ({doc}`/simulations/jobs`,
{doc}`/simulations/remote`). All three appear on this page and on no other: a
Run offered from the {guilabel}`Model` page would launch a config the user has
not been shown.

### Dependency pre-flight and device detection

{guilabel}`Check Environment` probes the interpreter selected above — a real subprocess `import mace` under it — and reports:

- Whether `mace-torch` is importable at all, and **which version**, read straight off the installed package (never assumed). The version is recorded as a comment at the top of the generated YAML, so a config file names the package it was actually generated/verified against.
- Which PyTorch **compute devices** are actually usable: `cpu` always; `cuda`/`mps` only when the installed PyTorch build and the machine's own hardware report them available (`torch.cuda.is_available()` / `torch.backends.mps.is_available()`, probed under the same interpreter). The best available device (cuda, then mps, then cpu) is suggested as the {guilabel}`Device` selection — not forced, so a deliberate choice (testing the cpu path, say) is left alone.

**Both {guilabel}`Run (Local)` and {guilabel}`Run (Remote)` run this same check automatically before launching anything.** `mace-torch` missing produces a clear message naming the package and `pip install mace-torch`, and the run is refused — never a crash partway through. (Run Remote checks the *local* interpreter field, which is what you have told Calango to resolve for this run; the actual remote host cannot be probed from here, but the common case — a conda env name reused verbatim on the cluster — is caught before anything is even staged.)

### Using the trained model

The finished checkpoint(s) land in the run's job directory under the name(s) the config's {guilabel}`name` field controls (one per Query-by-Committee seed, when that ensemble is enabled). To use one: open a MACE calculator's setup ({doc}`/simulations/calculators`), pick {guilabel}`Custom trained model` as the model, and {guilabel}`Browse…` to the checkpoint — already fully wired end to end, just not (yet) a single click straight from a finished training run.

### Live per-epoch metrics

The generated launcher installs a small logging hook around MACE's own training loop that parses its "`Epoch N: ... loss=X, RMSE_E_per_atom=Y meV, RMSE_F=Z meV`" log lines and writes them, one entry per epoch, to `mace_train_<seed>_metrics.json` beside the config — genuine training progress, not synthesized. It is intentionally **separate** from the committee progress file the launcher also writes (one entry per completed committee member): a re-entrant per-model training process cannot safely append into that shared file without risking another member's entry. The per-epoch file is not yet wired into a live chart in the Results panel — read it directly, or watch the raw training log, which already streams to the live-monitoring view like any other job's output.

% TODO screenshot: the Trainer wizard on its Config and launch page, with the editable YAML and the environment check below it
```{figure} /_static/img/sim_mlip_trainer.png
:alt: The Trainer wizard on its config page, with the editable YAML
:width: 92%
:figclass: screenshot

The Trainer: framework, then the framework's own parameter pages, then the config file — editable, and the last word on what runs.
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
