# Building structures

Everything under the {menuselection}`Build` menu produces a structure — either from nothing (a bulk crystal, a nanotube, a box of water) or by consuming the structure in the current tab (a slab from a bulk, an interface on a slab, a dislocation in a crystal). **Every builder opens its result as a new workspace tab and leaves the parent tab untouched**, so the input survives as a reference.

The menu is ordered by workflow: structure *sources* first ({menuselection}`Build --> From Database`), then the builders that grow a structure from an existing one, roughly by increasing complexity, with the reciprocal-space {menuselection}`Build --> Brillouin Zone Builder` last behind a separator.

One entry here is not a wizard at all: {doc}`molecular_design` is a 2D chemical sketcher — you draw a molecule the way it is drawn on paper and send it to a new tab as a 3D structure. It opens from the first button on the viewport toolbar rather than from the {menuselection}`Build` menu, because it is a window you work in rather than a dialog you fill in, but what it produces is a structure like any other builder's.

Two families coexist here. The database fetchers, `ase.build`-backed nanomaterials and the adsorbate placement run through the embedded Python/ASE bridge and need a working Python environment. The dislocation, solid-interface, polymer, water/ice, graphene-oxide and SQS builders are **native C++** — they run with or without Python.

Two entries here open from {menuselection}`Modules` rather than {menuselection}`Build`, because their physics belongs to a material class rather than to the general building toolkit: {doc}`2D Ripples <ripples>` is under {menuselection}`Modules --> 2D Materials`, and the Graphene Oxide builder under {menuselection}`Modules --> Graphene Oxide`. Both produce a structure like any other builder, so both are documented here.

A few tools have moved since earlier releases: supercell creation now lives on the {guilabel}`Structure` panel's action row (a 3×3 transformation matrix, not a menu entry), the SQS generator sits under {menuselection}`Modules --> Alloys`, structure perturbation is {menuselection}`Simulation --> Random Noise Setup`, and the Graphene Oxide builder — now generation only, one of four modules in its own family — is under {menuselection}`Modules --> Graphene Oxide`. All are documented in this section regardless of which menu hosts them.

---

```{toctree}
:maxdepth: 1

database
molecular_design
slabs
nanomaterials
ripples
interfaces
dislocations
disorder
molecules
adsorption
```
