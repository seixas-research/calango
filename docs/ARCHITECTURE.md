# Calango Architecture

## Layering (strict MVC separation)

```
            ┌──────────────────────────────────────────────┐
            │                  gui/  (Qt Widgets)          │
            │   MainWindow = Controller                    │
            │   ViewportWidget / docks / dialogs = Views   │
            └──────┬────────────────┬─────────────┬────────┘
                   │ observes       │ uses        │ uses
            ┌──────▼──────┐  ┌──────▼───────┐  ┌──▼───────────┐
            │   render/   │  │python_bridge/│  │    jobs/     │
            │ OpenGL View │  │ embedded ASE │  │   QProcess   │
            └──────┬──────┘  └──────┬───────┘  └──────────────┘
                   │ reads          │ converts
            ┌──────▼────────────────▼───────┐
            │            core/              │
            │  Model: Structure, Atom,      │
            │  UnitCell, CalculatorConfig,  │
            │  AseScriptGenerator           │
            └───────────────────────────────┘
```

Rules that keep the model separate from rendering:

- **`core/` depends on nothing** — no Qt, no OpenGL, no Python. It is the
  single source of truth (`Structure` = atoms + cell) plus pure functions
  on it (bond perception, formula, script generation). Double-precision
  `Vec3` is used because `QVector3D` is float-only.
- **`render/` is a pure View.** `StructureRenderer` reads a
  `const core::Structure*` once in `setStructure()` and derives GPU
  buffers; it stores no model reference and never mutates the model.
- **`gui/MainWindow` is the Controller.** All mutations flow through it:
  open/save/supercell go through `AseBridge`, then
  `notifyStructureChanged()` pushes the new model state into the views
  (viewport re-uploads instances, info panel re-reads counts).
- **`python_bridge/` is a stateless converter** at the model boundary:
  `ase.Atoms ⇄ core::Structure`. Python objects never leak past it.

## Embedded Python vs. subprocess Python

Two deliberate paths:

1. **Embedded interpreter** (`PythonEngine`, pybind11 `scoped_interpreter`)
   for *fast, interactive* operations: file I/O, supercells, future
   builder tools. Runs on the GUI thread; no GIL juggling needed (v0.1
   policy — if background in-process Python is ever added, release the
   GIL on the main thread and acquire it in workers).
2. **Subprocess** (`JobRunner`) for *simulations*: the generated script is
   executed by the same interpreter binary (`sys.executable`) as
   `python run.py` in a per-job directory. This keeps the GUI responsive,
   isolates solver crashes, and means every job is reproducible from its
   directory alone (`structure.extxyz` + `run.py`).

The generated script is the contract: plain ASE Python a user can edit or
run on a cluster. It reports back over stdout with greppable markers
(`CALANGO_PROGRESS step total`, `CALANGO_RESULT k=v`, `CALANGO_DONE`)
which `JobRunner` parses into Qt signals.

## Qt specifics

- Built with `QT_NO_KEYWORDS` (`Q_SIGNALS`/`Q_SLOTS`/`Q_EMIT`) because
  CPython headers used by pybind11 collide with Qt's lowercase `slots`
  macro.
- OpenGL 3.3 core profile requested before `QApplication` is created;
  macOS promotes this to 4.1 core. Rendering uses instanced draws — one
  call for all atoms, one for all bond half-cylinders — so shader
  attribute locations 2–5 carry a per-instance `mat4`, location 6 a color.

## Extension points

- **New calculator**: add an enum value in `core/CalculatorConfig.hpp`, a
  branch in `AseScriptGenerator::emitCalculator`, an item in
  `CalculatorDialog` — nothing else changes.
- **New representation** (polyhedra, vdW spheres): a new instanced mesh in
  `StructureRenderer`; the model is untouched.
- **New file format**: nothing to do — ASE handles it.
- **Headless/batch mode**: `core/` + `python_bridge/` + `jobs/` have no
  GUI dependency by design (only `jobs/` uses QtCore).
