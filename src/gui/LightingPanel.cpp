#include "gui/LightingPanel.hpp"
#include "gui/GuiUtils.hpp"

#include "gui/ViewportWidget.hpp"
#include "ui/IconManager.hpp"

#include <QColorDialog>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QVBoxLayout>

#include <algorithm>

namespace calango::gui {

namespace {

} // namespace

LightingPanel::LightingPanel(ViewportWidget* viewport, QWidget* parent)
    : QWidget(parent)
    , viewport_(viewport)
{
    auto* layout = new QVBoxLayout(this);

    lightList_ = new QListWidget(this);
    lightList_->setFixedHeight(72);
    layout->addWidget(lightList_);

    auto* lightButtons = new QHBoxLayout;
    addLightButton_ = new QPushButton(tr("Add"), this);
    ui::IconManager::bind(addLightButton_, QStringLiteral("add-circle-fill"));
    removeLightButton_ = new QPushButton(tr("Remove"), this);
    ui::IconManager::bind(removeLightButton_,
                          QStringLiteral("indeterminate-circle-fill"));
    lightButtons->addWidget(addLightButton_);
    lightButtons->addWidget(removeLightButton_);
    lightButtons->addStretch(1);
    layout->addLayout(lightButtons);
    connect(addLightButton_, &QPushButton::clicked, this, &LightingPanel::addLight);
    connect(removeLightButton_, &QPushButton::clicked, this, &LightingPanel::removeLight);

    auto* form = new QFormLayout;
    auto* directionRow = new QWidget(this);
    auto* directionLayout = new QHBoxLayout(directionRow);
    directionLayout->setContentsMargins(0, 0, 0, 0);
    for (auto*& spin : directionSpin_) {
        spin = new QDoubleSpinBox(directionRow);
        spin->setRange(-5.0, 5.0);
        spin->setDecimals(2);
        spin->setSingleStep(0.1);
        directionLayout->addWidget(spin);
        connect(spin, &QDoubleSpinBox::valueChanged,
                this, &LightingPanel::applyLightEdits);
    }
    form->addRow(tr("Direction (view space):"), directionRow);

    const auto makeColorButton = [this, form](const QString& label) {
        auto* button = new QPushButton(this);
        button->setFixedHeight(22);
        form->addRow(label, button);
        return button;
    };
    ambientButton_ = makeColorButton(tr("Ambient:"));
    diffuseButton_ = makeColorButton(tr("Diffuse:"));
    specularButton_ = makeColorButton(tr("Specular:"));

    layout->addLayout(form);
    layout->addStretch(1);

    connect(lightList_, &QListWidget::currentRowChanged,
            this, &LightingPanel::loadSelectedLight);

    const auto pickLightColor = [this](QPushButton* button, QColor render::Light::* member,
                                       const QString& title) {
        auto& lights = viewport_->lights();
        const int row = lightList_->currentRow();
        if (row < 0 || row >= static_cast<int>(lights.size()))
            return;
        auto& light = lights[static_cast<std::size_t>(row)];
        const QColor chosen = QColorDialog::getColor(light.*member, this, title);
        if (!chosen.isValid())
            return;
        light.*member = chosen;
        setButtonColor(button, chosen);
        viewport_->styleChanged(false);
    };
    connect(ambientButton_, &QPushButton::clicked, this, [this, pickLightColor] {
        pickLightColor(ambientButton_, &render::Light::ambient, tr("Ambient Color"));
    });
    connect(diffuseButton_, &QPushButton::clicked, this, [this, pickLightColor] {
        pickLightColor(diffuseButton_, &render::Light::diffuse, tr("Diffuse Color"));
    });
    connect(specularButton_, &QPushButton::clicked, this, [this, pickLightColor] {
        pickLightColor(specularButton_, &render::Light::specular, tr("Specular Color"));
    });

    refreshLightList(0);
}

void LightingPanel::refreshLightList(int selectRow)
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

void LightingPanel::addLight()
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

void LightingPanel::removeLight()
{
    auto& lights = viewport_->lights();
    const int row = lightList_->currentRow();
    if (lights.size() <= 1 || row < 0 || row >= static_cast<int>(lights.size()))
        return;
    lights.erase(lights.begin() + row);
    refreshLightList(std::max(0, row - 1));
    viewport_->styleChanged(false);
}

void LightingPanel::loadSelectedLight()
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

void LightingPanel::applyLightEdits()
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

} // namespace calango::gui
