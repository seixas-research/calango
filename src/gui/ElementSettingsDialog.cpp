#include "gui/ElementSettingsDialog.hpp"

#include "core/Structure.hpp"
#include "gui/ViewportWidget.hpp"
#include "render/StructureRenderer.hpp"

#include "ui/IconManager.hpp"

#include <QColorDialog>
#include <QFile>
#include <QFileDialog>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMessageBox>
#include <QDialogButtonBox>
#include <QHeaderView>
#include <QPushButton>
#include <QSpinBox>
#include <QTableWidget>
#include <QVBoxLayout>

#include <cmath>
#include <set>
#include <set>

namespace calango::gui {

namespace {

/// Elements listed when no structure is loaded.
constexpr int kCommonElements[] = {1, 6, 7, 8, 9, 14, 15, 16, 17, 26, 29, 79};

QString swatchStyle(const QColor& color)
{
    return QStringLiteral("background-color: %1; border: 1px solid #666;")
        .arg(color.name());
}

} // namespace

ElementSettingsDialog::ElementSettingsDialog(ViewportWidget* viewport, QWidget* parent)
    : QDialog(parent)
    , viewport_(viewport)
    , table_(new QTableWidget(this))
{
    setWindowTitle(tr("Element Settings"));
    resize(440, 460);

    table_->setColumnCount(4);
    table_->setHorizontalHeaderLabels({tr("Element"), tr("Color"), tr("Radius"),
                                       QString()});
    table_->horizontalHeader()->setStretchLastSection(true);
    table_->verticalHeader()->setVisible(false);
    table_->setSelectionMode(QAbstractItemView::NoSelection);
    table_->setEditTriggers(QAbstractItemView::NoEditTriggers);

    auto* buttons = new QDialogButtonBox(this);
    // Preset management sits beside Reset All: the three are the same kind of
    // action (replace the whole override set at once), as opposed to the
    // per-row edits in the table above.
    const auto addAction = [&](const QString& text, const QString& iconName,
                               const QString& tip) {
        auto* button =
            buttons->addButton(text, QDialogButtonBox::ResetRole);
        ui::IconManager::bind(button, iconName);
        button->setToolTip(tip);
        return button;
    };
    auto* resetAllButton = addAction(
        tr("Reset All"), QStringLiteral("arrow-go-back-line"),
        tr("Discard every color and radius override, restoring the Jmol CPK "
           "palette and 100% radii."));
    auto* loadButton = addAction(
        tr("Load Presets…"), QStringLiteral("folder-open-line"),
        tr("Replace the current overrides with a saved element preset (JSON)."));
    auto* saveButton = addAction(
        tr("Save Presets…"), QStringLiteral("save-line"),
        tr("Write the current color and radius overrides to a JSON preset."));
    buttons->addButton(QDialogButtonBox::Close);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(resetAllButton, &QPushButton::clicked, this, [this] { resetAll(); });
    connect(loadButton, &QPushButton::clicked, this, [this] { loadPresets(); });
    connect(saveButton, &QPushButton::clicked, this, [this] { savePresets(); });

    auto* layout = new QVBoxLayout(this);
    layout->addWidget(table_);
    layout->addWidget(buttons);

    populate();
}

void ElementSettingsDialog::populate()
{
    // Elements present in the current structure, ordered by Z; a sensible
    // default set when nothing is loaded.
    std::set<int> elements;
    if (const auto structure = viewport_->structure(); structure && !structure->empty()) {
        for (const core::Atom& atom : structure->atoms())
            elements.insert(atom.atomicNumber);
    } else {
        elements.insert(std::begin(kCommonElements), std::end(kCommonElements));
    }

    const auto& style = viewport_->style();

    table_->setRowCount(static_cast<int>(elements.size()));
    int row = 0;
    for (const int z : elements) {
        auto* symbolItem = new QTableWidgetItem(
            QString::fromLatin1(core::Elements::data(z).symbol));
        table_->setItem(row, 0, symbolItem);

        auto* colorButton = new QPushButton(table_);
        colorButton->setStyleSheet(
            swatchStyle(render::StructureRenderer::atomColor(z, style)));
        connect(colorButton, &QPushButton::clicked, this, [this, z] { editColor(z); });
        table_->setCellWidget(row, 1, colorButton);

        auto* radiusSpin = new QSpinBox(table_);
        radiusSpin->setRange(10, 300);
        radiusSpin->setSuffix(QStringLiteral(" %"));
        const auto it = style.radiusScaleOverrides.find(z);
        radiusSpin->setValue(it != style.radiusScaleOverrides.end()
                                 ? static_cast<int>(std::lround(it->second * 100.0f))
                                 : 100);
        connect(radiusSpin, &QSpinBox::valueChanged, this, [this, z](int percent) {
            if (percent == 100)
                viewport_->style().radiusScaleOverrides.erase(z);
            else
                viewport_->style().radiusScaleOverrides[z] =
                    static_cast<float>(percent) / 100.0f;
            viewport_->styleChanged(true);
        });
        table_->setCellWidget(row, 2, radiusSpin);

        auto* resetButton = new QPushButton(tr("Reset"), table_);
        connect(resetButton, &QPushButton::clicked, this,
                [this, z] { resetElement(z); });
        table_->setCellWidget(row, 3, resetButton);
        ++row;
    }
}

void ElementSettingsDialog::editColor(int atomicNumber)
{
    const QColor current =
        render::StructureRenderer::atomColor(atomicNumber, viewport_->style());
    const QColor chosen = QColorDialog::getColor(
        current, this,
        tr("Color for %1").arg(QLatin1String(core::Elements::data(atomicNumber).symbol)));
    if (!chosen.isValid())
        return;
    viewport_->style().colorOverrides[atomicNumber] = chosen;
    viewport_->styleChanged(true);
    populate();
}

void ElementSettingsDialog::resetElement(int atomicNumber)
{
    viewport_->style().colorOverrides.erase(atomicNumber);
    viewport_->style().radiusScaleOverrides.erase(atomicNumber);
    viewport_->styleChanged(true);
    populate();
}

void ElementSettingsDialog::resetAll()
{
    viewport_->style().colorOverrides.clear();
    viewport_->style().radiusScaleOverrides.clear();
    viewport_->styleChanged(true);
    populate();
}


void ElementSettingsDialog::savePresets()
{
    const QString path = QFileDialog::getSaveFileName(
        this, tr("Save Element Presets"),
        QStringLiteral("calango_elements.json"),
        tr("Element presets (*.json);;All files (*)"));
    if (path.isEmpty())
        return;

    const auto& style = viewport_->style();
    QJsonObject elements;
    // Keyed by SYMBOL rather than atomic number: a preset is meant to be
    // hand-editable and shareable, and "Fe" says what it is where "26" does not.
    const auto entryFor = [&](int z) {
        QJsonObject entry;
        if (const auto color = style.colorOverrides.find(z);
            color != style.colorOverrides.end())
            entry.insert(QStringLiteral("color"), color->second.name(QColor::HexRgb));
        if (const auto radius = style.radiusScaleOverrides.find(z);
            radius != style.radiusScaleOverrides.end())
            entry.insert(QStringLiteral("radiusScale"),
                         static_cast<double>(radius->second));
        return entry;
    };
    std::set<int> touched;
    for (const auto& [z, color] : style.colorOverrides) {
        (void)color;
        touched.insert(z);
    }
    for (const auto& [z, radius] : style.radiusScaleOverrides) {
        (void)radius;
        touched.insert(z);
    }
    for (const int z : touched) {
        const QJsonObject entry = entryFor(z);
        if (!entry.isEmpty())
            elements.insert(QLatin1String(core::Elements::data(z).symbol), entry);
    }

    QJsonObject root;
    root.insert(QStringLiteral("format"), QStringLiteral("calango-element-presets"));
    root.insert(QStringLiteral("version"), 1);
    root.insert(QStringLiteral("elements"), elements);

    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        QMessageBox::warning(this, tr("Save Element Presets"),
                             tr("Could not write %1").arg(path));
        return;
    }
    file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    if (elements.isEmpty())
        QMessageBox::information(
            this, tr("Save Element Presets"),
            tr("No overrides are set, so the preset is empty — loading it will "
               "simply restore the default CPK palette."));
}

void ElementSettingsDialog::loadPresets()
{
    const QString path = QFileDialog::getOpenFileName(
        this, tr("Load Element Presets"), QString(),
        tr("Element presets (*.json);;All files (*)"));
    if (path.isEmpty())
        return;

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        QMessageBox::warning(this, tr("Load Element Presets"),
                             tr("Could not read %1").arg(path));
        return;
    }
    QJsonParseError error{};
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &error);
    if (error.error != QJsonParseError::NoError || !document.isObject()) {
        QMessageBox::warning(this, tr("Load Element Presets"),
                             tr("%1 is not valid JSON:\n%2")
                                 .arg(path, error.errorString()));
        return;
    }
    const QJsonObject elements =
        document.object().value(QStringLiteral("elements")).toObject();
    if (elements.isEmpty()) {
        QMessageBox::warning(
            this, tr("Load Element Presets"),
            tr("%1 contains no \"elements\" object — it does not look like a "
               "Calango element preset.").arg(path));
        return;
    }

    // Replace rather than merge: a preset describes a complete look, and
    // merging would leave whatever the previous preset set for elements the
    // new one does not mention.
    auto& style = viewport_->style();
    style.colorOverrides.clear();
    style.radiusScaleOverrides.clear();

    QStringList unknown;
    for (auto it = elements.constBegin(); it != elements.constEnd(); ++it) {
        const int z = core::Elements::atomicNumber(it.key().toStdString());
        if (z == 0) {
            unknown << it.key();
            continue;
        }
        const QJsonObject entry = it.value().toObject();
        const QColor color(entry.value(QStringLiteral("color")).toString());
        if (color.isValid())
            style.colorOverrides[z] = color;
        const QJsonValue radius = entry.value(QStringLiteral("radiusScale"));
        if (radius.isDouble() && radius.toDouble() > 0.0)
            style.radiusScaleOverrides[z] = static_cast<float>(radius.toDouble());
    }
    viewport_->styleChanged(true);
    populate();

    // Unknown symbols are reported rather than dropped silently: a typo in a
    // hand-edited preset would otherwise just not apply, with no clue why.
    if (!unknown.isEmpty())
        QMessageBox::information(
            this, tr("Load Element Presets"),
            tr("Ignored %1 unrecognized element symbol(s): %2")
                .arg(unknown.size())
                .arg(unknown.join(QStringLiteral(", "))));
}

} // namespace calango::gui
