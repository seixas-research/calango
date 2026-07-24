# RemixIcon — bundled UI icons

Theme-aware vector icons loaded by `calango::ui::IconManager`
(`src/ui/IconManager.{hpp,cpp}`), sourced from the [RemixIcon](https://remixicon.com)
library (Apache License 2.0). They replace the earlier procedurally-painted and
Material-Symbols icons across toolbars, dock actions, buttons and status-bar
indicators.

## Naming convention

**One monochrome SVG per glyph** — no per-theme variants. RemixIcon SVGs are
authored with `fill="currentColor"`, so `IconManager` renders each glyph and
tints it at load time:

- **Dark Mode** → high-contrast light neutral (`#E8EAED`)
- **Light Mode** → dark neutral (`#3C4043`)
- plus per-state colours for Active / Disabled / Hovered / Pressed.

The file stem is the logical name passed to `IconManager::icon()`, e.g.
`file-copy-line.svg` → `IconManager::icon("file-copy-line")`.

## Adding an icon

1. Copy the desired `.svg` from the `RemixIcon/` repository folder (organised by
   category: `System/`, `Device/`, `Design/`, …) into this directory, keeping
   its original filename.
2. Add its path to the `icons` resource group in `CMakeLists.txt`
   (`qt_add_resources(calango "icons" ...)`).
3. Use it: `widget->setIcon(calango::ui::IconManager::icon("<stem>"));` or, for a
   plain pixmap, `IconManager::pixmap("<stem>", color, px)`.

## Bundled so far

Wired into the 3D-viewport toolbar, the tab context menu and the status bar:

`focus-3-line` (reset camera), `box-3-line` (orthographic),
`anticlockwise-2-line` (rotate), `drag-move-2-line` (pan), `cursor-line`
(select), `add-circle-line` (insert), `ruler-2-line` (distance),
`compasses-2-line` (angle), `file-copy-line` (duplicate/extract),
`font-size-2` (element labels), `hashtag` (index labels), `cpu-line`,
`ram-line`, `computer-line` (GPU), `hard-drive-2-line` (VRAM), `stack-line`
(threads).

The remaining components (menu items, wizard steppers, other dock panels) are a
mechanical follow-up: drop in the RemixIcon SVG, register it here, and call
`IconManager::icon()` at the widget.
