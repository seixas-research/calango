#include "gui/StructureEditorDialog.hpp"

#include "core/Element.hpp"
#include "gui/PeriodicTableDialog.hpp"
#include "python_bridge/AseBridge.hpp"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QStackedWidget>
#include <QStandardItemModel>
#include <QTableWidget>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>
#include <functional>
#include <limits>
#include <numeric>
#include <vector>

namespace calango::gui {

namespace {

/// Atom-table columns.
enum AtomColumn {
    ColElement = 0,
    ColX, ColY, ColZ,
    ColU, ColV, ColW,
    AtomColumnCount,
};

double degrees(double radians) { return radians * 180.0 / M_PI; }
double radians(double deg) { return deg * M_PI / 180.0; }

double angleBetween(const core::Vec3& a, const core::Vec3& b)
{
    const double lengths = a.norm() * b.norm();
    if (lengths < 1e-12)
        return 90.0;
    return degrees(std::acos(std::clamp(a.dot(b) / lengths, -1.0, 1.0)));
}

QTableWidgetItem* numberCell(double value, int decimals = 6)
{
    auto* item = new QTableWidgetItem(QString::number(value, 'f', decimals));
    item->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
    return item;
}

} // namespace

StructureEditorDialog::StructureEditorDialog(const core::Structure& structure,
                                             QWidget* parent)
    : QDialog(parent)
    // Deep copy: every edit here is speculative until the dialog is accepted,
    // and the caller still owns the live structure the viewport is drawing.
    , working_(std::make_shared<core::Structure>(structure))
{
    setWindowTitle(tr("Edit Structure"));
    resize(880, 700);

    auto* layout = new QVBoxLayout(this);
    buildCellSection(layout);
    buildAtomSection(layout);

    summaryLabel_ = new QLabel(this);
    summaryLabel_->setWordWrap(true);
    layout->addWidget(summaryLabel_);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel,
                                         this);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);

    refreshAll();
}

bool StructureEditorDialog::hasCell() const
{
    return working_->cell().isDefined();
}

// ---------------------------------------------------------------------------
// Section 1 — Unit Cell Editor
// ---------------------------------------------------------------------------

void StructureEditorDialog::buildCellSection(QVBoxLayout* parent)
{
    auto* group = new QGroupBox(tr("1 · Unit Cell"), this);
    auto* groupLayout = new QVBoxLayout(group);

    cellStack_ = new QStackedWidget(group);

    // -- Page 0: no cell -----------------------------------------------------
    auto* emptyPage = new QWidget(cellStack_);
    auto* emptyLayout = new QVBoxLayout(emptyPage);
    auto* emptyLabel = new QLabel(
        tr("This structure has no unit cell (an isolated molecule or cluster).\n"
           "Define one to enable fractional coordinates, periodic bonding and "
           "vacuum padding."),
        emptyPage);
    emptyLabel->setWordWrap(true);
    defineCellButton_ = new QPushButton(tr("Define Unit Cell…"), emptyPage);
    defineCellButton_->setToolTip(
        tr("Create an orthorhombic cell enclosing the structure, with a "
           "vacuum margin you choose."));
    emptyLayout->addWidget(emptyLabel);
    auto* defineRow = new QHBoxLayout;
    defineRow->addWidget(defineCellButton_);
    defineRow->addStretch(1);
    emptyLayout->addLayout(defineRow);
    emptyLayout->addStretch(1);
    cellStack_->addWidget(emptyPage);
    connect(defineCellButton_, &QPushButton::clicked,
            this, &StructureEditorDialog::defineUnitCell);

    // -- Page 1: cell editor -------------------------------------------------
    auto* cellPage = new QWidget(cellStack_);
    auto* cellLayout = new QVBoxLayout(cellPage);
    cellLayout->setContentsMargins(0, 0, 0, 0);

    // Lattice parameters
    auto* paramRow = new QHBoxLayout;
    static const char* kLengthNames[3] = {"a", "b", "c"};
    static const char* kAngleNames[3] = {"α", "β", "γ"};
    for (int i = 0; i < 3; ++i) {
        auto* spin = new QDoubleSpinBox(cellPage);
        spin->setRange(0.01, 10000.0);
        spin->setDecimals(6);
        spin->setSingleStep(0.01);
        spin->setSuffix(QStringLiteral(" Å"));
        spin->setKeyboardTracking(false); // don't rebuild on every keystroke
        lengthSpins_[static_cast<std::size_t>(i)] = spin;
        paramRow->addWidget(new QLabel(QLatin1String(kLengthNames[i]), cellPage));
        paramRow->addWidget(spin);
        connect(spin, &QDoubleSpinBox::valueChanged,
                this, &StructureEditorDialog::applyLatticeParameters);
    }
    for (int i = 0; i < 3; ++i) {
        auto* spin = new QDoubleSpinBox(cellPage);
        // 0° and 180° are degenerate (zero-volume cell); ASE and spglib both
        // reject them, so keep the editor inside the valid open interval.
        spin->setRange(0.1, 179.9);
        spin->setDecimals(4);
        spin->setSuffix(QStringLiteral("°"));
        spin->setKeyboardTracking(false);
        angleSpins_[static_cast<std::size_t>(i)] = spin;
        paramRow->addWidget(new QLabel(QString::fromUtf8(kAngleNames[i]), cellPage));
        paramRow->addWidget(spin);
        connect(spin, &QDoubleSpinBox::valueChanged,
                this, &StructureEditorDialog::applyLatticeParameters);
    }
    paramRow->addStretch(1);
    auto* paramBox = new QGroupBox(tr("Lattice parameters"), cellPage);
    paramBox->setLayout(paramRow);
    cellLayout->addWidget(paramBox);

    // Raw lattice vectors
    auto* vectorBox = new QGroupBox(tr("Lattice vectors (Cartesian Å)"), cellPage);
    auto* vectorGrid = new QFormLayout(vectorBox);
    for (int v = 0; v < 3; ++v) {
        auto* row = new QHBoxLayout;
        for (int c = 0; c < 3; ++c) {
            auto* spin = new QDoubleSpinBox(vectorBox);
            spin->setRange(-10000.0, 10000.0);
            spin->setDecimals(6);
            spin->setSingleStep(0.01);
            spin->setKeyboardTracking(false);
            vectorSpins_[static_cast<std::size_t>(v)][static_cast<std::size_t>(c)] =
                spin;
            row->addWidget(spin);
            connect(spin, &QDoubleSpinBox::valueChanged,
                    this, &StructureEditorDialog::applyLatticeVectors);
        }
        pbcChecks_[static_cast<std::size_t>(v)] =
            new QCheckBox(tr("periodic"), vectorBox);
        row->addWidget(pbcChecks_[static_cast<std::size_t>(v)]);
        connect(pbcChecks_[static_cast<std::size_t>(v)], &QCheckBox::toggled, this,
                [this] {
                    if (updating_)
                        return;
                    core::UnitCell cell = working_->cell();
                    cell.setPbc({pbcChecks_[0]->isChecked(),
                                 pbcChecks_[1]->isChecked(),
                                 pbcChecks_[2]->isChecked()});
                    working_->setCell(cell);
                });
        vectorGrid->addRow(tr("v%1:").arg(v + 1), row);
    }
    cellLayout->addWidget(vectorBox);

    // Transformations. All five act on the whole structure in place, so they
    // share one compact row rather than being split across two: the previous
    // split implied a distinction (geometry vs. crystallography) that did not
    // survive adding a translation, which is both.
    auto* actionRow = new QHBoxLayout;
    centerButton_ = new QPushButton(tr("Center Structure"), cellPage);
    centerButton_->setToolTip(
        tr("Translate every atom so the structure's centroid sits at the "
           "center of the cell."));
    vacuumButton_ = new QPushButton(tr("Add vacuum…"), cellPage);
    vacuumButton_->setToolTip(
        tr("Extend the cell along chosen directions and re-center the atoms "
           "in the enlarged cell."));
    standardizeButton_ = new QPushButton(tr("Standardize cell"), cellPage);
    standardizeButton_->setToolTip(
        tr("Convert lattice vectors and site positions to the standard "
           "crystallographic convention (spglib.standardize_cell)."));
    primitiveButton_ = new QPushButton(tr("Reduce to Primitive Cell"), cellPage);
    primitiveButton_->setToolTip(
        tr("Transform the cell into its minimal primitive representation "
           "(spglib.find_primitive)."));
    translateButton_ = new QPushButton(tr("Translate atoms…"), cellPage);
    translateButton_->setToolTip(
        tr("Shift every atom by a vector, in Å or in fractional cell "
           "coordinates."));
    for (QPushButton* button : {centerButton_, vacuumButton_, standardizeButton_,
                                primitiveButton_, translateButton_})
        actionRow->addWidget(button);
    actionRow->addStretch(1);
    cellLayout->addLayout(actionRow);
    connect(centerButton_, &QPushButton::clicked,
            this, &StructureEditorDialog::centerInUnitCell);
    connect(vacuumButton_, &QPushButton::clicked,
            this, &StructureEditorDialog::addVacuumLayer);
    connect(standardizeButton_, &QPushButton::clicked,
            this, &StructureEditorDialog::standardizeCell);
    connect(primitiveButton_, &QPushButton::clicked,
            this, &StructureEditorDialog::reduceToPrimitiveCell);
    connect(translateButton_, &QPushButton::clicked,
            this, &StructureEditorDialog::translateAtoms);

    cellStack_->addWidget(cellPage);
    groupLayout->addWidget(cellStack_);
    parent->addWidget(group);
}

std::array<core::Vec3, 3> StructureEditorDialog::vectorsFromParameters() const
{
    const double a = lengthSpins_[0]->value();
    const double b = lengthSpins_[1]->value();
    const double c = lengthSpins_[2]->value();
    const double alpha = radians(angleSpins_[0]->value()); // ∠(b, c)
    const double beta = radians(angleSpins_[1]->value());  // ∠(a, c)
    const double gamma = radians(angleSpins_[2]->value()); // ∠(a, b)

    // Standard crystallographic setting: a along +x, b in the xy plane.
    const core::Vec3 va{a, 0.0, 0.0};
    const core::Vec3 vb{b * std::cos(gamma), b * std::sin(gamma), 0.0};

    const double cx = c * std::cos(beta);
    const double sinGamma = std::sin(gamma);
    // sin γ can't be zero here (the spin box excludes 0°/180°), but guard
    // anyway: a degenerate value would otherwise produce inf/NaN vectors.
    const double cy = std::abs(sinGamma) > 1e-9
        ? c * (std::cos(alpha) - std::cos(beta) * std::cos(gamma)) / sinGamma
        : 0.0;
    const double czSquared = c * c - cx * cx - cy * cy;
    if (czSquared <= 0.0)
        return {va, vb, core::Vec3{cx, cy, 0.0}}; // inconsistent angles
    return {va, vb, core::Vec3{cx, cy, std::sqrt(czSquared)}};
}

void StructureEditorDialog::applyLatticeParameters()
{
    if (updating_)
        return;

    const auto vectors = vectorsFromParameters();
    core::UnitCell cell(vectors[0], vectors[1], vectors[2], working_->cell().pbc());
    if (!cell.isDefined()) {
        // Angles that cannot close a parallelepiped (e.g. α+β < γ). Say so
        // and leave the previous cell in place rather than writing a
        // zero-volume one the rest of the app would choke on.
        summaryLabel_->setStyleSheet(QStringLiteral("color: #d9534f;"));
        summaryLabel_->setText(
            tr("Those a/b/c and α/β/γ values do not describe a valid cell "
               "(zero or imaginary volume) — the previous cell was kept."));
        updating_ = true;
        refreshCellWidgets();
        updating_ = false;
        return;
    }

    working_->setCell(cell);
    updating_ = true;
    refreshCellWidgets(); // push the derived vectors into the vector spins
    updating_ = false;
    refreshAtomTable();   // fractional coordinates moved with the cell
    refreshSummary();
}

void StructureEditorDialog::applyLatticeVectors()
{
    if (updating_)
        return;

    std::array<core::Vec3, 3> vectors{};
    for (int v = 0; v < 3; ++v) {
        vectors[static_cast<std::size_t>(v)] = {
            vectorSpins_[static_cast<std::size_t>(v)][0]->value(),
            vectorSpins_[static_cast<std::size_t>(v)][1]->value(),
            vectorSpins_[static_cast<std::size_t>(v)][2]->value()};
    }
    core::UnitCell cell(vectors[0], vectors[1], vectors[2], working_->cell().pbc());
    working_->setCell(cell);

    updating_ = true;
    refreshCellWidgets(); // a/b/c/α/β/γ follow the vectors
    updating_ = false;
    refreshAtomTable();
    refreshSummary();
}

void StructureEditorDialog::defineUnitCell()
{
    if (working_->empty()) {
        QMessageBox::information(this, windowTitle(),
                                 tr("Add at least one atom before defining a cell."));
        return;
    }

    bool ok = false;
    const double margin = QInputDialog::getDouble(
        this, tr("Define Unit Cell"),
        tr("Vacuum margin around the structure on every side (Å):"),
        5.0, 0.0, 500.0, 2, &ok);
    if (!ok)
        return;

    // Axis-aligned bounding box + margin, then shift the atoms so the box's
    // lower corner is the cell origin (ASE's convention: the cell starts at
    // the origin, so a molecule sitting at negative coordinates would
    // otherwise fall outside its own cell).
    core::Vec3 lo = working_->atoms().front().position;
    core::Vec3 hi = lo;
    for (const auto& atom : working_->atoms()) {
        lo.x = std::min(lo.x, atom.position.x);
        lo.y = std::min(lo.y, atom.position.y);
        lo.z = std::min(lo.z, atom.position.z);
        hi.x = std::max(hi.x, atom.position.x);
        hi.y = std::max(hi.y, atom.position.y);
        hi.z = std::max(hi.z, atom.position.z);
    }
    const core::Vec3 span{hi.x - lo.x + 2.0 * margin,
                          hi.y - lo.y + 2.0 * margin,
                          hi.z - lo.z + 2.0 * margin};
    // A planar or linear molecule has zero extent along some axis; keep the
    // cell non-degenerate regardless of the requested margin.
    const double minimum = 1.0;
    core::UnitCell cell({std::max(span.x, minimum), 0.0, 0.0},
                        {0.0, std::max(span.y, minimum), 0.0},
                        {0.0, 0.0, std::max(span.z, minimum)},
                        {true, true, true});
    working_->setCell(cell);

    const core::Vec3 shift{margin - lo.x, margin - lo.y, margin - lo.z};
    for (auto& atom : working_->atoms())
        atom.position += shift;

    refreshAll();
}


void StructureEditorDialog::translateAtoms()
{
    if (!working_ || working_->empty()) {
        QMessageBox::information(this, tr("Translate Atoms"),
                                 tr("The structure has no atoms."));
        return;
    }

    QDialog dialog(this);
    dialog.setWindowTitle(tr("Translate Atoms"));
    auto* layout = new QVBoxLayout(&dialog);
    auto* form = new QFormLayout;
    layout->addLayout(form);

    auto* note = new QLabel(
        tr("Shifts every atom by the same vector. Fractional coordinates are "
           "the natural choice for a periodic cell — (0.5, 0, 0) is half a "
           "cell along a₁ whatever the lattice parameter is."),
        &dialog);
    note->setWordWrap(true);
    layout->insertWidget(0, note);

    QDoubleSpinBox* components[3] = {nullptr, nullptr, nullptr};
    // Plain ASCII component labels. The previous "Δx"/"Δy"/"Δz" were built from
    // const char* through QLatin1String, which reads each BYTE as Latin-1 — so
    // the two UTF-8 bytes of "Δ" rendered as the mojibake "Î”x". The row is
    // already titled "Translation vector", which carries the delta sense
    // without needing a glyph that has to survive an encoding round-trip.
    const QString labels[3] = {QStringLiteral("X:"), QStringLiteral("Y:"),
                               QStringLiteral("Z:")};
    auto* vectorRow = new QHBoxLayout;
    for (int i = 0; i < 3; ++i) {
        vectorRow->addWidget(new QLabel(labels[i], &dialog));
        components[i] = new QDoubleSpinBox(&dialog);
        components[i]->setDecimals(4);
        components[i]->setRange(-10000.0, 10000.0);
        components[i]->setSingleStep(0.1);
        vectorRow->addWidget(components[i], 1);
    }
    form->addRow(tr("Translation vector:"), vectorRow);

    const bool periodic = working_->cell().isDefined();
    // One dropdown rather than two mutually exclusive checkboxes: the two modes
    // are alternatives, not independent options, and a pair of linked
    // checkboxes has to fake that exclusivity with signal blocking (and can
    // still be driven into a both-off state by a stray setChecked).
    auto* coordinateCombo = new QComboBox(&dialog);
    coordinateCombo->addItem(tr("Cartesian (Å)"), false);
    coordinateCombo->addItem(tr("Fractional (direct cell vectors)"), true);
    if (!periodic) {
        // Without a cell there is nothing to be fractional with respect to.
        // The item stays visible but unselectable, so the mode is discoverable
        // and its unavailability is explained rather than simply absent.
        if (auto* model = qobject_cast<QStandardItemModel*>(coordinateCombo->model()))
            if (QStandardItem* item = model->item(1))
                item->setFlags(item->flags() & ~Qt::ItemIsEnabled);
        coordinateCombo->setToolTip(
            tr("This structure has no unit cell, so only Cartesian shifts "
               "apply."));
    }
    form->addRow(tr("Coordinate system:"), coordinateCombo);
    if (!periodic)
        form->addRow(new QLabel(
            tr("This structure has no cell, so only Cartesian shifts apply."),
            &dialog));

    auto* buttons = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    layout->addWidget(buttons);

    if (dialog.exec() != QDialog::Accepted)
        return;

    const core::Vec3 input{components[0]->value(), components[1]->value(),
                           components[2]->value()};
    // Fractional input is converted through the CELL, not by scaling each
    // component by a lattice length: for a non-orthogonal cell those differ,
    // and the per-length shortcut silently shears the translation.
    const bool fractional =
        coordinateCombo->currentData().toBool() && periodic;
    const core::Vec3 shift =
        fractional ? working_->cell().fractionalToCartesian(input) : input;
    if (shift.norm() < 1e-12)
        return;

    for (core::Atom& atom : working_->atoms())
        atom.position = atom.position + shift;
    refreshAtomTable();
    refreshSummary();
}

void StructureEditorDialog::centerInUnitCell()
{
    if (!hasCell() || working_->empty())
        return;

    const auto& vectors = working_->cell().vectors();
    const core::Vec3 cellCenter = (vectors[0] + vectors[1] + vectors[2]) * 0.5;
    const core::Vec3 shift = cellCenter - working_->centroid();
    for (auto& atom : working_->atoms())
        atom.position += shift;

    refreshAtomTable();
    refreshSummary();
}

void StructureEditorDialog::addVacuumLayer()
{
    if (!hasCell()) {
        QMessageBox::information(
            this, windowTitle(),
            tr("Define a unit cell first — vacuum padding extends an existing "
               "cell."));
        return;
    }

    QDialog dialog(this);
    dialog.setWindowTitle(tr("Add Vacuum Layer"));
    auto* form = new QFormLayout(&dialog);

    auto* amountSpin = new QDoubleSpinBox(&dialog);
    amountSpin->setRange(0.0, 1000.0);
    amountSpin->setDecimals(3);
    amountSpin->setSingleStep(0.5);
    amountSpin->setValue(10.0);
    amountSpin->setSuffix(QStringLiteral(" Å"));
    form->addRow(tr("Vacuum thickness:"), amountSpin);

    auto* bothSidesCheck = new QCheckBox(tr("Split evenly on both sides"), &dialog);
    bothSidesCheck->setChecked(true);
    bothSidesCheck->setToolTip(
        tr("On: the thickness above is the *total* added length, and the "
           "structure ends up centered along that direction (the usual choice "
           "for slabs and clusters).\n"
           "Off: the full amount is added past the structure on the far side "
           "only."));
    form->addRow(QString(), bothSidesCheck);

    // Per-direction: lattice directions rather than Cartesian axes, because
    // vacuum has to grow along the cell vector to stay commensurate with a
    // non-orthogonal cell. For an orthorhombic cell the two coincide.
    std::array<QCheckBox*, 3> axisChecks{};
    static const char* kAxisLabels[3] = {
        QT_TR_NOOP("a (v1)"), QT_TR_NOOP("b (v2)"), QT_TR_NOOP("c (v3)")};
    auto* axisRow = new QHBoxLayout;
    for (int i = 0; i < 3; ++i) {
        axisChecks[static_cast<std::size_t>(i)] =
            new QCheckBox(tr(kAxisLabels[i]), &dialog);
        axisRow->addWidget(axisChecks[static_cast<std::size_t>(i)]);
    }
    // +Z is the overwhelmingly common case (2D slabs); preselect it.
    axisChecks[2]->setChecked(true);
    form->addRow(tr("Along:"), axisRow);

    auto* clearPbcCheck =
        new QCheckBox(tr("Mark the padded directions non-periodic"), &dialog);
    clearPbcCheck->setChecked(true);
    clearPbcCheck->setToolTip(
        tr("Vacuum is normally added precisely to decouple periodic images; "
           "clearing pbc along those directions makes that explicit for the "
           "calculators."));
    form->addRow(QString(), clearPbcCheck);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel,
                                         &dialog);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    form->addRow(buttons);

    if (dialog.exec() != QDialog::Accepted)
        return;

    const double amount = amountSpin->value();
    if (amount <= 0.0)
        return;
    if (!axisChecks[0]->isChecked() && !axisChecks[1]->isChecked()
        && !axisChecks[2]->isChecked()) {
        QMessageBox::information(this, windowTitle(),
                                 tr("Select at least one direction."));
        return;
    }

    auto vectors = working_->cell().vectors();
    auto pbc = working_->cell().pbc();
    for (int axis = 0; axis < 3; ++axis) {
        if (!axisChecks[static_cast<std::size_t>(axis)]->isChecked())
            continue;
        const auto index = static_cast<std::size_t>(axis);
        const double length = vectors[index].norm();
        if (length < 1e-9)
            continue;

        const core::Vec3 unit = vectors[index] / length;
        const double added = amount;
        vectors[index] = unit * (length + added);
        if (clearPbcCheck->isChecked())
            pbc[index] = false;

        if (bothSidesCheck->isChecked()) {
            // Re-center along this direction: project the structure's extent
            // onto the axis and shift so equal vacuum sits on either side.
            double lo = std::numeric_limits<double>::max();
            double hi = std::numeric_limits<double>::lowest();
            for (const auto& atom : working_->atoms()) {
                const double projection = atom.position.dot(unit);
                lo = std::min(lo, projection);
                hi = std::max(hi, projection);
            }
            if (lo <= hi) {
                const double target = 0.5 * (length + added - (hi - lo));
                const core::Vec3 shift = unit * (target - lo);
                for (auto& atom : working_->atoms())
                    atom.position += shift;
            }
        }
    }

    core::UnitCell cell(vectors[0], vectors[1], vectors[2], pbc);
    working_->setCell(cell);
    refreshAll();
}

void StructureEditorDialog::standardizeCell()
{
    if (!hasCell()) {
        QMessageBox::information(
            this, windowTitle(),
            tr("Define a unit cell first — cell standardization needs a "
               "periodic lattice."));
        return;
    }
    try {
        // idealize=true snaps the lattice to its ideal symmetric form.
        working_ = std::make_shared<core::Structure>(
            pybridge::AseBridge::standardizeCell(*working_, /*symprec=*/1e-3,
                                                 /*toPrimitive=*/false,
                                                 /*idealize=*/true));
        refreshAll();
    } catch (const std::exception& e) {
        QMessageBox::critical(this, tr("Standardize Cell"),
                              QString::fromUtf8(e.what()));
    }
}

void StructureEditorDialog::reduceToPrimitiveCell()
{
    if (!hasCell()) {
        QMessageBox::information(
            this, windowTitle(),
            tr("Define a unit cell first — primitive-cell reduction needs a "
               "periodic lattice."));
        return;
    }
    try {
        working_ = std::make_shared<core::Structure>(
            pybridge::AseBridge::standardizeCell(*working_, /*symprec=*/1e-3,
                                                 /*toPrimitive=*/true,
                                                 /*idealize=*/false));
        refreshAll();
    } catch (const std::exception& e) {
        QMessageBox::critical(this, tr("Reduce to Primitive Cell"),
                              QString::fromUtf8(e.what()));
    }
}

// ---------------------------------------------------------------------------
// Section 2 — Atomic positions & elements
// ---------------------------------------------------------------------------

void StructureEditorDialog::buildAtomSection(QVBoxLayout* parent)
{
    auto* group = new QGroupBox(tr("2 · Atomic Positions && Elements"), this);
    auto* layout = new QVBoxLayout(group);

    atomTable_ = new QTableWidget(0, AtomColumnCount, group);
    atomTable_->setHorizontalHeaderLabels({tr("Element"), tr("x (Å)"), tr("y (Å)"),
                                           tr("z (Å)"), tr("u"), tr("v"), tr("w")});
    atomTable_->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    atomTable_->setSelectionBehavior(QAbstractItemView::SelectRows);
    atomTable_->setToolTip(
        tr("Edit Cartesian or fractional coordinates — the other set follows. "
           "Type an element symbol, or use the Periodic Table button."));
    layout->addWidget(atomTable_, 1);
    connect(atomTable_, &QTableWidget::cellChanged,
            this, &StructureEditorDialog::onAtomCellChanged);

    auto* buttonRow = new QHBoxLayout;
    auto* addButton = new QPushButton(tr("Add Atom"), group);
    auto* removeButton = new QPushButton(tr("Remove Selected"), group);
    wrapButton_ = new QPushButton(tr("Wrap within the unit cell"), group);
    originButton_ = new QPushButton(tr("Set as origin"), group);
    auto* elementButton = new QPushButton(tr("Periodic Table…"), group);
    wrapButton_->setToolTip(
        tr("Translate the selected atoms by whole lattice vectors until their "
           "fractional coordinates lie in [0, 1) — the same site, moved into "
           "the cell that is drawn.\n\n"
           "With no rows selected every atom is wrapped. Nothing about the "
           "structure changes but the representative image chosen for each "
           "site."));
    originButton_->setToolTip(
        tr("Put the selected atom at (0, 0, 0) and shift every other atom by "
           "the same vector, so the whole structure is re-expressed about that "
           "site.\n\n"
           "A rigid translation: the lattice, all interatomic distances and "
           "the chemistry are untouched. Select exactly one row."));
    buttonRow->addWidget(addButton);
    buttonRow->addWidget(removeButton);
    buttonRow->addWidget(wrapButton_);
    buttonRow->addWidget(originButton_);
    buttonRow->addWidget(elementButton);
    buttonRow->addStretch(1);
    layout->addLayout(buttonRow);

    connect(addButton, &QPushButton::clicked, this, [this] {
        core::Atom atom;
        atom.atomicNumber = 6;
        // Drop new atoms at the cell center (or the origin without a cell)
        // rather than on top of whatever is already at (0,0,0).
        if (hasCell()) {
            const auto& v = working_->cell().vectors();
            atom.position = (v[0] + v[1] + v[2]) * 0.5;
        }
        working_->addAtom(atom);
        refreshAtomTable();
        refreshSummary();
        atomTable_->selectRow(atomTable_->rowCount() - 1);
    });
    connect(removeButton, &QPushButton::clicked, this, [this] {
        // Collect first, then erase from the back: removeAtom() shifts every
        // later index, so deleting front-to-back would drop the wrong atoms.
        std::vector<int> rows;
        const auto selected = atomTable_->selectionModel()->selectedRows();
        rows.reserve(static_cast<std::size_t>(selected.size()));
        for (const QModelIndex& index : selected)
            rows.push_back(index.row());
        std::sort(rows.begin(), rows.end(), std::greater<int>());
        for (const int row : rows)
            working_->removeAtom(static_cast<std::size_t>(row));
        refreshAtomTable();
        refreshSummary();
    });
    connect(wrapButton_, &QPushButton::clicked, this,
            &StructureEditorDialog::wrapSelectedIntoCell);
    connect(originButton_, &QPushButton::clicked, this,
            &StructureEditorDialog::setSelectedAsOrigin);
    connect(elementButton, &QPushButton::clicked, this, [this] {
        const auto selected = atomTable_->selectionModel()->selectedRows();
        if (selected.isEmpty()) {
            QMessageBox::information(this, windowTitle(),
                                     tr("Select one or more rows first."));
            return;
        }
        const int current =
            working_->atoms()[static_cast<std::size_t>(selected.front().row())]
                .atomicNumber;
        const int z = PeriodicTableDialog::pickElement(this, current);
        if (z <= 0)
            return;
        for (const QModelIndex& index : selected) {
            working_->atoms()[static_cast<std::size_t>(index.row())].atomicNumber = z;
        }
        refreshAtomTable();
        refreshSummary();
    });

    parent->addWidget(group, 1);
}

void StructureEditorDialog::wrapSelectedIntoCell()
{
    if (!hasCell()) {
        QMessageBox::information(
            this, windowTitle(),
            tr("Define a unit cell first — without a lattice there are no cell "
               "boundaries to wrap into."));
        return;
    }

    // No selection means "all atoms": wrapping a whole imported structure back
    // into its cell is the common case, and demanding a select-all first would
    // be busywork. The tooltip says so.
    std::vector<std::size_t> rows;
    for (const QModelIndex& index : atomTable_->selectionModel()->selectedRows())
        rows.push_back(static_cast<std::size_t>(index.row()));
    if (rows.empty()) {
        rows.resize(working_->size());
        std::iota(rows.begin(), rows.end(), std::size_t{0});
    }

    const core::UnitCell& cell = working_->cell();
    // Only the PERIODIC axes wrap: folding a slab's vacuum direction back into
    // the box would push atoms through the vacuum they were placed in. A cell
    // with no periodic axis at all is a plain bounding box, and there the
    // drawn boundaries are exactly what "within the unit cell" means, so all
    // three wrap.
    const std::array<bool, 3> pbc = cell.pbc();
    const bool anyPeriodic = pbc[0] || pbc[1] || pbc[2];
    int moved = 0;
    for (const std::size_t row : rows) {
        if (row >= working_->size())
            continue;
        core::Atom& atom = working_->atoms()[row];
        core::Vec3 fractional = cell.cartesianToFractional(atom.position);
        double* components[3] = {&fractional.x, &fractional.y, &fractional.z};
        bool changed = false;
        for (int axis = 0; axis < 3; ++axis) {
            if (anyPeriodic && !pbc[static_cast<std::size_t>(axis)])
                continue;
            const double wrapped = *components[axis] - std::floor(*components[axis]);
            if (std::abs(wrapped - *components[axis]) > 1e-12) {
                *components[axis] = wrapped;
                changed = true;
            }
        }
        if (!changed)
            continue;
        atom.position = cell.fractionalToCartesian(fractional);
        ++moved;
    }

    refreshAtomTable();
    refreshSummary();
    if (moved == 0) {
        summaryLabel_->setText(
            tr("Every selected atom already lies inside the unit cell."));
    }
}

void StructureEditorDialog::setSelectedAsOrigin()
{
    const auto selected = atomTable_->selectionModel()->selectedRows();
    if (selected.size() != 1) {
        QMessageBox::information(
            this, windowTitle(),
            tr("Select exactly one row — that atom becomes the origin, and "
               "every other atom shifts with it."));
        return;
    }
    const auto row = static_cast<std::size_t>(selected.front().row());
    if (row >= working_->size())
        return;

    // A rigid translation of the whole structure: the lattice is untouched, so
    // every interatomic distance (and hence every calculated property) is
    // unchanged — only the origin the coordinates are quoted against moves.
    const core::Vec3 shift = working_->atoms()[row].position;
    if (shift.dot(shift) < 1e-24) {
        summaryLabel_->setText(tr("That atom is already at the origin."));
        return;
    }
    for (core::Atom& atom : working_->atoms())
        atom.position = atom.position - shift;

    refreshAtomTable();
    refreshSummary();
    atomTable_->selectRow(static_cast<int>(row));
}

void StructureEditorDialog::onAtomCellChanged(int row, int column)
{
    if (updating_)
        return;
    if (row < 0 || row >= static_cast<int>(working_->size()))
        return;

    auto& atom = working_->atoms()[static_cast<std::size_t>(row)];
    const QTableWidgetItem* item = atomTable_->item(row, column);
    if (!item)
        return;
    const QString text = item->text().trimmed();

    if (column == ColElement) {
        const int z = core::Elements::atomicNumber(text.toStdString());
        if (z > 0) {
            atom.atomicNumber = z;
        } else {
            summaryLabel_->setStyleSheet(QStringLiteral("color: #d9534f;"));
            summaryLabel_->setText(tr("“%1” is not a known element symbol.").arg(text));
        }
    } else if (column <= ColZ) {
        bool ok = false;
        const double value = text.toDouble(&ok);
        if (!ok)
            return;
        double* target = column == ColX ? &atom.position.x
                       : column == ColY ? &atom.position.y
                                        : &atom.position.z;
        *target = value;
    } else {
        // Fractional edit: recombine all three components of this row and
        // convert back, so editing u alone doesn't discard v and w.
        if (!hasCell())
            return;
        bool ok = false;
        const double value = text.toDouble(&ok);
        if (!ok)
            return;
        core::Vec3 fractional =
            working_->cell().cartesianToFractional(atom.position);
        double* target = column == ColU ? &fractional.x
                       : column == ColV ? &fractional.y
                                        : &fractional.z;
        *target = value;
        atom.position = working_->cell().fractionalToCartesian(fractional);
    }

    // Rewrite the row so the companion coordinate set (and the element
    // symbol's canonical capitalization) reflect the edit.
    refreshAtomTable();
    refreshSummary();
}

// ---------------------------------------------------------------------------
// Refresh
// ---------------------------------------------------------------------------

void StructureEditorDialog::refreshAll()
{
    updating_ = true;
    refreshCellWidgets();
    updating_ = false;
    refreshAtomTable();
    refreshSummary();
}

void StructureEditorDialog::refreshCellWidgets()
{
    const bool defined = hasCell();
    cellStack_->setCurrentIndex(defined ? 1 : 0);
    centerButton_->setEnabled(defined);
    vacuumButton_->setEnabled(defined);
    if (standardizeButton_)
        standardizeButton_->setEnabled(defined);
    if (primitiveButton_)
        primitiveButton_->setEnabled(defined);
    if (wrapButton_)
        wrapButton_->setEnabled(defined);
    if (!defined)
        return;

    const auto& v = working_->cell().vectors();
    for (int i = 0; i < 3; ++i) {
        lengthSpins_[static_cast<std::size_t>(i)]->setValue(
            std::max(0.01, v[static_cast<std::size_t>(i)].norm()));
        for (int c = 0; c < 3; ++c) {
            const double component = c == 0 ? v[static_cast<std::size_t>(i)].x
                                   : c == 1 ? v[static_cast<std::size_t>(i)].y
                                            : v[static_cast<std::size_t>(i)].z;
            vectorSpins_[static_cast<std::size_t>(i)][static_cast<std::size_t>(c)]
                ->setValue(component);
        }
    }
    // Crystallographic convention: α = ∠(b, c), β = ∠(a, c), γ = ∠(a, b).
    angleSpins_[0]->setValue(angleBetween(v[1], v[2]));
    angleSpins_[1]->setValue(angleBetween(v[0], v[2]));
    angleSpins_[2]->setValue(angleBetween(v[0], v[1]));

    const auto pbc = working_->cell().pbc();
    for (int i = 0; i < 3; ++i)
        pbcChecks_[static_cast<std::size_t>(i)]->setChecked(pbc[static_cast<std::size_t>(i)]);
}

void StructureEditorDialog::refreshAtomTable()
{
    // cellChanged fires for every setItem; suppress it or each refresh would
    // recurse back through onAtomCellChanged.
    updating_ = true;
    atomTable_->setRowCount(static_cast<int>(working_->size()));
    const bool defined = hasCell();
    for (int row = 0; row < static_cast<int>(working_->size()); ++row) {
        const auto& atom = working_->atoms()[static_cast<std::size_t>(row)];
        atomTable_->setItem(row, ColElement,
                            new QTableWidgetItem(QLatin1String(atom.symbol())));
        atomTable_->setItem(row, ColX, numberCell(atom.position.x));
        atomTable_->setItem(row, ColY, numberCell(atom.position.y));
        atomTable_->setItem(row, ColZ, numberCell(atom.position.z));

        if (defined) {
            const core::Vec3 f =
                working_->cell().cartesianToFractional(atom.position);
            atomTable_->setItem(row, ColU, numberCell(f.x));
            atomTable_->setItem(row, ColV, numberCell(f.y));
            atomTable_->setItem(row, ColW, numberCell(f.z));
        } else {
            // Without a cell there are no fractional coordinates; show the
            // columns as read-only dashes rather than misleading zeros.
            for (int column : {ColU, ColV, ColW}) {
                auto* item = new QTableWidgetItem(QStringLiteral("—"));
                item->setFlags(item->flags() & ~Qt::ItemIsEditable);
                item->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
                atomTable_->setItem(row, column, item);
            }
        }
    }
    updating_ = false;
}

void StructureEditorDialog::refreshSummary()
{
    summaryLabel_->setStyleSheet(QString());
    if (hasCell()) {
        summaryLabel_->setText(tr("%1 · %2 atoms · cell volume %3 Å³")
                                   .arg(QString::fromStdString(
                                       working_->chemicalFormula()))
                                   .arg(working_->size())
                                   .arg(working_->cell().volume(), 0, 'f', 3));
    } else {
        summaryLabel_->setText(tr("%1 · %2 atoms · no unit cell")
                                   .arg(QString::fromStdString(
                                       working_->chemicalFormula()))
                                   .arg(working_->size()));
    }
}

} // namespace calango::gui
