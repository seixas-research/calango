#include "gui/CellAxesTabs.hpp"

#include "core/Structure.hpp"
#include "gui/GuiUtils.hpp"
#include "gui/NeighborCellsDialog.hpp"
#include "ui/IconManager.hpp"

#include "gui/ViewportWidget.hpp"

#include <QCheckBox>
#include <QColorDialog>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSlider>
#include <QSpinBox>
#include <cmath>
#include <QStandardItemModel>
#include <QLabel>

namespace calango::gui {

UnitCellPanel::UnitCellPanel(ViewportWidget* viewport, QWidget* parent)
    : QWidget(parent)
    , viewport_(viewport)
{
    auto* form = new QFormLayout(this);

    // The three periodic-image controls on one icon row. They are the same
    // question at three scales — draw the cell boundary, complete the bonds
    // that leave it, repeat the whole cell — so they read as a group; stacked
    // as two full-width check boxes and a labelled button they read as three
    // unrelated settings, and cost three rows to say it.
    auto* cellRow = new QHBoxLayout;
    cellRow->setSpacing(4);
    const auto makeCellButton = [this, cellRow](const QString& icon,
                                                const QString& tip) {
        auto* button = new QPushButton(this);
        ui::IconManager::bind(button, icon);
        button->setIconSize(QSize(20, 20));
        button->setFocusPolicy(Qt::NoFocus);
        button->setToolTip(tip);
        cellRow->addWidget(button);
        return button;
    };

    // The three read as one story: the home cell, the home cell with what its
    // bonds reach into next door, and the whole neighbourhood.
    auto* cellShowButton = makeCellButton(
        QStringLiteral("home-2-fill"),
        tr("Show unit cell — draw the cell edges as a wireframe box."));
    cellShowButton->setCheckable(true);
    cellShowButton->setChecked(viewport_->style().showCell);
    connect(cellShowButton, &QPushButton::toggled,
            viewport_, &ViewportWidget::setShowCell);

    // Directly beside "Show unit cell", because it is the SAME box drawn a
    // second way rather than a different thing to draw: the wireframe says
    // where the edges are, the fill says which side of them is inside. A bare
    // wireframe box is genuinely ambiguous about that — the Necker-cube flip —
    // and a faint tinted solid settles it without a caption.
    auto* fillButton = makeCellButton(
        QStringLiteral("paint-fill"),
        tr("Fill the unit cell — shade its six faces with a translucent "
           "solid, so the box reads as a volume rather than as twelve "
           "lines.\n\n"
           "Independent of \"Show unit cell\": either depiction may be used "
           "alone, so a solid block with no wireframe is available as well as "
           "the wireframe with no fill. The colour and opacity are below, "
           "under the line settings.\n\n"
           "Purely visual — nothing about the structure or any exported file "
           "changes."));
    fillButton->setCheckable(true);
    fillButton->setChecked(viewport_->style().fillCell);

    // The home cell plus the annex its bonds spill into.
    auto* ghostButton = makeCellButton(
        QStringLiteral("home-office-fill"),
        tr("Draw the periodic images the home cell's bonds actually reach "
           "into:\n"
           "• the far end of every bond that wraps around the cell, so a "
           "periodic bond terminates on an atom instead of stopping in "
           "mid-air;\n"
           "• atoms lying exactly on a face, edge or vertex, repeated at the "
           "opposite side (fractional 0 → 1) together with the atoms their own "
           "bonds reach, so the cell reads as a closed motif instead of one "
           "sliced off at two faces.\n\n"
           "Purely visual: the atom count, the chemical formula and every "
           "exported POSCAR/CIF are unchanged."));
    ghostButton->setCheckable(true);
    ghostButton->setChecked(viewport_->style().showNeighborCellAtoms);
    connect(ghostButton, &QPushButton::toggled, this, [this](bool on) {
        viewport_->style().showNeighborCellAtoms = on;
        // Extra instances — the geometry buffers must be rebuilt.
        viewport_->styleChanged(true);
    });

    // A row of dwellings: this one repeats the whole cell over a range. Not
    // checkable — it opens a dialog rather than flipping a bit, which is why
    // it keeps a push button's look beside the two toggles.
    auto* neighborCellsButton = makeCellButton(
        QStringLiteral("community-fill"),
        tr("Show neighboring cells… — draw the periodic images of the "
           "cell over a range of fractional "
           "coordinates — e.g. x from 0 to 2 adds the neighboring cell along "
           "+x, with its atoms and bonds.\n\n"
           "Purely visual: the atom count, the formula and every exported "
           "structure file are unchanged."));
    cellRow->addStretch(1);
    form->addRow(cellRow);
    connect(neighborCellsButton, &QPushButton::clicked, this, [this] {
        // Modeless and singleton-per-panel: the dialog applies live, so it has
        // to stay open beside the viewport while the user judges the result,
        // and a second copy would fight the first over the same style field.
        if (!neighborCellsDialog_) {
            neighborCellsDialog_ = new NeighborCellsDialog(viewport_, this);
            neighborCellsDialog_->setAttribute(Qt::WA_DeleteOnClose);
            connect(neighborCellsDialog_, &QObject::destroyed, this,
                    [this] { neighborCellsDialog_ = nullptr; });
        }
        neighborCellsDialog_->show();
        neighborCellsDialog_->raise();
        neighborCellsDialog_->activateWindow();
    });

    auto* cellColorButton = new QPushButton(this);
    cellColorButton->setFixedHeight(22);
    setButtonColor(cellColorButton, viewport_->style().cellColor);
    form->addRow(tr("Cell color:"), cellColorButton);
    connect(cellColorButton, &QPushButton::clicked, this, [this, cellColorButton] {
        const QColor chosen = QColorDialog::getColor(
            viewport_->style().cellColor, this, tr("Unit Cell Wireframe Color"));
        if (!chosen.isValid())
            return;
        setButtonColor(cellColorButton, chosen);
        viewport_->style().cellColor = chosen;
        viewport_->styleChanged(true);
    });

    auto* lineStyleCombo = new QComboBox(this);
    // Order matches render::CellLineStyle.
    lineStyleCombo->addItem(tr("Solid"));
    lineStyleCombo->addItem(tr("Dashed"));
    lineStyleCombo->addItem(tr("Dotted"));
    lineStyleCombo->setCurrentIndex(
        static_cast<int>(viewport_->style().cellLineStyle));
    lineStyleCombo->setToolTip(
        tr("How the cell edges are stroked.\n\n"
           "A broken stroke is the convention for a boundary that is a "
           "CONSTRUCTION rather than a physical object: the cell edge is a "
           "choice of origin and axes, not a wall, and a dashed box says so "
           "without a caption. It also stops the wireframe competing with the "
           "bonds it crosses in a dense structure.\n\n"
           "Each edge is cut into a whole number of marks, so the pattern "
           "always starts and ends exactly on a corner and a small cell reads "
           "the same as a large one."));
    form->addRow(tr("Cell line style:"), lineStyleCombo);
    connect(lineStyleCombo, &QComboBox::currentIndexChanged, this,
            [this](int index) {
                viewport_->style().cellLineStyle =
                    static_cast<render::CellLineStyle>(index);
                // The break is cut into the geometry, so this is a rebuild.
                viewport_->styleChanged(true);
            });

    auto* cellWidthSpin = new QDoubleSpinBox(this);
    cellWidthSpin->setRange(1.0, 8.0);
    cellWidthSpin->setSingleStep(0.5);
    cellWidthSpin->setValue(viewport_->style().cellLineWidth);
    cellWidthSpin->setToolTip(tr("1 = thin lines; larger values render lit tubes"));
    form->addRow(tr("Cell line width:"), cellWidthSpin);
    connect(cellWidthSpin, &QDoubleSpinBox::valueChanged, this, [this](double value) {
        viewport_->style().cellLineWidth = static_cast<float>(value);
        viewport_->styleChanged(true);
    });

    // -- Filled cell ------------------------------------------------------
    //
    // Below the two line settings, because the order on the page is the order
    // of the box's parts: how its edges are stroked, then how its interior is
    // shaded. Both rows follow the fill toggle above and are greyed out with
    // it — a colour nothing is drawn in is a control with no effect.
    auto* fillColorButton = new QPushButton(this);
    fillColorButton->setFixedHeight(22);
    setButtonColor(fillColorButton, viewport_->style().cellFillColor);
    fillColorButton->setToolTip(
        tr("Tint of the filled faces. Kept separate from \"Cell color\" on "
           "purpose: the edge colour is chosen to READ against the atoms, "
           "while the fill is chosen to stay behind them, and one value "
           "cannot do both."));
    auto* fillColorLabel = new QLabel(tr("Cell fill color:"), this);
    form->addRow(fillColorLabel, fillColorButton);
    connect(fillColorButton, &QPushButton::clicked, this, [this, fillColorButton] {
        const QColor chosen = QColorDialog::getColor(
            viewport_->style().cellFillColor, this, tr("Unit Cell Fill Color"));
        if (!chosen.isValid())
            return;
        setButtonColor(fillColorButton, chosen);
        viewport_->style().cellFillColor = chosen;
        // The tint is baked into the face vertices, so this is a rebuild.
        viewport_->styleChanged(true);
    });

    // Slider (coarse) + spin box (exact), bidirectionally synced — the same
    // pairing the Vectors tab uses for its continuous quantities.
    auto* alphaRow = new QWidget(this);
    auto* alphaLayout = new QHBoxLayout(alphaRow);
    alphaLayout->setContentsMargins(0, 0, 0, 0);
    auto* alphaSlider = new QSlider(Qt::Horizontal, alphaRow);
    alphaSlider->setRange(0, 100); // percent
    auto* alphaSpin = new QSpinBox(alphaRow);
    alphaSpin->setRange(0, 100);
    alphaSpin->setSuffix(tr(" %"));
    const int alphaPercent =
        static_cast<int>(std::lround(viewport_->style().cellFillAlpha * 100.0f));
    alphaSlider->setValue(alphaPercent);
    alphaSpin->setValue(alphaPercent);
    alphaRow->setToolTip(
        tr("Opacity of the filled faces. Low values are the useful range: the "
           "fill exists to say where the box is, and much above ~30% it starts "
           "washing out the structure it is drawn around.\n\n"
           "The fill never occludes the atoms whatever this is set to — it "
           "blends without writing depth."));
    alphaLayout->addWidget(alphaSlider, 1);
    alphaLayout->addWidget(alphaSpin);
    auto* alphaLabel = new QLabel(tr("Cell fill opacity:"), this);
    form->addRow(alphaLabel, alphaRow);
    // Opacity is a shader uniform rather than baked geometry, so it repaints
    // instead of rebuilding — which is what keeps dragging the slider smooth.
    connect(alphaSlider, &QSlider::valueChanged, this, [this, alphaSpin](int percent) {
        {
            const QSignalBlocker blocker(alphaSpin);
            alphaSpin->setValue(percent);
        }
        viewport_->style().cellFillAlpha = static_cast<float>(percent) / 100.0f;
        viewport_->styleChanged(false);
    });
    connect(alphaSpin, &QSpinBox::valueChanged, this, [this, alphaSlider](int percent) {
        {
            const QSignalBlocker blocker(alphaSlider);
            alphaSlider->setValue(percent);
        }
        viewport_->style().cellFillAlpha = static_cast<float>(percent) / 100.0f;
        viewport_->styleChanged(false);
    });

    // The faces are always in the vertex buffer; the toggle only decides
    // whether the draw happens, so it repaints rather than rebuilding.
    const auto syncFillControls = [fillColorLabel, fillColorButton, alphaLabel,
                                   alphaRow](bool on) {
        for (QWidget* widget : {static_cast<QWidget*>(fillColorLabel),
                                static_cast<QWidget*>(fillColorButton),
                                static_cast<QWidget*>(alphaLabel), alphaRow})
            widget->setEnabled(on);
    };
    syncFillControls(fillButton->isChecked());
    connect(fillButton, &QPushButton::toggled, this,
            [this, syncFillControls](bool on) {
                viewport_->style().fillCell = on;
                syncFillControls(on);
                viewport_->styleChanged(false);
            });

    form->addRow(new QWidget(this)); // trailing spacer keeps the rows top-aligned
}

AxesTriadPanel::AxesTriadPanel(ViewportWidget* viewport, QWidget* parent)
    : QWidget(parent)
    , viewport_(viewport)
{
    auto* form = new QFormLayout(this);

    auto* axesCheck = new QCheckBox(tr("Show axes triad"), this);
    axesCheck->setChecked(true);
    form->addRow(axesCheck);
    connect(axesCheck, &QCheckBox::toggled, viewport_, &ViewportWidget::setShowAxes);

    // Directly below "Show axes triad" and styled the same way — it is a
    // refinement of that triad, not an independent overlay.
    // "Show arrowheads", not "Show axes triad with arrows": this row sits
    // directly under "Show axes triad" and is disabled with it, so restating
    // what it belongs to only made the label long enough to wrap.
    auto* axesArrowsCheck = new QCheckBox(tr("Show arrowheads"), this);
    axesArrowsCheck->setChecked(viewport_->showAxesArrows());
    axesArrowsCheck->setToolTip(
        tr("Draw arrowheads at the tips of X, Y and Z. Useful when a figure "
           "must state axis direction unambiguously; plain segments read more "
           "cleanly at small triad sizes."));
    form->addRow(axesArrowsCheck);
    connect(axesArrowsCheck, &QCheckBox::toggled,
            viewport_, &ViewportWidget::setShowAxesArrows);
    // Arrowheads are part of the triad: hiding the triad disables the option.
    axesArrowsCheck->setEnabled(axesCheck->isChecked());
    connect(axesCheck, &QCheckBox::toggled,
            axesArrowsCheck, &QCheckBox::setEnabled);

    auto* axesModeCombo = new QComboBox(this);
    axesModeCombo->addItems({tr("Cartesian (X, Y, Z)"),
                             tr("Lattice vectors (a1, a2, a3)")});
    form->addRow(tr("Axes style:"), axesModeCombo);
    connect(axesModeCombo, &QComboBox::currentIndexChanged, this, [this](int index) {
        viewport_->setAxesLatticeMode(index == 1);
    });

    // Axes triad size: slider + spinbox, bidirectionally synced.
    auto* sizeRow = new QWidget(this);
    auto* sizeLayout = new QHBoxLayout(sizeRow);
    sizeLayout->setContentsMargins(0, 0, 0, 0);
    auto* sizeSlider = new QSlider(Qt::Horizontal, sizeRow);
    sizeSlider->setRange(48, 240); // px
    sizeSlider->setValue(viewport_->axesSize());
    auto* sizeSpin = new QSpinBox(sizeRow);
    sizeSpin->setRange(48, 240);
    sizeSpin->setValue(viewport_->axesSize());
    sizeSpin->setSuffix(tr(" px"));
    sizeLayout->addWidget(sizeSlider, 1);
    sizeLayout->addWidget(sizeSpin);
    form->addRow(tr("Axes size:"), sizeRow);
    connect(sizeSlider, &QSlider::valueChanged, this, [this, sizeSpin](int px) {
        {
            const QSignalBlocker blocker(sizeSpin);
            sizeSpin->setValue(px);
        }
        viewport_->setAxesSize(px);
    });
    connect(sizeSpin, &QSpinBox::valueChanged, this, [this, sizeSlider](int px) {
        {
            const QSignalBlocker blocker(sizeSlider);
            sizeSlider->setValue(px);
        }
        viewport_->setAxesSize(px);
    });
}


VectorsPanel::VectorsPanel(ViewportWidget* viewport, QWidget* parent)
    : QWidget(parent), viewport_(viewport)
{
    auto* form = new QFormLayout(this);

    overlayCombo_ = new QComboBox(this);
    // Order matches render::VectorOverlay.
    overlayCombo_->addItem(tr("None"));
    overlayCombo_->addItem(tr("Velocity"));
    overlayCombo_->addItem(tr("Force"));
    overlayCombo_->addItem(tr("Magnetic moment"));
    overlayCombo_->addItem(tr("Initial magnetic moments"));
    overlayCombo_->setCurrentIndex(
        static_cast<int>(viewport_->style().vectorOverlay));
    form->addRow(tr("Vector overlay:"), overlayCombo_);
    connect(overlayCombo_, &QComboBox::currentIndexChanged, this, [this](int index) {
        viewport_->style().vectorOverlay =
            static_cast<render::VectorOverlay>(index);
        syncColorButton();
        viewport_->styleChanged(true);
    });

    // Slider (coarse) + spin box (exact), bidirectionally synced.
    auto* scaleRow = new QWidget(this);
    auto* scaleLayout = new QHBoxLayout(scaleRow);
    scaleLayout->setContentsMargins(0, 0, 0, 0);
    scaleSlider_ = new QSlider(Qt::Horizontal, scaleRow);
    scaleSlider_->setRange(10, 1000); // x0.1 .. x10.0 in hundredths
    scaleSpin_ = new QDoubleSpinBox(scaleRow);
    scaleSpin_->setRange(0.1, 10.0);
    scaleSpin_->setDecimals(2);
    scaleSpin_->setSingleStep(0.1);
    scaleSpin_->setSuffix(QStringLiteral("×"));
    const double current = viewport_->style().vectorScale;
    scaleSlider_->setValue(static_cast<int>(std::lround(current * 100.0)));
    scaleSpin_->setValue(current);
    scaleSpin_->setToolTip(
        tr("Arrow length relative to the calibrated baseline (1.0×), which is "
           "half an Å of arrow per field unit\n"
           "(eV/Å for forces, Å/fs·√(amu) for velocities, μB for magnetic "
           "moments). Velocities keep an extra 20× so they stay visible."));
    scaleLayout->addWidget(scaleSlider_, 1);
    scaleLayout->addWidget(scaleSpin_);
    form->addRow(tr("Vector scale:"), scaleRow);

    connect(scaleSlider_, &QSlider::valueChanged, this, [this](int hundredths) {
        const float factor = static_cast<float>(hundredths) / 100.0f;
        {
            const QSignalBlocker blocker(scaleSpin_);
            scaleSpin_->setValue(factor);
        }
        viewport_->style().vectorScale = factor;
        viewport_->styleChanged(true);
    });
    connect(scaleSpin_, &QDoubleSpinBox::valueChanged, this, [this](double factor) {
        {
            const QSignalBlocker blocker(scaleSlider_);
            scaleSlider_->setValue(static_cast<int>(std::lround(factor * 100.0)));
        }
        viewport_->style().vectorScale = static_cast<float>(factor);
        viewport_->styleChanged(true);
    });

    // Directly below the scale, because the two together are the arrow's
    // geometry: how long it is and how thick.
    auto* widthRow = new QWidget(this);
    auto* widthLayout = new QHBoxLayout(widthRow);
    widthLayout->setContentsMargins(0, 0, 0, 0);
    widthSlider_ = new QSlider(Qt::Horizontal, widthRow);
    widthSlider_->setRange(10, 500); // x0.1 .. x5.0 in hundredths
    widthSpin_ = new QDoubleSpinBox(widthRow);
    widthSpin_->setRange(0.1, 5.0);
    widthSpin_->setDecimals(2);
    widthSpin_->setSingleStep(0.1);
    widthSpin_->setSuffix(QStringLiteral("×"));
    const double currentWidth = viewport_->style().vectorWidth;
    widthSlider_->setValue(static_cast<int>(std::lround(currentWidth * 100.0)));
    widthSpin_->setValue(currentWidth);
    widthRow->setToolTip(
        tr("Arrow thickness, relative to the calibrated baseline (1.0×). The "
           "head scales with it, so the arrow stays proportioned.\n\n"
           "This is what a dense magnetic structure wants: thin arrows stay "
           "legible where thick ones merge into a mat, without giving up the "
           "heads that say which way each one points."));
    widthLayout->addWidget(widthSlider_, 1);
    widthLayout->addWidget(widthSpin_);
    form->addRow(tr("Vector width:"), widthRow);

    connect(widthSlider_, &QSlider::valueChanged, this, [this](int hundredths) {
        const float factor = static_cast<float>(hundredths) / 100.0f;
        {
            const QSignalBlocker blocker(widthSpin_);
            widthSpin_->setValue(factor);
        }
        viewport_->style().vectorWidth = factor;
        viewport_->styleChanged(true);
    });
    connect(widthSpin_, &QDoubleSpinBox::valueChanged, this, [this](double factor) {
        {
            const QSignalBlocker blocker(widthSlider_);
            widthSlider_->setValue(static_cast<int>(std::lround(factor * 100.0)));
        }
        viewport_->style().vectorWidth = static_cast<float>(factor);
        viewport_->styleChanged(true);
    });

    colorButton_ = new QPushButton(this);
    colorButton_->setToolTip(
        tr("Arrow color for the selected overlay. Each property (velocity, "
           "force, magnetic moment) remembers its own."));
    form->addRow(tr("Vector color:"), colorButton_);
    connect(colorButton_, &QPushButton::clicked, this, [this] {
        QColor* target = overlayColor();
        if (!target)
            return;
        const QColor chosen =
            QColorDialog::getColor(*target, this, tr("Vector Overlay Color"));
        if (!chosen.isValid())
            return;
        *target = chosen;
        setButtonColor(colorButton_, chosen);
        viewport_->styleChanged(true); // arrow colours live in the instance buffer
    });


    // The "Draw arrowheads" toggle was removed and heads are now always drawn.
    // A headless arrow is ambiguous about direction, which is the one thing a
    // vector overlay exists to state; the clutter it was there to relieve is
    // answered by "Vector width" above, which thins the whole arrow instead of
    // deleting the part that carries the meaning.

    // The "Hide below:" magnitude filter was removed from this tab. The style
    // field behind it (Style::vectorMinMagnitude) stays at its 0 default, i.e.
    // no filtering, and the renderer still honours it — so the capability is
    // intact for the ray-trace path and for any future UI, it simply has no
    // control here any more.

    // Re-check availability when the frame changes: scrubbing a trajectory can
    // move to a frame that carries different per-atom columns.
    connect(viewport_, &ViewportWidget::structureReplaced, this,
            [this] { refreshAvailability(); });

    refreshAvailability();
    syncColorButton();
}

QColor* VectorsPanel::overlayColor()
{
    auto& style = viewport_->style();
    switch (style.vectorOverlay) {
    case render::VectorOverlay::Velocity: return &style.velocityColor;
    case render::VectorOverlay::Force: return &style.forceColor;
    case render::VectorOverlay::MagneticMoment: return &style.magmomColor;
    case render::VectorOverlay::InitialMagneticMoment:
        return &style.initialMagmomColor;
    case render::VectorOverlay::None: break;
    }
    return nullptr; // nothing is drawn, so there is no colour to edit
}

void VectorsPanel::syncColorButton()
{
    const QColor* color = overlayColor();
    colorButton_->setEnabled(color != nullptr);
    setButtonColor(colorButton_, color ? *color : palette().color(QPalette::Button));
    if (!color)
        colorButton_->setToolTip(
            tr("Select a vector overlay above to choose its arrow color."));
}

void VectorsPanel::refreshAvailability()
{
    const auto held = viewport_->structure();
    const core::Structure* structure = held ? held.get() : nullptr;

    // Grey out entries the frame has no data for rather than hiding them: a
    // fixed list keeps the indices aligned with render::VectorOverlay, and the
    // disabled tooltip explains what is missing instead of silently offering
    // nothing.
    const QSignalBlocker blocker(overlayCombo_);
    auto* model = qobject_cast<QStandardItemModel*>(overlayCombo_->model());
    bool currentStillValid = true;
    for (int i = 1; i < overlayCombo_->count(); ++i) {
        const auto overlay = static_cast<render::VectorOverlay>(i);
        const std::string field = render::vectorFieldName(overlay);
        // hasVectorData, not vectorFields(): a collinear magnetic moment is a
        // scalar column that the renderer promotes to (0, 0, m). Testing only
        // for a vector field disabled "Magnetic moment" for every collinear
        // spin-polarized result — the majority of them — with a tool tip
        // saying the frame carried no such data when it plainly did.
        const bool available = structure && structure->hasVectorData(field);
        if (model) {
            if (QStandardItem* item = model->item(i)) {
                item->setEnabled(available);
                item->setToolTip(
                    available ? QString()
                              : tr("This frame carries no per-atom \"%1\" data.")
                                    .arg(QString::fromStdString(field)));
            }
        }
        if (!available && overlayCombo_->currentIndex() == i)
            currentStillValid = false;
    }
    if (!currentStillValid) {
        overlayCombo_->setCurrentIndex(0);
        viewport_->style().vectorOverlay = render::VectorOverlay::None;
        syncColorButton();
        viewport_->styleChanged(true);
    }
}

} // namespace calango::gui
