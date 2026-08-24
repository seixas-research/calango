# Molecular Design — the 2D sketcher

**Molecular Design** is a chemical drawing program inside Calango: you sketch a
molecule the way it is drawn on paper — line junctions for carbon, symbols for
heteroatoms, ring templates, wedge and hash stereo bonds — and send it to the
3D viewport as a real structure with hydrogens added and the geometry cleaned
up.

It opens from the **first button on the viewport toolbar** (the benzene
hexagon, before the rotation-mode button), and it is **modeless**: the 3D
viewport stays visible and usable beside it. The window keeps its drawing for
the rest of the session — closing it does not throw the sketch away.

% TODO screenshot: the Molecular Design window with a benzene drawn on the canvas, the ring tool active, and the SMILES field showing c1ccccc1
```{figure} /_static/img/builders_molecular_design.png
:alt: The Molecular Design window — tool palette, drawing canvas, output sidebar
:width: 95%
:figclass: screenshot

The three zones: drawing tools on the left, the canvas in the middle, output
and appearance on the right.
```

---

## The three zones

**Left — the tool palette.** A two-column grid of icon-only buttons: the
selection and eraser tools, the bond family (single, double, triple, wedge,
hash), the chain tool, the atom-label and caption text tools, formal charge,
and the ring-template tool. Below them sit the ring palette, the element the
atom tool writes, and the undo / redo / copy / paste / tidy / zoom-to-fit /
clear actions.

**Centre — the canvas.** Everything the other two zones do not need.
Mouse-wheel zooms toward the pointer, middle-drag (or {kbd}`Alt`-drag) pans.

**Right — output and appearance.** The molecular formula of the drawing (or of
the selection), the SMILES field, the two output buttons, the appearance
controls, and the highlight controls.

---

## Drawing

### Bonds

Select a bond tool and either **drag between two atoms** to bond them, or
**drag into empty space** to create a new atom on the end of the new bond. A
plain **click on an atom** grows a bond in whichever direction around that atom
is emptiest, so clicking the same carbon repeatedly walks out a chain.

While you drag, the free end **snaps to the 30° family** at the standard bond
length — the feel every sketcher has, and what makes a hand-drawn hexagon come
out as a hexagon. Drag further than about two bond lengths and the length stops
snapping (the angle still does), so a deliberately long bond is still possible.

**Drawing onto an existing bond cycles its order**: single → double → triple →
single. This is the ChemDraw convention and it is the fastest way to put a
double bond in a ring. The double-bond and triple-bond tools *set* the order
instead of cycling, so they are the deterministic version of the same thing.

The **wedge** and **hash** tools mark stereochemistry: the narrow end of the
glyph sits on the atom you start the drag from. Clicking a bond that already
carries that stereo mark **reverses** it.

### Atoms and hydrogens

**Carbon vertices are not labelled** — a line junction *is* a carbon, which is
the strongest convention in chemical drawing. Heteroatoms show their symbol,
and with it their hydrogen count: `OH`, `NH`, `NH2`. A carbon that carries a
formal charge or a radical is labelled anyway, because a bare dot at the end of
nothing says nothing.

Those hydrogens are **implicit**: they are counted, not drawn, and they come
from the standard valences —

| | Valence | | Valence |
|---|---|---|---|
| B | 3 | P | 3, 5 |
| C | 4 | S | 2, 4, 6 |
| N | 3 | Cl, Br, F | 1 |
| O | 2 | I | 1, 3, 5, 7 |

— with Si, Ge, Sn, As, Sb, Se and Te alongside their lighter congeners. The
**smallest** valence that still accommodates the drawn bonds is the one used,
so a three-bonded carbon gets one hydrogen and a four-bonded nitrogen is read
as a valence-5 nitrogen with none.

A **formal charge shifts the valence** the conventional way: group 15–17
elements gain a bond per positive charge and lose one per negative charge
(ammonium N is tetravalent, hydroxide O monovalent), while group 13–14 elements
lose one for a charge of *either* sign — a carbocation and a carbanion are both
trivalent — except that group 13 gains one when negative, which is what makes
borohydride tetravalent.

The **atom-label tool** puts an inline text field on the atom you click; type an
element symbol and press {kbd}`Return`. **An unknown symbol is refused with a
message on the status line and the atom is left unchanged** — the same
input-validation style as everywhere else in Calango. Clicking empty space with
this tool places a new atom of the active element (the coloured swatch in the
sidebar, which opens the periodic table).

### Impossible atoms are drawn, not refused

An atom whose drawn bonds exceed every valence its element has at that charge —
a pentavalent carbon, a divalent hydrogen — is **circled in orange and left
alone**. Chemists sketch intermediates, transition states and deliberate
nonsense, and a program that refuses the stroke is one nobody finishes a
mechanism in. The circle is a remark; nothing is blocked.

### Rings

Pick a ring from the palette and click. The full set is
**cyclopropane, cyclobutane, cyclopentane, cyclohexane, cycloheptane,
cyclooctane, benzene, cyclopentadiene** and **naphthalene**.

- Clicking **empty space** stamps the ring free-standing.
- Clicking an **existing bond** *fuses* the ring onto it: the two atoms of that
  bond become two atoms of the new ring and only the remainder are created, on
  whichever side of the bond is emptier. Two benzenes fused this way give
  naphthalene, and the ring-junction carbons come out with the right valence
  and no hydrogens.
- Clicking an **atom** fuses across one of its bonds, or hangs the ring off it
  when it has none.

Naphthalene is already a fused system, so it is always stamped free-standing
rather than fused edge-on.

### Chains

Drag from an atom (or from empty space, which creates the anchor) and a
**zig-zag alkyl chain** follows the drag, one carbon per bond length. The count
is shown live on the status line, and the chain is laid down when you release.

### Charges, captions, selection

- The **charge tool** raises an atom's formal charge on click and lowers it on
  {kbd}`Shift`-click or right-click; the charge is drawn as a superscript.
- The **caption tool** puts free text anywhere on the canvas. Captions are
  annotations — they are never part of the chemistry, never affect a formula,
  and are not exported to 3D.
- The **selection tool** does click-select, {kbd}`Shift`-click to extend, and
  rubber-band selection; dragging a selection moves it. {kbd}`Delete` removes
  it (deleting an atom takes its bonds with it).

### Tidy

The **Tidy** button (the wand) regularizes the drawing: every bond relaxes
toward the standard length, every angle toward what its coordination implies,
and every ring toward a regular polygon. With a selection, only the selected
atoms move and everything else is held exactly where it was; with none, the
whole canvas is tidied. **Separate molecules keep their relative placement** —
tidying a canvas of three fragments must not stack them on each other.

### Multiple molecules

Disconnected fragments are legal and normal. The formula read-out covers the
whole canvas and says how many fragments there are.

### Clearing the canvas

The **clear** button (the bin, in the same action grid as undo and tidy) wipes
the whole drawing — atoms, bonds, captions and highlights — in **one undo
step**. There is no confirmation prompt, deliberately: this dialog's entire
editing model is snapshot undo, {kbd}`Ctrl+Z` brings the drawing straight back,
and none of the other destructive actions here ({kbd}`Delete`, pasting over a
selection, a SMILES import that replaces what is on the canvas) asks either.
The status line says how many atoms went and that undo restores them.

---

## SMILES

The **SMILES** field on the right does both directions. It shows the SMILES of
whatever is on the canvas as you draw (except while you are typing into it),
and typing or pasting a string and pressing {kbd}`Return` draws that molecule,
laid out cleanly, as one undo step.

The parser is **native C++** — nothing in Calango's dependency set ships one,
and adding RDKit for a single text field is not a trade worth making. What it
covers:

**Supported**
: Organic-subset atoms without brackets (`B C N O P S F Cl Br I` and the
  aromatic `b c n o p s`); bracket atoms with an element symbol, hydrogen count
  and formal charge (`[nH]`, `[CH3]`, `[NH4+]`, `[O-]`, `[Fe+2]`); the bond
  symbols `-` `=` `#` `:` and the disconnection `.`; branches `(...)` nested to
  any depth; ring closures `1`–`9` and `%nn`, with a bond order allowed on the
  closure; and **aromatic rings, kekulized on import** — `c1ccccc1` and
  `C1=CC=CC=C1` produce exactly the same drawing.

**Parsed and then dropped**
: Stereochemistry — both `@`/`@@` tetrahedral chirality and the `/` `\` double-bond
  configuration marks, which are read as plain single bonds; isotope labels
  (`[13C]`); atom maps (`[CH3:1]`).

**Refused, with a reason**
: Wildcards `*` and SMARTS query syntax, unclosed rings and branches, unknown
  element symbols, and aromatic systems that admit no Kekulé structure. **A
  refused string leaves the canvas untouched** and says what was wrong and
  where: *"ring-closure 1 was opened and never closed"*, not *"parse failed"*.

On export, perceived aromatic rings are written lowercase — you get
`c1ccccc1`, not the equally valid `C1=CC=CC=C1`. Aromaticity is **perceived,
never stored**: the canvas draws and the model holds a Kekulé structure, which
is both what a chemical drawing shows and what makes the valence arithmetic
exact.

---

## Send to 3D Viewport

The button builds a real 3D structure from the drawing and opens it in a **new
viewport tab**, named after the molecular formula, through the same import
machinery every builder uses. From there it is a workspace like any other:
undoable, saveable, and available to every wizard.

**The selection wins.** With atoms selected, only those are sent. With nothing
selected, every fragment on the canvas goes, disconnected molecules included.

Three things happen, in order:

1. The 2D layout is scaled into Ångström and taken as the starting plane — it
   already carries the topology and a consistent geometry, which is exactly what
   a distance-geometry embedding would have to work to recover.
2. **Implicit hydrogens become real atoms**, placed out of the drawing plane on
   saturated centres and in it on sp² ones. (The {guilabel}`Add implicit
   hydrogens` checkbox turns this off, which sends the bare skeleton — not
   something to hand a calculator.)
3. The geometry is **relaxed**.

### The force field, and what it is worth

:::{admonition} This is a clean-up, not a converged geometry
:class: warning

The relaxation uses a **small molecular-mechanics force field written for this
module** — bond stretching to a length from the covalent radii and the bond
order, angle bending to the angle the coordination implies, a planarity
restraint on every sp² centre, a torsion term that keeps a double bond and its
substituents coplanar, and soft non-bonded repulsion beyond 1–3. Perceived
aromatic rings get *one* bond length for the whole ring rather than the
alternating long/short pair a Kekulé structure would otherwise relax to.

It reproduces bond lengths to about 0.02 Å and angles to a couple of degrees
for ordinary organic connectivity, and it gets planarity and ring geometry
right. It has **no electrostatics, no hydrogen bonding and no dispersion**, and
it is **not a conformational search** — it will not find the global minimum of
a flexible chain. Anything that needs a real geometry should go through
{menuselection}`Simulation --> Geometry Optimization` afterwards, which is what
the new tab is set up for.
:::

The alternatives were each disqualified for a concrete reason. **EMT**, the only
calculator that always runs in-process, is parameterized for FCC metals and has
no carbon, nitrogen or oxygen at all — a benzene handed to it is an exception,
not a poor answer. **xTB** is wired into Calango, but only as a generated ASE
script run in a subprocess against the optional `xtb` Python package; making the
sketcher depend on it would put several seconds and an "install xtb" dialog
between a drawn ring and the viewport, and would make the sketcher unusable
without a Python environment — which the polymer, ice and graphene-oxide
builders deliberately are not.

Benzene, as the reference case: **C6H6**, planar to within 10⁻³ Å RMS of the
best-fit plane, all six C–C bonds equal at **1.391 Å** (experiment: 1.397 Å),
C–H at **1.070 Å** (experiment: 1.084 Å), and every C–C–C angle 120.000°.

---

## Export image

Saves the drawing as a **PNG** rendered at 3× for print, or as a
resolution-independent **SVG**. {guilabel}`Transparent background` exports with
no background fill, for dropping onto a coloured slide. An export is the
*structure* — no selection halos, no hover highlights, no in-flight rubber
band.

---

## Appearance

| Control | What it does |
|---|---|
| {guilabel}`Bond width` | Line width of every bond, 0.5–6.0 |
| {guilabel}`Label size` | Point size of atom labels and captions, 6–36 pt |
| {guilabel}`Element colours` | On, heteroatoms and the bond halves reaching them take their CPK colour. Off, the drawing is monochrome — which is what most journals still want |
| {guilabel}`Canvas follows theme` | Off (the default), the drawing surface is **white** whatever the application theme is — the same rule every 2D figure in Calango follows, because a sketch ends up in a paper. On, the canvas follows Dark / Light instead. The export is unaffected either way |

---

## Highlights

Two kinds of soft fill, painted **under** the structure so they read as
highlighter behind the drawing rather than as ink on top of it. Both are
**annotations, not chemistry**: they are part of the drawing, so they are drawn
into an exported PNG or SVG, they survive undo, copy and paste — and they are
*not* part of what {guilabel}`Send to 3D Viewport` exports. A highlighted
benzene still sends C₆H₆ and nothing else.

### Aromatic rings

{guilabel}`Aromatic rings` fills every ring the model perceives as aromatic,
in a colour of your choosing. **Off by default** — a Kekulé drawing is what a
chemist expects to see, and the fill is an opinion about it.

The rule is deliberately conservative, and it is **derived, never stored**: the
sketch holds Kekulé bond orders only, so a ring drawn by hand, one stamped from
the benzene template and one imported from `c1ccccc1` are all judged the same
way. A five- or six-membered ring qualifies when every member contributes to a
closed π system — one ring double bond, or a heteroatom donating a lone pair
with none — and the total is 4n+2.

| Fills | Does not fill |
|---|---|
| benzene, pyridine, pyrrole, furan, thiophene | cyclohexene, cyclopentadiene (its sp³ CH₂ breaks the ring) |
| both rings of naphthalene, separately | a cross-conjugated ring ketone |

Larger aromatic systems (azulene, the porphyrins) are handled correctly as
Kekulé structures but are not filled: perception stops at six-membered rings.

### Colouring a region

Select atoms with the selection tool, then click one of the six palette
swatches to mark them. Several differently-coloured regions can coexist on one
canvas — the palette is a small fixed set rather than a free colour picker
precisely so that two regions stay told apart.

Highlights live **on the atoms**, which is what makes everything else follow
without a special case:

- a **bond** is coloured exactly when both of its atoms carry the same colour,
  so two adjacent regions of different colours meet at the bond between them
  instead of blending across it;
- the **eraser** and **clear** take a highlight away with the atom it belonged
  to;
- **copy and paste** carry the colour into the pasted copy;
- **undo** restores it like any other edit — applying or removing a highlight
  is one step.

{guilabel}`Remove highlight` takes the colour off the selected atoms without
touching the atoms themselves.

---

## Keyboard shortcuts

All of these are **scoped to the Molecular Design window** — they are live only
while it has focus, and the main window's own single-letter viewport modes are
untouched. Every one is remappable in
{menuselection}`Edit --> Preferences --> Hotkeys`, under **Molecular Design**.

| Key | Tool |
|---|---|
| {kbd}`V` | Selection |
| {kbd}`1` | Single bond |
| {kbd}`2` | Double bond |
| {kbd}`3` | Triple bond |
| {kbd}`4` | Wedge (bold) bond |
| {kbd}`5` | Hashed bond |
| {kbd}`C` | Chain |
| {kbd}`L` | Atom label |
| {kbd}`X` | Caption |
| {kbd}`P` | Formal charge |
| {kbd}`E` | Eraser |
| {kbd}`B` | Ring template |

| Key | Action |
|---|---|
| {kbd}`Y` | Tidy |
| {kbd}`Ctrl+Return` | Send to 3D Viewport |
| {kbd}`Ctrl+Z` / {kbd}`Ctrl+Shift+Z` | Undo / redo — the *application's* bindings, so a user who remaps undo has it remapped here too |
| {kbd}`Ctrl+C` / {kbd}`Ctrl+V` | Copy / paste the selection, within the canvas |
| {kbd}`Ctrl+A` | Select everything |
| {kbd}`Delete` / {kbd}`Backspace` | Delete the selection |
| {kbd}`Esc` | Clear the selection, or close an open inline text field |

---

## What it does not do yet

The gaps against a full ChemDraw-class program are listed in `FUTURE.md` at the
repository root, with the reasoning for each.
