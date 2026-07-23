# Calango — User Guide

A task-oriented guide to the simulation workflows. For build and packaging
instructions see [`Calango_Packaging_Guide.md`](Calango_Packaging_Guide.md);
for the feature inventory see the top-level `README.md`.

---

## 1. The four-stage simulation wizards

Every simulation workflow under the **Simulation** menu shares one stepper
shell, so the flow is the same whichever calculation you are setting up:

| Stage | Contents |
|---|---|
| **Calculator & Execution Environment** | engine, Conda environment / interpreter, local vs remote |
| **Calculator Settings** | per-engine parameters (GPAW, MACE, ORCA, …) |
| *task stage* | the workflow's own settings — position varies, see below |
| **ASE Script Review** | the generated Python, editable, then Run or Export |

**Where the task stage sits.** Most wizards define *what to compute* first, so
their task stage leads. The Electronic Structure, Phonon and Effective Bands
wizards instead define a **k-path**, which only makes sense once the engine is
chosen — their task stage therefore comes *after* Calculator Settings.

**The script is the contract.** Stage 4 shows the actual `run.py` that will
execute. Edit it freely; the first manual edit pauses regeneration so your
changes are never silently overwritten (press **Regenerate** to go back to the
generated version). **Export Script…** writes it plus its `calango_log.py`
helper module, so an exported script runs standalone.

**Local vs remote.** Choose in Stage 1; the run buttons in Stage 4 follow.
A remote run stages `run.py`, the structure and a generated `job.sh`, uploads
them, submits, and tracks the job in the Zone-11 Remote Access panel. The
scheduler settings there (queue, walltime, cores, setup lines) are what the
submission is wrapped with.

### Available workflows

| Menu entry | Produces |
|---|---|
| **Single-point Calculation…** | energy, forces, stress at fixed geometry |
| **Geometry Optimization…** | relaxed structure + `opt.traj` |
| **Molecular Dynamics…** | `md.traj` + `md.extxyz` with per-frame forces and velocities |
| **Phonon…** | dispersion + PhDOS (`phonon_band.json`, `phonon_dos.json`) |
| **Electronic Structure…** | bands + PDOS (`bands.json`, `pdos.json`) |
| **Monte Carlo Simulation…** | sampled configurations |
| **Cluster Expansion Calculation…** | relaxed ensemble + convex hull |
| **Effective Bands…** | unfolded spectral function `A(k, E)` |

> **Trajectories start at step 1.** The raw input geometry is deliberately not
> written to `md.traj` / `md.extxyz` / `opt.traj`: it has no evaluated forces
> and no thermalized velocities, so it was the one frame with a blank vector
> overlay when scrubbing. Every recorded frame carries evaluated physics.

---

## 2. Interactive Brillouin zone and k-path definition

The Electronic Structure, Phonon and Effective Bands wizards embed the 3D
Brillouin-zone builder **directly in their k-path stage** — there is no export
and re-import step.

**Building a path.** Click high-symmetry points (Γ, X, M, L, K, …) on the
Wigner-Seitz cell in the order you want them traversed. Drag rotates, Shift +
drag pans, the wheel zooms. The sequence table lists the path with fractional
coordinates.

**Action bar.**

| Action | Effect |
|---|---|
| **Suggested** | load ASE's suggested path for the lattice |
| **Break** | start a discontinuous section (Γ→X ¦ M→R) |
| **Undo** | remove the last point |
| **Remove** | delete the selected row |
| **Clear** | empty the path |

**Points per segment** sets sampling density. This is the *only* k-density
control — the total point count is derived from it, so there is no second box
that can disagree.

The text field below the viewport mirrors the path as an ASE string
(`GXWK,UX`) and is authoritative: you can type a path the picker cannot
express. Editing on either side updates the script preview live, so **Next ›**
carries the path straight through.

A structure with no periodic cell has no Brillouin zone; the stage falls back
to the text field with an explanation rather than blocking the wizard.

**Standalone builder.** *Build → Brillouin Zone Builder…* hosts the same
widget with file exporters added: VASP `KPOINTS` (line mode), Quantum ESPRESSO
`K_POINTS crystal_b`, CASTEP, SIESTA `BandLines`, an ASE script, plus PNG/SVG
figure capture.

---

## 3. Electronic structure results

The viewer opens from the Process panel's **Load Result**, or automatically
when a bands job finishes.

**Fermi level.** Shown read-only — it is a result of the run, not a display
setting. Two independent controls sit beneath it:

* **Show Fermi level** — visibility of the dashed reference line.
* **Shift Fermi level to zero** — plot `E − E_F` (default) or absolute
  eigenvalues.

**Band gap.** Computed automatically and reported below those toggles:
magnitude, nature (direct / indirect / metallic), the VBM→CBM energies with
their k-point labels, and — for an indirect material — the **minimum direct
gap**, which is the optical onset and is strictly larger than the fundamental
gap. Reporting only the fundamental gap routinely misleads when comparing
against absorption data.

The classification is per *band*, not per eigenvalue: a band with eigenvalues
both below and above `E_F` is crossing it, so the system is metallic whatever
the k-spacing. Splitting raw eigenvalues at `E_F` instead would report a coarse
k-mesh metal as a small-gap semiconductor.

> The analysis only sees the plotted k-path. A true VBM or CBM lying off the
> path cannot be found by any band-structure plot, and the summary says so.

**Appearance and export.** **Customize Appearance…** exposes fonts (axis
titles, ticks, annotations), curve color/stroke/thickness, the reference
line's styling, plot background, spine and tick colors and widths, and DOS
fill. **Export Image…** writes PNG/JPEG at 3× or PDF/SVG vector art, rendered
through the same code path as the screen so exports match exactly.

---

## 4. Phonons

Stage 2 collects the finite-displacement settings (displacement δ, supercell,
acoustic sum rule, DOS mesh density, Gaussian broadening σ); Stage 3 is the
q-path. Molecular systems get Γ-point normal modes and no path.

**Gaussian smearing σ.** A finite q-mesh gives a comb of delta peaks; σ smooths
it into a continuous PhDOS. Too small leaves spikes, too large erases van Hove
features. Raise the mesh density before lowering σ.

The viewer offers the same **Customize Appearance…** and **Export Image…**
actions plus two granular CSV exporters — **Export Phonon Bands (.csv)** and
**Export PhDOS (.csv)**. They are separate because dispersion and DOS have
different independent variables (k-distance vs frequency); one combined file
had to concatenate two differently-shaped tables that most tools cannot read
back.

Imaginary (unstable) modes are reported as negative frequencies and plotted
below ω = 0; the appearance dialog's frequency-axis bounds let you bring them
into view.

---

## 5. Cluster expansion and convex hulls

Two steps.

**1. Build the ensemble.** *Build → Cluster Expansion…* enumerates
symmetry-inequivalent decorated configurations on the active sublattice and
opens them as a multi-frame trajectory.

**2. Relax and analyze.** *Simulation → Cluster Expansion Calculation…* takes
that trajectory and relaxes every frame in turn with the chosen calculator.
Stage 1 covers per-configuration relaxation (single-point vs relax, optimizer,
force convergence, per-configuration step cap, continue-on-failure) and the
formation-energy references.

**Formation energy.** By default the ensemble's own lowest-energy
configurations at the extreme compositions define μ_A and μ_B. This pins
`E_form = 0` at both endpoints and cancels the calculator's absolute energy
scale exactly, which is what makes hulls from different codes comparable.
Supply explicit references only when the ensemble lacks both pure endpoints.

**The hull.** On completion the Results panel gains a **Convex Hull** tab:
`E_form` vs concentration `x`. Configurations on the lower hull (stable) are
filled and joined by tie-lines; everything above it is hollow. Hovering reports
formula, composition and **energy above hull**; double-clicking jumps to that
configuration in the optimized trajectory.

Only the *lower* hull is physically meaningful — a point is stable exactly when
no combination of two other compositions reaches the same `x` at lower energy.
A configuration sitting exactly on a tie-line has zero energy above hull and
counts as stable.

---

## 6. Surface slabs and nanoparticles

**Surface Slab** (*Build → Surface Slab…*) is a four-stage builder: Miller
indices `(h k l)`, layer count, vacuum padding, and a preview before commit.

**Nanoparticle Builder** (*Build → Nanoparticle Builder…*) generates
Wulff-type clusters and cuts. Pair it with GCN coloring in the Representation
panel to highlight terraces, steps, edges and vertices.

**Adding vacuum after the fact.** The Structure panel's **Edit Structure…**
dialog has *Add Vacuum Layer…*: choose the thickness and the lattice
directions, and whether to split it evenly (re-centering the slab) or add it
all on one side. It can also mark the padded directions non-periodic, which is
usually the point of adding vacuum.

---

## 7. Effective band structure (band unfolding)

*Simulation → Effective Bands…* implements the **Popescu-Zunger** unfolding
scheme (Phys. Rev. B **85**, 085201 (2012)).

### What it is for

A supercell containing a defect, dopant or alloy disorder folds the primitive
Brillouin zone onto a smaller one, so its band structure is an uninterpretable
tangle of flat folded bands. Unfolding projects each supercell eigenstate back
onto the primitive Bloch basis:

```
P_Km(k) = Σ_g |⟨Km | k + g⟩|²          (spectral weight)
A(k, E) = Σ_m P_Km(k) · δ(E − E_m)      (spectral function)
```

`P = 1` means the state is a pure primitive Bloch state at `k` — the pristine
limit. `P → 0` means it has no primitive character there and is not drawn. The
result is a band structure in the *primitive* zone that can be compared
directly against the pristine host, with defect states appearing as smeared
or broken features.

### Setup

**Stage 1 — Structure & Geometry Link.** Select the active supercell and its
reference pristine primitive cell. The mapping matrix `M` (supercell = M ·
primitive) is deduced from the two cells and shown for editing.

> The deduction reports a **commensurability residual**. Unfolding is only
> defined when the supercell is an integer multiple of the primitive cell; a
> large residual means the two are incommensurate and the result would be
> meaningless. The generated script re-checks this and refuses to run.

**Stage 2 — Calculator & Execution Environment.** GPAW is the reference
backend: unfolding needs the plane-wave expansion coefficients, and GPAW's
PW mode exposes them. Quantum ESPRESSO and SIESTA emit an editable template
with the wavefunction-reading hook left to complete — their ASE calculators do
not expose coefficients without extra tooling.

**Stage 3 — Calculator & Unfolding Settings.** Energy window and bin count,
Gaussian broadening σ, the spectral-weight threshold below which states are
discarded, and the usual XC/convergence parameters.

**Stage 4 — k-Path & Script Review.** The embedded Brillouin-zone builder runs
on the **primitive** lattice — that is the zone the effective band structure
is drawn in — followed by the generated script.

### Reading the result

The Results panel renders `A(k, E)` as an intensity heatmap: `E − E_F` against
the primitive k-path, with color and weight showing spectral intensity.
Controls cover colormap (Viridis, Plasma, Coolwarm, Greys), an intensity
scaling threshold, and the Fermi zero-shift toggle.

Practical notes:

* **σ must exceed the eigenvalue spacing** or the map degenerates into isolated
  dots rather than continuous bands.
* A pristine supercell should unfold back to the primitive band structure with
  `P ≈ 1` everywhere — a useful sanity check before trusting a defect run.
* Sharp, intense features are host-like states; diffuse low-weight smears are
  where the defect has broken the primitive symmetry.

---

## 8. Monitoring runs

**Processes panel (Zone 5 area)** lists every background run with its status,
and offers Load Result, View ASE Script and Delete.

**Results panel (Zone 10)** carries per-run Log, Energy, Temperature, Force,
Pressure and Convex Hull tabs. Each run keeps its own metric history — the
process selector at the top switches between them, so a new run never
overwrites an earlier one's data. Every plot has **Export Data…** for CSV/DAT.

The Temperature and Force axes are locked to start at zero: those quantities
are read against zero, and an auto-scaled axis makes a well-behaved 299–301 K
thermostat look like wild oscillation. Thermostat and barostat setpoints are
drawn as annotated dashed lines.

**Status bar** shows live CPU, GPU, memory and ASE thread counts.

Job scripts write `metrics.json`, `log.json` and `warnings.log` into their
process directory via the shared `calango_log.py` module. Python warnings from
ASE, PyTorch, SciPy and GPAW are routed to `warnings.log` rather than stdout,
which is what keeps the Log tab readable.
