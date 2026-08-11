#include "gui/CalculatorParametersDialog.hpp"
#include "gui/GuiUtils.hpp"

#include "gui/CalculatorParameters.hpp"
#include "gui/EnginePresets.hpp"

#include <QComboBox>
#include <QFile>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLabel>
#include <QPushButton>
#include <QSaveFile>
#include <QTableWidget>
#include <QVBoxLayout>

#include <algorithm>
#include <array>
#include <functional>

namespace calango::gui {

namespace {

enum Column { ElementColumn, CutoffColumn, K1Column, K2Column, K3Column };

/// The engines for which pw/kpts suggestions mean anything — the ones that
/// read the shared cutoff / k-grid rows in the wizards.
///
/// SIESTA is in the list for its k-grid alone: it has no plane-wave cutoff, so
/// only the mesh columns of its rows are meaningful. The engines with neither
/// (FHI-aims, OpenMX, FLEUR, NWChem's molecular modules, every potential) are
/// absent because a suggested cutoff for them would be a number nothing reads.
const std::array<core::CalculatorKind, 5> kEngines = {
    core::CalculatorKind::Gpaw,
    core::CalculatorKind::Vasp,
    core::CalculatorKind::QuantumEspresso,
    core::CalculatorKind::Abinit,
    core::CalculatorKind::Siesta,
};

QTableWidgetItem* numberItem(const QString& text)
{
    auto* item = new QTableWidgetItem(text);
    item->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
    return item;
}

/// {"pw": …, "kpts": […]} from one table row's numeric cells; fields the
/// user left blank (or wrote nonsense into) are simply absent.
QJsonObject rowToEntry(const QTableWidget* table, int row)
{
    QJsonObject entry;
    const auto text = [table, row](int column) {
        const QTableWidgetItem* item = table->item(row, column);
        return item ? item->text().trimmed() : QString();
    };
    bool ok = false;
    const double pw = text(CutoffColumn).toDouble(&ok);
    if (ok && pw > 0.0)
        entry.insert(QStringLiteral("pw"), pw);
    QJsonArray kpts;
    bool kptsValid = true;
    for (int column : {K1Column, K2Column, K3Column}) {
        const int k = text(column).toInt(&ok);
        kptsValid = kptsValid && ok && k >= 1;
        kpts.append(k);
    }
    if (kptsValid)
        entry.insert(QStringLiteral("kpts"), kpts);
    return entry;
}

} // namespace

CalculatorParametersDialog::CalculatorParametersDialog(QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle(tr("Calculator Parameter Suggestions"));
    resize(560, 480);

    auto* layout = new QVBoxLayout(this);

    auto* note = new QLabel(
        tr("Suggested defaults the simulation wizards open with, stored in "
           "<code>%1</code>. Per element, the strictest value among the "
           "structure's elements wins; the <b>(default)</b> row applies when "
           "no element matches. Empty cells fall through to the built-in "
           "defaults.")
            .arg(CalculatorParameters::filePath().toHtmlEscaped()),
        this);
    note->setWordWrap(true);
    note->setTextFormat(Qt::RichText);
    layout->addWidget(note);

    auto* engineRow = new QHBoxLayout;
    engineRow->addWidget(new QLabel(tr("Calculator:"), this));
    engineCombo_ = new QComboBox(this);
    for (core::CalculatorKind kind : kEngines)
        engineCombo_->addItem(EnginePresets::displayName(kind),
                              EnginePresets::presetName(kind));
    engineRow->addWidget(engineCombo_, 1);
    layout->addLayout(engineRow);

    table_ = new QTableWidget(0, 5, this);
    table_->setHorizontalHeaderLabels({tr("Element"), tr("pw (eV)"),
                                       tr("k₁"), tr("k₂"), tr("k₃")});
    table_->horizontalHeader()->setSectionResizeMode(ElementColumn,
                                                     QHeaderView::Stretch);
    for (int column : {CutoffColumn, K1Column, K2Column, K3Column})
        table_->horizontalHeader()->setSectionResizeMode(
            column, QHeaderView::ResizeToContents);
    table_->verticalHeader()->setVisible(false);
    disableTypeToEdit(table_);
    layout->addWidget(table_, 1);

    auto* rowButtons = new QHBoxLayout;
    auto* addButton = new QPushButton(tr("Add Element"), this);
    removeButton_ = new QPushButton(tr("Remove Selected"), this);
    rowButtons->addWidget(addButton);
    rowButtons->addWidget(removeButton_);
    rowButtons->addStretch(1);
    layout->addLayout(rowButtons);

    statusLabel_ = new QLabel(this);
    statusLabel_->setWordWrap(true);
    layout->addWidget(statusLabel_);

    auto* actions = new QHBoxLayout;
    actions->addStretch(1);
    auto* saveButton = new QPushButton(tr("Save"), this);
    saveButton->setToolTip(
        tr("Write the file. Wizards read it every time they open, so the "
           "next one already sees this."));
    auto* closeButton = new QPushButton(tr("Close"), this);
    actions->addWidget(saveButton);
    actions->addWidget(closeButton);
    layout->addLayout(actions);

    connect(addButton, &QPushButton::clicked, this,
            &CalculatorParametersDialog::addElementRow);
    connect(removeButton_, &QPushButton::clicked, this,
            &CalculatorParametersDialog::removeSelectedRows);
    connect(saveButton, &QPushButton::clicked, this,
            &CalculatorParametersDialog::save);
    connect(closeButton, &QPushButton::clicked, this, &QWidget::close);
    connect(engineCombo_, &QComboBox::currentIndexChanged, this, [this] {
        // Keep edits made under the previous engine (in memory — disk only
        // changes on Save), then show the newly selected one.
        commitTable(shownEngineKey_);
        populateTable();
    });
    connect(table_, &QTableWidget::itemSelectionChanged, this, [this] {
        // The "(default)" row is structural — it can be emptied, not removed.
        removeButton_->setEnabled(table_->currentRow() > 0);
    });

    // Return must edit cells, never trigger a button (the QDialog
    // autoDefault trap).
    for (QPushButton* button : findChildren<QPushButton*>()) {
        button->setAutoDefault(false);
        button->setDefault(false);
    }

    loadFile();
    populateTable();
}

void CalculatorParametersDialog::loadFile()
{
    CalculatorParameters::ensureFileExists();
    QFile file(CalculatorParameters::filePath());
    if (file.open(QIODevice::ReadOnly)) {
        const QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
        if (doc.isObject())
            root_ = doc.object();
    }
    statusLabel_->setText(
        root_.isEmpty() ? tr("Could not read the file — saving will create "
                             "it fresh.")
                        : QString());
}

QString CalculatorParametersDialog::selectedEngineKey() const
{
    return engineCombo_->currentData().toString();
}

void CalculatorParametersDialog::populateTable()
{
    shownEngineKey_ = selectedEngineKey();
    const QJsonObject engine = root_.value(shownEngineKey_).toObject();

    table_->setRowCount(0);
    const auto appendRow = [this](const QString& element,
                                  const QJsonObject& entry, bool isDefault) {
        const int row = table_->rowCount();
        table_->insertRow(row);
        auto* elementItem = new QTableWidgetItem(element);
        if (isDefault) // structural row: label not editable, values are
            elementItem->setFlags(elementItem->flags()
                                  & ~Qt::ItemIsEditable);
        table_->setItem(row, ElementColumn, elementItem);
        const QJsonValue pw = entry.value(QStringLiteral("pw"));
        table_->setItem(row, CutoffColumn,
                        numberItem(pw.isDouble()
                                       ? QString::number(pw.toDouble())
                                       : QString()));
        const QJsonArray kpts =
            entry.value(QStringLiteral("kpts")).toArray();
        for (int axis = 0; axis < 3; ++axis)
            table_->setItem(row, K1Column + axis,
                            numberItem(kpts.size() == 3
                                           ? QString::number(
                                                 kpts.at(axis).toInt())
                                           : QString()));
    };

    appendRow(tr("(default)"),
              engine.value(QStringLiteral("default")).toObject(),
              /*isDefault=*/true);
    const QJsonObject elements =
        engine.value(QStringLiteral("elements")).toObject();
    QStringList symbols = elements.keys();
    std::sort(symbols.begin(), symbols.end());
    for (const QString& symbol : symbols)
        appendRow(symbol, elements.value(symbol).toObject(),
                  /*isDefault=*/false);
    removeButton_->setEnabled(false);
}

void CalculatorParametersDialog::commitTable(const QString& engineKey)
{
    if (engineKey.isEmpty())
        return;
    QJsonObject engine = root_.value(engineKey).toObject();

    const QJsonObject defaultEntry = rowToEntry(table_, 0);
    if (defaultEntry.isEmpty())
        engine.remove(QStringLiteral("default"));
    else
        engine.insert(QStringLiteral("default"), defaultEntry);

    QJsonObject elements;
    for (int row = 1; row < table_->rowCount(); ++row) {
        const QTableWidgetItem* symbolItem =
            table_->item(row, ElementColumn);
        const QString symbol =
            symbolItem ? symbolItem->text().trimmed() : QString();
        if (symbol.isEmpty())
            continue; // an abandoned "Add Element" row
        const QJsonObject entry = rowToEntry(table_, row);
        if (!entry.isEmpty())
            elements.insert(symbol, entry);
    }
    engine.insert(QStringLiteral("elements"), elements);
    root_.insert(engineKey, engine);
}

void CalculatorParametersDialog::addElementRow()
{
    const int row = table_->rowCount();
    table_->insertRow(row);
    table_->setItem(row, ElementColumn, new QTableWidgetItem(QString()));
    for (int column : {CutoffColumn, K1Column, K2Column, K3Column})
        table_->setItem(row, column, numberItem(QString()));
    table_->setCurrentCell(row, ElementColumn);
    table_->editItem(table_->item(row, ElementColumn));
}

void CalculatorParametersDialog::removeSelectedRows()
{
    // Collect first, remove bottom-up so indices stay valid; row 0 (the
    // default entry) is structural and stays.
    QList<int> rows;
    for (const QTableWidgetItem* item : table_->selectedItems())
        if (item->row() > 0 && !rows.contains(item->row()))
            rows.append(item->row());
    std::sort(rows.begin(), rows.end(), std::greater<int>());
    for (int row : rows)
        table_->removeRow(row);
}

void CalculatorParametersDialog::save()
{
    commitTable(shownEngineKey_);

    QSaveFile file(CalculatorParameters::filePath());
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        statusLabel_->setText(
            tr("Could not write %1.").arg(CalculatorParameters::filePath()));
        return;
    }
    file.write(QJsonDocument(root_).toJson(QJsonDocument::Indented));
    if (file.commit())
        statusLabel_->setText(
            tr("Saved. Wizards opened from now on use these values."));
    else
        statusLabel_->setText(
            tr("Could not write %1.").arg(CalculatorParameters::filePath()));
}

} // namespace calango::gui
