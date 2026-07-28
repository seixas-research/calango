#include "gui/CellAxesTabs.hpp"

#include "core/Structure.hpp"
#include "gui/GuiUtils.hpp"
#include "gui/NeighborCellsDialog.hpp"

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

    auto* cellShowCheck = new QCheckBox(tr("Show unit cell"), this);
    cellShowCheck->setChecked(viewport_->style().showCell);
    form->addRow(cellShowCheck);
    connect(cellShowCheck, &QCheckBox::toggled,
            viewport_, &ViewportWidget::setShowCell);

    auto* ghostCheck =
        new QCheckBox(tr("Show atoms of the neighboring unit cell"), this);
    ghostCheck->setChecked(viewport_->style().showNeighborCellAtoms);
    ghostCheck->setToolTip(
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
    form->addRow(ghostCheck);
    connect(ghostCheck, &QCheckBox::toggled, this, [this](bool on) {
        viewport_->style().showNeighborCellAtoms = on;
        // Extra instances — the geometry buffers must be rebuilt.
        viewport_->styleChanged(true);
    });

    // Directly under the bond-completion toggle above, because the two are the
    // periodic-image controls and differ only in scale: that one completes the
    // bonds that leave the cell, this one repeats the whole cell.
    auto* neighborCellsButton =
        new QPushButton(tr("Show neighboring cells…"), this);
    neighborCellsButton->setToolTip(
        tr("Draw the periodic images of the cell over a range of fractional "
           "coordinates — e.g. x from 0 to 2 adds the neighboring cell along "
           "+x, with its atoms and bonds.\n\n"
           "Purely visual: the atom count, the formula and every exported "
           "structure file are unchanged."));
    form->addRow(neighborCellsButton);
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
        const bool available =
            structure && structure->vectorFields().count(field) > 0;
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
