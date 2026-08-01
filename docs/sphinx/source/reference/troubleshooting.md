# Troubleshooting

Symptoms, causes, fixes — ordered roughly by how early in a session they
bite. When something fails during a *run*, read the job's own files first:
`warnings.log`, `log.json`, and the console stderr all live in the job
directory ({doc}`/reference/job_protocol`).

---

## Startup and environment

**Structures will not open; job features are disabled.**
ASE is not importable in the embedded interpreter. Calango still starts —
deliberately, so you can reach Preferences — but shows a warning and runs
in a reduced mode. Diagnose with:

```bash
calango --probe-python
```

It prints the interpreter, the Python version, and the ASE version (or the
import error), and exits 0/1 accordingly. Either install ASE into that
interpreter or point `CALANGO_PYTHON` at an environment that has it
({doc}`/python_environment`).

**The wrong interpreter is being used.**
Resolution order is `CALANGO_PYTHON` → `VIRTUAL_ENV` → the build-time
default. An activated virtualenv silently wins over the default — which is
usually what you want, and occasionally the surprise. `--probe-python`
names the winner.

**A job fails immediately with an import error.**
The job runs in a *different* environment from Calango itself — that is a
feature, but it means the package must exist where the job runs. Check the
{guilabel}`Execution Environment` status line in the wizard before
launching.

**The GPAW backend is greyed out.**
GPAW is not importable in the selected environment. Point the wizard's
environment selector at a conda env where it is installed — and on Apple
Silicon remember GPAW comes from `pip`, not conda-forge
({doc}`/reference/dependencies`).

---

## Structure and display

**The space group reads P1 for an obviously symmetric crystal.**
Numerical noise in relaxed coordinates. Raise {guilabel}`Tolerance` in the
{guilabel}`Structure` panel (or the Symmetry dialog) — a relaxed cell often
needs 0.01 Å or more.

**Bonds are missing or spurious.**
Adjust the covalent cutoff multiplier in the bond editor ({kbd}`Ctrl+B`),
or add and suppress individual bonds by hand. Metallic and ionic systems
rarely match a covalent-radius rule.

**Double and triple bonds do not appear automatically.**
By design — bond orders are assigned manually with the bond-order buttons
in the bond editor.

**The viewport is blank or reports OpenGL errors.**
Calango requires an **OpenGL 3.3 core profile** context (it requests 3.3
core with multisampling at startup). Typical culprits: very old drivers, or
running over remote X11 forwarding without GPU GL. Update drivers, or use a
local session / a remote-desktop solution that provides accelerated or
Mesa-software GL. On macOS the system profile (4.1 core) is always
sufficient.

**The viewport looks soft after enabling depth of field.**
Expected — the effect trades multisample anti-aliasing for the blur pass.
Disable it in the Visual Effects dock before exporting still images.

---

## Calculations

**A MACE run stalls on first use, or fails without network.**
The MACE-MP / MACE-OFF foundation models are **downloaded on first use and
cached under `~/.cache/mace`**. Run one small job with network access (or
copy the cache from another machine, or pin an explicit local checkpoint in
the wizard) before going offline.

**MACE on Apple Silicon: dtype/device errors on `mps`.**
PyTorch's MPS backend does not support float64. Pick `float32` precision
with the `mps` device, or fall back to `cpu` for float64 work — and load a
trained model with the same `default_dtype` it was trained in.

**Adsorption site detection finds no bridge or hollow sites.**
The surface cell is too small to have in-plane neighbours. Build a
supercell of the slab first.

**The remote panel reports a connection failure.**
Test the same host, port, user, and key with `ssh` from a terminal first.
The passphrase field doubles as the key passphrase in SSH-key mode. Once
connected, remote job stdout/stderr land in `calango_job.out` /
`calango_job.err` in the synced job directory.

**Animation export fails.**
Install `pillow` for GIF, or `imageio` + `imageio-ffmpeg` for the video
formats, in Calango's Python environment.

---

## Where things live

| What | Where |
|---|---|
| Settings | `~/.calango/settings.json` (JSON is authoritative; mirrored to QSettings) |
| Job directories | `.calango_tmp/proc_<id>` beside the `.calproj`, or the simulations dir from Preferences |
| Per-job telemetry | `metrics.json`, `log.json`, `warnings.log` in the job directory |
| Remote job output | `calango_job.out`, `calango_job.err` in the job directory |
| MACE model cache | `~/.cache/mace` |
| Packaging guide | `docs/tex/packaging/packaging.pdf` in the repository |

:::{tip}
`warnings.log` is the most underrated file in a job directory: ASE, GPAW,
and PyTorch deprecation and numerical warnings are routed there instead of
stdout, each recorded once per source location — a convergence warning that
would have scrolled past in a console survives there for the post-mortem.
:::
