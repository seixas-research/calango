#include "gui/MolecularDesignDialog.hpp"

#include "core/Element.hpp"
#include "core/MoleculeEmbed3d.hpp"
#include "core/MoleculeGraph.hpp"
#include "core/Smiles.hpp"
#include "gui/GuiUtils.hpp"
#include "gui/MoleculeCanvas.hpp"
#include "gui/PeriodicTableDialog.hpp"
#include "gui/PlotPalette.hpp"
#include "gui/ShortcutRegistry.hpp"
#include "ui/IconManager.hpp"

#include <QCheckBox>
#include <QColorDialog>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QFrame>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPainter>
#include <QPushButton>
#include <QEvent>
#include <QImage>
#include <QShortcut>
#include <QSpinBox>
#include <QSvgGenerator>
#include <QToolButton>
#include <QVBoxLayout>

#include <memory>

namespace calango::gui {
namespace {

using Tool = MoleculeCanvas::Tool;

/// The sidebar's icon size and the width of its grid. Two columns is what the
/// reference layout uses and what keeps a twenty-button palette from becoming
/// a scrolling column.
constexpr int kToolIcon = 20;
constexpr int kToolColumns = 2;

/// Exported figures render at three times their logical size, matching
/// GuiUtils::savePlotImage() — a Calango figure is a Calango figure whichever
/// window produced it.
constexpr int kExportScale = 3;
constexpr int kExportWidth = 900;
constexpr int kExportHeight = 700;

/// Shortcut ids, paired with the tool they select. Registered in
/// ShortcutRegistry (see ShortcutRegistry.cpp) so Preferences → Hotkeys can
/// remap every one of them; the dialog only looks them up.
struct ToolShortcut {
    const char* id;
    Tool tool;
};

const std::vector<ToolShortcut>& toolShortcuts()
{
    static const std::vector<ToolShortcut> kAll = {
        {"moleculardesign.tool.select", Tool::Select},
        {"moleculardesign.tool.singleBond", Tool::SingleBond},
        {"moleculardesign.tool.doubleBond", Tool::DoubleBond},
        {"moleculardesign.tool.tripleBond", Tool::TripleBond},
        {"moleculardesign.tool.wedgeBond", Tool::WedgeBond},
        {"moleculardesign.tool.hashBond", Tool::HashBond},
        {"moleculardesign.tool.chain", Tool::Chain},
        {"moleculardesign.tool.atomLabel", Tool::AtomLabel},
        {"moleculardesign.tool.caption", Tool::Caption},
        {"moleculardesign.tool.charge", Tool::Charge},
        {"moleculardesign.tool.eraser", Tool::Eraser},
        {"moleculardesign.tool.ring", Tool::Ring},
    };
    return kAll;
}

/// The icon each ring template shows in the palette combo.
QString ringIcon(core::RingTemplate ring)
{
    switch (ring) {
    case core::RingTemplate::Cyclopropane:    return QStringLiteral("ring-3-line");
    case core::RingTemplate::Cyclobutane:     return QStringLiteral("ring-4-line");
    case core::RingTemplate::Cyclopentane:    return QStringLiteral("ring-5-line");
    case core::RingTemplate::Cyclohexane:     return QStringLiteral("ring-6-line");
    case core::RingTemplate::Cycloheptane:    return QStringLiteral("ring-7-line");
    case core::RingTemplate::Cyclooctane:     return QStringLiteral("ring-8-line");
    case core::RingTemplate::Benzene:         return QStringLiteral("ring-benzene-line");
    case core::RingTemplate::Cyclopentadiene:
        return QStringLiteral("ring-cyclopentadiene-line");
    case core::RingTemplate::Naphthalene:
        return QStringLiteral("ring-naphthalene-line");
    }
    return QStringLiteral("hexagon-fill");
}

} // namespace

MolecularDesignDialog::MolecularDesignDialog(QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle(tr("Molecular Design"));
    setObjectName(QStringLiteral("molecularDesignDialog"));
    // Modeless: the whole point is to sketch with the 3D viewport in view.
    setModal(false);
    // A dialog, not a tool window, so it gets a real title bar and can be
    // moved to a second screen — which is where a sketcher usually ends up.
    setWindowFlag(Qt::Window, true);

    canvas_ = new MoleculeCanvas(this);
    canvas_->setObjectName(QStringLiteral("moleculeCanvas"));

    auto* columns = new QHBoxLayout;
    columns->setContentsMargins(0, 0, 0, 0);
    columns->setSpacing(6);
    columns->addWidget(buildToolSidebar(), 0);
    columns->addWidget(canvas_, 1);
    columns->addWidget(buildOutputSidebar(), 0);

    status_ = new QLabel(tr("Draw a bond, stamp a ring, or paste a SMILES "
                            "string to begin."),
                         this);
    status_->setObjectName(QStringLiteral("molecularDesignStatus"));
    status_->setWordWrap(true);

    auto* layout = new QVBoxLayout(this);
    layout->addLayout(columns, 1);
    layout->addWidget(status_, 0);

    connect(canvas_, &MoleculeCanvas::sketchChanged, this,
            &MolecularDesignDialog::refreshReadouts);
    connect(canvas_, &MoleculeCanvas::selectionChanged, this,
            &MolecularDesignDialog::refreshReadouts);
    connect(canvas_, &MoleculeCanvas::statusMessage, this,
            [this](const QString& text) { status_->setText(text); });

    applyShortcuts();
    selectTool(static_cast<int>(Tool::SingleBond));
    refreshReadouts();
    resize(1080, 720);
}

// ---------------------------------------------------------------------------
// Zone 1 — the tool sidebar
// ---------------------------------------------------------------------------

QToolButton* MolecularDesignDialog::addToolButton(QGridLayout* grid, int row,
                                                  int column,
                                                  const QString& icon,
                                                  const QString& tip,
                                                  int toolId)
{
    auto* button = new QToolButton(this);
    ui::IconManager::bind(button, icon, kToolIcon);
    button->setIconSize(QSize(kToolIcon, kToolIcon));
    button->setToolTip(tip);
    button->setCheckable(true);
    button->setAutoRaise(true);
    connect(button, &QToolButton::clicked, this,
            [this, toolId] { selectTool(toolId); });
    grid->addWidget(button, row, column);
    toolButtons_[toolId] = button;
    return button;
}

QWidget* MolecularDesignDialog::buildToolSidebar()
{
    auto* panel = new QWidget(this);
    panel->setObjectName(QStringLiteral("moleculeToolSidebar"));
    auto* layout = new QVBoxLayout(panel);
    layout->setContentsMargins(2, 2, 2, 2);
    layout->setSpacing(6);

    auto* grid = new QGridLayout;
    grid->setSpacing(2);
    int row = 0;

    // -- Selection and the bond family --------------------------------------
    addToolButton(grid, row, 0, QStringLiteral("cursor-line"),
                  tr("Selection — click an atom, or drag a box.\n"
                     "Drag the selection to move it; Shift extends it."),
                  static_cast<int>(Tool::Select));
    addToolButton(grid, row++, 1, QStringLiteral("eraser-line"),
                  tr("Eraser — click a bond to remove it, or an atom to "
                     "remove it and every bond it carried."),
                  static_cast<int>(Tool::Eraser));

    addToolButton(grid, row, 0, QStringLiteral("bond-single-line"),
                  tr("Single bond — drag between atoms, or click an atom to "
                     "grow one.\nDrawing onto an EXISTING bond cycles its "
                     "order: single → double → triple → single."),
                  static_cast<int>(Tool::SingleBond));
    addToolButton(grid, row++, 1, QStringLiteral("bond-double-line"),
                  tr("Double bond — draws, or sets an existing bond to, "
                     "order 2."),
                  static_cast<int>(Tool::DoubleBond));

    addToolButton(grid, row, 0, QStringLiteral("bond-triple-line"),
                  tr("Triple bond — draws, or sets an existing bond to, "
                     "order 3."),
                  static_cast<int>(Tool::TripleBond));
    addToolButton(grid, row++, 1, QStringLiteral("chain-zigzag-line"),
                  tr("Chain — drag from an atom to lay down a zig-zag alkyl "
                     "chain.\nThe length follows the drag and is counted on "
                     "the status line."),
                  static_cast<int>(Tool::Chain));

    addToolButton(grid, row, 0, QStringLiteral("bond-wedge-line"),
                  tr("Bold (wedge) bond — comes out of the page, narrow end "
                     "at the atom you start from.\nClicking a wedge again "
                     "reverses it."),
                  static_cast<int>(Tool::WedgeBond));
    addToolButton(grid, row++, 1, QStringLiteral("bond-hash-line"),
                  tr("Hashed bond — goes behind the page, narrow end at the "
                     "atom you start from.\nClicking a hash again reverses "
                     "it."),
                  static_cast<int>(Tool::HashBond));

    // -- Labels, captions, charge -------------------------------------------
    addToolButton(grid, row, 0, QStringLiteral("atom-label-line"),
                  tr("Atom label — click an atom and type its element symbol "
                     "(N, O, Cl…).\nClick empty space to place a new atom of "
                     "the active element."),
                  static_cast<int>(Tool::AtomLabel));
    addToolButton(grid, row++, 1, QStringLiteral("text-caption-line"),
                  tr("Caption — free text anywhere on the canvas. Captions "
                     "are annotations:\nthey are never part of the chemistry "
                     "and are not exported to 3D."),
                  static_cast<int>(Tool::Caption));

    addToolButton(grid, row, 0, QStringLiteral("add-circle-fill"),
                  tr("Formal charge — click an atom to raise its charge, "
                     "Shift-click (or right-click) to lower it.\n"
                     "An impossible valence (a pentavalent carbon) is circled "
                     "rather than refused: intermediates are drawings too."),
                  static_cast<int>(Tool::Charge));
    addToolButton(grid, row++, 1, QStringLiteral("ring-benzene-line"),
                  tr("Ring template — click empty space to stamp the ring "
                     "chosen below,\nor click an existing bond to FUSE the "
                     "ring onto it."),
                  static_cast<int>(Tool::Ring));

    layout->addLayout(grid);

    // -- The ring palette ----------------------------------------------------
    ringCombo_ = new QComboBox(panel);
    ringCombo_->setObjectName(QStringLiteral("ringTemplateCombo"));
    ringCombo_->setToolTip(tr("Which ring the ring tool stamps."));
    ringCombo_->setIconSize(QSize(kToolIcon, kToolIcon));
    for (core::RingTemplate ring : core::ringTemplates()) {
        // The name comes from the core table at run time, so it is a
        // QLatin1String rather than a tr() literal — this repo ships no
        // translation catalogs, and a tr() over a non-literal is invisible to
        // lupdate anyway. The ICON is filled in by refreshRingIcons(), which
        // also runs again on every theme change.
        ringCombo_->addItem(QLatin1String(core::ringTemplateName(ring)),
                            static_cast<int>(ring));
    }
    refreshRingIcons();
    ringCombo_->setCurrentIndex(
        ringCombo_->findData(static_cast<int>(core::RingTemplate::Benzene)));
    connect(ringCombo_, &QComboBox::currentIndexChanged, this, [this](int) {
        canvas_->setRingTemplate(
            static_cast<core::RingTemplate>(ringCombo_->currentData().toInt()));
        // Picking a ring is unambiguously a statement that the next click
        // should stamp one.
        selectTool(static_cast<int>(Tool::Ring));
    });
    layout->addWidget(ringCombo_);

    // -- The element the atom tool writes ------------------------------------
    //
    // Same control the main window's Insertion mode uses — a swatch in the
    // element's own CPK colour with its symbol on it — so the two read as the
    // same idea in two places.
    elementButton_ = new QToolButton(panel);
    elementButton_->setObjectName(QStringLiteral("moleculeElementButton"));
    elementButton_->setToolButtonStyle(Qt::ToolButtonTextOnly);
    elementButton_->setToolTip(tr("Element placed by the atom tool — click to "
                                  "choose from the periodic table."));
    const auto refreshElementButton = [this] {
        const QColor background = cpkColor(canvas_->activeElement());
        const QColor text = readableTextColor(background);
        elementButton_->setStyleSheet(
            QStringLiteral("QToolButton { background-color: %1; color: %2;"
                           " font-weight: bold; border: 1px solid %3;"
                           " border-radius: 3px; padding: 2px 4px; }")
                .arg(background.name(), text.name(),
                     background.darker(140).name()));
        elementButton_->setText(QLatin1String(
            core::Elements::data(canvas_->activeElement()).symbol));
    };
    refreshElementButton();
    connect(elementButton_, &QToolButton::clicked, this,
            [this, refreshElementButton] {
                if (const int z = PeriodicTableDialog::pickElement(
                        this, canvas_->activeElement())) {
                    canvas_->setActiveElement(z);
                    refreshElementButton();
                    selectTool(static_cast<int>(Tool::AtomLabel));
                }
            });
    connect(canvas_, &MoleculeCanvas::sketchChanged, this, refreshElementButton);
    layout->addWidget(elementButton_);

    auto* separator = new QFrame(panel);
    separator->setFrameShape(QFrame::HLine);
    separator->setFrameShadow(QFrame::Sunken);
    layout->addWidget(separator);

    // -- Undo / redo / clipboard / tidy --------------------------------------
    auto* actions = new QGridLayout;
    actions->setSpacing(2);
    const auto addAction = [this, actions](int row, int column,
                                           const QString& icon,
                                           const QString& tip,
                                           void (MoleculeCanvas::*slot)()) {
        auto* button = new QToolButton(this);
        ui::IconManager::bind(button, icon, kToolIcon);
        button->setIconSize(QSize(kToolIcon, kToolIcon));
        button->setToolTip(tip);
        button->setAutoRaise(true);
        connect(button, &QToolButton::clicked, canvas_, slot);
        actions->addWidget(button, row, column);
        return button;
    };
    undoButton_ = addAction(0, 0, QStringLiteral("arrow-go-back-line"),
                            tr("Undo"), &MoleculeCanvas::undo);
    redoButton_ = addAction(0, 1, QStringLiteral("arrow-go-forward-line"),
                            tr("Redo"), &MoleculeCanvas::redo);
    addAction(1, 0, QStringLiteral("file-copy-line"),
              tr("Copy the selection"), &MoleculeCanvas::copySelection);
    addAction(1, 1, QStringLiteral("clipboard-line"), tr("Paste"),
              &MoleculeCanvas::pasteClipboard);
    addAction(2, 0, QStringLiteral("magic-line"),
              tr("Tidy — regularize bond lengths and angles.\n"
                 "Acts on the selection when there is one, on the whole "
                 "sketch otherwise."),
              &MoleculeCanvas::tidySelection);
    addAction(2, 1, QStringLiteral("focus-3-line"), tr("Zoom to fit"),
              &MoleculeCanvas::zoomToFit);
    // Clear the whole canvas. Here, beside the other whole-sketch edits,
    // rather than in the output sidebar: it is an EDIT, and putting it next to
    // "Send to 3D Viewport" would be putting a destructive button next to the
    // one people reach for constantly. No confirmation — see
    // MoleculeCanvas::clearCanvas() for why undo is the answer instead.
    addAction(3, 0, QStringLiteral("delete-bin-line"),
              tr("Clear the canvas.\nOne undo step — Ctrl+Z brings the "
                 "drawing back."),
              &MoleculeCanvas::clearCanvas);
    layout->addLayout(actions);

    layout->addStretch(1);
    panel->setFixedWidth(kToolColumns * (kToolIcon + 18) + 44);
    return panel;
}

// ---------------------------------------------------------------------------
// Zone 3 — output and appearance
// ---------------------------------------------------------------------------

QWidget* MolecularDesignDialog::buildOutputSidebar()
{
    auto* panel = new QWidget(this);
    panel->setObjectName(QStringLiteral("moleculeOutputSidebar"));
    auto* layout = new QVBoxLayout(panel);
    layout->setContentsMargins(2, 2, 2, 2);
    layout->setSpacing(6);

    formula_ = new QLabel(panel);
    formula_->setObjectName(QStringLiteral("moleculeFormulaLabel"));
    formula_->setTextFormat(Qt::RichText);
    formula_->setWordWrap(true);
    layout->addWidget(formula_);

    // -- SMILES in and out ---------------------------------------------------
    auto* smilesGroup = new QGroupBox(tr("SMILES"), panel);
    auto* smilesLayout = new QVBoxLayout(smilesGroup);
    smilesEdit_ = new QLineEdit(smilesGroup);
    smilesEdit_->setObjectName(QStringLiteral("smilesEdit"));
    smilesEdit_->setPlaceholderText(QStringLiteral("c1ccccc1"));
    smilesEdit_->setToolTip(
        tr("Type or paste a SMILES string and press Return to draw it.\n"
           "The organic subset plus bracket atoms is supported; "
           "stereochemistry and isotope labels parse but are dropped.\n\n"
           "The field also shows the SMILES of whatever is on the canvas, "
           "updated as you draw."));
    connect(smilesEdit_, &QLineEdit::returnPressed, this,
            [this] { loadSmiles(smilesEdit_->text()); });
    smilesLayout->addWidget(smilesEdit_);
    layout->addWidget(smilesGroup);

    // -- Output --------------------------------------------------------------
    auto* outputGroup = new QGroupBox(tr("Output"), panel);
    auto* outputLayout = new QVBoxLayout(outputGroup);

    auto* send = new QPushButton(tr("Send to 3D Viewport"), outputGroup);
    send->setObjectName(QStringLiteral("sendToViewportButton"));
    ui::IconManager::bind(send, QStringLiteral("box-3-line"), kToolIcon);
    send->setToolTip(
        tr("Build a 3D structure from the drawing and open it in a new "
           "viewport tab.\n\n"
           "THE SELECTION WINS: with atoms selected, only those are sent; "
           "with nothing selected,\nevery fragment on the canvas is sent, "
           "disconnected molecules included.\n\n"
           "Implicit hydrogens become real atoms and the geometry is relaxed "
           "with Calango's\ninternal molecular-mechanics clean-up — a "
           "starting geometry, not a converged one."));
    connect(send, &QPushButton::clicked, this,
            [this] { sendToViewport(); });
    outputLayout->addWidget(send);

    addHydrogensBox_ = new QCheckBox(tr("Add implicit hydrogens"), outputGroup);
    addHydrogensBox_->setObjectName(QStringLiteral("addHydrogensBox"));
    addHydrogensBox_->setChecked(true);
    addHydrogensBox_->setToolTip(
        tr("On, every hydrogen the valences imply becomes a real atom in the "
           "exported structure.\nOff, only the drawn skeleton is sent — which "
           "no calculator should be handed."));
    outputLayout->addWidget(addHydrogensBox_);

    relaxBox_ = new QCheckBox(tr("Relax geometry"), outputGroup);
    relaxBox_->setObjectName(QStringLiteral("relaxGeometryBox"));
    relaxBox_->setChecked(true);
    relaxBox_->setToolTip(
        tr("On, the 3D coordinates are relaxed against Calango's internal "
           "molecular-mechanics\nrestraints (bonds, angles, sp² planarity, "
           "steric repulsion). Off, the flat 2D drawing\nis exported "
           "verbatim, which is occasionally what you want for a planar "
           "system."));
    outputLayout->addWidget(relaxBox_);

    auto* exportButton = new QPushButton(tr("Export image…"), outputGroup);
    exportButton->setObjectName(QStringLiteral("exportImageButton"));
    ui::IconManager::bind(exportButton, QStringLiteral("camera-lens-line"),
                          kToolIcon);
    exportButton->setToolTip(
        tr("Save the drawing as a PNG (rendered at 3× for print) or as a "
           "resolution-independent SVG."));
    connect(exportButton, &QPushButton::clicked, this,
            &MolecularDesignDialog::exportImage);
    outputLayout->addWidget(exportButton);

    transparentBox_ = new QCheckBox(tr("Transparent background"), outputGroup);
    transparentBox_->setObjectName(QStringLiteral("transparentBackgroundBox"));
    transparentBox_->setToolTip(
        tr("Export with no background fill, so the structure can be dropped "
           "onto a coloured slide."));
    outputLayout->addWidget(transparentBox_);
    layout->addWidget(outputGroup);

    // -- Appearance ----------------------------------------------------------
    auto* appearance = new QGroupBox(tr("Appearance"), panel);
    auto* form = new QFormLayout(appearance);

    lineWidthBox_ = new QDoubleSpinBox(appearance);
    lineWidthBox_->setObjectName(QStringLiteral("bondLineWidthBox"));
    lineWidthBox_->setRange(0.5, 6.0);
    lineWidthBox_->setSingleStep(0.2);
    lineWidthBox_->setDecimals(1);
    lineWidthBox_->setValue(canvas_->lineWidth());
    connect(lineWidthBox_, &QDoubleSpinBox::valueChanged, canvas_,
            &MoleculeCanvas::setLineWidth);
    form->addRow(tr("Bond width"), lineWidthBox_);

    fontSizeBox_ = new QSpinBox(appearance);
    fontSizeBox_->setObjectName(QStringLiteral("labelFontSizeBox"));
    fontSizeBox_->setRange(6, 36);
    fontSizeBox_->setSuffix(tr(" pt"));
    fontSizeBox_->setValue(canvas_->labelPointSize());
    connect(fontSizeBox_, &QSpinBox::valueChanged, canvas_,
            &MoleculeCanvas::setLabelPointSize);
    form->addRow(tr("Label size"), fontSizeBox_);

    elementColorsBox_ = new QCheckBox(tr("Element colours"), appearance);
    elementColorsBox_->setObjectName(QStringLiteral("elementColorsBox"));
    elementColorsBox_->setChecked(canvas_->elementColors());
    elementColorsBox_->setToolTip(
        tr("On, heteroatoms and the bond halves that reach them take their "
           "CPK colour.\nOff, the whole drawing is monochrome — which is what "
           "most journals still want."));
    connect(elementColorsBox_, &QCheckBox::toggled, canvas_,
            &MoleculeCanvas::setElementColors);
    form->addRow(elementColorsBox_);

    followThemeBox_ = new QCheckBox(tr("Canvas follows theme"), appearance);
    followThemeBox_->setObjectName(QStringLiteral("followThemeBox"));
    followThemeBox_->setToolTip(
        tr("Off (the default), the drawing surface is white whatever the "
           "application theme is —\nthe same rule every 2D figure in Calango "
           "follows, because a sketch ends up in a paper.\n"
           "On, the canvas follows the Dark / Light theme instead. The export "
           "is unaffected either way."));
    connect(followThemeBox_, &QCheckBox::toggled, canvas_,
            &MoleculeCanvas::setFollowsTheme);
    form->addRow(followThemeBox_);
    layout->addWidget(appearance);

    // -- Highlights ----------------------------------------------------------
    //
    // Annotations, kept apart from Appearance because they are not a property
    // of the drawing style: they are marks on THIS drawing, they live in the
    // sketch, and they survive undo. Both of them export with the image and
    // neither reaches the 3D structure.
    auto* highlights = new QGroupBox(tr("Highlights"), panel);
    auto* highlightLayout = new QVBoxLayout(highlights);
    highlightLayout->setSpacing(4);

    auto* aromaticRow = new QHBoxLayout;
    aromaticRow->setContentsMargins(0, 0, 0, 0);
    aromaticBox_ = new QCheckBox(tr("Aromatic rings"), highlights);
    aromaticBox_->setObjectName(QStringLiteral("aromaticHighlightBox"));
    aromaticBox_->setChecked(canvas_->aromaticHighlight());
    aromaticBox_->setToolTip(
        tr("Fill every ring the model perceives as aromatic.\n\n"
           "The rule is deliberately conservative and derived, never stored: a "
           "5- or 6-membered ring qualifies when every member contributes to a "
           "closed π system — one ring double bond, or a heteroatom lone pair "
           "with none — and the total is 4n+2. Benzene, pyridine, pyrrole, "
           "furan, thiophene and both rings of naphthalene fill; cyclohexene "
           "and cyclopentadiene do not.\n\n"
           "A ring drawn by hand, one stamped from the template and one "
           "imported from SMILES are all judged the same way, because the "
           "sketch stores Kekulé bond orders in all three cases."));
    connect(aromaticBox_, &QCheckBox::toggled, canvas_,
            &MoleculeCanvas::setAromaticHighlight);
    aromaticRow->addWidget(aromaticBox_, 1);

    aromaticColorButton_ = new QPushButton(highlights);
    aromaticColorButton_->setObjectName(QStringLiteral("aromaticColorButton"));
    aromaticColorButton_->setFixedSize(28, 20);
    aromaticColorButton_->setToolTip(tr("Colour of the aromatic ring fill"));
    setButtonColor(aromaticColorButton_, canvas_->aromaticHighlightColor());
    connect(aromaticColorButton_, &QPushButton::clicked, this, [this] {
        const QColor picked =
            QColorDialog::getColor(canvas_->aromaticHighlightColor(), this,
                                   tr("Aromatic Ring Colour"));
        if (!picked.isValid())
            return;
        canvas_->setAromaticHighlightColor(picked);
        setButtonColor(aromaticColorButton_, picked);
    });
    aromaticRow->addWidget(aromaticColorButton_, 0);
    highlightLayout->addLayout(aromaticRow);

    auto* regionLabel = new QLabel(tr("Colour selection:"), highlights);
    regionLabel->setToolTip(
        tr("Select atoms with the selection tool, then click a colour to mark "
           "them.\nRegions of different colours can coexist; a bond is "
           "coloured when both of its atoms are.\n\n"
           "Highlights are annotations. They are drawn under the structure, "
           "they follow their atoms through copy, paste and undo, the eraser "
           "and Clear take them away with the atoms they belong to, and they "
           "are not part of what Send to 3D Viewport exports."));
    highlightLayout->addWidget(regionLabel);

    auto* swatches = new QHBoxLayout;
    swatches->setContentsMargins(0, 0, 0, 0);
    swatches->setSpacing(3);
    for (int i = 0; i < MoleculeCanvas::highlightPaletteSize(); ++i) {
        auto* swatch = new QPushButton(highlights);
        swatch->setObjectName(QStringLiteral("highlightSwatch%1").arg(i));
        swatch->setFixedSize(24, 20);
        swatch->setToolTip(tr("Highlight the selection — %1")
                               .arg(MoleculeCanvas::highlightPaletteName(i)));
        setButtonColor(swatch, MoleculeCanvas::highlightPaletteColor(i));
        connect(swatch, &QPushButton::clicked, this,
                [this, i] { canvas_->highlightSelection(i); });
        swatches->addWidget(swatch);
    }
    swatches->addStretch(1);
    highlightLayout->addLayout(swatches);

    auto* clearHighlight =
        new QPushButton(tr("Remove highlight"), highlights);
    clearHighlight->setObjectName(QStringLiteral("clearHighlightButton"));
    clearHighlight->setToolTip(
        tr("Take the highlight off the selected atoms. One undo step, like "
           "applying one."));
    connect(clearHighlight, &QPushButton::clicked, this,
            [this] { canvas_->highlightSelection(-1); });
    highlightLayout->addWidget(clearHighlight);
    layout->addWidget(highlights);

    layout->addStretch(1);
    panel->setFixedWidth(250);
    return panel;
}

// ---------------------------------------------------------------------------
// Shortcuts
// ---------------------------------------------------------------------------

void MolecularDesignDialog::applyShortcuts()
{
    // Qt::WidgetWithChildrenShortcut is what "scoped to the dialog" means
    // literally: these keys are live while this window has focus and are
    // invisible to the main window's own single-letter viewport modes.
    for (const ToolShortcut& entry : toolShortcuts()) {
        const QKeySequence key =
            ShortcutRegistry::binding(QLatin1String(entry.id));
        if (key.isEmpty())
            continue;
        auto* shortcut = new QShortcut(key, this);
        shortcut->setContext(Qt::WidgetWithChildrenShortcut);
        const int toolId = static_cast<int>(entry.tool);
        connect(shortcut, &QShortcut::activated, this,
                [this, toolId] { selectTool(toolId); });
        // Show the binding on the button it drives, the way the main toolbar
        // does — a palette whose keys are undiscoverable has no keys.
        const auto button = toolButtons_.find(toolId);
        if (button != toolButtons_.end()) {
            button->second->setToolTip(
                QStringLiteral("%1  [%2]")
                    .arg(button->second->toolTip(),
                         key.toString(QKeySequence::NativeText)));
        }
    }

    const auto add = [this](const QKeySequence& key, auto slot) {
        if (key.isEmpty())
            return;
        auto* shortcut = new QShortcut(key, this);
        shortcut->setContext(Qt::WidgetWithChildrenShortcut);
        connect(shortcut, &QShortcut::activated, canvas_, slot);
    };
    // Undo/redo reuse the APPLICATION's ids rather than inventing sketcher
    // ones: undo is undo, and a user who remaps it expects it remapped here.
    add(ShortcutRegistry::binding(QStringLiteral("edit.undo")),
        &MoleculeCanvas::undo);
    add(ShortcutRegistry::binding(QStringLiteral("edit.redo")),
        &MoleculeCanvas::redo);
    add(QKeySequence(QKeySequence::Copy), &MoleculeCanvas::copySelection);
    add(QKeySequence(QKeySequence::Paste), &MoleculeCanvas::pasteClipboard);
    add(QKeySequence(QKeySequence::SelectAll), &MoleculeCanvas::selectAll);
    add(ShortcutRegistry::binding(QStringLiteral("moleculardesign.tidy")),
        &MoleculeCanvas::tidySelection);

    const QKeySequence send =
        ShortcutRegistry::binding(QStringLiteral("moleculardesign.sendToViewport"));
    if (!send.isEmpty()) {
        auto* shortcut = new QShortcut(send, this);
        shortcut->setContext(Qt::WidgetWithChildrenShortcut);
        connect(shortcut, &QShortcut::activated, this,
                [this] { sendToViewport(); });
    }
}

void MolecularDesignDialog::refreshRingIcons()
{
    if (!ringCombo_)
        return;
    const auto& templates = core::ringTemplates();
    for (int i = 0; i < ringCombo_->count()
         && i < static_cast<int>(templates.size());
         ++i) {
        ringCombo_->setItemIcon(
            i, ui::IconManager::icon(ringIcon(templates[static_cast<std::size_t>(i)]),
                                     kToolIcon));
    }
}

void MolecularDesignDialog::changeEvent(QEvent* event)
{
    QDialog::changeEvent(event);
    // APPLICATION-level events only, exactly as IconManager's own theme
    // watcher does. QEvent::PaletteChange is the per-WIDGET one Qt sends as a
    // consequence of restyling — and setting a combo item's icon restyles the
    // combo — so handling it here would close a
    // refresh -> setItemIcon -> PaletteChange -> refresh loop that pegs the CPU.
    if (event->type() == QEvent::ApplicationPaletteChange
        || event->type() == QEvent::ThemeChange) {
        refreshRingIcons();
    }
}

void MolecularDesignDialog::selectTool(int toolId)
{
    canvas_->setTool(static_cast<Tool>(toolId));
    for (const auto& [id, button] : toolButtons_)
        button->setChecked(id == toolId);
}

// ---------------------------------------------------------------------------
// Read-outs
// ---------------------------------------------------------------------------

void MolecularDesignDialog::refreshReadouts()
{
    const core::MoleculeGraph& graph = canvas_->graph();
    const std::vector<int> selection = canvas_->selectedAtoms();
    const std::string formula = selection.empty() ? graph.formula()
                                                  : graph.formula(selection);
    const int fragments = static_cast<int>(graph.fragments().size());

    QString text;
    if (formula.empty()) {
        text = tr("<b>Empty canvas</b>");
    } else {
        // Subscript the digits, which is what a formula is supposed to look
        // like and costs one pass over the string.
        QString pretty;
        for (const QChar c : QString::fromStdString(formula)) {
            if (c.isDigit())
                pretty += QStringLiteral("<sub>%1</sub>").arg(c);
            else
                pretty += c;
        }
        text = selection.empty()
            ? tr("<b>%1</b>").arg(pretty)
            : tr("<b>%1</b> (selected)").arg(pretty);
        if (selection.empty() && fragments > 1)
            text += tr("<br>%n separate fragment(s)", "", fragments);
    }
    formula_->setText(text);

    if (undoButton_)
        undoButton_->setEnabled(canvas_->canUndo());
    if (redoButton_)
        redoButton_->setEnabled(canvas_->canRedo());

    // Keep the SMILES field showing the canvas, but never while the user is
    // typing into it — overwriting a half-typed string is the fastest way to
    // make a text field unusable.
    if (smilesEdit_ && !smilesEdit_->hasFocus()) {
        const QSignalBlocker blocker(smilesEdit_);
        smilesEdit_->setText(
            QString::fromStdString(core::smiles::write(graph)));
    }
}

// ---------------------------------------------------------------------------
// SMILES import
// ---------------------------------------------------------------------------

bool MolecularDesignDialog::loadSmiles(const QString& text)
{
    const QString trimmed = text.trimmed();
    if (trimmed.isEmpty()) {
        status_->setText(tr("Type a SMILES string first."));
        return false;
    }
    core::MoleculeGraph parsed;
    std::string error;
    if (!core::smiles::parse(trimmed.toStdString(), parsed, &error)) {
        // The app's standard input-validation style: refuse, say exactly what
        // was wrong and where, change nothing.
        status_->setText(tr("Not a valid SMILES string — %1. The canvas was "
                            "left unchanged.")
                             .arg(QString::fromStdString(error)));
        return false;
    }
    canvas_->setGraph(std::move(parsed), tr("Import SMILES"));
    status_->setText(tr("Drew %1 from SMILES.")
                         .arg(QString::fromStdString(canvas_->graph().formula())));
    return true;
}

// ---------------------------------------------------------------------------
// 2D -> 3D
// ---------------------------------------------------------------------------

bool MolecularDesignDialog::sendToViewport()
{
    const core::MoleculeGraph& graph = canvas_->graph();
    if (graph.atomCount() == 0) {
        status_->setText(tr("Draw a molecule first."));
        return false;
    }

    core::EmbedOptions options;
    options.addHydrogens = !addHydrogensBox_ || addHydrogensBox_->isChecked();
    options.steps = (!relaxBox_ || relaxBox_->isChecked()) ? 1200 : 0;

    auto structure = std::make_shared<core::Structure>();
    const core::EmbedResult result =
        core::embed(graph, canvas_->selectedAtoms(), *structure, options);
    if (!result.ok) {
        status_->setText(QString::fromStdString(result.error));
        return false;
    }

    const QString name =
        QString::fromStdString(structure->chemicalFormula());
    Q_EMIT structureReady(structure, name);
    status_->setText(tr("Sent %1 to a new viewport tab: %2 drawn atoms, "
                        "%3 hydrogens added.")
                         .arg(name)
                         .arg(result.heavyAtoms)
                         .arg(result.hydrogensAdded));
    return true;
}

// ---------------------------------------------------------------------------
// Image export
// ---------------------------------------------------------------------------

void MolecularDesignDialog::exportImage()
{
    if (canvas_->graph().atomCount() == 0) {
        status_->setText(tr("Draw a molecule first."));
        return;
    }
    const QString stem =
        QString::fromStdString(canvas_->graph().formula());
    QString selectedFilter;
    QString path = QFileDialog::getSaveFileName(
        this, tr("Export Drawing"),
        stem.isEmpty() ? QStringLiteral("sketch.png")
                       : stem + QStringLiteral(".png"),
        tr("PNG image (*.png);;Scalable vector graphics (*.svg)"),
        &selectedFilter);
    if (path.isEmpty())
        return;
    // The static QFileDialog helpers have no setDefaultSuffix(), so a name
    // typed without one would otherwise produce an extension-less file — and,
    // worse here, one written in whichever format the suffix test below
    // guesses rather than the one the user picked in the filter.
    path = withFilterSuffix(path, selectedFilter);

    const QColor background =
        transparentBox_ && transparentBox_->isChecked()
        ? QColor()                 // invalid: renderTo() paints nothing
        : QColor(PlotPalette::canvas);

    const QSize logical(kExportWidth, kExportHeight);
    if (QFileInfo(path).suffix().compare(QStringLiteral("svg"),
                                         Qt::CaseInsensitive)
        == 0) {
        // A vector export is the one a figure actually wants — a structure
        // drawing is lines and text, and a raster of it is a raster of lines
        // and text.
        QSvgGenerator generator;
        generator.setFileName(path);
        generator.setSize(logical);
        generator.setViewBox(QRect(QPoint(0, 0), logical));
        generator.setTitle(stem);
        generator.setDescription(tr("Drawn in Calango's Molecular Design"));
        QPainter painter(&generator);
        painter.setRenderHint(QPainter::Antialiasing, true);
        canvas_->renderTo(painter, logical, background);
    } else {
        QImage image(logical * kExportScale, QImage::Format_ARGB32_Premultiplied);
        image.fill(Qt::transparent);
        QPainter painter(&image);
        painter.setRenderHint(QPainter::Antialiasing, true);
        painter.setRenderHint(QPainter::TextAntialiasing, true);
        painter.scale(kExportScale, kExportScale);
        canvas_->renderTo(painter, logical, background);
        painter.end();
        if (!image.save(path)) {
            status_->setText(tr("Could not write %1.").arg(path));
            return;
        }
    }
    status_->setText(tr("Exported %1.").arg(QFileInfo(path).fileName()));
}

} // namespace calango::gui
