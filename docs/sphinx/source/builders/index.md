# Building structures

Everything under the {menuselection}`Build` menu produces a structure — either from nothing (a bulk crystal, a nanotube, a box of water) or by consuming the structure in the current tab (a slab from a bulk, an interface on a slab, a dislocation in a crystal). **Every builder opens its result as a new workspace tab and leaves the parent tab untouched**, so the input survives as a reference.

The menu is ordered by workflow: structure *sources* first ({menuselection}`Build --> From Database`), then the builders that grow a structure from an existing one, roughly by increasing complexity, with the reciprocal-space {menuselection}`Build --> Brillouin Zone Builder` last behind a separator.

Two families coexist here. The database fetchers, `ase.build`-backed nanomaterials and the adsorbate placement run through the embedded Python/ASE bridge and need a working Python environment. The dislocation, solid-interface, polymer, water/ice, graphene-oxide and SQS builders are **native C++** — they run with or without Python.

A few tools have moved since earlier releases: supercell creation now lives on the {guilabel}`Structure` panel's action row (a 3×3 transformation matrix, not a menu entry), the SQS generator sits under {menuselection}`Modules --> Alloys`, structure perturbation is {menuselection}`Simulation --> Random Noise Setup`, and the Graphene Oxide builder — now generation only, one of four modules in its own family — is under {menuselection}`Modules --> Graphene Oxide`. All are documented in this section regardless of which menu hosts them.

---

```{toctree}
:maxdepth: 1

database
slabs
nanomaterials
interfaces
dislocations
disorder
molecules
adsorption
```
