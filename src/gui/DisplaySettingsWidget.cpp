#include "gui/DisplaySettingsWidget.hpp"

#include "gui/ElementSettingsDialog.hpp"
#include "gui/ViewportWidget.hpp"

#include <QCheckBox>
#include <QColorDialog>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QSignalBlocker>
#include <QVBoxLayout>

#include <cmath>
#include <functional>

namespace calango::gui {

DisplaySettingsWidget::DisplaySettingsWidget(ViewportWidget* viewport, QWidget* parent)
    : QWidget(parent)
    , viewport_(viewport)
{
    auto* layout = new QVBoxLayout(this);

    // --- Representation ---------------------------------------------------
    auto* reprGroup = new QGroupBox(tr("Representation"), this);
    auto* reprForm = new QFormLayout(reprGroup);

    modeCombo_ = new QComboBox(reprGroup);
    modeCombo_->addItems({tr("Ball-and-Stick"), tr("Space-filling (CPK)"),
                          tr("Wireframe")});
    reprForm->addRow(tr("Mode:"), modeCombo_);
    connect(modeCombo_, &QComboBox::currentIndexChanged, this, [this](int index) {
        viewport_->setRepresentation(static_cast<render::RepresentationMode>(index));
    });

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

    reprForm->addRow(tr("Atom radius:"),
                     makeScaleRow(atomScaleSlider_, atomScaleSpin_, [this](float factor) {
                         viewport_->style().atomScaleFactor = factor;
                         viewport_->styleChanged(true);
                     }));
    reprForm->addRow(tr("Bond width:"),
                     makeScaleRow(bondWidthSlider_, bondWidthSpin_, [this](float factor) {
                         viewport_->style().bondWidthFactor = factor;
                         viewport_->styleChanged(true);
                     }));

    // --- Atom coloring -----------------------------------------------------
    colorModeCombo_ = new QComboBox(reprGroup);
    colorModeCombo_->addItems({tr("Element (CPK)"),
                               tr("Coordination number (CN)"),
                               tr("Generalized CN (GCN)"),
                               tr("Custom property")});
    reprForm->addRow(tr("Color by:"), colorModeCombo_);
    connect(colorModeCombo_, &QComboBox::currentIndexChanged,
            this, &DisplaySettingsWidget::applyColorMode);

    gradientCombo_ = new QComboBox(reprGroup);
    gradientCombo_->addItems({tr("Viridis"), tr("Plasma"), tr("Turbo")});
    reprForm->addRow(tr("Gradient:"), gradientCombo_);
    connect(gradientCombo_, &QComboBox::currentIndexChanged, this, [this](int index) {
        viewport_->setColorGradient(static_cast<render::ColorGradient>(index));
    });

    propertyCombo_ = new QComboBox(reprGroup);
    propertyCombo_->setToolTip(tr("Per-atom scalar fields of the current structure\n"
                                  "(charges, |forces|, extxyz columns, ...)"));
    reprForm->addRow(tr("Property:"), propertyCombo_);
    connect(propertyCombo_, &QComboBox::currentIndexChanged,
            this, &DisplaySettingsWidget::applyColorMode);

    rangeLabel_ = new QLabel(reprGroup);
    reprForm->addRow(tr("Range:"), rangeLabel_);

    connect(viewport_, &ViewportWidget::structureReplaced,
            this, &DisplaySettingsWidget::refreshPropertyList);
    connect(viewport_, &ViewportWidget::colorMappingChanged,
            this, &DisplaySettingsWidget::syncColoringFromViewport);
    refreshPropertyList();
    syncColoringFromViewport();

    auto* elementsButton = new QPushButton(tr("Element Settings…"), reprGroup);
    reprForm->addRow(elementsButton);
    connect(elementsButton, &QPushButton::clicked, this, [this] {
        ElementSettingsDialog dialog(viewport_, this);
        dialog.exec();
    });

    auto* backgroundButton = new QPushButton(reprGroup);
    backgroundButton->setFixedHeight(22);
    setButtonColor(backgroundButton, viewport_->backgroundColor());
    reprForm->addRow(tr("Background:"), backgroundButton);
    connect(backgroundButton, &QPushButton::clicked, this, [this, backgroundButton] {
        const QColor chosen = QColorDialog::getColor(
            viewport_->backgroundColor(), this, tr("Viewport Background Color"));
        if (!chosen.isValid())
            return;
        setButtonColor(backgroundButton, chosen);
        viewport_->setBackgroundColor(chosen);
    });

    layout->addWidget(reprGroup);

    // --- Overlays: unit cell wireframe + axes triad -------------------------
    auto* overlayGroup = new QGroupBox(tr("Unit Cell && Axes"), this);
    auto* overlayForm = new QFormLayout(overlayGroup);

    auto* cellShowCheck = new QCheckBox(tr("Show unit cell"), overlayGroup);
    cellShowCheck->setChecked(viewport_->style().showCell);
    overlayForm->addRow(cellShowCheck);
    connect(cellShowCheck, &QCheckBox::toggled,
            viewport_, &ViewportWidget::setShowCell);

    auto* cellColorButton = new QPushButton(overlayGroup);
    cellColorButton->setFixedHeight(22);
    setButtonColor(cellColorButton, viewport_->style().cellColor);
    overlayForm->addRow(tr("Cell color:"), cellColorButton);
    connect(cellColorButton, &QPushButton::clicked, this, [this, cellColorButton] {
        const QColor chosen = QColorDialog::getColor(
            viewport_->style().cellColor, this, tr("Unit Cell Wireframe Color"));
        if (!chosen.isValid())
            return;
        setButtonColor(cellColorButton, chosen);
        viewport_->style().cellColor = chosen;
        viewport_->styleChanged(true);
    });

    auto* cellWidthSpin = new QDoubleSpinBox(overlayGroup);
    cellWidthSpin->setRange(1.0, 8.0);
    cellWidthSpin->setSingleStep(0.5);
    cellWidthSpin->setValue(viewport_->style().cellLineWidth);
    cellWidthSpin->setToolTip(tr("1 = thin lines; larger values render lit tubes"));
    overlayForm->addRow(tr("Cell line width:"), cellWidthSpin);
    connect(cellWidthSpin, &QDoubleSpinBox::valueChanged, this, [this](double value) {
        viewport_->style().cellLineWidth = static_cast<float>(value);
        viewport_->styleChanged(true);
    });

    auto* axesCheck = new QCheckBox(tr("Show axes triad"), overlayGroup);
    axesCheck->setChecked(true);
    overlayForm->addRow(axesCheck);
    connect(axesCheck, &QCheckBox::toggled, viewport_, &ViewportWidget::setShowAxes);

    auto* axesModeCombo = new QComboBox(overlayGroup);
    axesModeCombo->addItems({tr("Cartesian (X, Y, Z)"),
                             tr("Lattice vectors (a1, a2, a3)")});
    overlayForm->addRow(tr("Axes style:"), axesModeCombo);
    connect(axesModeCombo, &QComboBox::currentIndexChanged, this, [this](int index) {
        viewport_->setAxesLatticeMode(index == 1);
    });

    layout->addWidget(overlayGroup);

    // --- Lighting ----------------------------------------------------------
    auto* lightGroup = new QGroupBox(tr("Lighting"), this);
    auto* lightLayout = new QVBoxLayout(lightGroup);

    lightList_ = new QListWidget(lightGroup);
    lightList_->setFixedHeight(72);
    lightLayout->addWidget(lightList_);

    auto* lightButtons = new QHBoxLayout;
    addLightButton_ = new QPushButton(tr("Add"), lightGroup);
    removeLightButton_ = new QPushButton(tr("Remove"), lightGroup);
    lightButtons->addWidget(addLightButton_);
    lightButtons->addWidget(removeLightButton_);
    lightButtons->addStretch(1);
    lightLayout->addLayout(lightButtons);
    connect(addLightButton_, &QPushButton::clicked, this, &DisplaySettingsWidget::addLight);
    connect(removeLightButton_, &QPushButton::clicked,
            this, &DisplaySettingsWidget::removeLight);

    auto* lightForm = new QFormLayout;
    auto* directionRow = new QWidget(lightGroup);
    auto* directionLayout = new QHBoxLayout(directionRow);
    directionLayout->setContentsMargins(0, 0, 0, 0);
    for (auto*& spin : directionSpin_) {
        spin = new QDoubleSpinBox(directionRow);
        spin->setRange(-5.0, 5.0);
        spin->setDecimals(2);
        spin->setSingleStep(0.1);
        directionLayout->addWidget(spin);
        connect(spin, &QDoubleSpinBox::valueChanged,
                this, &DisplaySettingsWidget::applyLightEdits);
    }
    lightForm->addRow(tr("Direction (view space):"), directionRow);

    const auto makeColorButton = [lightGroup, lightForm](const QString& label) {
        auto* button = new QPushButton(lightGroup);
        button->setFixedHeight(22);
        lightForm->addRow(label, button);
        return button;
    };
    ambientButton_ = makeColorButton(tr("Ambient:"));
    diffuseButton_ = makeColorButton(tr("Diffuse:"));
    specularButton_ = makeColorButton(tr("Specular:"));

    lightLayout->addLayout(lightForm);
    layout->addWidget(lightGroup);
    layout->addStretch(1);

    connect(lightList_, &QListWidget::currentRowChanged,
            this, &DisplaySettingsWidget::loadSelectedLight);
    connect(ambientButton_, &QPushButton::clicked, this, [this] {
        auto& lights = viewport_->lights();
        const int row = lightList_->currentRow();
        if (row < 0 || row >= static_cast<int>(lights.size()))
            return;
        auto& light = lights[static_cast<std::size_t>(row)];
        light.ambient = pickColor(ambientButton_, light.ambient, tr("Ambient Color"));
        viewport_->styleChanged(false);
    });
    connect(diffuseButton_, &QPushButton::clicked, this, [this] {
        auto& lights = viewport_->lights();
        const int row = lightList_->currentRow();
        if (row < 0 || row >= static_cast<int>(lights.size()))
            return;
        auto& light = lights[static_cast<std::size_t>(row)];
        light.diffuse = pickColor(diffuseButton_, light.diffuse, tr("Diffuse Color"));
        viewport_->styleChanged(false);
    });
    connect(specularButton_, &QPushButton::clicked, this, [this] {
        auto& lights = viewport_->lights();
        const int row = lightList_->currentRow();
        if (row < 0 || row >= static_cast<int>(lights.size()))
            return;
        auto& light = lights[static_cast<std::size_t>(row)];
        light.specular = pickColor(specularButton_, light.specular, tr("Specular Color"));
        viewport_->styleChanged(false);
    });

    refreshLightList(0);
}

void DisplaySettingsWidget::refreshLightList(int selectRow)
{
    const auto& lights = viewport_->lights();
    lightList_->clear();
    for (std::size_t i = 0; i < lights.size(); ++i)
        lightList_->addItem(tr("Light %1").arg(i + 1));
    lightList_->setCurrentRow(
        std::clamp(selectRow, 0, static_cast<int>(lights.size()) - 1));

    addLightButton_->setEnabled(static_cast<int>(lights.size()) < render::kMaxLights);
    removeLightButton_->setEnabled(lights.size() > 1);
    loadSelectedLight();
}

void DisplaySettingsWidget::addLight()
{
    auto& lights = viewport_->lights();
    if (static_cast<int>(lights.size()) >= render::kMaxLights)
        return;
    render::Light light;
    light.direction = QVector3D(0.6f, 0.3f, -0.7f); // fill light from the other side
    light.ambient = QColor::fromRgbF(0.0f, 0.0f, 0.0f);
    light.diffuse = QColor::fromRgbF(0.35f, 0.35f, 0.40f);
    light.specular = QColor::fromRgbF(0.10f, 0.10f, 0.10f);
    lights.push_back(light);
    refreshLightList(static_cast<int>(lights.size()) - 1);
    viewport_->styleChanged(false);
}

void DisplaySettingsWidget::removeLight()
{
    auto& lights = viewport_->lights();
    const int row = lightList_->currentRow();
    if (lights.size() <= 1 || row < 0 || row >= static_cast<int>(lights.size()))
        return;
    lights.erase(lights.begin() + row);
    refreshLightList(std::max(0, row - 1));
    viewport_->styleChanged(false);
}

void DisplaySettingsWidget::loadSelectedLight()
{
    const auto& lights = viewport_->lights();
    const int row = lightList_->currentRow();
    if (row < 0 || row >= static_cast<int>(lights.size()))
        return;
    const render::Light& light = lights[static_cast<std::size_t>(row)];

    loadingLight_ = true;
    directionSpin_[0]->setValue(light.direction.x());
    directionSpin_[1]->setValue(light.direction.y());
    directionSpin_[2]->setValue(light.direction.z());
    setButtonColor(ambientButton_, light.ambient);
    setButtonColor(diffuseButton_, light.diffuse);
    setButtonColor(specularButton_, light.specular);
    loadingLight_ = false;
}

void DisplaySettingsWidget::applyLightEdits()
{
    if (loadingLight_)
        return;
    auto& lights = viewport_->lights();
    const int row = lightList_->currentRow();
    if (row < 0 || row >= static_cast<int>(lights.size()))
        return;
    auto& light = lights[static_cast<std::size_t>(row)];
    light.direction = QVector3D(static_cast<float>(directionSpin_[0]->value()),
                                static_cast<float>(directionSpin_[1]->value()),
                                static_cast<float>(directionSpin_[2]->value()));
    viewport_->styleChanged(false);
}

void DisplaySettingsWidget::applyColorMode()
{
    const auto mode = static_cast<render::ColorMode>(colorModeCombo_->currentIndex());
    const bool custom = mode == render::ColorMode::CustomScalar;
    viewport_->setColorMode(mode, custom ? propertyCombo_->currentText() : QString());
}

void DisplaySettingsWidget::refreshPropertyList()
{
    const QSignalBlocker blocker(propertyCombo_);
    const QString previous = propertyCombo_->currentText();
    propertyCombo_->clear();
    if (const auto structure = viewport_->structure()) {
        for (const auto& [name, values] : structure->scalarFields()) {
            (void)values;
            propertyCombo_->addItem(QString::fromStdString(name));
        }
    }
    const int index = propertyCombo_->findText(previous);
    if (index >= 0)
        propertyCombo_->setCurrentIndex(index);
}

void DisplaySettingsWidget::syncColoringFromViewport()
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

void DisplaySettingsWidget::setButtonColor(QPushButton* button, const QColor& color)
{
    button->setStyleSheet(QStringLiteral("background-color: %1; border: 1px solid #666;")
                              .arg(color.name()));
}

QColor DisplaySettingsWidget::pickColor(QPushButton* button, const QColor& current,
                                        const QString& title)
{
    const QColor chosen = QColorDialog::getColor(current, this, title);
    if (!chosen.isValid())
        return current;
    setButtonColor(button, chosen);
    return chosen;
}

} // namespace calango::gui
