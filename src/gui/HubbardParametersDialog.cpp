#include "gui/HubbardParametersDialog.hpp"

#include <QCheckBox>
#include <QComboBox>
#include <QCompleter>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QTableWidget>
#include <QVBoxLayout>

#include <algorithm>
#include <functional>

namespace calango::gui {

namespace {

// Column layout of the editor table.
enum Column { ColElement = 0, ColOrbital, ColU, ColScale, ColCount };

/// The shells a Hubbard correction is normally applied to. s is offered for
/// completeness but is rare — DFT+U exists because semilocal functionals
/// mistreat *localized* states, which in practice means d and f.
const char* const kOrbitals[] = {"d", "f", "p", "s"};

} // namespace

HubbardParametersDialog::HubbardParametersDialog(
    bool enabled, const std::vector<core::HubbardU>& initial,
    const QStringList& elements, QWidget* parent)
    : QDialog(parent)
    , elements_(elements)
{
    setWindowTitle(tr("Hubbard Parameters (DFT+U)"));
    resize(560, 420);

    auto* layout = new QVBoxLayout(this);

    enabledCheck_ = new QCheckBox(tr("Include Hubbard parameters (DFT+U)"), this);
    enabledCheck_->setChecked(enabled);
    layout->addWidget(enabledCheck_);

    auto* note = new QLabel(
        tr("Adds an on-site Coulomb repulsion U to the named orbital shell, "
           "written as GPAW's <code>setups</code> dictionary. Use it when a "
           "semilocal functional over-delocalizes a narrow d or f band — the "
           "usual symptoms are a metallic ground state for a known insulator, "
           "or magnetic moments that come out too small.<br><br>"
           "U is not a property of the element alone: it depends on the "
           "chemical environment and on how it was determined. Report the "
           "value and its source alongside any result that uses it."),
        this);
    note->setWordWrap(true);
    note->setTextFormat(Qt::RichText);
    layout->addWidget(note);

    table_ = new QTableWidget(this);
    table_->setColumnCount(ColCount);
    table_->setHorizontalHeaderLabels(
        {tr("Element"), tr("Orbital"), tr("U (eV)"), tr("Scale")});
    table_->horizontalHeader()->setStretchLastSection(true);
    table_->horizontalHeader()->setSectionResizeMode(ColElement,
                                                     QHeaderView::Stretch);
    table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    table_->verticalHeader()->setVisible(false);
    layout->addWidget(table_, 1);

    auto* buttonRow = new QHBoxLayout;
    auto* addButton = new QPushButton(tr("Add Element"), this);
    removeButton_ = new QPushButton(tr("Remove Selected"), this);
    buttonRow->addWidget(addButton);
    buttonRow->addWidget(removeButton_);
    buttonRow->addStretch(1);
    layout->addLayout(buttonRow);

    previewLabel_ = new QLabel(this);
    previewLabel_->setWordWrap(true);
    previewLabel_->setTextFormat(Qt::RichText);
    layout->addWidget(previewLabel_);

    auto* buttons = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    layout->addWidget(buttons);

    for (const core::HubbardU& entry : initial)
        appendRow(entry);
    if (initial.empty()) {
        // Seed one blank row so the table is obviously editable rather than an
        // empty box the user has to guess at.
        core::HubbardU seed;
        seed.element = elements_.isEmpty() ? std::string()
                                           : elements_.first().toStdString();
        appendRow(seed);
    }

    connect(addButton, &QPushButton::clicked, this,
            &HubbardParametersDialog::addRow);
    connect(removeButton_, &QPushButton::clicked, this,
            &HubbardParametersDialog::removeSelectedRows);
    connect(enabledCheck_, &QCheckBox::toggled, this,
            &HubbardParametersDialog::updateState);
    connect(table_, &QTableWidget::itemSelectionChanged, this,
            &HubbardParametersDialog::updateState);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    updateState();
}

void HubbardParametersDialog::appendRow(const core::HubbardU& entry)
{
    const int row = table_->rowCount();
    table_->insertRow(row);

    auto* element = new QLineEdit(QString::fromStdString(entry.element), table_);
    element->setPlaceholderText(tr("e.g. Fe"));
    if (!elements_.isEmpty()) {
        // Complete against the species actually in the cell: a U on an absent
        // element is silently inert and easy to miss in a generated script.
        auto* completer = new QCompleter(elements_, element);
        completer->setCaseSensitivity(Qt::CaseInsensitive);
        element->setCompleter(completer);
        element->setToolTip(
            tr("Species present in this structure: %1").arg(elements_.join(", ")));
    }
    connect(element, &QLineEdit::textChanged, this,
            &HubbardParametersDialog::updateState);
    table_->setCellWidget(row, ColElement, element);

    auto* orbital = new QComboBox(table_);
    for (const char* shell : kOrbitals)
        orbital->addItem(QString::fromLatin1(shell));
    orbital->setCurrentText(QString::fromStdString(
        entry.orbital.empty() ? std::string("d") : entry.orbital));
    connect(orbital, &QComboBox::currentTextChanged, this,
            &HubbardParametersDialog::updateState);
    table_->setCellWidget(row, ColOrbital, orbital);

    auto* u = new QDoubleSpinBox(table_);
    u->setRange(0.0, 20.0);
    u->setDecimals(2);
    u->setSingleStep(0.5);
    u->setSuffix(tr(" eV"));
    u->setValue(entry.u);
    connect(u, &QDoubleSpinBox::valueChanged, this,
            &HubbardParametersDialog::updateState);
    table_->setCellWidget(row, ColU, u);

    auto* scale = new QCheckBox(table_);
    scale->setChecked(entry.scale);
    scale->setToolTip(
        tr("Scale the correction by the shell's electron count (GPAW's "
           "optional third field). Off is the usual convention."));
    connect(scale, &QCheckBox::toggled, this,
            &HubbardParametersDialog::updateState);
    table_->setCellWidget(row, ColScale, scale);
}

void HubbardParametersDialog::addRow()
{
    appendRow(core::HubbardU{});
    updateState();
}

void HubbardParametersDialog::removeSelectedRows()
{
    const auto selected = table_->selectionModel()->selectedRows();
    std::vector<int> rows;
    rows.reserve(selected.size());
    for (const QModelIndex& index : selected)
        rows.push_back(index.row());
    // Remove from the bottom up so earlier indices stay valid.
    std::sort(rows.begin(), rows.end(), std::greater<int>());
    for (int row : rows)
        table_->removeRow(row);
    updateState();
}

bool HubbardParametersDialog::isEnabled() const
{
    return enabledCheck_->isChecked();
}

std::vector<core::HubbardU> HubbardParametersDialog::parameters() const
{
    std::vector<core::HubbardU> result;
    for (int row = 0; row < table_->rowCount(); ++row) {
        const auto* element =
            qobject_cast<QLineEdit*>(table_->cellWidget(row, ColElement));
        const auto* orbital =
            qobject_cast<QComboBox*>(table_->cellWidget(row, ColOrbital));
        const auto* u =
            qobject_cast<QDoubleSpinBox*>(table_->cellWidget(row, ColU));
        const auto* scale =
            qobject_cast<QCheckBox*>(table_->cellWidget(row, ColScale));
        if (!element || !orbital || !u)
            continue;
        const QString symbol = element->text().trimmed();
        // A blank element or U = 0 would emit a setups entry that changes
        // nothing while implying the run is a DFT+U calculation. Dropped.
        if (symbol.isEmpty() || u->value() <= 0.0)
            continue;
        result.push_back({symbol.toStdString(),
                          orbital->currentText().toStdString(), u->value(),
                          scale && scale->isChecked()});
    }
    return result;
}

void HubbardParametersDialog::updateState()
{
    const bool on = enabledCheck_->isChecked();
    table_->setEnabled(on);
    removeButton_->setEnabled(
        on && !table_->selectionModel()->selectedRows().isEmpty());

    if (!on) {
        previewLabel_->setText(
            tr("<i>No Hubbard correction is written; the run is plain DFT.</i>"));
        return;
    }
    const auto entries = parameters();
    if (entries.empty()) {
        previewLabel_->setText(
            tr("<i>No complete rows yet — a row needs an element and a "
               "non-zero U to be written.</i>"));
        return;
    }
    QStringList parts;
    for (const core::HubbardU& entry : entries) {
        parts << QStringLiteral("\"%1\": \":%2,%3%4\"")
                     .arg(QString::fromStdString(entry.element),
                          QString::fromStdString(entry.orbital))
                     .arg(entry.u, 0, 'g', 4)
                     .arg(entry.scale ? QStringLiteral(",1") : QString());
    }
    previewLabel_->setText(tr("Generated: <code>setups={%1}</code>")
                               .arg(parts.join(QStringLiteral(", "))));
}

} // namespace calango::gui
