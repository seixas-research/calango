# Tutorials

The tutorials are complete, checkable walkthroughs: each one carries a real material from an empty workspace to a finished set of results, and every stage ends with a number you can compare against the literature — so when something goes wrong, we find out at that stage instead of three stages later.

They assume the basics from the {doc}`/quickstart` (opening structures, moving the camera, launching a wizard) and a working Python setup ({doc}`/python_environment`). The DFT stages need **GPAW** in a Conda environment that Calango can bind to; without it, every workflow still runs with the EMT calculator, minus the electronic-structure stages.

```{toctree}
:maxdepth: 1

silicon
```

| Tutorial | System | What it exercises | Checkable endpoints |
|---|---|---|---|
| {doc}`/tutorials/silicon` | Crystalline Si (diamond) | Build → relax → single-point baseline → phonons → bands + PDOS | Raman line at 520 cm⁻¹, indirect PBE gap ≈ 0.6 eV, Dulong–Petit limit |
