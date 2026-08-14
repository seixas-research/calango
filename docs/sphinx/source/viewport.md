# The 3D viewport

The viewport is an instanced OpenGL 3.3 canvas — spheres, bond cylinders, and arrows are drawn in single instanced calls, so structures with tens of thousands of atoms rotate interactively. A dedicated toolbar sits between the tab bar and the canvas and carries everything camera-related: interaction modes, projection, alignment, fixed-angle rotation, and display toggles.

% TODO screenshot: the full viewport toolbar, annotated with the mode cluster (R/T/S/I), measurement pair (D/A), camera group (F/O), XY/XZ/YZ alignment, and the angle-step spinner with the six axis buttons
```{figure} /_static/img/viewport_toolbar.png
:alt: The viewport toolbar with interaction-mode, camera, alignment, and rotation-step controls
:width: 92%
:figclass: screenshot

The viewport toolbar. Every camera command lives here — there is no View → Alignment submenu.
```

---

## Interaction modes

Six exclusive modes, switched by single-letter shortcuts (text fields still receive these keys normally while you type):

| Key | Mode | Drag behavior |
|---|---|---|
| {kbd}`R` | Rotate *(startup default)* | Arcball orbit around the structure |
| {kbd}`T` | Translate | Pans the scene |
| {kbd}`S` | Select | Rubber-band box selection |
| {kbd}`I` | Insert | Click adds an atom; drag atom-to-atom bonds them |
| {kbd}`D` | Measure distance | Click two atoms — separation in Å |
| {kbd}`A` | Measure angle | Click three atoms (vertex second) — angle in degrees |

In every mode: middle-drag (or {kbd}`Shift`+left-drag) pans, the wheel zooms, and a double-click frames the structure. Two {kbd}`Shift` specializations override the pan: in *Rotate* mode, {kbd}`Shift`+drag rolls the structure about the view direction at 0.4°/px; in *Translate* mode, {kbd}`Shift`+drag on an atom grabs that single atom and moves it in the viewer-facing plane at its own depth — one undo entry per drag. There is no trackpad pinch gesture; two-finger scroll zooms as an ordinary wheel event.

### Camera

- {kbd}`O` toggles orthographic/perspective projection. **The camera starts orthographic** — crystallography is conventionally drawn in parallel projection. The perspective button carries a field-of-view popup (5–120°, default 40°).
- {kbd}`F` resets the camera: it restores the default point of view saved in `~/.calango/settings.json`, or, when none has been set, centers and frames the structure. The neighboring {guilabel}`Set point-of-view...` button records the current view as that default.
- The {guilabel}`XY` / {guilabel}`XZ` / {guilabel}`YZ` buttons align the view with the corresponding Cartesian plane. They are toolbar-only — no keyboard shortcuts — and, like {kbd}`F`, they clear any accumulated fixed-angle scene rotation.

### Fixed-angle rotations

An editable angle step (1.0–180.0°, default **15.0°**, step 5.0) feeds six buttons — {guilabel}`X+` {guilabel}`X−` {guilabel}`Y+` {guilabel}`Y−` {guilabel}`Z+` {guilabel}`Z−` — that rotate the scene about the world axes. Each click animates over 200 ms and rotations compose exactly, so rapid clicks accumulate to precise multiples: three clicks of {guilabel}`Z+` at 15° is exactly 45°.

---

## Selecting atoms

Picking is a true ray–sphere intersection: the click is unprojected to a world ray and tested against every atom *at the radius it is drawn at*, so what you hit is what you see — in any representation mode. Hidden hydrogens are unpickable.

- **Click** an atom to select it (any mode). A plain click on empty space clears the selection.
- **{kbd}`Ctrl`+click** ({kbd}`Cmd` on macOS) toggles an atom in or out of the selection; on empty space it keeps the selection.
- **Rubber band** (Select mode): drag a box — atoms whose projected centers fall inside are selected. Hold {kbd}`Ctrl` to add to the existing selection instead of replacing it.
- **{kbd}`Delete` / {kbd}`Backspace`** removes the selected atoms (Backspace works whenever the viewport has focus; Delete is also bound window-wide via {menuselection}`Edit --> Delete Selected Atoms`).

A single selected atom shows an info overlay above the axes triad: symbol, 1-based index (`#N`), Cartesian coordinates to 3 decimals, and fractional coordinates to 4 decimals when a cell is defined. Multiple selections show a count plus a per-element tally.

---

## Measurements

Distance mode picks **2 atoms**; angle mode picks **3 atoms, with the second click as the vertex**. Picked atoms get accent-colored circles joined by dashed lines, with the value in a floating pill — distances as `Å` to 3 decimals, angles as `°` to 2 decimals; the status bar repeats the reading with the atoms named `Symbol(index)`. Clicking the same atom twice is ignored; clicking empty space resets the running measurement. A completed overlay stays on screen until the next click starts a fresh one, and orbiting still works mid-measurement — dragging between clicks rotates the structure. Switching modes or replacing the structure clears the overlay.

:::{note}
Measurement status-bar text uses 0-based atom indices, while the viewport `#N` labels are 1-based — atom `#5` in the overlay is `Symbol(4)` in a measurement readout.
:::

% TODO screenshot: an angle measurement on a water molecule — three highlighted atoms, dashed connecting lines, and the floating value pill reading approximately 104.5 degrees
```{figure} /_static/img/viewport_measurement.png
:alt: Angle measurement overlay showing three picked atoms with dashed lines and a floating angle readout
:width: 92%
:figclass: screenshot

An angle measurement — the second-clicked atom is the vertex; the overlay persists until the next click.
```

---

## Inserting atoms and bonds

Insert mode ({kbd}`I`) works with the *active element*, shown on the toolbar button right of the mode cluster (default: carbon). Click it to pick any element from the periodic table.

- **Click empty space** — the click is unprojected onto the plane through the camera target, perpendicular to the view direction: *what you click is where the atom appears, at the depth the camera orbits around*.
- **Drag from atom A to atom B** — creates a manual bond override between them.
- **{kbd}`Shift`+click an atom** — substitutes it with the active element.

Every insertion, bond, and substitution is a single undo step. The neighboring {guilabel}`Complete with hydrogens` button performs valence-based hydrogen completion, also undoable.

---

## Visual effects

Visual effects live in the **Visual Effects dock** (zone 9), a six-tab panel running from what lights the scene, through the scene itself, to the passes that post-process the finished image: {guilabel}`Light` (covered in {doc}`/representation`), {guilabel}`Shadow`, {guilabel}`Floor`, {guilabel}`Fog`, {guilabel}`Blur`, and {guilabel}`SSAO`. All effects default to off.

### Distance fog

Fades geometry into the background with camera distance — the fog color tracks the viewport background automatically.

| Control | Range | Default |
|---|---|---|
| {guilabel}`Mode` | Linear (start → end) / Exponential (density) | Linear |
| {guilabel}`Start distance` | 0.0–500.0 Å | 15.0 Å |
| {guilabel}`End distance` | 1.0–1000.0 Å (clamped to ≥ start + 1) | 80.0 Å |
| {guilabel}`Density` | 0.001–0.5 | 0.300 |

### Depth of field

A circle-of-confusion blur: sharpness falls off with distance from the focal plane, using a 12-tap Poisson disc whose taps are weighted by their own blur radius so in-focus geometry never bleeds.

| Control | Range | Default |
|---|---|---|
| {guilabel}`Blur strength` | 1–20 px | 6 px |
| {guilabel}`Focus range` | 1.0–200.0 Å | 12.0 Å |
| {guilabel}`Focus offset` | −200.0–200.0 Å | 0.0 Å |

The focal plane sits at the camera target plus the offset; the focus range is the depth band that stays sharp.

### Screen-space ambient occlusion

SSAO darkens crevices — bond junctions, the gaps between close-packed spheres — using a hemisphere sampling kernel over reconstructed view-space depth, followed by a depth-aware bilateral blur so occlusion never bleeds across silhouettes.

| Control | Range | Default |
|---|---|---|
| {guilabel}`SSAO radius` | 0.1–10.0 Å | 1.2 Å |
| {guilabel}`SSAO intensity` | 0–100 % | 70 % |
| {guilabel}`Kernel samples` | 4–64 | 32 |
| {guilabel}`Noise texture scale` | 0.25–4.0 | 1.0 |

A radius around one atomic radius reads best. There is no bias control — the depth bias is derived as 2 % of the radius, so small-molecule scenes don't develop self-occlusion acne. Sample count is a live quality/speed dial; the kernel uses a fixed random seed, so renders are reproducible run to run.

:::{warning}
**Depth of field and SSAO trade away MSAA.** Either effect reroutes rendering into a non-multisampled off-screen G-buffer, so edges lose multisample antialiasing while it is enabled. With both active, ambient occlusion is composited first and depth of field blurs the composited image — sharp occlusion never survives on top of blurred geometry.
:::

The {guilabel}`Shadow` tab adds PCF directional shadows following the primary light: {guilabel}`Intensity` 0.0–1.0 (step 0.05) and {guilabel}`Softness / blur radius` 0–6 shadow-map texels — 2–3 suits most structures.

### The ground plane

The {guilabel}`Floor` tab — next to {guilabel}`Shadow`, because that is the relationship the two have — carries {guilabel}`Ground plane (floor)`: a large plane just under the structure, so an isolated molecule reads as an object resting in a space rather than one floating in a void. It is a shadow **receiver** — the atoms and bonds cast onto it, and it casts nothing itself. The group's own checkbox is the on/off switch; there is no toolbar button for it.

| Control | Values |
|---|---|
| {guilabel}`Height offset` | −100 to +100 Å, default 0 — relative to the automatic level |
| {guilabel}`Color` | White by default — a figure's page is white, so the plane disappears into it and leaves only the shadow |
| {guilabel}`Material` | Standard, Shiny, Matte (default), Glassy — the same four finishes the Representation panel offers |
| {guilabel}`Opacity` | 0.05–1.0, default 1.0 |
| {guilabel}`Plane` | {guilabel}`xy` (default), {guilabel}`xz`, {guilabel}`yz`, {guilabel}`Custom` |
| {guilabel}`Normal (x, y, z)` | Any direction; length is irrelevant |

### Orientation

{guilabel}`Plane` and {guilabel}`Normal (x, y, z)` are two views of **one** value — the plane's normal. Each preset names the two axes the plane is spanned by, and its normal is the remaining one: {guilabel}`xy` → +z, {guilabel}`xz` → +y, {guilabel}`yz` → +x. Picking a preset fills the normal fields; typing a normal that is not an axis switches the dropdown to {guilabel}`Custom`. Only the normal is stored, so the two cannot disagree and a project file carries three numbers rather than a preset name.

The length of the vector does not matter — it is normalized before use, so `(0, 0, 2)` and `(0, 0, 1)` are the same plane. A **zero** vector defines no plane: the previous orientation is kept and the field is flagged in red rather than the keystroke being refused, so you can still type one component at a time through zero.

Everything else follows the orientation. The plane is placed on the **negative** side of the structure along the normal, the height offset moves it **along the normal**, and the auto-fit extent is measured in the plane's own two axes — so a vertical plane fits the structure's vertical extent rather than its footprint. Reversing a normal (say `(0, 0, -1)`) puts the plane *over* the structure instead of under it; that is a ceiling, a real choice rather than an error, so it is left available and simply reads as {guilabel}`Custom`.

Shadows follow too, in the live viewport and in every export: the shadow lookup works off the fragment's actual normal, so a vertical plane used as a **backdrop** catches the structure's shadow exactly as a horizontal one does — re-aim the primary light along the plane's normal to throw a shadow onto it.

### Placement

With the default {guilabel}`xy` plane the floor is perpendicular to **c** (world +z), the axis Calango's default view has pointing up. It lands one fixed 0.25 Å clearance under the lowest drawn point — the bottom of the lowest atom's *sphere*, or of the unit cell when that is shown, so nothing ever intersects it. It re-fits itself whenever the structure changes, which is why the manual control is an **offset** rather than an absolute height: the plane keeps following an edited structure or a scrubbed trajectory while your adjustment survives. Its lateral extent scales with the structure's own footprint and fades out toward the edges, so at any normal camera distance it reads as ground rather than as a tile; with distance fog on, it recedes into the fog colour instead.

Hidden atoms do not push it down (a hidden hydrogen would otherwise open a gap under the molecule with nothing in it), and seen from its back it is not drawn at all, so orbiting past it never hides the structure.

The floor is **display only**. It is never part of the structure, is not picked by clicks, and never appears in an exported POSCAR, CIF or XYZ. It *does* appear in every render of the scene — off-screen and publication renders, turntable and trajectory animations, POV-Ray and Tachyon scene files, and the Alembic cache (where it rides the same {guilabel}`Include unit cell` switch as the rest of the scene furniture). With a transparent background it still renders, with the shadow on it; turn it off if you want the molecule alone on transparency.

{guilabel}`Material` selects a Blinn-Phong finish, not a mirror — none of the four reflects the structure, which would need a reflection pass the viewport does not have.

% TODO screenshot: the same nanoparticle rendered twice side by side, SSAO off and SSAO on, with the SSAO tab of the Visual Effects dock visible
```{figure} /_static/img/viewport_ssao.png
:alt: Side-by-side comparison of a nanoparticle without and with screen-space ambient occlusion
:width: 92%
:figclass: screenshot

SSAO at the default 1.2 Å radius and 70 % intensity — crevices between close-packed atoms gain contact shadows.
```

---

## Atom labels

Three independent per-atom overlays combine into one pill label per atom (e.g. `Fe #12 6.75`):

- {guilabel}`Show element symbols` — viewport toolbar toggle; overlays each atom's chemical symbol.
- {guilabel}`Show atomic indices` — viewport toolbar toggle; 1-based, rendered `#N` so it never reads as an element symbol.
- {guilabel}`Show CN / GCN values` — a checkable button on the {guilabel}`Color by` row of the Representation panel; prints each atom's value of the property its cast is colored by (CN as an integer, GCN and custom properties to 2 decimals). Unavailable in Element color mode.

Labels are capped at 600 atoms for legibility, and hidden hydrogens are never labelled. The same toolbar group holds {guilabel}`Draw hydrogen atoms` and {guilabel}`Show bonds smoothly` — see {doc}`/representation` for the latter.
