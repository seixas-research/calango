#include "gui/LightingPanel.hpp"
#include "gui/GuiUtils.hpp"

#include "gui/ViewportWidget.hpp"
#include "ui/IconManager.hpp"

#include <QColorDialog>
#include <QFile>
#include <QFileDialog>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMessageBox>
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

    // The whole row is icon-only. A + and a − beside a list need no spelling
    // out, and the five labels together were wider than the dock.
    auto* lightButtons = new QHBoxLayout;
    addLightButton_ = new QPushButton(this);
    ui::IconManager::bind(addLightButton_, QStringLiteral("add-circle-fill"));
    addLightButton_->setIconSize(QSize(20, 20));
    addLightButton_->setToolTip(
        tr("Add — append another directional light (up to the renderer's "
           "limit of four)."));
    removeLightButton_ = new QPushButton(this);
    ui::IconManager::bind(removeLightButton_,
                          QStringLiteral("indeterminate-circle-fill"));
    removeLightButton_->setIconSize(QSize(20, 20));
    removeLightButton_->setToolTip(
        tr("Remove — delete the selected light. The last one cannot be "
           "removed: a scene with no lights renders black."));
    lightButtons->addWidget(addLightButton_);
    lightButtons->addWidget(removeLightButton_);

    const auto makeIconButton = [this, lightButtons](const QString& icon,
                                                     const QString& tip) {
        auto* button = new QPushButton(this);
        ui::IconManager::bind(button, icon);
        button->setIconSize(QSize(20, 20));
        button->setToolTip(tip);
        button->setFocusPolicy(Qt::NoFocus);
        lightButtons->addWidget(button);
        return button;
    };
    loadPresetButton_ = makeIconButton(
        QStringLiteral("folder-open-line"),
        tr("Load presets… — replace the light set with one saved earlier.\n\n"
           "Lighting is the part of a figure hardest to reproduce by hand, and "
           "a set that worked for one structure usually works for the next."));
    savePresetButton_ = makeIconButton(
        QStringLiteral("save-line"),
        tr("Save presets… — write the current lights to a .json file: each "
           "light's view-space direction and its ambient, diffuse and specular "
           "colors."));
    resetLightsButton_ = makeIconButton(
        QStringLiteral("arrow-go-back-line"),
        tr("Reset lights — restore the two-light studio default (warm key "
           "light + soft cool fill) a fresh viewport starts with."));

    lightButtons->addStretch(1);
    layout->addLayout(lightButtons);
    connect(addLightButton_, &QPushButton::clicked, this, &LightingPanel::addLight);
    connect(removeLightButton_, &QPushButton::clicked, this, &LightingPanel::removeLight);
    connect(loadPresetButton_, &QPushButton::clicked,
            this, &LightingPanel::loadPresets);
    connect(savePresetButton_, &QPushButton::clicked,
            this, &LightingPanel::savePresets);
    connect(resetLightsButton_, &QPushButton::clicked,
            this, &LightingPanel::resetLights);

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

namespace {

/// One light as JSON. Colours are stored as float RGB rather than "#rrggbb"
/// because their INTENSITY lives in the same channels — a light at 0.72 grey
/// is dimmer than one at 1.0 — and 8-bit hex would quantize that away.
QJsonObject lightToJson(const render::Light& light)
{
    const auto colour = [](const QColor& c) {
        QJsonArray out;
        out.append(c.redF());
        out.append(c.greenF());
        out.append(c.blueF());
        return out;
    };
    QJsonObject object;
    QJsonArray direction;
    direction.append(light.direction.x());
    direction.append(light.direction.y());
    direction.append(light.direction.z());
    object[QStringLiteral("direction")] = direction;
    object[QStringLiteral("ambient")] = colour(light.ambient);
    object[QStringLiteral("diffuse")] = colour(light.diffuse);
    object[QStringLiteral("specular")] = colour(light.specular);
    return object;
}

/// Inverse of lightToJson. Missing or malformed fields keep the Light's own
/// defaults rather than becoming black — a preset file that lost a channel
/// should light the scene imperfectly, not not at all.
render::Light lightFromJson(const QJsonObject& object)
{
    const auto colour = [&object](const QString& key, const QColor& fallback) {
        const QJsonArray array = object.value(key).toArray();
        if (array.size() != 3)
            return fallback;
        return QColor::fromRgbF(
            std::clamp(array.at(0).toDouble(), 0.0, 1.0),
            std::clamp(array.at(1).toDouble(), 0.0, 1.0),
            std::clamp(array.at(2).toDouble(), 0.0, 1.0));
    };
    render::Light light;
    const QJsonArray direction = object.value(QStringLiteral("direction")).toArray();
    if (direction.size() == 3) {
        const QVector3D parsed(static_cast<float>(direction.at(0).toDouble()),
                               static_cast<float>(direction.at(1).toDouble()),
                               static_cast<float>(direction.at(2).toDouble()));
        // A zero direction has no direction to travel in and would leave the
        // shader normalizing (0,0,0); keep the default instead.
        if (!parsed.isNull())
            light.direction = parsed;
    }
    light.ambient = colour(QStringLiteral("ambient"), light.ambient);
    light.diffuse = colour(QStringLiteral("diffuse"), light.diffuse);
    light.specular = colour(QStringLiteral("specular"), light.specular);
    return light;
}

} // namespace

void LightingPanel::savePresets()
{
    QString path = QFileDialog::getSaveFileName(
        this, tr("Save Lighting Preset"),
        QStringLiteral("calango-lights.json"),
        tr("Lighting preset (*.json)"));
    if (path.isEmpty())
        return;
    if (!path.endsWith(QStringLiteral(".json"), Qt::CaseInsensitive))
        path += QStringLiteral(".json");

    QJsonArray lights;
    for (const render::Light& light : viewport_->lights())
        lights.append(lightToJson(light));
    QJsonObject root;
    root[QStringLiteral("calango_lighting_preset")] = 1;
    root[QStringLiteral("lights")] = lights;

    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QMessageBox::warning(this, tr("Save Lighting Preset"),
                             tr("Could not write %1.").arg(path));
        return;
    }
    file.write(QJsonDocument(root).toJson());
}

void LightingPanel::loadPresets()
{
    const QString path = QFileDialog::getOpenFileName(
        this, tr("Load Lighting Preset"), QString(),
        tr("Lighting preset (*.json)"));
    if (path.isEmpty())
        return;

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        QMessageBox::warning(this, tr("Load Lighting Preset"),
                             tr("Could not read %1.").arg(path));
        return;
    }
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll());
    const QJsonArray array =
        document.object().value(QStringLiteral("lights")).toArray();
    if (array.isEmpty()) {
        // Replacing the scene's lighting with nothing would render a black
        // frame and look like a crash, so an unreadable file changes nothing.
        QMessageBox::warning(
            this, tr("Load Lighting Preset"),
            tr("%1 contains no lights — it is not a Calango lighting preset. "
               "The current lighting is unchanged.").arg(path));
        return;
    }

    std::vector<render::Light> loaded;
    // The shader has a hard cap (render::kMaxLights); a longer file is
    // truncated rather than silently ignored past the limit.
    for (const QJsonValue& value : array) {
        if (static_cast<int>(loaded.size()) >= render::kMaxLights)
            break;
        loaded.push_back(lightFromJson(value.toObject()));
    }
    if (array.size() > render::kMaxLights) {
        QMessageBox::information(
            this, tr("Load Lighting Preset"),
            tr("The preset holds %1 lights; the renderer supports %2, so only "
               "the first %2 were loaded.")
                .arg(array.size())
                .arg(render::kMaxLights));
    }

    viewport_->lights() = std::move(loaded);
    refreshLightList(0);
    viewport_->styleChanged(false);
}

void LightingPanel::resetLights()
{
    // The renderer's own default set, not a second copy of it here — the point
    // of "reset" is to match a fresh viewport exactly.
    viewport_->lights() = render::StructureRenderer::defaultLights();
    refreshLightList(0);
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
