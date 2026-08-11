#include "gui/GeometryConstraintsDialog.hpp"
#include "gui/GuiUtils.hpp"

#include "core/Structure.hpp"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSignalBlocker>
#include <QTableWidget>
#include <QTabWidget>
#include <QVBoxLayout>

#include <algorithm>
#include <functional>

namespace calango::gui {

namespace {

/// Per-atom table columns. The three mask columns are adjacent so the pattern
/// of ticks down the table reads as a picture of what is held.
enum AtomColumn {
    ColIndex = 0, ColElement, ColX, ColY, ColZ, ColFixX, ColFixY, ColFixZ,
    AtomColumnCount
};

/// Region table columns.
enum RegionColumn {
    ColAxis = 0, ColUseMin, ColMin, ColUseMax, ColMax,
    ColRegionFixX, ColRegionFixY, ColRegionFixZ, RegionColumnCount
};

/// A centered checkbox in a table cell. A bare QCheckBox as a cell widget
/// hugs the left edge, which makes a column of them read as ragged rather than
/// as a column.
QWidget* centeredCheck(QWidget* parent, QCheckBox*& out, bool checked)
{
    auto* holder = new QWidget(parent);
    auto* layout = new QHBoxLayout(holder);
    layout->setContentsMargins(0, 0, 0, 0);
    out = new QCheckBox(holder);
    out->setChecked(checked);
    layout->addStretch(1);
    layout->addWidget(out);
    layout->addStretch(1);
    return holder;
}

QCheckBox* cellCheck(QTableWidget* table, int row, int column)
{
    QWidget* holder = table->cellWidget(row, column);
    return holder ? holder->findChild<QCheckBox*>() : nullptr;
}

} // namespace

GeometryConstraintsDialog::GeometryConstraintsDialog(
    std::shared_ptr<const core::Structure> structure,
    const std::vector<core::GeometryConstraint>& initial, QWidget* parent)
    : QDialog(parent)
    , structure_(std::move(structure))
{
    setWindowTitle(tr("Geometry Constraints"));
    resize(720, 560);

    auto* layout = new QVBoxLayout(this);

    auto* note = new QLabel(
        tr("Degrees of freedom the relaxation may not move. Freezing all three "
           "directions of an atom writes ASE's <code>FixAtoms</code>; freezing "
           "only some writes <code>FixCartesian</code>.<br><br>"
           "Fixing the bottom layers of a slab is what keeps it a slab — an "
           "unconstrained surface calculation relaxes the whole thing toward "
           "the bulk it was cut from. Prefer a <b>region</b> for that: it is "
           "re-evaluated against the geometry the script reads, so the rule "
           "still means \"the bottom layers\" after the slab is re-cleaved."),
        this);
    note->setWordWrap(true);
    note->setTextFormat(Qt::RichText);
    layout->addWidget(note);

    auto* tabs = new QTabWidget(this);
    tabs->addTab(buildAtomsTab(), tr("Atoms"));
    tabs->addTab(buildRegionsTab(), tr("Regions"));
    layout->addWidget(tabs, 1);

    summaryLabel_ = new QLabel(this);
    summaryLabel_->setWordWrap(true);
    summaryLabel_->setTextFormat(Qt::RichText);
    layout->addWidget(summaryLabel_);

    auto* buttons = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    applyInitial(initial);
    updateSummary();
}

QWidget* GeometryConstraintsDialog::buildAtomsTab()
{
    auto* page = new QWidget(this);
    auto* layout = new QVBoxLayout(page);

    // Filter first: a slab has hundreds of rows, and "every O" or "every atom
    // whose row I can see" is how the selection is actually expressed.
    auto* filterRow = new QHBoxLayout;
    filterRow->addWidget(new QLabel(tr("Filter:"), page));
    atomFilterEdit_ = new QLineEdit(page);
    atomFilterEdit_->setPlaceholderText(
        tr("element symbol or atom index — e.g. \"Pt\" or \"12\""));
    atomFilterEdit_->setClearButtonEnabled(true);
    filterRow->addWidget(atomFilterEdit_, 1);
    layout->addLayout(filterRow);

    atomTable_ = new QTableWidget(page);
    atomTable_->setColumnCount(AtomColumnCount);
    disableTypeToEdit(atomTable_);
    atomTable_->setHorizontalHeaderLabels({tr("Index"), tr("Element"),
                                           tr("x (Å)"), tr("y (Å)"), tr("z (Å)"),
                                           tr("Fix x"), tr("Fix y"), tr("Fix z")});
    // Spend the spare width on the element column, not on the trailing mask
    // column: the three Fix columns want to sit together and read as one block
    // of ticks, which a stretched last section breaks apart.
    atomTable_->horizontalHeader()->setStretchLastSection(false);
    atomTable_->horizontalHeader()->setSectionResizeMode(ColElement,
                                                         QHeaderView::Stretch);
    atomTable_->setSelectionBehavior(QAbstractItemView::SelectRows);
    atomTable_->verticalHeader()->setVisible(false);
    layout->addWidget(atomTable_, 1);

    static const std::vector<core::Atom> kNoAtoms;
    const std::vector<core::Atom>& atoms =
        structure_ ? structure_->atoms() : kNoAtoms;
    atomTable_->setRowCount(static_cast<int>(atoms.size()));
    for (std::size_t i = 0; i < atoms.size(); ++i) {
        const core::Atom& atom = atoms[i];
        const int row = static_cast<int>(i);
        const auto readOnly = [](const QString& text) {
            auto* item = new QTableWidgetItem(text);
            item->setFlags(item->flags() & ~Qt::ItemIsEditable);
            return item;
        };
        atomTable_->setItem(row, ColIndex, readOnly(QString::number(row)));
        atomTable_->setItem(row, ColElement,
                            readOnly(QString::fromLatin1(atom.symbol())));
        atomTable_->setItem(
            row, ColX, readOnly(QString::number(atom.position.x, 'f', 3)));
        atomTable_->setItem(
            row, ColY, readOnly(QString::number(atom.position.y, 'f', 3)));
        atomTable_->setItem(
            row, ColZ, readOnly(QString::number(atom.position.z, 'f', 3)));
        for (int axis = 0; axis < 3; ++axis) {
            QCheckBox* box = nullptr;
            atomTable_->setCellWidget(row, ColFixX + axis,
                                      centeredCheck(atomTable_, box, false));
            connect(box, &QCheckBox::toggled, this,
                    &GeometryConstraintsDialog::updateSummary);
        }
    }
    atomTable_->resizeColumnsToContents();

    connect(atomFilterEdit_, &QLineEdit::textChanged, this,
            [this](const QString& text) {
                const QString needle = text.trimmed();
                for (int row = 0; row < atomTable_->rowCount(); ++row) {
                    const QTableWidgetItem* element =
                        atomTable_->item(row, ColElement);
                    const QTableWidgetItem* index = atomTable_->item(row, ColIndex);
                    const bool match =
                        needle.isEmpty()
                        || (element
                            && element->text().contains(needle, Qt::CaseInsensitive))
                        || (index && index->text() == needle);
                    atomTable_->setRowHidden(row, !match);
                }
            });

    // Mass assignment: pick a mask, select rows, apply. Ticking 200 individual
    // boxes for "freeze the substrate" is not a workflow.
    auto* assignRow = new QHBoxLayout;
    assignRow->addWidget(new QLabel(tr("Assign to selected rows:"), page));
    const char* labels[3] = {"x", "y", "z"};
    for (int axis = 0; axis < 3; ++axis) {
        assignChecks_[axis] = new QCheckBox(QLatin1String(labels[axis]), page);
        assignChecks_[axis]->setChecked(true);
        assignRow->addWidget(assignChecks_[axis]);
    }
    auto* applyButton = new QPushButton(tr("Apply"), page);
    auto* clearButton = new QPushButton(tr("Free All Atoms"), page);
    assignRow->addWidget(applyButton);
    assignRow->addWidget(clearButton);
    assignRow->addStretch(1);
    layout->addLayout(assignRow);

    connect(applyButton, &QPushButton::clicked, this,
            &GeometryConstraintsDialog::assignToSelectedAtoms);
    connect(clearButton, &QPushButton::clicked, this,
            &GeometryConstraintsDialog::clearAtomConstraints);

    if (atoms.empty()) {
        auto* empty = new QLabel(
            tr("<i>No structure is loaded, so there are no atoms to list. "
               "Region rules on the next tab still apply.</i>"),
            page);
        empty->setWordWrap(true);
        empty->setTextFormat(Qt::RichText);
        layout->addWidget(empty);
    }
    return page;
}

QWidget* GeometryConstraintsDialog::buildRegionsTab()
{
    auto* page = new QWidget(this);
    auto* layout = new QVBoxLayout(page);

    auto* note = new QLabel(
        tr("Each row selects every atom whose coordinate along one axis lies "
           "between the given bounds — e.g. axis <b>z</b>, min 5, max 10 for "
           "<code>z &gt; 5 and z &lt; 10</code>. Leave a bound unticked for a "
           "one-sided rule."),
        page);
    note->setWordWrap(true);
    note->setTextFormat(Qt::RichText);
    layout->addWidget(note);

    regionTable_ = new QTableWidget(page);
    regionTable_->setColumnCount(RegionColumnCount);
    disableTypeToEdit(regionTable_);
    regionTable_->setHorizontalHeaderLabels(
        {tr("Axis"), tr("Min"), tr("Value (Å)"), tr("Max"), tr("Value (Å)"),
         tr("Fix x"), tr("Fix y"), tr("Fix z")});
    // Same rule as the atom table: the bound spin boxes take the spare width,
    // and the three Fix columns stay a compact block at the right.
    regionTable_->horizontalHeader()->setStretchLastSection(false);
    for (const int column : {ColMin, ColMax})
        regionTable_->horizontalHeader()->setSectionResizeMode(
            column, QHeaderView::Stretch);
    regionTable_->setSelectionBehavior(QAbstractItemView::SelectRows);
    regionTable_->verticalHeader()->setVisible(false);
    layout->addWidget(regionTable_, 1);

    auto* buttonRow = new QHBoxLayout;
    auto* addButton = new QPushButton(tr("Add Region"), page);
    removeRegionButton_ = new QPushButton(tr("Remove Selected"), page);
    buttonRow->addWidget(addButton);
    buttonRow->addWidget(removeRegionButton_);
    buttonRow->addStretch(1);
    layout->addLayout(buttonRow);

    connect(addButton, &QPushButton::clicked, this,
            &GeometryConstraintsDialog::addRegion);
    connect(removeRegionButton_, &QPushButton::clicked, this,
            &GeometryConstraintsDialog::removeSelectedRegions);
    connect(regionTable_, &QTableWidget::itemSelectionChanged, this,
            &GeometryConstraintsDialog::updateSummary);
    return page;
}

void GeometryConstraintsDialog::appendRegionRow(
    const core::GeometryConstraint& region)
{
    const int row = regionTable_->rowCount();
    regionTable_->insertRow(row);

    auto* axis = new QComboBox(regionTable_);
    axis->addItems({QStringLiteral("x"), QStringLiteral("y"), QStringLiteral("z")});
    axis->setCurrentIndex(std::clamp(region.axis, 0, 2));
    connect(axis, &QComboBox::currentIndexChanged, this,
            &GeometryConstraintsDialog::updateSummary);
    regionTable_->setCellWidget(row, ColAxis, axis);

    // The bound spin boxes are enabled by their own "use this bound" tick, so a
    // one-sided rule ("everything below z = 5") never carries a stale, ignored
    // number the user might read as active.
    const auto addBound = [&](int useColumn, int valueColumn, bool use,
                              double value) {
        QCheckBox* box = nullptr;
        regionTable_->setCellWidget(row, useColumn,
                                    centeredCheck(regionTable_, box, use));
        auto* spin = new QDoubleSpinBox(regionTable_);
        spin->setRange(-10000.0, 10000.0);
        spin->setDecimals(3);
        spin->setSingleStep(0.5);
        spin->setValue(value);
        spin->setEnabled(use);
        regionTable_->setCellWidget(row, valueColumn, spin);
        connect(box, &QCheckBox::toggled, spin, &QWidget::setEnabled);
        connect(box, &QCheckBox::toggled, this,
                &GeometryConstraintsDialog::updateSummary);
        connect(spin, &QDoubleSpinBox::valueChanged, this,
                &GeometryConstraintsDialog::updateSummary);
    };
    addBound(ColUseMin, ColMin, region.hasMin, region.minValue);
    addBound(ColUseMax, ColMax, region.hasMax, region.maxValue);

    for (int i = 0; i < 3; ++i) {
        QCheckBox* box = nullptr;
        regionTable_->setCellWidget(
            row, ColRegionFixX + i,
            centeredCheck(regionTable_, box, region.fix[i]));
        connect(box, &QCheckBox::toggled, this,
                &GeometryConstraintsDialog::updateSummary);
    }
    regionTable_->resizeColumnsToContents();
}

void GeometryConstraintsDialog::addRegion()
{
    core::GeometryConstraint region;
    region.selection = core::GeometryConstraint::Selection::Region;
    // Seeded from the structure's own extent along z rather than at 0: the
    // overwhelmingly common rule is "freeze the bottom of this slab", and a
    // bound near the actual lower surface is a far better starting guess than
    // the origin, which may be nowhere near the atoms.
    region.axis = 2;
    region.hasMax = true;
    region.maxValue = 0.0;
    if (structure_ && !structure_->empty()) {
        double low = structure_->atoms().front().position.z;
        double high = low;
        for (const core::Atom& atom : structure_->atoms()) {
            low = std::min(low, atom.position.z);
            high = std::max(high, atom.position.z);
        }
        region.maxValue = low + 0.25 * (high - low);
    }
    appendRegionRow(region);
    updateSummary();
}

void GeometryConstraintsDialog::removeSelectedRegions()
{
    const auto selected = regionTable_->selectionModel()->selectedRows();
    std::vector<int> rows;
    rows.reserve(selected.size());
    for (const QModelIndex& index : selected)
        rows.push_back(index.row());
    // Bottom-up so the earlier indices stay valid as rows disappear.
    std::sort(rows.begin(), rows.end(), std::greater<int>());
    for (int row : rows)
        regionTable_->removeRow(row);
    updateSummary();
}

void GeometryConstraintsDialog::assignToSelectedAtoms()
{
    const bool mask[3] = {assignChecks_[0]->isChecked(),
                          assignChecks_[1]->isChecked(),
                          assignChecks_[2]->isChecked()};
    const auto selected = atomTable_->selectionModel()->selectedRows();
    for (const QModelIndex& index : selected)
        setAtomMask(index.row(), mask);
    updateSummary();
}

void GeometryConstraintsDialog::clearAtomConstraints()
{
    const bool none[3] = {false, false, false};
    for (int row = 0; row < atomTable_->rowCount(); ++row)
        setAtomMask(row, none);
    updateSummary();
}

void GeometryConstraintsDialog::atomMask(int row, bool mask[3]) const
{
    for (int axis = 0; axis < 3; ++axis) {
        const QCheckBox* box = cellCheck(atomTable_, row, ColFixX + axis);
        mask[axis] = box && box->isChecked();
    }
}

void GeometryConstraintsDialog::setAtomMask(int row, const bool mask[3])
{
    for (int axis = 0; axis < 3; ++axis) {
        QCheckBox* box = cellCheck(atomTable_, row, ColFixX + axis);
        if (box) {
            const QSignalBlocker blocker(box);
            box->setChecked(mask[axis]);
        }
    }
}

void GeometryConstraintsDialog::applyInitial(
    const std::vector<core::GeometryConstraint>& initial)
{
    for (const core::GeometryConstraint& constraint : initial) {
        if (constraint.selection == core::GeometryConstraint::Selection::Region) {
            appendRegionRow(constraint);
            continue;
        }
        for (int index : constraint.indices) {
            if (index < 0 || index >= atomTable_->rowCount())
                continue; // the structure changed under a saved rule
            // OR the masks together: two rules may name the same atom, each
            // freezing a different direction, and both are in force.
            bool mask[3];
            atomMask(index, mask);
            for (int axis = 0; axis < 3; ++axis)
                mask[axis] = mask[axis] || constraint.fix[axis];
            setAtomMask(index, mask);
        }
    }
}

std::vector<core::GeometryConstraint>
GeometryConstraintsDialog::constraints() const
{
    std::vector<core::GeometryConstraint> result;

    // The per-atom table collapses into at most seven index rules, one per
    // distinct freeze mask. That is what keeps a 400-atom slab from generating
    // 400 separate FixCartesian objects for what is really one rule.
    std::vector<int> byMask[8];
    for (int row = 0; row < atomTable_->rowCount(); ++row) {
        bool mask[3];
        atomMask(row, mask);
        const int key = (mask[0] ? 1 : 0) | (mask[1] ? 2 : 0) | (mask[2] ? 4 : 0);
        if (key != 0)
            byMask[key].push_back(row);
    }
    for (int key = 1; key < 8; ++key) {
        if (byMask[key].empty())
            continue;
        core::GeometryConstraint constraint;
        constraint.selection = core::GeometryConstraint::Selection::Indices;
        constraint.indices = byMask[key];
        constraint.fix[0] = (key & 1) != 0;
        constraint.fix[1] = (key & 2) != 0;
        constraint.fix[2] = (key & 4) != 0;
        result.push_back(std::move(constraint));
    }

    for (int row = 0; row < regionTable_->rowCount(); ++row) {
        const auto* axis =
            qobject_cast<QComboBox*>(regionTable_->cellWidget(row, ColAxis));
        const auto* minSpin =
            qobject_cast<QDoubleSpinBox*>(regionTable_->cellWidget(row, ColMin));
        const auto* maxSpin =
            qobject_cast<QDoubleSpinBox*>(regionTable_->cellWidget(row, ColMax));
        if (!axis || !minSpin || !maxSpin)
            continue;
        core::GeometryConstraint constraint;
        constraint.selection = core::GeometryConstraint::Selection::Region;
        constraint.axis = axis->currentIndex();
        const QCheckBox* useMin = cellCheck(regionTable_, row, ColUseMin);
        const QCheckBox* useMax = cellCheck(regionTable_, row, ColUseMax);
        constraint.hasMin = useMin && useMin->isChecked();
        constraint.hasMax = useMax && useMax->isChecked();
        constraint.minValue = minSpin->value();
        constraint.maxValue = maxSpin->value();
        for (int i = 0; i < 3; ++i) {
            const QCheckBox* box = cellCheck(regionTable_, row, ColRegionFixX + i);
            constraint.fix[i] = box && box->isChecked();
        }
        // A row that freezes no direction is a row the user emptied rather than
        // deleted; writing it would be a constraint that constrains nothing.
        if (!constraint.fixesAnyDirection())
            continue;
        result.push_back(std::move(constraint));
    }
    return result;
}

void GeometryConstraintsDialog::updateSummary()
{
    if (removeRegionButton_)
        removeRegionButton_->setEnabled(
            !regionTable_->selectionModel()->selectedRows().isEmpty());
    // The tabs are built before the summary label, and their controls connect
    // here as they are created — so this can be re-entered against a dialog
    // that is still half-assembled.
    if (!summaryLabel_ || !atomTable_ || !regionTable_)
        return;

    const auto rules = constraints();
    if (rules.empty()) {
        summaryLabel_->setText(
            tr("<i>No constraints — every atom relaxes freely.</i>"));
        return;
    }
    int fixedAtoms = 0;
    int regions = 0;
    for (const core::GeometryConstraint& rule : rules) {
        if (rule.selection == core::GeometryConstraint::Selection::Region)
            ++regions;
        else
            fixedAtoms += static_cast<int>(rule.indices.size());
    }
    QStringList parts;
    if (fixedAtoms > 0)
        parts << tr("%n atom(s) constrained by index", nullptr, fixedAtoms);
    if (regions > 0)
        parts << tr("%n region rule(s)", nullptr, regions);
    summaryLabel_->setText(tr("Will write %1 ASE constraint(s): %2.")
                               .arg(rules.size())
                               .arg(parts.join(tr(", "))));
}

} // namespace calango::gui
