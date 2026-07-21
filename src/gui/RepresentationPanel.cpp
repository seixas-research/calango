#include "gui/RepresentationPanel.hpp"

#include "gui/ElementSettingsDialog.hpp"
#include "gui/ViewportWidget.hpp"

#include <QColorDialog>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QSignalBlocker>
#include <QVBoxLayout>

#include <cmath>
#include <functional>

namespace calango::gui {

namespace {

void setButtonColor(QPushButton* button, const QColor& color)
{
    button->setStyleSheet(QStringLiteral("background-color: %1; border: 1px solid #666;")
                              .arg(color.name()));
}

} // namespace

RepresentationPanel::RepresentationPanel(ViewportWidget* viewport, QWidget* parent)
    : QWidget(parent)
    , viewport_(viewport)
{
    auto* form = new QFormLayout(this);

    modeCombo_ = new QComboBox(this);
    modeCombo_->addItems({tr("Ball-and-Stick"), tr("Space-filling (CPK)"),
                          tr("Wireframe")});
    form->addRow(tr("Mode:"), modeCombo_);
    connect(modeCombo_, &QComboBox::currentIndexChanged, this, [this](int index) {
        viewport_->setRepresentation(static_cast<render::RepresentationMode>(index));
    });

    // --- Atom coloring -----------------------------------------------------
    colorModeCombo_ = new QComboBox(this);
    colorModeCombo_->addItems({tr("Element (CPK)"),
                               tr("Coordination number (CN)"),
                               tr("Generalized CN (GCN)"),
                               tr("Custom property")});
    form->addRow(tr("Color by:"), colorModeCombo_);
    connect(colorModeCombo_, &QComboBox::currentIndexChanged,
            this, &RepresentationPanel::applyColorMode);

    gradientCombo_ = new QComboBox(this);
    gradientCombo_->addItems({tr("Viridis"), tr("Plasma"), tr("Turbo")});
    form->addRow(tr("Gradient:"), gradientCombo_);
    connect(gradientCombo_, &QComboBox::currentIndexChanged, this, [this](int index) {
        viewport_->setColorGradient(static_cast<render::ColorGradient>(index));
    });

    propertyCombo_ = new QComboBox(this);
    propertyCombo_->setToolTip(tr("Per-atom scalar fields of the current structure\n"
                                  "(charges, |forces|, extxyz columns, ...)"));
    form->addRow(tr("Property:"), propertyCombo_);
    connect(propertyCombo_, &QComboBox::currentIndexChanged,
            this, &RepresentationPanel::applyColorMode);

    rangeLabel_ = new QLabel(this);
    form->addRow(tr("Range:"), rangeLabel_);

    connect(viewport_, &ViewportWidget::structureReplaced,
            this, &RepresentationPanel::refreshPropertyList);
    connect(viewport_, &ViewportWidget::colorMappingChanged,
            this, &RepresentationPanel::syncColoringFromViewport);

    // --- Scales ------------------------------------------------------------
    // Slider for coarse adjustment + spinbox for exact typed values,
    // bidirectionally synced (both drive the same style factor).
    const auto makeScaleRow = [this](QSlider*& slider, QDoubleSpinBox*& spin,
                                     const std::function<void(float)>& apply) {
        auto* row = new QWidget(this);
        auto* rowLayout = new QHBoxLayout(row);
        rowLayout->setContentsMargins(0, 0, 0, 0);
        slider = new QSlider(Qt::Horizontal, row);
        slider->setRange(20, 300); // percent
        slider->setValue(100);
        spin = new QDoubleSpinBox(row);
        spin->setRange(0.20, 3.00);
        spin->setDecimals(2);
        spin->setSingleStep(0.05);
        spin->setValue(1.00);
        spin->setSuffix(QStringLiteral("×"));
        rowLayout->addWidget(slider, 1);
        rowLayout->addWidget(spin);

        connect(slider, &QSlider::valueChanged, this, [spin, apply](int percent) {
            const float factor = static_cast<float>(percent) / 100.0f;
            {
                const QSignalBlocker blocker(spin);
                spin->setValue(factor);
            }
            apply(factor);
        });
        connect(spin, &QDoubleSpinBox::valueChanged, this,
                [slider = slider, apply](double factor) {
                    const QSignalBlocker blocker(slider);
                    slider->setValue(static_cast<int>(std::lround(factor * 100.0)));
                    apply(static_cast<float>(factor));
                });
        return row;
    };

    form->addRow(tr("Atom radius:"),
                 makeScaleRow(atomScaleSlider_, atomScaleSpin_, [this](float factor) {
                     viewport_->style().atomScaleFactor = factor;
                     viewport_->styleChanged(true);
                 }));
    form->addRow(tr("Bond width:"),
                 makeScaleRow(bondWidthSlider_, bondWidthSpin_, [this](float factor) {
                     viewport_->style().bondWidthFactor = factor;
                     viewport_->styleChanged(true);
                 }));

    gradientBondsCheck_ = new QCheckBox(tr("Gradient bond coloring"), this);
    gradientBondsCheck_->setChecked(viewport_->style().gradientBonds);
    gradientBondsCheck_->setToolTip(tr("Blend each bond smoothly from one atom's "
                                       "color to the other's\ninstead of the classic "
                                       "half-and-half split"));
    form->addRow(gradientBondsCheck_);
    connect(gradientBondsCheck_, &QCheckBox::toggled, this, [this](bool on) {
        viewport_->style().gradientBonds = on;
        viewport_->styleChanged(true);
    });

    // --- Force / velocity vector arrows ------------------------------------
    forcesCheck_ = new QCheckBox(tr("Force vectors"), this);
    velocitiesCheck_ = new QCheckBox(tr("Velocity vectors"), this);
    form->addRow(forcesCheck_);
    form->addRow(velocitiesCheck_);
    connect(forcesCheck_, &QCheckBox::toggled, this, [this](bool on) {
        viewport_->style().showForces = on;
        viewport_->styleChanged(true);
    });
    connect(velocitiesCheck_, &QCheckBox::toggled, this, [this](bool on) {
        viewport_->style().showVelocities = on;
        viewport_->styleChanged(true);
    });

    // Arrow scale: slider (coarse) + spinbox (exact), Å per field unit.
    auto* vectorRow = new QWidget(this);
    auto* vectorLayout = new QHBoxLayout(vectorRow);
    vectorLayout->setContentsMargins(0, 0, 0, 0);
    vectorScaleSlider_ = new QSlider(Qt::Horizontal, vectorRow);
    vectorScaleSlider_->setRange(5, 2000); // ×0.05 .. ×20 in hundredths
    vectorScaleSlider_->setValue(100);
    vectorScaleSpin_ = new QDoubleSpinBox(vectorRow);
    vectorScaleSpin_->setRange(0.05, 20.0);
    vectorScaleSpin_->setDecimals(2);
    vectorScaleSpin_->setSingleStep(0.05);
    vectorScaleSpin_->setValue(1.00);
    vectorScaleSpin_->setSuffix(QStringLiteral("×"));
    vectorScaleSpin_->setToolTip(tr("Arrow length in Å per field unit\n"
                                    "(eV/Å for forces, Å/fs·√(amu) units for "
                                    "velocities)"));
    vectorLayout->addWidget(vectorScaleSlider_, 1);
    vectorLayout->addWidget(vectorScaleSpin_);
    form->addRow(tr("Vector scale:"), vectorRow);
    connect(vectorScaleSlider_, &QSlider::valueChanged, this, [this](int hundredths) {
        const float factor = static_cast<float>(hundredths) / 100.0f;
        {
            const QSignalBlocker blocker(vectorScaleSpin_);
            vectorScaleSpin_->setValue(factor);
        }
        viewport_->style().vectorScale = factor;
        viewport_->styleChanged(true);
    });
    connect(vectorScaleSpin_, &QDoubleSpinBox::valueChanged, this, [this](double factor) {
        {
            const QSignalBlocker blocker(vectorScaleSlider_);
            vectorScaleSlider_->setValue(static_cast<int>(std::lround(factor * 100.0)));
        }
        viewport_->style().vectorScale = static_cast<float>(factor);
        viewport_->styleChanged(true);
    });

    auto* elementsButton = new QPushButton(tr("Element Settings…"), this);
    form->addRow(elementsButton);
    connect(elementsButton, &QPushButton::clicked, this, [this] {
        ElementSettingsDialog dialog(viewport_, this);
        dialog.exec();
    });

    auto* backgroundButton = new QPushButton(this);
    backgroundButton->setFixedHeight(22);
    setButtonColor(backgroundButton, viewport_->backgroundColor());
    form->addRow(tr("Background:"), backgroundButton);
    connect(backgroundButton, &QPushButton::clicked, this, [this, backgroundButton] {
        const QColor chosen = QColorDialog::getColor(
            viewport_->backgroundColor(), this, tr("Viewport Background Color"));
        if (!chosen.isValid())
            return;
        setButtonColor(backgroundButton, chosen);
        viewport_->setBackgroundColor(chosen);
    });

    refreshPropertyList();
    syncColoringFromViewport();
}

void RepresentationPanel::applyColorMode()
{
    const auto mode = static_cast<render::ColorMode>(colorModeCombo_->currentIndex());
    const bool custom = mode == render::ColorMode::CustomScalar;
    viewport_->setColorMode(mode, custom ? propertyCombo_->currentText() : QString());
}

void RepresentationPanel::refreshPropertyList()
{
    const QSignalBlocker blocker(propertyCombo_);
    const QString previous = propertyCombo_->currentText();
    propertyCombo_->clear();
    bool hasForces = false;
    bool hasVelocities = false;
    if (const auto structure = viewport_->structure()) {
        for (const auto& [name, values] : structure->scalarFields()) {
            (void)values;
            propertyCombo_->addItem(QString::fromStdString(name));
        }
        const auto& vectors = structure->vectorFields();
        hasForces = vectors.count("forces") > 0;
        hasVelocities = vectors.count("velocities") > 0;
    }
    const int index = propertyCombo_->findText(previous);
    if (index >= 0)
        propertyCombo_->setCurrentIndex(index);

    // Arrow toggles only make sense when the structure carries the data.
    forcesCheck_->setEnabled(hasForces);
    forcesCheck_->setToolTip(hasForces
                                 ? QString()
                                 : tr("No per-atom \"forces\" data in this "
                                      "structure (load an extxyz with a "
                                      "forces column)"));
    velocitiesCheck_->setEnabled(hasVelocities);
    velocitiesCheck_->setToolTip(hasVelocities
                                     ? QString()
                                     : tr("No velocities/momenta in this "
                                          "structure (e.g. an MD .traj "
                                          "frame)"));
}

void RepresentationPanel::syncColoringFromViewport()
{
    // The mapping can also be driven from outside (Coordination Analysis
    // dialog) — mirror the viewport state without re-triggering it.
    const auto mode = viewport_->colorMode();
    {
        const QSignalBlocker blocker(colorModeCombo_);
        colorModeCombo_->setCurrentIndex(static_cast<int>(mode));
    }
    if (mode == render::ColorMode::CustomScalar) {
        const QSignalBlocker blocker(propertyCombo_);
        const int index = propertyCombo_->findText(viewport_->customScalarField());
        if (index >= 0)
            propertyCombo_->setCurrentIndex(index);
    }

    const bool scalarMode = mode != render::ColorMode::Element;
    gradientCombo_->setEnabled(scalarMode);
    propertyCombo_->setEnabled(mode == render::ColorMode::CustomScalar);
    rangeLabel_->setEnabled(scalarMode);

    const auto range = viewport_->scalarRange();
    if (!scalarMode)
        rangeLabel_->setText(tr("—"));
    else if (!range.valid)
        rangeLabel_->setText(tr("no data"));
    else if (mode == render::ColorMode::CoordinationNumber)
        rangeLabel_->setText(QStringLiteral("%1 – %2")
                                 .arg(static_cast<int>(range.min))
                                 .arg(static_cast<int>(range.max)));
    else
        rangeLabel_->setText(QStringLiteral("%1 – %2")
                                 .arg(static_cast<double>(range.min), 0, 'f', 3)
                                 .arg(static_cast<double>(range.max), 0, 'f', 3));
}

} // namespace calango::gui
