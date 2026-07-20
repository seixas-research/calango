#include "gui/DisplaySettingsWidget.hpp"

#include "gui/ElementSettingsDialog.hpp"
#include "gui/ViewportWidget.hpp"

#include <QColorDialog>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QVBoxLayout>

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

    const auto makeScaleRow = [this](QSlider*& slider, QLabel*& label) {
        auto* row = new QWidget(this);
        auto* rowLayout = new QHBoxLayout(row);
        rowLayout->setContentsMargins(0, 0, 0, 0);
        slider = new QSlider(Qt::Horizontal, row);
        slider->setRange(20, 300); // percent
        slider->setValue(100);
        label = new QLabel(QStringLiteral("100%"), row);
        label->setMinimumWidth(44);
        rowLayout->addWidget(slider, 1);
        rowLayout->addWidget(label);
        return row;
    };

    reprForm->addRow(tr("Atom radius:"), makeScaleRow(atomScaleSlider_, atomScaleLabel_));
    reprForm->addRow(tr("Bond width:"), makeScaleRow(bondWidthSlider_, bondWidthLabel_));

    connect(atomScaleSlider_, &QSlider::valueChanged, this, [this](int percent) {
        atomScaleLabel_->setText(QStringLiteral("%1%").arg(percent));
        viewport_->style().atomScaleFactor = static_cast<float>(percent) / 100.0f;
        viewport_->styleChanged(true);
    });
    connect(bondWidthSlider_, &QSlider::valueChanged, this, [this](int percent) {
        bondWidthLabel_->setText(QStringLiteral("%1%").arg(percent));
        viewport_->style().bondWidthFactor = static_cast<float>(percent) / 100.0f;
        viewport_->styleChanged(true);
    });

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
