#include "gui/CastSetupDialog.hpp"

#include "core/Structure.hpp"
#include "gui/ViewportWidget.hpp"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSignalBlocker>
#include <QTableWidget>
#include <QVBoxLayout>

#include <algorithm>

namespace calango::gui {

namespace {

enum Column { ColIndex = 0, ColElement, ColX, ColY, ColZ, ColCast, ColumnCount };

QString castLabel(int cast)
{
    return CastSetupDialog::tr("Cast: %1").arg(cast);
}

/// Human name of a representation, matching the Representation panel's combo.
QString modeName(render::RepresentationMode mode)
{
    switch (mode) {
    case render::RepresentationMode::BallAndStick:
        return CastSetupDialog::tr("Ball-and-Stick");
    case render::RepresentationMode::SpaceFilling:
        return CastSetupDialog::tr("Space-filling (CPK)");
    case render::RepresentationMode::Wireframe:
        return CastSetupDialog::tr("Wireframe");
    case render::RepresentationMode::Polyhedral:
        return CastSetupDialog::tr("Polyhedral");
    }
    return {};
}

} // namespace

CastSetupDialog::CastSetupDialog(ViewportWidget* viewport,
                                 std::shared_ptr<const core::Structure> structure,
                                 QWidget* parent)
    : QDialog(parent)
    , viewport_(viewport)
    , structure_(std::move(structure))
{
    setWindowTitle(tr("Cast Setup"));
    resize(680, 560);

    initialCasts_ = viewport_->style().atomCasts;
    initialCastModes_ = viewport_->style().castModes;
    initialMode_ = viewport_->style().mode;

    auto* layout = new QVBoxLayout(this);

    auto* note = new QLabel(
        tr("A <b>cast</b> is a group of atoms drawn in its own representation. "
           "Put the substrate in one cast and the adsorbate in another, then "
           "give each the representation that suits it — a slab as "
           "space-filling spheres with a ball-and-stick molecule on top.<br><br>"
           "Each cast's representation is chosen in the Representation panel: "
           "pick the cast there, then pick its mode. Bonds are drawn only "
           "between atoms of compatible casts, so a space-filling cast never "
           "grows sticks into its neighbour."),
        this);
    note->setWordWrap(true);
    note->setTextFormat(Qt::RichText);
    layout->addWidget(note);

    // Cast management row.
    auto* castRow = new QHBoxLayout;
    auto* addCastButton = new QPushButton(tr("Add Cast"), this);
    removeCastButton_ = new QPushButton(tr("Remove Last Cast"), this);
    removeCastButton_->setToolTip(
        tr("Removes the highest-numbered cast; its atoms return to Cast: 0. "
           "Cast 0 always exists and cannot be removed."));
    castRow->addWidget(addCastButton);
    castRow->addWidget(removeCastButton_);
    castRow->addStretch(1);
    layout->addLayout(castRow);
    connect(addCastButton, &QPushButton::clicked, this, &CastSetupDialog::addCast);
    connect(removeCastButton_, &QPushButton::clicked, this,
            &CastSetupDialog::removeLastCast);

    auto* filterRow = new QHBoxLayout;
    filterRow->addWidget(new QLabel(tr("Filter:"), this));
    filterEdit_ = new QLineEdit(this);
    filterEdit_->setPlaceholderText(
        tr("element symbol or atom index — e.g. \"Pt\" or \"12\""));
    filterEdit_->setClearButtonEnabled(true);
    filterRow->addWidget(filterEdit_, 1);
    layout->addLayout(filterRow);
    connect(filterEdit_, &QLineEdit::textChanged, this, [this](const QString& text) {
        const QString needle = text.trimmed();
        for (int row = 0; row < table_->rowCount(); ++row) {
            const QTableWidgetItem* element = table_->item(row, ColElement);
            const QTableWidgetItem* index = table_->item(row, ColIndex);
            const bool match =
                needle.isEmpty()
                || (element && element->text().contains(needle, Qt::CaseInsensitive))
                || (index && index->text() == needle);
            table_->setRowHidden(row, !match);
        }
    });

    table_ = new QTableWidget(this);
    table_->setColumnCount(ColumnCount);
    table_->setHorizontalHeaderLabels({tr("Index"), tr("Element"), tr("x (Å)"),
                                       tr("y (Å)"), tr("z (Å)"), tr("Cast")});
    table_->horizontalHeader()->setStretchLastSection(true);
    table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    table_->verticalHeader()->setVisible(false);
    layout->addWidget(table_, 1);

    // Bulk assignment: filter, select, assign. Reassigning a 200-atom slab one
    // combo at a time is not a workflow.
    auto* assignRow = new QHBoxLayout;
    assignRow->addWidget(new QLabel(tr("Move selected rows to:"), this));
    assignCastCombo_ = new QComboBox(this);
    assignRow->addWidget(assignCastCombo_);
    auto* assignButton = new QPushButton(tr("Assign"), this);
    assignRow->addWidget(assignButton);
    assignRow->addStretch(1);
    layout->addLayout(assignRow);
    connect(assignButton, &QPushButton::clicked, this,
            &CastSetupDialog::assignSelected);

    summaryLabel_ = new QLabel(this);
    summaryLabel_->setWordWrap(true);
    summaryLabel_->setTextFormat(Qt::RichText);
    layout->addWidget(summaryLabel_);

    auto* buttons = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    refreshTable();
    refreshCastChoices();
    updateSummary();
}

std::vector<int>& CastSetupDialog::casts()
{
    std::vector<int>& assignment = viewport_->style().atomCasts;
    const std::size_t count = structure_ ? structure_->size() : 0;
    // A stale vector belongs to a structure that has since been replaced;
    // resizing to the current atom count is what makes "everything is cast 0"
    // the honest starting point rather than a half-applied old assignment.
    if (assignment.size() != count)
        assignment.assign(count, 0);
    return assignment;
}

void CastSetupDialog::refreshTable()
{
    populating_ = true;
    const std::vector<int>& assignment = casts();
    static const std::vector<core::Atom> kNoAtoms;
    const std::vector<core::Atom>& atoms =
        structure_ ? structure_->atoms() : kNoAtoms;

    table_->setRowCount(static_cast<int>(atoms.size()));
    for (std::size_t i = 0; i < atoms.size(); ++i) {
        const int row = static_cast<int>(i);
        const core::Atom& atom = atoms[i];
        const auto readOnly = [](const QString& text) {
            auto* item = new QTableWidgetItem(text);
            item->setFlags(item->flags() & ~Qt::ItemIsEditable);
            return item;
        };
        table_->setItem(row, ColIndex, readOnly(QString::number(row)));
        table_->setItem(row, ColElement,
                        readOnly(QString::fromLatin1(atom.symbol())));
        table_->setItem(row, ColX,
                        readOnly(QString::number(atom.position.x, 'f', 3)));
        table_->setItem(row, ColY,
                        readOnly(QString::number(atom.position.y, 'f', 3)));
        table_->setItem(row, ColZ,
                        readOnly(QString::number(atom.position.z, 'f', 3)));

        auto* combo = qobject_cast<QComboBox*>(table_->cellWidget(row, ColCast));
        if (!combo) {
            combo = new QComboBox(table_);
            table_->setCellWidget(row, ColCast, combo);
            connect(combo, &QComboBox::currentIndexChanged, this,
                    [this, row](int cast) {
                        if (populating_ || cast < 0)
                            return;
                        casts()[static_cast<std::size_t>(row)] = cast;
                        apply();
                        updateSummary();
                    });
        }
        const QSignalBlocker blocker(combo);
        combo->setCurrentIndex(std::clamp(assignment[i], 0,
                                          viewport_->style().castCount() - 1));
    }
    table_->resizeColumnsToContents();
    populating_ = false;
}

void CastSetupDialog::refreshCastChoices()
{
    populating_ = true;
    const int count = viewport_->style().castCount();
    const auto fill = [this, count](QComboBox* combo, int keep) {
        const QSignalBlocker blocker(combo);
        combo->clear();
        for (int cast = 0; cast < count; ++cast)
            combo->addItem(tr("%1 — %2").arg(
                castLabel(cast), modeName(viewport_->style().castMode(cast))));
        combo->setCurrentIndex(std::clamp(keep, 0, count - 1));
    };
    fill(assignCastCombo_, assignCastCombo_->currentIndex());
    const std::vector<int>& assignment = casts();
    for (int row = 0; row < table_->rowCount(); ++row) {
        auto* combo = qobject_cast<QComboBox*>(table_->cellWidget(row, ColCast));
        if (combo)
            fill(combo, assignment[static_cast<std::size_t>(row)]);
    }
    removeCastButton_->setEnabled(count > 1);
    populating_ = false;
}

void CastSetupDialog::addCast()
{
    // A new cast starts on Ball-and-Stick regardless of cast 0's mode: the
    // whole point of splitting one off is to draw it DIFFERENTLY, and starting
    // it identical to what it was just split from shows nothing.
    viewport_->style().castModes.push_back(
        render::RepresentationMode::BallAndStick);
    refreshCastChoices();
    apply();
    updateSummary();
}

void CastSetupDialog::removeLastCast()
{
    auto& modes = viewport_->style().castModes;
    if (modes.empty())
        return; // cast 0 is not removable
    const int removed = static_cast<int>(modes.size()); // its index
    modes.pop_back();
    // Its atoms fall back to cast 0 rather than being left pointing at an index
    // that no longer exists.
    for (int& cast : casts())
        if (cast >= removed)
            cast = 0;
    refreshCastChoices();
    refreshTable();
    apply();
    updateSummary();
}

void CastSetupDialog::assignSelected()
{
    const int cast = assignCastCombo_->currentIndex();
    if (cast < 0)
        return;
    std::vector<int>& assignment = casts();
    for (const QModelIndex& index : table_->selectionModel()->selectedRows()) {
        const auto row = static_cast<std::size_t>(index.row());
        if (row < assignment.size())
            assignment[row] = cast;
    }
    refreshTable();
    apply();
    updateSummary();
}

void CastSetupDialog::apply()
{
    // Geometry rebuild, not just a repaint: a cast change alters radii, which
    // bonds exist and which buffer each atom lands in.
    viewport_->styleChanged(true);
}

void CastSetupDialog::updateSummary()
{
    const int count = viewport_->style().castCount();
    std::vector<int> populations(static_cast<std::size_t>(count), 0);
    for (int cast : casts())
        if (cast >= 0 && cast < count)
            ++populations[static_cast<std::size_t>(cast)];

    QStringList parts;
    for (int cast = 0; cast < count; ++cast)
        parts << tr("%1: %2 atom(s) as %3")
                     .arg(castLabel(cast))
                     .arg(populations[static_cast<std::size_t>(cast)])
                     .arg(modeName(viewport_->style().castMode(cast)));
    summaryLabel_->setText(parts.join(QStringLiteral("<br>")));
}

void CastSetupDialog::reject()
{
    viewport_->style().atomCasts = initialCasts_;
    viewport_->style().castModes = initialCastModes_;
    viewport_->style().mode = initialMode_;
    apply();
    QDialog::reject();
}

} // namespace calango::gui
