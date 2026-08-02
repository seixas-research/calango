# Reference

Lookup material: every keyboard shortcut and menu item, every file format
Calango reads and writes, the stdout/JSON protocol that keeps the job panel
live, the Python packages and external binaries each feature needs, and the
answers to the questions that come up most.

These pages describe **Calango v26.8 as built from the current sources** —
where an older guide and this reference disagree (menu locations moved,
shortcuts changed), this reference wins.

---

## The pages

| Page | What it answers |
|---|---|
| {doc}`/reference/shortcuts` | Which key does what — viewport single-key modes, application shortcuts, mouse gestures, and the complete map of all nine menus |
| {doc}`/reference/file_formats` | What Calango opens and saves — structure and trajectory formats, volumetric grids, the `.calproj` project format and its MIME types, and every artifact a job directory can contain with its producer and consumer |
| {doc}`/reference/job_protocol` | How a running script talks to the GUI — the `CALANGO_*` stdout markers, the polled JSON metric files, the job directory layout, and the `calculator.json` provenance chain baseline-inheriting wizards follow |
| {doc}`/reference/dependencies` | Which Python package or external binary each feature needs, the environment variables Calango reads, and the version constraints it is validated against |
| {doc}`/reference/troubleshooting` | Symptoms and fixes — from a missing ASE to a blank viewport, plus where the logs live |

---

## Orientation

A few facts the whole reference leans on:

- **Python resolution order.** Calango embeds CPython and picks its
  interpreter from `CALANGO_PYTHON`, then `VIRTUAL_ENV`, then the
  build-time default. `calango --probe-python` prints the decision and
  whether ASE imports there ({doc}`/python_environment`).
- **Scripts are the contract.** Every calculation is a standalone,
  self-contained Python/ASE script (`run.py`) in a per-job directory. The
  script needs nothing from Calango to run — its logging block is embedded
  — which is what makes remote submission a file upload rather than an
  installation ({doc}`/simulations/jobs`).
- **Jobs communicate two ways.** Live plots and streamed frames arrive as
  `CALANGO_*` markers on stdout; metric history, log events, and Python
  warnings are written as `metrics.json`, `log.json`, and `warnings.log`
  in the job directory, replaced atomically and polled by the Results
  panel. Both halves are documented in {doc}`/reference/job_protocol`.
- **Results are files.** Viewers dispatch on which result files a job
  directory actually holds (`bands.json`, `optics.json`, `raman_ir.json`,
  …). The full producer/consumer table is in
  {doc}`/reference/file_formats`.
- **Baselines are inherited.** Post-processing wizards (Optics, GW,
  Wannier, Born charges, Raman/IR) read a completed run's
  `calculator.json` sidecar to adopt its engine, parameters, and conda
  environment rather than asking again — the provenance format is in
  {doc}`/reference/job_protocol`.

---

## Further reading

- `README.md` in the repository — feature overview and build instructions.
- `docs/tex/packaging/packaging.pdf` — building the macOS and Linux
  installers.
- <https://github.com/seixas-research/calango> — source, issues, releases.
- <https://wiki.fysik.dtu.dk/ase/> — ASE documentation, the reference for
  anything appearing in a generated script.

---

```{toctree}
:maxdepth: 1

shortcuts
file_formats
job_protocol
dependencies
troubleshooting
```
