#include "gui/CastSetupDialog.hpp"
#include "gui/GuiUtils.hpp"

#include "core/Structure.hpp"
#include "gui/ViewportWidget.hpp"

#include <QAbstractItemModel>
#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QItemSelection>
#include <QItemSelectionModel>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSignalBlocker>
#include <QTableWidget>
#include <QVBoxLayout>

#include <algorithm>
#include <cstddef>

namespace calango::gui {

namespace {

enum Column { ColIndex = 0, ColElement, ColX, ColY, ColZ, ColCast, ColumnCount };

/// A cast is referred to by its number alone — here, in the Representation
/// panel's dropdown and in the atom table. One name for one thing.
QString castLabel(int cast)
{
    return QString::number(cast);
}

/// Human name of a representation, matching the Representation panel's combo.
QString modeName(render::RepresentationMode mode)
{
    switch (mode) {
    case render::RepresentationMode::BallAndStick:
        return CastSetupDialog::tr("Ball-and-Stick");
    case render::RepresentationMode::SpaceFilling:
        return CastSetupDialog::tr("Space-filling");
    case render::RepresentationMode::Wireframe:
        return CastSetupDialog::tr("Wireframe");
    case render::RepresentationMode::Polyhedral:
        return CastSetupDialog::tr("Polyhedral");
    case render::RepresentationMode::Ribbon:
        return CastSetupDialog::tr("Ribbon Diagram");
    case render::RepresentationMode::MolecularSurface:
        return CastSetupDialog::tr("Molecular Surface");
    case render::RepresentationMode::Licorice:
        return CastSetupDialog::tr("Licorice");
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
    resize(680, 700);

    initialCasts_ = viewport_->style().atomCasts;
    initialCastStyles_ = viewport_->style().castStyles;
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
           "grows sticks into its neighbor."),
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
    disableTypeToEdit(table_);
    table_->verticalHeader()->setVisible(false);
    layout->addWidget(table_, 1);

    // --- Spatial region -----------------------------------------------------
    // A second way to BUILD THE SELECTION, beside the text filter above: a box
    // in space instead of a name in a column — "everything below z = 10 Å is
    // substrate" is how a slab actually splits. It deliberately drives the
    // table SELECTION rather than assigning to a cast directly: the selection
    // is the common currency here, so a region can be widened or pruned with
    // manual Ctrl-clicks before the assign row below commits it. Region →
    // select → assign, composable with everything else feeding the same row.
    auto* regionGroup = new QGroupBox(tr("Spatial region"), this);
    auto* regionLayout = new QVBoxLayout(regionGroup);
    auto* regionNote = new QLabel(
        tr("Ranges are Cartesian Å in the cell's frame — or 0–1 along the "
           "lattice vectors with the fractional box checked. Enabled axes "
           "combine (an atom must satisfy all of them); a disabled axis "
           "constrains nothing. The button replaces the table selection, "
           "which the assign row below then moves."),
        regionGroup);
    regionNote->setWordWrap(true);
    regionLayout->addWidget(regionNote);

    for (int axis = 0; axis < 3; ++axis) {
        RegionAxis& row = regionAxes_[axis];
        auto* axisRow = new QHBoxLayout;
        static const char* const kAxisNames[3] = {"X", "Y", "Z"};
        row.enabled = new QCheckBox(QLatin1String(kAxisNames[axis]), regionGroup);
        row.enabled->setToolTip(
            tr("Constrain this axis. Unchecked leaves it unconstrained, so a "
               "single Z window selects a whole slab layer."));
        axisRow->addWidget(row.enabled);
        row.min = new QDoubleSpinBox(regionGroup);
        row.max = new QDoubleSpinBox(regionGroup);
        row.min->setDecimals(3);
        row.max->setDecimals(3);
        axisRow->addWidget(row.min, 1);
        axisRow->addWidget(new QLabel(tr("to"), regionGroup));
        axisRow->addWidget(row.max, 1);
        regionLayout->addLayout(axisRow);
        // The spins follow the checkbox: a greyed window says "unconstrained"
        // at a glance where an editable-but-ignored one would lie.
        row.min->setEnabled(false);
        row.max->setEnabled(false);
        connect(row.enabled, &QCheckBox::toggled, this,
                [min = row.min, max = row.max](bool on) {
                    min->setEnabled(on);
                    max->setEnabled(on);
                });
    }

    auto* regionActions = new QHBoxLayout;
    fractionalCheck_ = new QCheckBox(tr("Fractional coordinates"), regionGroup);
    const bool periodic = structure_ && structure_->cell().isDefined();
    fractionalCheck_->setEnabled(periodic);
    fractionalCheck_->setToolTip(
        periodic
            ? tr("Ranges as fractions 0–1 along the lattice vectors a, b, c "
                 "instead of Cartesian Å — the natural frame for \"the lower "
                 "half of the cell\" whatever its shape.")
            : tr("Needs a periodic cell: fractional coordinates are positions "
                 "along the lattice vectors, and this structure has none."));
    regionActions->addWidget(fractionalCheck_);
    regionActions->addStretch(1);
    auto* regionSelectButton =
        new QPushButton(tr("Select atoms in region"), regionGroup);
    regionActions->addWidget(regionSelectButton);
    regionLayout->addLayout(regionActions);
    layout->addWidget(regionGroup);
    connect(regionSelectButton, &QPushButton::clicked, this,
            &CastSetupDialog::selectAtomsInRegion);
    connect(fractionalCheck_, &QCheckBox::toggled, this,
            [this](bool) { seedRegionRanges(); });
    seedRegionRanges();

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
    render::StructureRenderer::CastStyle fresh;
    fresh.mode = render::RepresentationMode::BallAndStick;
    viewport_->style().castStyles.push_back(fresh);
    refreshCastChoices();
    apply();
    updateSummary();
}

void CastSetupDialog::removeLastCast()
{
    auto& styles = viewport_->style().castStyles;
    if (styles.empty())
        return; // cast 0 is not removable
    const int removed = static_cast<int>(styles.size()); // its index
    styles.pop_back();
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

void CastSetupDialog::seedRegionRanges()
{
    const bool fractional = fractionalCheck_ && fractionalCheck_->isChecked();

    // Bounding box of the atoms in the active frame, so the seeded window
    // starts out containing everything and the user narrows it from there.
    double lo[3] = {0.0, 0.0, 0.0};
    double hi[3] = {1.0, 1.0, 1.0}; // the seed when there are no atoms
    bool first = true;
    if (structure_) {
        for (const core::Atom& atom : structure_->atoms()) {
            const core::Vec3 p = fractional
                ? structure_->cell().cartesianToFractional(atom.position)
                : atom.position;
            const double v[3] = {p.x, p.y, p.z};
            for (int axis = 0; axis < 3; ++axis) {
                if (first || v[axis] < lo[axis])
                    lo[axis] = v[axis];
                if (first || v[axis] > hi[axis])
                    hi[axis] = v[axis];
            }
            first = false;
        }
    }

    for (int axis = 0; axis < 3; ++axis) {
        for (QDoubleSpinBox* spin : {regionAxes_[axis].min,
                                     regionAxes_[axis].max}) {
            // Generous limits rather than the box itself: a range must be
            // able to reach past the atoms ("everything above the surface").
            // Fractional still allows a few cells either way, because an
            // unwrapped structure's coordinates can sit outside [0, 1).
            spin->setRange(fractional ? -10.0 : -1e4,
                           fractional ? 10.0 : 1e4);
            spin->setSingleStep(fractional ? 0.05 : 0.5);
            spin->setSuffix(fractional ? QString() : tr(" Å"));
        }
        regionAxes_[axis].min->setValue(lo[axis]);
        regionAxes_[axis].max->setValue(hi[axis]);
    }
}

void CastSetupDialog::selectAtomsInRegion()
{
    if (!structure_ || structure_->empty())
        return;
    const bool fractional = fractionalCheck_ && fractionalCheck_->isChecked();

    bool constrained[3];
    double lo[3];
    double hi[3];
    for (int axis = 0; axis < 3; ++axis) {
        constrained[axis] = regionAxes_[axis].enabled->isChecked();
        // A window typed backwards still means the same slab.
        lo[axis] = std::min(regionAxes_[axis].min->value(),
                            regionAxes_[axis].max->value());
        hi[axis] = std::max(regionAxes_[axis].min->value(),
                            regionAxes_[axis].max->value());
    }

    // Table row i is atom i (refreshTable builds it that way), so the region
    // test maps straight onto rows — including rows the text filter currently
    // hides, which the region legitimately reaches: the filter narrows what
    // is SHOWN, not what exists.
    QItemSelection selection;
    const QAbstractItemModel* model = table_->model();
    const std::vector<core::Atom>& atoms = structure_->atoms();
    for (std::size_t i = 0; i < atoms.size()
         && i < static_cast<std::size_t>(table_->rowCount()); ++i) {
        const core::Vec3 p = fractional
            ? structure_->cell().cartesianToFractional(atoms[i].position)
            : atoms[i].position;
        const double v[3] = {p.x, p.y, p.z};
        bool inside = true;
        for (int axis = 0; axis < 3 && inside; ++axis)
            inside = !constrained[axis]
                || (v[axis] >= lo[axis] && v[axis] <= hi[axis]);
        if (!inside)
            continue;
        const int row = static_cast<int>(i);
        selection.select(model->index(row, 0),
                         model->index(row, ColumnCount - 1));
    }
    // ClearAndSelect: the button STATES the region's selection — growing or
    // pruning it afterwards is what the manual Ctrl-clicks are for. Adding to
    // whatever was selected would make the second press mean something
    // different from the first.
    table_->selectionModel()->select(selection,
                                     QItemSelectionModel::ClearAndSelect);
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
    for (int cast = 0; cast < count; ++cast) {
        // The renderer's label rather than the bare number: a cast a module
        // created knows what it is ("Epoxide"), and the summary is where that
        // is worth reading.
        parts << tr("%1: %2 atom(s) as %3")
                     .arg(render::StructureRenderer::castLabel(
                         cast, viewport_->style()))
                     .arg(populations[static_cast<std::size_t>(cast)])
                     .arg(modeName(viewport_->style().castMode(cast)));
    }
    summaryLabel_->setText(parts.join(QStringLiteral("<br>")));
}

void CastSetupDialog::reject()
{
    viewport_->style().atomCasts = initialCasts_;
    viewport_->style().castStyles = initialCastStyles_;
    viewport_->style().mode = initialMode_;
    apply();
    QDialog::reject();
}

} // namespace calango::gui
