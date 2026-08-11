#include "gui/DatabaseImportDialog.hpp"

#include "gui/SettingsManager.hpp"
#include "python_bridge/MaterialsProject.hpp"

#include <QAbstractItemView>
#include <QApplication>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSettings>
#include <QSpinBox>
#include <QTableWidget>
#include <QVBoxLayout>

namespace calango::gui {

namespace {

enum Column {
    ColId,
    ColFormula,
    ColSpaceGroup,
    ColGap,
    ColHull,
    ColSites,
    ColumnCount,
};

QTableWidgetItem* cell(const QString& text, bool numeric = false)
{
    auto* item = new QTableWidgetItem(text);
    item->setFlags(item->flags() & ~Qt::ItemIsEditable);
    if (numeric)
        item->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
    return item;
}

} // namespace

DatabaseImportDialog::DatabaseImportDialog(QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle(tr("Import Structures from Database"));
    resize(760, 520);
    auto* layout = new QVBoxLayout(this);

    auto* caption = new QLabel(
        tr("Search the Materials Project and add structures to the "
           "container. Several searches can be added in one visit — the "
           "collected structures are kept until you press OK."),
        this);
    caption->setWordWrap(true);
    layout->addWidget(caption);

    auto* form = new QFormLayout;
    apiKeyEdit_ = new QLineEdit(this);
    apiKeyEdit_->setEchoMode(QLineEdit::Password);
    apiKeyEdit_->setPlaceholderText(
        tr("API key from materialsproject.org/api"));
    // Same stored key the database browser uses, then the environment (loaded
    // from the configured .env at launch), so a key entered in either place
    // works in both.
    QString storedKey =
        QSettings()
            .value(QLatin1String(SettingsManager::kMaterialsProjectApiKey))
            .toString();
    if (storedKey.isEmpty())
        storedKey = qEnvironmentVariable("MP_API_KEY");
    apiKeyEdit_->setText(storedKey);
    connect(apiKeyEdit_, &QLineEdit::textChanged, this, [this] {
        QSettings().setValue(
            QLatin1String(SettingsManager::kMaterialsProjectApiKey),
            apiKeyEdit_->text());
    });
    form->addRow(tr("API key:"), apiKeyEdit_);
    layout->addLayout(form);

    auto* searchRow = new QHBoxLayout;
    queryEdit_ = new QLineEdit(this);
    queryEdit_->setPlaceholderText(tr("Li-Fe-O, or LiFePO4, or Si"));
    modeCombo_ = new QComboBox(this);
    modeCombo_->addItem(tr("Exact chemical system"));
    modeCombo_->addItem(tr("Contains these elements"));
    modeCombo_->addItem(tr("Formula"));
    limitSpin_ = new QSpinBox(this);
    limitSpin_->setRange(1, 200);
    limitSpin_->setValue(20);
    limitSpin_->setPrefix(tr("max "));
    searchButton_ = new QPushButton(tr("Search"), this);
    searchButton_->setDefault(true);
    searchRow->addWidget(queryEdit_, 1);
    searchRow->addWidget(modeCombo_);
    searchRow->addWidget(limitSpin_);
    searchRow->addWidget(searchButton_);
    layout->addLayout(searchRow);

    results_ = new QTableWidget(0, ColumnCount, this);
    results_->setHorizontalHeaderLabels({tr("ID"), tr("Formula"),
                                         tr("Space group"), tr("Gap (eV)"),
                                         tr("E above hull"), tr("Sites")});
    results_->setSelectionBehavior(QAbstractItemView::SelectRows);
    results_->setSelectionMode(QAbstractItemView::ExtendedSelection);
    results_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    results_->setSortingEnabled(true);
    results_->verticalHeader()->setVisible(false);
    results_->horizontalHeader()->setStretchLastSection(true);
    layout->addWidget(results_, 1);

    auto* actions = new QHBoxLayout;
    addButton_ = new QPushButton(tr("Add Selected to Container"), this);
    addButton_->setEnabled(false);
    basket_ = new QLabel(this);
    actions->addWidget(addButton_);
    actions->addWidget(basket_, 1);
    layout->addLayout(actions);

    status_ = new QLabel(this);
    status_->setWordWrap(true);
    layout->addWidget(status_);

    auto* buttons = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    buttons->button(QDialogButtonBox::Ok)->setText(tr("Done"));
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    connect(searchButton_, &QPushButton::clicked, this,
            &DatabaseImportDialog::search);
    connect(queryEdit_, &QLineEdit::returnPressed, this,
            &DatabaseImportDialog::search);
    connect(addButton_, &QPushButton::clicked, this,
            &DatabaseImportDialog::addSelected);
    connect(results_, &QTableWidget::itemSelectionChanged, this, [this] {
        addButton_->setEnabled(!selectedIds().isEmpty());
    });
    // Double-clicking a row adds it: the fast path for "that one".
    connect(results_, &QTableWidget::itemDoubleClicked, this,
            [this](QTableWidgetItem*) { addSelected(); });

    refreshBasket();
}

QStringList DatabaseImportDialog::selectedIds() const
{
    QStringList ids;
    // selectedRows() rather than selectedItems(): the latter reports cells,
    // so a row selection would add the same structure six times.
    for (const QModelIndex& index :
         results_->selectionModel()->selectedRows(ColId))
        if (const QTableWidgetItem* item = results_->item(index.row(), ColId))
            if (!item->text().isEmpty())
                ids.append(item->text());
    return ids;
}

void DatabaseImportDialog::setBusy(bool busy, const QString& message)
{
    searchButton_->setEnabled(!busy);
    addButton_->setEnabled(!busy && !selectedIds().isEmpty());
    queryEdit_->setEnabled(!busy);
    if (!message.isNull())
        status_->setText(message);
    if (busy)
        QGuiApplication::setOverrideCursor(Qt::WaitCursor);
    else
        QGuiApplication::restoreOverrideCursor();
    // Repaint the status line without letting a second click through. No
    // QProgressDialog: this dialog already sits on top of the container
    // editor, and a third window-modal layer is what made the previous
    // version of this feature stop responding.
    QApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
}

void DatabaseImportDialog::refreshBasket()
{
    basket_->setText(entries_.isEmpty()
                         ? tr("Nothing collected yet.")
                         : tr("%n structure(s) will be added.", nullptr,
                              static_cast<int>(entries_.size())));
}

void DatabaseImportDialog::search()
{
    const QString query = queryEdit_->text().trimmed();
    if (query.isEmpty())
        return;
    if (apiKeyEdit_->text().trimmed().isEmpty()) {
        status_->setStyleSheet(QStringLiteral("color: #d9534f;"));
        status_->setText(
            tr("A Materials Project API key is required — get one at "
               "materialsproject.org/api and paste it above."));
        return;
    }

    const int mode = modeCombo_->currentIndex();
    setBusy(true, tr("Searching for “%1”…").arg(query));
    status_->setStyleSheet(QString());

    std::vector<pybridge::MaterialsProject::SearchHit> hits;
    QString error;
    try {
        hits = pybridge::MaterialsProject::search(
            query.toStdString(), apiKeyEdit_->text().trimmed().toStdString(),
            /*asFormula=*/mode == 2,
            /*exactSystem=*/mode == 0, limitSpin_->value());
    } catch (const std::exception& e) {
        error = QString::fromUtf8(e.what());
    }
    setBusy(false, QString());

    if (!error.isEmpty()) {
        status_->setStyleSheet(QStringLiteral("color: #d9534f;"));
        status_->setText(error.section(QLatin1Char('\n'), 0, 0));
        return;
    }

    results_->setSortingEnabled(false);
    results_->setRowCount(0);
    for (const auto& hit : hits) {
        const int row = results_->rowCount();
        results_->insertRow(row);
        results_->setItem(row, ColId,
                          cell(QString::fromStdString(hit.materialId)));
        results_->setItem(row, ColFormula,
                          cell(QString::fromStdString(hit.formula)));
        results_->setItem(
            row, ColSpaceGroup,
            cell(QStringLiteral("%1 (%2)")
                     .arg(QString::fromStdString(hit.spaceGroup))
                     .arg(hit.spaceGroupNumber)));
        // A null band gap upstream is not zero — a metal and an unmeasured
        // entry are different statements, so the blank stays blank.
        results_->setItem(row, ColGap,
                          cell(hit.hasBandGap
                                   ? QString::number(hit.bandGap, 'f', 3)
                                   : QStringLiteral("—"),
                               true));
        results_->setItem(
            row, ColHull,
            cell(hit.hasEnergyAboveHull
                     ? QString::number(hit.energyAboveHull, 'f', 3)
                     : QStringLiteral("—"),
                 true));
        results_->setItem(row, ColSites, cell(QString::number(hit.nSites), true));
    }
    results_->setSortingEnabled(true);
    results_->resizeColumnsToContents();
    status_->setText(hits.empty()
                         ? tr("No matches for “%1”.").arg(query)
                         : tr("%n match(es) — select rows and press Add.",
                              nullptr, static_cast<int>(hits.size())));
}

void DatabaseImportDialog::addSelected()
{
    const QStringList ids = selectedIds();
    if (ids.isEmpty())
        return;
    const std::string key = apiKeyEdit_->text().trimmed().toStdString();

    QStringList failures;
    int added = 0;
    for (int index = 0; index < ids.size(); ++index) {
        const QString id = ids.at(index);
        setBusy(true, tr("Fetching %1 (%2 of %3)…")
                          .arg(id)
                          .arg(index + 1)
                          .arg(ids.size()));
        try {
            auto structure = std::make_shared<const core::Structure>(
                pybridge::MaterialsProject::fetchStructure(id.toStdString(),
                                                           key));
            // "mp-149 Si" — the id alone is unreadable in a container list,
            // the formula alone is ambiguous across polymorphs.
            entries_.append(
                {QStringLiteral("%1 %2").arg(
                     id, QString::fromStdString(structure->chemicalFormula())),
                 structure});
            ++added;
        } catch (const std::exception& e) {
            // One bad id must not lose the rest of a twenty-row selection.
            failures.append(QStringLiteral("%1: %2").arg(
                id, QString::fromUtf8(e.what()).section(QLatin1Char('\n'), 0, 0)));
        }
    }
    setBusy(false, QString());
    refreshBasket();

    status_->setStyleSheet(failures.isEmpty() ? QString()
                                              : QStringLiteral("color: #d9534f;"));
    status_->setText(
        failures.isEmpty()
            ? tr("Added %n structure(s). Search again to add more, or press "
                 "Done.",
                 nullptr, added)
            : tr("Added %1 of %2; failed: %3")
                  .arg(added)
                  .arg(ids.size())
                  .arg(failures.join(QStringLiteral("; "))));
}

QList<DatabaseImportDialog::Entry> DatabaseImportDialog::pick(QWidget* parent)
{
    DatabaseImportDialog dialog(parent);
    if (dialog.exec() != QDialog::Accepted)
        return {};
    return dialog.entries();
}

} // namespace calango::gui
