# Analysis

The {guilabel}`Analysis` menu is where a structure on screen is turned into
numbers: distribution functions and simulated diffraction, coordination and
chemical-order statistics, symmetry and magnetic-symmetry classification,
transport coefficients from an MD trajectory, and the volumetric-field and
reciprocal-space tooling the rest of the application feeds.

Most tools follow one pattern — **they compute the moment they open, run on a
worker thread so the interface stays responsive, and export their data as CSV
or whitespace-separated `.dat`**, the format chosen by the extension you type
in the save dialog. The exceptions announce themselves: XRD waits for
{guilabel}`Simulate`, and the Magnetic Space Group dialog recomputes on
{guilabel}`Determine` because its inputs (the moment table) are editable.

% TODO screenshot: the Analysis menu fully expanded over the main window
```{figure} /_static/img/analysis_menu.png
:alt: The Analysis menu with its twelve entries, from Symmetry to Adsorption & Catalysis
:width: 92%
:figclass: screenshot

The Analysis menu — symmetry tools at the top, distribution and order
statistics in the middle, charge- and density-based analyses below.
```

---

## What lives where

The Analysis menu holds twelve entries; a few closely related tools live on
neighbouring menus, and this section documents those too:

| Tool | Opens from | Page |
|---|---|---|
| Symmetry, Raman & IR Activity | {menuselection}`Analysis --> Symmetry, Raman && IR Activity…` | {doc}`/analysis/symmetry` |
| Magnetic Space Group | {menuselection}`Analysis --> Magnetic Space Group…` | {doc}`/analysis/symmetry` |
| Structure Factor $S(q)$ | {menuselection}`Analysis --> Structure Factor S(q)…` | {doc}`/analysis/structural` |
| X-Ray Diffraction | {menuselection}`Analysis --> X-Ray Diffraction (XRD)…` | {doc}`/analysis/structural` |
| Radial Distribution Function | {menuselection}`Analysis --> Radial Distribution Function…` | {doc}`/analysis/structural` |
| Bond Length / Angle Distributions | {menuselection}`Analysis --> Bond Length / Angle Distributions…` | {doc}`/analysis/structural` |
| Coordination Numbers (CN / GCN) | {menuselection}`Analysis --> Coordination Numbers (CN / GCN)…` | {doc}`/analysis/order` |
| Local Entropy | {menuselection}`Analysis --> Local Entropy Analysis…` | {doc}`/analysis/order` |
| Partial Charge Analysis | {menuselection}`Analysis --> Partial Charge Analysis…` | {doc}`/electronic/charges` |
| Velocity Autocorrelation (VACF) | {menuselection}`Analysis --> Velocity Autocorrelation Function (VACF)…` | {doc}`/analysis/structural` |
| Charge Density Difference (CDD) | {menuselection}`Analysis --> Charge Density Difference (CDD)…` | {doc}`/electronic/charges` |
| Adsorption & Catalysis | {menuselection}`Analysis --> Adsorption && Catalysis…` | {doc}`/builders/index` |
| Warren–Cowley Analysis | {menuselection}`Modules --> Alloys --> Warren-Cowley Analysis…` | {doc}`/analysis/order` |
| Volumetric Data dock | {menuselection}`View --> Volumetric Data` | {doc}`/analysis/volumetric` |
| Brillouin Zone Builder | {menuselection}`Build --> Brillouin Zone Builder…` | {doc}`/analysis/reciprocal` |

:::{note}
Three historical relocations, in case you are looking for a tool where an
older guide put it: **Warren–Cowley** moved to {menuselection}`Modules -->
Alloys`, joining the rest of the alloy toolchain; the **Brillouin Zone
Builder** moved to the {guilabel}`Build` menu; and **Volumetric Data** is no
longer a dialog on this menu at all — it is a permanent dock that renders
into the main 3D viewport, toggled from the {guilabel}`View` menu.
:::

---

## Geometry in, physics out

The pages in this section split by what kind of question is being asked:

- {doc}`/analysis/structural` — *where are the atoms?* Pair and angle
  distributions, the static structure factor, simulated powder XRD, and the
  velocity autocorrelation function with its Green–Kubo transport
  coefficients.
- {doc}`/analysis/order` — *how are the sites arranged?* Coordination and
  generalized coordination numbers, Warren–Cowley short-range-order
  parameters, and the Piaggi–Parrinello local-entropy fingerprint.
- {doc}`/analysis/symmetry` — *what group does the structure realize?*
  Space-group and factor-group analysis with Raman/IR activity, and the
  magnetic space group determined from coordinates plus magnetic moments.
- {doc}`/analysis/volumetric` — *what does a 3D scalar field look like?*
  Isosurfaces, colour slices, potential maps, and direct volume rendering of
  densities, potentials, orbitals, and ELF.
- {doc}`/analysis/reciprocal` — *which k-path?* The interactive Brillouin
  zone with click-to-build paths and exporters for five external codes.

Every geometric tool here handles periodic boundary conditions by
**enumerating explicit periodic images rather than relying on the
minimum-image convention**, so results stay correct beyond half the cell
length and in triclinic cells — the two places where naive implementations
quietly produce artefacts.

Tools that need the embedded Python stack say so on their own pages: the
symmetry family requires `spglib`, and the charge analyses require a
completed calculation ({doc}`/electronic/charges`). Everything else is pure
C++ and works on any structure you can open.

---

```{toctree}
:maxdepth: 1

structural
order
symmetry
volumetric
reciprocal
```
