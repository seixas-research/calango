#include "gui/CellAxesPanel.hpp"
#include "gui/GuiUtils.hpp"

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

namespace calango::gui {

namespace {

} // namespace

CellAxesPanel::CellAxesPanel(ViewportWidget* viewport, QWidget* parent)
    : QWidget(parent)
    , viewport_(viewport)
{
    auto* form = new QFormLayout(this);

    auto* cellShowCheck = new QCheckBox(tr("Show unit cell"), this);
    cellShowCheck->setChecked(viewport_->style().showCell);
    form->addRow(cellShowCheck);
    connect(cellShowCheck, &QCheckBox::toggled,
            viewport_, &ViewportWidget::setShowCell);

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

    auto* axesCheck = new QCheckBox(tr("Show axes triad"), this);
    axesCheck->setChecked(true);
    form->addRow(axesCheck);
    connect(axesCheck, &QCheckBox::toggled, viewport_, &ViewportWidget::setShowAxes);

    // Directly below "Show axes triad" and styled the same way — it is a
    // refinement of that triad, not an independent overlay.
    auto* axesArrowsCheck = new QCheckBox(tr("Show axes triad with arrows"), this);
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

} // namespace calango::gui
