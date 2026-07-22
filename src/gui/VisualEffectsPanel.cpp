#include "gui/VisualEffectsPanel.hpp"

#include "gui/LightingPanel.hpp"
#include "gui/ViewportWidget.hpp"

#include <QColorDialog>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QPushButton>
#include <QSlider>
#include <QTabWidget>
#include <QVBoxLayout>

#include <algorithm>

namespace calango::gui {

VisualEffectsPanel::VisualEffectsPanel(ViewportWidget* viewport, QWidget* parent)
    : QWidget(parent), viewport_(viewport)
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    auto* tabs = new QTabWidget(this);
    tabs->addTab(new LightingPanel(viewport_, tabs), tr("Lighting"));
    tabs->addTab(buildFogTab(), tr("Distance Fog"));
    tabs->addTab(buildDepthBlurTab(), tr("Depth Blur"));
    layout->addWidget(tabs);
}

QWidget* VisualEffectsPanel::buildFogTab()
{
    auto* page = new QWidget(this);
    auto* pageLayout = new QVBoxLayout(page);
    auto& style = viewport_->style();

    auto* group = new QGroupBox(tr("Distance fog"), page);
    group->setCheckable(true);
    group->setChecked(style.fogMode != 0);
    auto* form = new QFormLayout(group);

    auto* modeCombo = new QComboBox(group);
    modeCombo->addItems({tr("Linear (start → end)"), tr("Exponential (density)")});
    modeCombo->setCurrentIndex(style.fogMode == 2 ? 1 : 0);
    form->addRow(tr("Mode:"), modeCombo);

    // Fog color swatch (fades geometry into this color; changing the viewport
    // background re-syncs it).
    auto* colorButton = new QPushButton(group);
    colorButton->setAutoFillBackground(true);
    const auto paintSwatch = [colorButton](const QColor& c) {
        colorButton->setStyleSheet(
            QStringLiteral("background-color: %1; min-height: 18px;").arg(c.name()));
    };
    paintSwatch(style.fogColor);
    form->addRow(tr("Color:"), colorButton);

    auto* startSpin = new QDoubleSpinBox(group);
    startSpin->setRange(0.0, 500.0);
    startSpin->setSuffix(QStringLiteral(" Å"));
    startSpin->setValue(style.fogStart);
    form->addRow(tr("Start distance:"), startSpin);

    auto* endSpin = new QDoubleSpinBox(group);
    endSpin->setRange(1.0, 1000.0);
    endSpin->setSuffix(QStringLiteral(" Å"));
    endSpin->setValue(style.fogEnd);
    form->addRow(tr("End distance:"), endSpin);

    auto* densitySpin = new QDoubleSpinBox(group);
    densitySpin->setRange(0.001, 0.5);
    densitySpin->setDecimals(3);
    densitySpin->setSingleStep(0.005);
    densitySpin->setValue(style.fogDensity);
    form->addRow(tr("Density:"), densitySpin);

    pageLayout->addWidget(group);
    pageLayout->addStretch(1);

    const auto applyFog = [this, group, modeCombo, startSpin, endSpin,
                           densitySpin] {
        auto& s = viewport_->style();
        s.fogMode = !group->isChecked() ? 0 : modeCombo->currentIndex() == 0 ? 1 : 2;
        s.fogStart = static_cast<float>(startSpin->value());
        s.fogEnd = static_cast<float>(
            std::max(endSpin->value(), startSpin->value() + 1.0));
        s.fogDensity = static_cast<float>(densitySpin->value());
        viewport_->update();
    };
    connect(group, &QGroupBox::toggled, this, applyFog);
    connect(modeCombo, &QComboBox::currentIndexChanged, this, applyFog);
    connect(startSpin, &QDoubleSpinBox::valueChanged, this, applyFog);
    connect(endSpin, &QDoubleSpinBox::valueChanged, this, applyFog);
    connect(densitySpin, &QDoubleSpinBox::valueChanged, this, applyFog);
    connect(colorButton, &QPushButton::clicked, this, [this, paintSwatch] {
        const QColor picked = QColorDialog::getColor(
            viewport_->style().fogColor, this, tr("Fog Color"));
        if (picked.isValid()) {
            viewport_->style().fogColor = picked;
            paintSwatch(picked);
            viewport_->update();
        }
    });
    return page;
}

QWidget* VisualEffectsPanel::buildDepthBlurTab()
{
    auto* page = new QWidget(this);
    auto* pageLayout = new QVBoxLayout(page);

    auto* group = new QGroupBox(tr("Depth blur (depth of field)"), page);
    group->setCheckable(true);
    group->setChecked(viewport_->depthOfField().enabled);
    group->setToolTip(tr("Blurs geometry away from the focal plane.\n"
                         "While enabled the scene renders without MSAA."));
    auto* form = new QFormLayout(group);

    auto* strengthSlider = new QSlider(Qt::Horizontal, group);
    strengthSlider->setRange(1, 20);
    strengthSlider->setValue(static_cast<int>(viewport_->depthOfField().strength));
    form->addRow(tr("Blur strength:"), strengthSlider);

    auto* rangeSpin = new QDoubleSpinBox(group);
    rangeSpin->setRange(1.0, 200.0);
    rangeSpin->setSuffix(QStringLiteral(" Å"));
    rangeSpin->setValue(viewport_->depthOfField().focusRange);
    rangeSpin->setToolTip(tr("Depth band around the focal plane that stays sharp"));
    form->addRow(tr("Focus range:"), rangeSpin);

    auto* offsetSpin = new QDoubleSpinBox(group);
    offsetSpin->setRange(-200.0, 200.0);
    offsetSpin->setSuffix(QStringLiteral(" Å"));
    offsetSpin->setValue(viewport_->depthOfField().focusOffset);
    offsetSpin->setToolTip(tr("Shift of the focal plane relative to the camera target"));
    form->addRow(tr("Focus offset:"), offsetSpin);

    pageLayout->addWidget(group);
    pageLayout->addStretch(1);

    const auto applyDof = [this, group, strengthSlider, rangeSpin, offsetSpin] {
        auto& dof = viewport_->depthOfField();
        dof.enabled = group->isChecked();
        dof.strength = static_cast<float>(strengthSlider->value());
        dof.focusRange = static_cast<float>(rangeSpin->value());
        dof.focusOffset = static_cast<float>(offsetSpin->value());
        viewport_->update();
    };
    connect(group, &QGroupBox::toggled, this, applyDof);
    connect(strengthSlider, &QSlider::valueChanged, this, applyDof);
    connect(rangeSpin, &QDoubleSpinBox::valueChanged, this, applyDof);
    connect(offsetSpin, &QDoubleSpinBox::valueChanged, this, applyDof);
    return page;
}

} // namespace calango::gui
