# The simulation wizards

Every calculation setup in Calango — Single-point, Geometry Optimization, Molecular Dynamics, Phonon, Monte Carlo, the convergence sweeps, Electronic Structure and its post-processing relatives — is built on one shared wizard shell. Learn it once and every dialog on the {menuselection}`Simulation` menu reads the same way: a short sequence of stages ending in an editable script, with {guilabel}`Run (Local)` and {guilabel}`Run (Remote)` at the end.

**The form never runs anything directly — it generates an ordinary ASE Python script, and that script is what runs.** Everything the wizard can express, the script expresses in plain `ase` calls; everything the wizard cannot express, you add by editing the script.

---

## The staged flow

A wizard is a stepper: {guilabel}`Back` / {guilabel}`Next ›` move between stages, and the header always reads *Stage N of M — \<name\>*. Most wizards use three stages:

| Stage | Content |
|---|---|
| 1 — Task settings | What to compute: the ensemble and temperatures for MD, the optimizer and `fmax` for a relaxation, the sweep range for a convergence study. |
| 2 — Calculator Settings | The engine dropdown and every backend knob — cutoff, k-points, XC, per-engine groups. See {doc}`/simulations/calculators`. |
| 3 — ASE Script Review | The generated script, editable, plus the launch command and the run buttons. |

Two families deviate deliberately:

- **k-path wizards put the task stage after the engine.** The Phonon and Electronic Structure wizards define a path through the Brillouin zone, and a k-path only makes sense once the engine — and with it the structure's sampling — is decided. Their flow is *Calculator Settings → (task stages) → Script Review*.
- **The Phonon wizard adds a second task stage**, separating two genuinely different decisions: how the force constants are sampled (supercell, displacement δ, symmetry reduction) and where the dispersion is read out (the q-path). See {doc}`/simulations/phonons`.

The Single-point wizard folds its convergence controls into the calculator page instead of holding a stage of its own, so its calculator stage is titled *Calculator & Convergence Settings*.

% TODO screenshot: Molecular Dynamics wizard on Stage 2 (Calculator Settings) with GPAW selected, showing the header "Stage 2 of 3" and the thematic group boxes
```{figure} /_static/img/sim_wizard_stages.png
:alt: A simulation wizard on its Calculator Settings stage, with the stage header and the Back/Next/Run action bar
:width: 92%
:figclass: screenshot

The shared wizard shell: stage header, calculator page, and the action bar every wizard ends in.
```

---

## The script review stage

The final stage shows the complete generated script — syntax-highlighted Python in an editable pane.

- **Start typing and form sync pauses.** From the first keystroke the wizard stops regenerating the text, so hand edits are never silently overwritten. Going back to change a form value will *not* update an edited script.
- {guilabel}`Regenerate` discards your edits and re-syncs the script from the form.
- {guilabel}`Export Script…` writes the current text to a `.py` file (`run.py` by default; each wizard proposes its own name, e.g. `phonon.py`). The exported file is self-contained — it carries its own logging block and runs unmodified on a cluster.
- The {guilabel}`Running:` line above the script is the shell command the job launches with, resolved from the engine's template in {menuselection}`Preferences --> Run`. It is editable, so last-minute questions — MPI rank counts, thread pinning — are answered here for this run only, without touching the saved template. Once edited, the wizard stops re-resolving it.

:::{tip}
The editable script is the intended escape hatch for anything the GUI does not expose. Configure what you can in the form, then edit the rest — custom observers, exotic constraints, a different optimizer. See {doc}`/simulations/scripts` for the script anatomy.
:::

---

## Execution environment

The Python that runs the job is independent of the interpreter Calango embeds, and it is resolved **silently, per engine** — there is no environment stage in the wizard. Resolution order:

1. The engine's environment preset from {menuselection}`Preferences --> Python & Environments` (e.g. a `gpaw` conda environment bound to GPAW, a CUDA `mace-torch` environment bound to MACE).
2. The last globally selected environment, if the engine has no preset.
3. The embedded interpreter, if neither is set.

This is how heavyweight solver stacks stay isolated: each engine names its own environment once, and every wizard that selects that engine inherits it. The standalone NEB dialog and the MLIP Trainer additionally offer an explicit environment field of their own.

---

## Run locally, run remotely

The review stage ends in three actions:

- {guilabel}`Run (Local)` stages the structure and script into a fresh job directory and launches the run as a separate process — see {doc}`/simulations/jobs` for the directory layout, live metrics, streaming and the queue.
- {guilabel}`Run (Remote)` submits the same script through the remote execution machinery instead — see {doc}`/simulations/remote`.
- {guilabel}`Export Script…` takes the script and runs nothing.

Alongside the script, the host writes a compact **`calculator.json`** provenance record into the job directory: engine, XC functional, plane-wave cutoff, GPAW mode and grid spacing, k-grid, the symmetry flag, the resolved interpreter and the engine's environment preset.

---

## Baseline inheritance

The post-processing wizards on the {menuselection}`Electronics` menu do not converge an SCF of their own — they **inherit a completed baseline**. A GPAW Single-point run writes `single_point.gpw` (density + wavefunctions) next to its `calculator.json`, and a downstream run restarts from that file:

- The **Electronic Structure** wizard keeps the calculator page but locks it: the plane-wave cutoff, XC functional and mode are fixed by the baseline `.gpw`, the controls are hidden, and a note says so. Editing them would invite a change the restart ignores.
- The **Wannier Functions (MLWF), Optics, GW, ELF and Raman** wizards go further and drop the calculator stage entirely — the calculator is read back from the baseline's `calculator.json`, giving a strict two-stage flow (task settings → review) with a one-line summary of the inherited calculator (engine · XC · cutoff · k-grid · symmetry flag).

:::{important}
A baseline produced by an older Calango release may have no `calculator.json`. The inheriting wizards then fall back to their defaults — check the generated script before running.
:::

---

## Smaller conveniences

- **Suggested parameters.** When `~/.calango/calculator_parameters.json` contains an entry for the selected engine and the structure's elements, the wizard pre-fills the plane-wave cutoff and k-grid from it. No file, no engine entry, no element match — the hardcoded defaults stand. Suggestions are skipped entirely for inherited calculators.
- **Engine restriction.** A wizard may offer only the engines that can do its job — the convergence sweeps list GPAW and VASP only, the Electronic Structure wizard lists only DFT-capable engines.
- **Orchestration mode.** When a wizard is opened from the {doc}`/simulations/orchestration` canvas it configures a node instead of launching: {guilabel}`Run (Local)` becomes {guilabel}`Save process node`, and {guilabel}`Run (Remote)` is withdrawn — queueing is the canvas's concern.

% TODO screenshot: Script Review stage with the editable "Running:" command line, the Regenerate button and the Run (Local)/Run (Remote)/Export Script buttons
```{figure} /_static/img/sim_wizard_review.png
:alt: The ASE Script Review stage with the editable script pane and run buttons
:width: 92%
:figclass: screenshot

The review stage: the generated script is fully editable, and the launch command can be adjusted for this run only.
```
