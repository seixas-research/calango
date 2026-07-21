#include "gui/VisualEffectsDialog.hpp"

#include "gui/ViewportWidget.hpp"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QSlider>
#include <QVBoxLayout>

#include <algorithm>

namespace calango::gui {

VisualEffectsDialog::VisualEffectsDialog(ViewportWidget* viewport, QWidget* parent)
    : QDialog(parent)
    , viewport_(viewport)
{
    setWindowTitle(tr("Visual Effects"));
    auto* layout = new QVBoxLayout(this);
    auto& style = viewport_->style();

    // --- Distance fog ------------------------------------------------------
    auto* fogGroup = new QGroupBox(tr("Distance fog"), this);
    fogGroup->setCheckable(true);
    fogGroup->setChecked(style.fogMode != 0);
    auto* fogForm = new QFormLayout(fogGroup);

    auto* modeCombo = new QComboBox(fogGroup);
    modeCombo->addItems({tr("Linear (start → end)"),
                         tr("Exponential (density)")});
    modeCombo->setCurrentIndex(style.fogMode == 2 ? 1 : 0);
    fogForm->addRow(tr("Mode:"), modeCombo);

    auto* startSpin = new QDoubleSpinBox(fogGroup);
    startSpin->setRange(0.0, 500.0);
    startSpin->setSuffix(QStringLiteral(" Å"));
    startSpin->setValue(style.fogStart);
    fogForm->addRow(tr("Start distance:"), startSpin);

    auto* endSpin = new QDoubleSpinBox(fogGroup);
    endSpin->setRange(1.0, 1000.0);
    endSpin->setSuffix(QStringLiteral(" Å"));
    endSpin->setValue(style.fogEnd);
    fogForm->addRow(tr("End distance:"), endSpin);

    auto* densitySpin = new QDoubleSpinBox(fogGroup);
    densitySpin->setRange(0.001, 0.5);
    densitySpin->setDecimals(3);
    densitySpin->setSingleStep(0.005);
    densitySpin->setValue(style.fogDensity);
    fogForm->addRow(tr("Density:"), densitySpin);
    layout->addWidget(fogGroup);

    const auto applyFog = [this, fogGroup, modeCombo, startSpin, endSpin,
                           densitySpin] {
        auto& s = viewport_->style();
        s.fogMode = !fogGroup->isChecked() ? 0
            : modeCombo->currentIndex() == 0 ? 1
                                             : 2;
        s.fogStart = static_cast<float>(startSpin->value());
        s.fogEnd = static_cast<float>(
            std::max(endSpin->value(), startSpin->value() + 1.0));
        s.fogDensity = static_cast<float>(densitySpin->value());
        viewport_->update();
    };
    connect(fogGroup, &QGroupBox::toggled, this, applyFog);
    connect(modeCombo, &QComboBox::currentIndexChanged, this, applyFog);
    connect(startSpin, &QDoubleSpinBox::valueChanged, this, applyFog);
    connect(endSpin, &QDoubleSpinBox::valueChanged, this, applyFog);
    connect(densitySpin, &QDoubleSpinBox::valueChanged, this, applyFog);

    // --- Depth of field ----------------------------------------------------
    auto* dofGroup = new QGroupBox(tr("Depth blur (depth of field)"), this);
    dofGroup->setCheckable(true);
    dofGroup->setChecked(viewport_->depthOfField().enabled);
    dofGroup->setToolTip(tr("Blurs geometry away from the focal plane.\n"
                            "While enabled the scene renders without MSAA."));
    auto* dofForm = new QFormLayout(dofGroup);

    auto* strengthSlider = new QSlider(Qt::Horizontal, dofGroup);
    strengthSlider->setRange(1, 20);
    strengthSlider->setValue(
        static_cast<int>(viewport_->depthOfField().strength));
    dofForm->addRow(tr("Blur strength:"), strengthSlider);

    auto* rangeSpin = new QDoubleSpinBox(dofGroup);
    rangeSpin->setRange(1.0, 200.0);
    rangeSpin->setSuffix(QStringLiteral(" Å"));
    rangeSpin->setValue(viewport_->depthOfField().focusRange);
    rangeSpin->setToolTip(tr("Depth band around the focal plane that "
                             "stays sharp"));
    dofForm->addRow(tr("Focus range:"), rangeSpin);

    auto* offsetSpin = new QDoubleSpinBox(dofGroup);
    offsetSpin->setRange(-200.0, 200.0);
    offsetSpin->setSuffix(QStringLiteral(" Å"));
    offsetSpin->setValue(viewport_->depthOfField().focusOffset);
    offsetSpin->setToolTip(tr("Shift of the focal plane relative to the "
                              "camera target"));
    dofForm->addRow(tr("Focus offset:"), offsetSpin);
    layout->addWidget(dofGroup);

    const auto applyDof = [this, dofGroup, strengthSlider, rangeSpin,
                           offsetSpin] {
        auto& dof = viewport_->depthOfField();
        dof.enabled = dofGroup->isChecked();
        dof.strength = static_cast<float>(strengthSlider->value());
        dof.focusRange = static_cast<float>(rangeSpin->value());
        dof.focusOffset = static_cast<float>(offsetSpin->value());
        viewport_->update();
    };
    connect(dofGroup, &QGroupBox::toggled, this, applyDof);
    connect(strengthSlider, &QSlider::valueChanged, this, applyDof);
    connect(rangeSpin, &QDoubleSpinBox::valueChanged, this, applyDof);
    connect(offsetSpin, &QDoubleSpinBox::valueChanged, this, applyDof);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);
}

} // namespace calango::gui
