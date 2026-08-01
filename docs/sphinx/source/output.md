# Publication output

Everything on screen can leave the program at publication quality: stills at
any resolution, animations in seven containers, a baked geometry cache for
3D packages, and ray-traced scenes that match the live viewport exactly.
Numeric data takes its own paths — see the table at the end and
{doc}`/data_io`.

---

## Still images

{menuselection}`File --> Import / Export --> Export Image` ({kbd}`Ctrl+E`)
renders the current view off-screen at any resolution, independent of the
window size, with **8× multisample anti-aliasing**.

- {guilabel}`Resolution preset` — 720p (1280 × 720), 1080p (1920 × 1080),
  4K UHD (3840 × 2160), or custom width and height, **64–8192 px** each. The
  default is twice the viewport size, for a crisp result at 1:1 zoom.
- {guilabel}`Background` — {guilabel}`Transparent (PNG only)`,
  {guilabel}`Solid white`, or {guilabel}`Viewport color`.
- Format — PNG or JPEG, chosen by the file extension. **JPEG has no alpha
  channel**: a transparent export saved as `.jpg` is composited over white
  rather than written broken.

:::{admonition} Reproducible figures
:class: tip
Transparent PNG is the right choice for figures that will sit on a colored
background in a paper or slide. Combine it with an orthographic, axis-aligned
view ({kbd}`O`, then an alignment button — see {doc}`/viewport`) for a clean,
reproducible result.
:::

---

## Animations

{menuselection}`File --> Import / Export --> Export Animation` writes a
turntable rotation, a trajectory playback, or a film (below).

| Control | Values |
|---|---|
| {guilabel}`Source` | {guilabel}`Turntable rotation (360°)`; {guilabel}`Trajectory frames (N)` when the workspace holds one; {guilabel}`Film production (N shots, X s)` when a film exists |
| {guilabel}`Rotation frames` | 8–360, default 72 — turntable only |
| {guilabel}`Resolution preset` | Up to 4K UHD; width/height 64–4096 px, default 640 × 480 |
| {guilabel}`Frames per second` | 1–60, default 24 (a film export adopts the film's own rate) |
| {guilabel}`Format` | MP4 (H.264), MP4 (H.265/HEVC), QuickTime `.mov` (H.264), Matroska `.mkv` (H.264), WebM (VP9), AVI (MPEG-4), Animated GIF |
| {guilabel}`Background` | Solid white, viewport color, custom color, or {guilabel}`Transparent (GIF only)` |

The format is picked in the dialog rather than inferred from the typed
extension — `.mp4` alone does not say H.264 or HEVC, and a silently chosen
codec is what makes an export unplayable somewhere else. H.264 MP4 leads the
list because it is the one file that plays everywhere; HEVC and VP9 encode
the same picture smaller at the cost of older players. **Animated GIF is the
only format that carries transparency — and the only one limited to 256
colors per frame.** Asking for a transparent video falls back to solid white
with a notice. Frame dimensions are kept even (H.264 `yuv420p` requires it),
and a trajectory export restores the frame you were viewing afterwards.

:::{warning}
GIF export needs `pillow`; video export needs `imageio` and `imageio-ffmpeg`
in the embedded Python environment. See {doc}`/installation`.
:::

---

## Alembic geometry caches

{menuselection}`File --> Import / Export --> Export to Alembic` writes a
baked `.abc` cache: atoms, bonds and cell as polygon meshes, **one object
per element**, so materials can be assigned per species after import —
Blender, Houdini, Maya, Cinema 4D, Unreal. The export uses the viewport's
own style — same radii, element colors and bond perception — not a second
set of defaults that happens to look similar.

- {guilabel}`Source` — current structure, or the whole trajectory as an
  animated cache (default when one exists).
- {guilabel}`Frames per second` — 1–120, default 24.
- {guilabel}`Sphere detail` — Low (12 sides, large systems), Medium (24,
  the default), High (40, close-ups). Tessellation is the one real
  trade-off: a 5 000-atom cell at high detail is a multi-hundred-megabyte
  file.
- {guilabel}`Include bonds`, {guilabel}`Include unit cell wireframe` —
  checkboxes; the cell option is available only when a cell is defined.

---

## Ray-traced rendering

{menuselection}`File --> Import / Export --> Ray-Traced Render` exports the
*active viewport scene* — same atom radii, colors, multi-bond layout, cell
wireframe, camera pose and lights as the OpenGL view — to an external ray
tracer, for true shadows and reflections. **The ray-traced image matches
what is on screen**; compose in the viewport, then render.

- {guilabel}`Engine` — {guilabel}`POV-Ray` (`.pov` scenes) or
  {guilabel}`Tachyon` (`.dat`, VMD-style syntax).
- {guilabel}`Width (px)`, {guilabel}`Height (px)` — 64–16384, default
  **1920 × 1440**.
- {guilabel}`Background` — {guilabel}`Solid white` or {guilabel}`Viewport
  color`.
- {guilabel}`Renderer binary` — path to the executable; the bare names
  `povray` and `tachyon` resolve on `PATH`, and **the path is remembered per
  engine** across sessions.

Three actions:

- {guilabel}`Save Scene File…` writes just the scene for rendering
  elsewhere, or for hand-editing first.
- {guilabel}`Render…` writes the scene next to your chosen PNG and invokes
  the renderer, streaming its output into the dialog's log pane so progress
  and failures are visible.
- {guilabel}`Render Trajectory…` ray-traces **every trajectory frame** and
  stitches them into an MP4 or GIF at the dialog's {guilabel}`Animation FPS`
  (1–60, default 24) — the highest-quality animation path, at the cost of
  one full render per frame.

:::{warning}
POV-Ray and Tachyon are installed separately — neither ships with Calango.
If the binary cannot be started, the log says so explicitly rather than
failing silently.
:::

% TODO screenshot: Ray-Traced Render dialog mid-render, log pane streaming POV-Ray output next to the finished PNG
```{figure} /_static/img/output_raytrace.png
:alt: The Ray-Traced Render dialog with the renderer log streaming
:width: 92%
:figclass: screenshot

The Ray-Traced Render dialog — the scene is written next to the output PNG and the renderer's own log streams into the pane.
```

---

## The Film system

A **film** turns saved camera positions into a scripted camera move: shots,
transitions and fades, previewed live and exported like any other animation.
Films are per document — each workspace tab carries its own. They are *not*
saved in the project file: a film lives for the session, so export the
animation (or keep the session open) before closing.

{guilabel}`Film mode` on the View toolbar swaps the trajectory timeline
below the viewport for a **film timeline**, graduated in seconds rather than
frames — a film is authored in seconds, because its duration is the number
that has to fit a slide or a talk; the frame count is derived from duration
and rate. While film mode is on, the film timeline also drives the
trajectory, so only one scrubber is in charge at a time.

{guilabel}`Film production…` opens the authoring dialog — modeless and
live: every edit republishes the script, so the viewport and timeline update
as the film is built. The unit of authoring is the **shot** — a saved
point-of-view plus what the scene looks like while the camera is there:

- Shots are added from the saved points-of-view list or from the current
  view. The camera state is *copied* into the shot, so renaming or deleting
  a saved view cannot silently change a film.
- Each shot has a **transition to the next** and its own duration
  (0.05–3600 s; unset segments fall back to an even split of the film).
- Per-shot **cast opacities** fade a cast down and back up across shots —
  the usual way to reveal a molecule inside a substrate without deleting the
  substrate ({doc}`/representation`).
- Per-shot **overlay sets**: a shot can name which "Additional Overlays"
  are visible while it is on screen. It names ids, not copies — editing an
  overlay label updates every shot that shows it.

Four transitions:

| Transition | Effect |
|---|---|
| {guilabel}`Interpolation` | Fly — the camera eases between the shots; the default, and the only transition that shows the structure from the angles *between* the keyframes. Zoom blends geometrically and yaw takes the short way round. |
| {guilabel}`Hard cut` | Snap — the editing cut, no motion |
| {guilabel}`Fade in / out` | Cut through black at the midpoint — separates two shots that would otherwise read as one take |
| {guilabel}`Crossfade` | Dissolve — both shots rendered and mixed; the camera never occupies the angles between, which is the point: it joins views with nothing sensible between them (opposite faces of a slab) without flying through the structure |

Film-level settings: {guilabel}`Duration` (default 10 s), {guilabel}`FPS`
(default 30), and — when the workspace also holds a trajectory — a
**priority** rule deciding which timeline sets the length: {guilabel}`Film`
stretches or compresses the trajectory to play exactly once across the
film's duration; {guilabel}`Trajectory` keeps the trajectory's natural
length and re-times the camera moves to fit. Dragging the total duration
re-times shots in proportion to what they already have, preserving the
pacing.

Export goes through {menuselection}`File --> Import / Export --> Export
Animation` with {guilabel}`Film production` as the source: the export
renders exactly what the preview showed — dissolves as two complete renders
mixed, fades to black, per-shot overlays and cast opacities — and restores
your camera and view when it finishes.

% TODO screenshot: Film production dialog with three shots in the table, crossfade selected, film timeline in seconds below the viewport
```{figure} /_static/img/output_film_production.png
:alt: The Film production dialog and the seconds-based film timeline
:width: 92%
:figclass: screenshot

Film production — shots drawn from saved points-of-view, each with its own duration and transition; the timeline below plays the film live.
```

---

## Data exports at a glance

Nearly every analysis tool exports its numbers, so figures can be
regenerated in your own plotting stack.

| Source | Formats | Typical file |
|---|---|---|
| RDF | CSV, DAT | `rdf.csv` |
| Bond length / angle | CSV, DAT | `bond_lengths.csv` |
| Structure factor | CSV, DAT | `structure_factor.csv` |
| XRD pattern | CSV, DAT | `xrd_pattern.csv` |
| XRD peak list | CSV, DAT | `xrd_peaks.csv` |
| Warren-Cowley | CSV | `warren_cowley.csv` |
| Job metric series | CSV, DAT | `energy.csv` … ({doc}`/simulations/jobs`) |
| Band structure | CSV, DAT | `bands.csv` |
| PDOS | CSV, DAT | `pdos.csv` |
| Isosurface mesh | OBJ | `isosurface.obj` |
| Volumetric slice | CSV | `slice.csv` |
| Phonon bands / DOS | CSV, DAT | `phonon_bands.csv` |
| k-path | 5 codes | see the Brillouin-zone tools |
| ML datasets | extxyz, ASE db | see the Dataset Manager |
| Brillouin zone figure | PNG, SVG | `brillouin_zone.svg` |
