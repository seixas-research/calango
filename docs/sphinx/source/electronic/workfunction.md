# 2D work function

{menuselection}`Modules --> 2D Materials --> 2D Workfunction…` computes the
work function of a slab or 2D material,

$$
\Phi = E_\text{vac} - E_F,
$$

from the planar-averaged electrostatic potential along the vacuum direction.
The run is a pure post-process of a completed ground state: like
{doc}`/electronic/optics`, it **inherits a finished Single-Point `.gpw`** and
never re-converges — the potential it integrates is the one you already
inspected, evaluated by the same GPAW build that produced it.

---

## What the run does

The generated script loads the baseline (`GPAW(r"….gpw")`), takes the
geometry **from the `.gpw` itself** — a workspace structure edited since the
SCF would put the vacuum bookkeeping on the wrong cell — averages
`get_electrostatic_potential()` over the two in-plane axes to get
$\bar V(z)$, reads $E_F$, and evaluates the vacuum level at **both** cell
edges. Each face gets its own Φ.

| Control | Default | Meaning |
|---|---|---|
| {guilabel}`Baseline SCF (.gpw)` | — | mandatory completed GPAW single point |
| {guilabel}`Vacuum axis` | auto-detected | seeded from the cell, confirm it — the wrong axis reads the "vacuum level" from inside the material |
| {guilabel}`Plateau fraction` | 0.15 | outermost fraction of the vacuum gap used for the flatness check |

:::{admonition} Two faces, one honest caveat
:class: caution

An asymmetric slab has **two different work functions** — but only when the
baseline carried a dipole correction. Without one, the periodic boundary
forces both vacuum tails to a shared artificial average: equal values on an
asymmetric slab mean the correction is *missing*, not that the slab is
symmetric. The wizard note and the viewer both say so.
:::

The script reports the plateau flatness (the worst $|d\bar V/dz|$ over the
outer window of either face) and warns beyond 5 meV/Å — a sloped "plateau"
means the vacuum region is too thin to define $E_\text{vac}$, and the number
it yields would be an artifact of the cell height. A vacuum gap under 4 Å
draws a runtime warning for the same reason.

---

## Output and viewer

The job writes `workfunction.json`:

```text
{"vacuum_axis": 2, "z_A": [...], "v_planar_eV": [...],
 "efermi_eV": f, "vacuum_level_low_eV": f, "vacuum_level_high_eV": f,
 "workfunction_low_eV": f, "workfunction_high_eV": f,
 "plateau_flatness_eV_per_A": f}
```

The viewer opens automatically when the file appears in a finished job
directory: a headline Φ (both faces when they differ by more than 0.02 eV),
the $\bar V(z)$ profile with dashed reference lines at $E_F$ and the vacuum
level(s), the flatness readout, and a red warning when the plateau check
fails. {guilabel}`Export CSV…` writes the `z_A,v_planar_eV` profile;
{guilabel}`Export Image…` renders at 3× for print.

% TODO screenshot: 2D Workfunction viewer with the planar-averaged potential, E_F and vacuum-level reference lines and the Φ headline
```{figure} /_static/img/elec_workfunction_viewer.png
:alt: Planar-averaged electrostatic potential with Fermi and vacuum reference lines
:width: 92%
:figclass: screenshot

The work-function viewer — $\bar V(z)$ with the Fermi level and vacuum
plateau marked; Φ is their difference.
```
