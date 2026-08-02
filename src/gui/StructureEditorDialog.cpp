#include "gui/StructureEditorDialog.hpp"

#include "core/Element.hpp"
#include "gui/PeriodicTableDialog.hpp"
#include "python_bridge/AseBridge.hpp"

#include <QApplication>
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
#include <QSignalBlocker>
#include <QStackedWidget>
#include <QStandardItemModel>
#include <QTableWidget>
#include <QVBoxLayout>

#include <algorithm>
#include <numeric>
#include <cmath>
#include <functional>
#include <limits>
#include <numeric>
#include <vector>

namespace calango::gui {

namespace {

/// Fixed atom-table columns.
///
/// ONE coordinate triple, not two. The table used to carry Cartesian (x, y, z)
/// and fractional (u, v, w) side by side; the "Fractional coordinates" toggle
/// now decides which of the two is shown, and the columns are relabelled with
/// it. That is what makes room for the extended per-atom properties an
/// extended-XYZ trajectory carries — charges, velocities, forces — which are
/// appended after the magnetic-moment columns and are what a table of "atomic
/// configurations" is actually for.
///
/// The magnetic-moment columns come after these (added/removed with the spin
/// mode), and the extended-property columns after those, so neither is in the
/// enum.
enum AtomColumn {
    ColElement = 0,
    ColC1, ColC2, ColC3, ///< x/y/z or u/v/w, per the fractional toggle
    AtomColumnCount,
};

/// Spin-polarization modes offered in the combo, in combo order. A local enum
/// rather than core::SpinMode: the structure editor is about the structure, and
/// pulling in the calculator configuration would make a geometry editor depend
/// on what DFT codes call things. The orders are deliberately identical, and
/// the moments themselves are what actually travel between the two.
enum class SpinDisplayMode { Unpolarized, Collinear, NonCollinear };

/// Sort keys offered in the "Sort by" combo, in combo order.
enum class SortKey { Element, X, Y, Z, U, V, W };

/// The name ASE (and therefore every generated script) reads initial moments
/// from. Using ASE's own key is what makes the moments survive the round trip
/// through the staged extxyz without any translation step.
constexpr const char* kInitialMagmoms = "initial_magmoms";

/// The two extended arrays the table lets the user EDIT (see PropertyColumn in
/// the header for why exactly these). Named as ASE names them: "velocities" is
/// what AseBridge derives from momenta/masses on import and feeds back through
/// atoms.set_velocities() on export, and "forces" is the array/
/// SinglePointCalculator column — so an edit written under these keys is
/// exactly what a save to extxyz carries.
constexpr const char* kVelocities = "velocities";
constexpr const char* kForces = "forces";

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

    // Transformations that need this dialog's own editing context. Centring,
    // vacuum padding and wrapping moved to the Structure panel's action row —
    // they are one-click whole-structure operations that want to be undoable
    // through the document, not reverted only by cancelling this dialog.
    auto* actionRow = new QHBoxLayout;
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
    for (QPushButton* button : {standardizeButton_, primitiveButton_,
                                translateButton_})
        actionRow->addWidget(button);
    actionRow->addStretch(1);
    cellLayout->addLayout(actionRow);
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
    auto* group = new QGroupBox(tr("2 · Atomic Configurations"), this);
    auto* layout = new QVBoxLayout(group);

    // -- Sorting and spin ----------------------------------------------------
    auto* optionRow = new QHBoxLayout;
    optionRow->addWidget(new QLabel(tr("Sort by:"), group));
    sortKeyCombo_ = new QComboBox(group);
    // Order matches SortKey.
    sortKeyCombo_->addItem(tr("Element"), static_cast<int>(SortKey::Element));
    sortKeyCombo_->addItem(tr("x"), static_cast<int>(SortKey::X));
    sortKeyCombo_->addItem(tr("y"), static_cast<int>(SortKey::Y));
    sortKeyCombo_->addItem(tr("z"), static_cast<int>(SortKey::Z));
    sortKeyCombo_->addItem(tr("u"), static_cast<int>(SortKey::U));
    sortKeyCombo_->addItem(tr("v"), static_cast<int>(SortKey::V));
    sortKeyCombo_->addItem(tr("w"), static_cast<int>(SortKey::W));
    sortKeyCombo_->setToolTip(
        tr("Sorting RENUMBERS the atoms — it reorders the structure itself, "
           "not just this view.\n\n"
           "That is usually the point: grouping by element is what VASP's "
           "POSCAR/POTCAR pairing needs, and sorting by z gives a slab a "
           "layer-ordered index list. Everything index-aligned (forces, "
           "moments, bond orders, residues) moves with its atom."));
    optionRow->addWidget(sortKeyCombo_);
    sortDescendingCheck_ = new QCheckBox(tr("descending"), group);
    optionRow->addWidget(sortDescendingCheck_);
    auto* sortButton = new QPushButton(tr("Sort"), group);
    optionRow->addWidget(sortButton);
    optionRow->addSpacing(18);

    optionRow->addWidget(new QLabel(tr("Spin polarization:"), group));
    spinModeCombo_ = new QComboBox(group);
    // Order matches core::SpinMode.
    spinModeCombo_->addItem(tr("Unpolarized"));
    spinModeCombo_->addItem(tr("Collinear Spin-Polarized"));
    spinModeCombo_->addItem(tr("Non-collinear Spin"));
    spinModeCombo_->setToolTip(
        tr("Adds editable magnetic-moment columns to the table below.\n\n"
           "Collinear gives one moment per atom (μB, signed — that sign is "
           "what makes a seed antiferromagnetic rather than ferromagnetic); "
           "non-collinear gives the full (mx, my, mz) vector.\n\n"
           "What you type is stored as the structure's INITIAL magnetic "
           "moments: it travels with the geometry into every calculation, and "
           "can be drawn in the viewport through Representation → Vector "
           "overlay → Initial magnetic moments."));
    optionRow->addWidget(spinModeCombo_);
    optionRow->addSpacing(18);

    fractionalCheck_ = new QCheckBox(tr("Fractional coordinates"), group);
    fractionalCheck_->setChecked(false);
    fractionalCheck_->setToolTip(
        tr("Show the three coordinate columns as fractional (u, v, w) instead "
           "of Cartesian (x, y, z) Å.\n\n"
           "The values convert as you toggle and stay editable either way — "
           "editing a fractional coordinate recombines all three components "
           "of that row and converts back, so changing u alone does not "
           "discard v and w.\n\n"
           "Needs a unit cell: without one there are no fractional "
           "coordinates to show."));
    connect(fractionalCheck_, &QCheckBox::toggled, this, [this](bool) {
        // Commit whatever is in the moment cells before the columns are
        // rewritten, exactly as the spin-mode switch does.
        writeMomentsToStructure();
        refreshAtomTable();
    });
    optionRow->addWidget(fractionalCheck_);
    optionRow->addStretch(1);
    layout->addLayout(optionRow);

    connect(sortButton, &QPushButton::clicked, this,
            &StructureEditorDialog::applySort);
    connect(spinModeCombo_, &QComboBox::currentIndexChanged, this,
            &StructureEditorDialog::onSpinModeChanged);

    atomTable_ = new QTableWidget(0, AtomColumnCount, group);
    atomTable_->setHorizontalHeaderLabels(
        {tr("Element"), tr("x (Å)"), tr("y (Å)"), tr("z (Å)")});
    atomTable_->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    atomTable_->setSelectionBehavior(QAbstractItemView::SelectRows);
    atomTable_->setToolTip(
        tr("Element and coordinates are editable; toggle "
           "\"Fractional coordinates\" to switch the three coordinate columns "
           "between Å and cell fractions.\n\n"
           "Columns to the right of the moments are the extended per-atom "
           "properties the structure arrived with. Velocities and forces are "
           "editable — hand-set velocities are the initial conditions of a "
           "molecular-dynamics run, and hand-set forces are how force-field "
           "training frames and force-arrow viewers are fed. Computed arrays "
           "such as charges stay read-only: they are results of a "
           "calculation, and typing over them would leave a frame whose "
           "arrays no longer match anything that was computed."));
    layout->addWidget(atomTable_, 1);
    connect(atomTable_, &QTableWidget::cellChanged,
            this, &StructureEditorDialog::onAtomCellChanged);

    auto* buttonRow = new QHBoxLayout;
    auto* addButton = new QPushButton(tr("Add Atom"), group);
    auto* removeButton = new QPushButton(tr("Remove Selected"), group);
    originButton_ = new QPushButton(tr("Set as origin"), group);
    auto* elementButton = new QPushButton(tr("Periodic Table…"), group);
    originButton_->setToolTip(
        tr("Put the selected atom at (0, 0, 0) and shift every other atom by "
           "the same vector, so the whole structure is re-expressed about that "
           "site.\n\n"
           "A rigid translation: the lattice, all interatomic distances and "
           "the chemistry are untouched. Select exactly one row."));
    buttonRow->addWidget(addButton);
    buttonRow->addWidget(removeButton);
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


int StructureEditorDialog::momentColumnCount() const
{
    if (!spinModeCombo_)
        return 0;
    switch (static_cast<SpinDisplayMode>(spinModeCombo_->currentIndex())) {
    case SpinDisplayMode::Collinear:
        return 1;
    case SpinDisplayMode::NonCollinear:
        return 3;
    case SpinDisplayMode::Unpolarized:
        break;
    }
    return 0;
}

std::vector<core::Vec3> StructureEditorDialog::momentsFromStructure() const
{
    std::vector<core::Vec3> moments(working_->size(), core::Vec3{0, 0, 0});
    // Vector first: a non-collinear structure has only the (N,3) form, and a
    // collinear one carries both (the vector copy exists so the viewport
    // overlay has a direction to draw).
    const auto vectors = working_->vectorFields().find(kInitialMagmoms);
    if (vectors != working_->vectorFields().end()
        && vectors->second.size() == moments.size())
        return vectors->second;
    const auto scalars = working_->scalarFields().find(kInitialMagmoms);
    if (scalars != working_->scalarFields().end()
        && scalars->second.size() == moments.size()) {
        for (std::size_t i = 0; i < moments.size(); ++i)
            moments[i] = {0.0, 0.0, scalars->second[i]};
    }
    return moments;
}

void StructureEditorDialog::writeMomentsToStructure()
{
    const int columns = momentColumnCount();
    const auto n = working_->size();

    if (columns == 0) {
        // Unpolarized means "no moments", not "moments of zero": an all-zero
        // initial_magmoms column would still switch a calculator into
        // spin-polarized mode and cost the run its speed for nothing.
        working_->setScalarField(kInitialMagmoms, {});
        working_->setVectorField(kInitialMagmoms, {});
        return;
    }

    std::vector<core::Vec3> vectors(n, core::Vec3{0, 0, 0});
    std::vector<double> scalars(n, 0.0);
    for (std::size_t row = 0; row < n; ++row) {
        const auto readCell = [&](int column) {
            const QTableWidgetItem* item =
                atomTable_->item(static_cast<int>(row), column);
            if (!item)
                return 0.0;
            bool ok = false;
            const double value = item->text().trimmed().toDouble(&ok);
            return ok ? value : 0.0;
        };
        if (columns == 1) {
            scalars[row] = readCell(kFirstMomentColumn);
            // Promoted to (0, 0, m) so the viewport's vector overlay has a
            // direction: a collinear calculation quantizes along z by
            // convention, and up/down then read as opposite arrows.
            vectors[row] = {0.0, 0.0, scalars[row]};
        } else {
            vectors[row] = {readCell(kFirstMomentColumn),
                            readCell(kFirstMomentColumn + 1),
                            readCell(kFirstMomentColumn + 2)};
        }
    }

    // Collinear stores BOTH: the scalar is the real datum (and the one written
    // as `initial_magmoms:R:1`, which is what ASE reads back), the vector is
    // the display promotion. Non-collinear has only the vector — writing a
    // scalar too would let the export path mistake it for a collinear run.
    working_->setScalarField(kInitialMagmoms,
                             columns == 1 ? scalars : std::vector<double>{});
    working_->setVectorField(kInitialMagmoms, std::move(vectors));
}

void StructureEditorDialog::onSpinModeChanged()
{
    if (updating_)
        return;
    // Read what is currently in the table BEFORE the columns change, so
    // switching collinear -> non-collinear keeps the moments already typed
    // (as the z component) instead of discarding them.
    writeMomentsToStructure();
    refreshAtomTable();
    refreshSummary();
}

void StructureEditorDialog::applySort()
{
    const auto n = working_->size();
    if (n < 2)
        return;
    const auto key = static_cast<SortKey>(sortKeyCombo_->currentData().toInt());
    const bool fractional = key == SortKey::U || key == SortKey::V
        || key == SortKey::W;
    if (fractional && !hasCell()) {
        summaryLabel_->setStyleSheet(QStringLiteral("color: #d9534f;"));
        summaryLabel_->setText(
            tr("Fractional coordinates need a unit cell — sort by x, y, z or "
               "element instead."));
        return;
    }

    // Whatever is typed in the moment cells is part of the structure, so it
    // has to be committed before the permutation moves the rows.
    writeMomentsToStructure();

    const auto sortValue = [&](std::size_t index) {
        const core::Atom& atom = working_->atoms()[index];
        const core::Vec3 p = fractional
            ? working_->cell().cartesianToFractional(atom.position)
            : atom.position;
        switch (key) {
        case SortKey::Element:
            return static_cast<double>(atom.atomicNumber);
        case SortKey::X:
        case SortKey::U:
            return p.x;
        case SortKey::Y:
        case SortKey::V:
            return p.y;
        case SortKey::Z:
        case SortKey::W:
            return p.z;
        }
        return 0.0;
    };

    std::vector<std::size_t> order(n);
    std::iota(order.begin(), order.end(), std::size_t{0});
    // Stable, so atoms that tie on the key (every atom of one element, all
    // sites in one layer) keep their existing relative order rather than being
    // shuffled arbitrarily on each sort.
    std::stable_sort(order.begin(), order.end(),
                     [&](std::size_t a, std::size_t b) {
                         const double va = sortValue(a);
                         const double vb = sortValue(b);
                         return sortDescendingCheck_->isChecked() ? va > vb
                                                                  : va < vb;
                     });
    working_->reorder(order);

    refreshAtomTable();
    refreshSummary();
    summaryLabel_->setStyleSheet(QString());
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

    // Property columns. Velocities and forces write through to the working
    // structure's vector fields immediately, exactly as a coordinate edit
    // writes through to the position — which is what lets a later sort carry
    // the edited value with its atom, and what the caller's undo-aware install
    // of result() snapshots on accept. The remaining columns are read-only, so
    // a change there can only be a programmatic one that slipped the
    // `updating_` guard; ignore it rather than parsing a result column as
    // geometry.
    if (column >= firstPropertyColumn()) {
        const std::vector<PropertyColumn> properties = propertyColumns();
        const auto index =
            static_cast<std::size_t>(column - firstPropertyColumn());
        if (index >= properties.size() || !properties[index].editable)
            return;
        const PropertyColumn& property = properties[index];
        bool ok = false;
        const double value = text.toDouble(&ok);
        if (!ok) {
            refreshAtomTable(); // restore the stored value over the typo
            return;
        }
        // Copy-modify-set rather than mutating in place: setVectorField() is
        // the one write path, and the field is guaranteed present — a column
        // only exists for an array the structure already carries.
        auto vectors = working_->vectorFields().at(property.field);
        core::Vec3& edited = vectors[static_cast<std::size_t>(row)];
        (&edited.x)[property.component] = value;
        const double magnitude = edited.norm();
        working_->setVectorField(property.field, std::move(vectors));
        // The import bridge derives a "|name|" magnitude scalar for color
        // mapping; left stale, it would color the atom by the force it no
        // longer has. (Never created here — only refreshed where it exists.)
        const std::string magnitudeField = "|" + property.field + "|";
        if (const auto it = working_->scalarFields().find(magnitudeField);
            it != working_->scalarFields().end()
            && it->second.size() == working_->size()) {
            auto magnitudes = it->second;
            magnitudes[static_cast<std::size_t>(row)] = magnitude;
            working_->setScalarField(magnitudeField, std::move(magnitudes));
        }
        refreshAtomTable(); // the |name| column has to follow the edit
        refreshSummary();
        return;
    }

    if (column >= kFirstMomentColumn) {
        // A moment edit needs no coordinate round trip; commit the whole
        // moment set and leave the rest of the table alone, so the cursor does
        // not jump out of the cell being typed in.
        bool ok = false;
        (void)text.toDouble(&ok);
        if (!ok)
            return;
        writeMomentsToStructure();
        refreshSummary();
        return;
    }

    if (column == ColElement) {
        const int z = core::Elements::atomicNumber(text.toStdString());
        if (z > 0) {
            atom.atomicNumber = z;
        } else {
            summaryLabel_->setStyleSheet(QStringLiteral("color: #d9534f;"));
            summaryLabel_->setText(tr("“%1” is not a known element symbol.").arg(text));
        }
    } else {
        bool ok = false;
        const double value = text.toDouble(&ok);
        if (!ok)
            return;
        const int component = column - ColC1; // 0, 1 or 2
        const bool fractional = fractionalCheck_ && fractionalCheck_->isChecked()
            && hasCell();
        if (fractional) {
            // Recombine all three components of this row and convert back, so
            // editing u alone does not discard v and w.
            core::Vec3 coordinates =
                working_->cell().cartesianToFractional(atom.position);
            (&coordinates.x)[component] = value;
            atom.position = working_->cell().fractionalToCartesian(coordinates);
        } else {
            (&atom.position.x)[component] = value;
        }
    }

    // Rewrite the row so the element symbol's canonical capitalization — and,
    // in fractional mode, the round trip through the cell — are reflected.
    refreshAtomTable();
    refreshSummary();
}

// ---------------------------------------------------------------------------
// Refresh
// ---------------------------------------------------------------------------

void StructureEditorDialog::refreshAll()
{
    // Open on the mode the structure is already in, so a magnetic structure
    // shows its moments rather than hiding them behind a combo the user has to
    // know to change. The (N,3) form wins: a structure carrying both is
    // collinear (the vector copy is this dialog's own display promotion), and
    // only a vector WITHOUT a scalar is genuinely non-collinear.
    if (spinModeCombo_ && spinModeCombo_->currentIndex() == 0) {
        const bool hasScalar =
            working_->scalarFields().count(kInitialMagmoms) > 0;
        const bool hasVector =
            working_->vectorFields().count(kInitialMagmoms) > 0;
        const QSignalBlocker block(spinModeCombo_);
        if (hasScalar)
            spinModeCombo_->setCurrentIndex(
                static_cast<int>(SpinDisplayMode::Collinear));
        else if (hasVector)
            spinModeCombo_->setCurrentIndex(
                static_cast<int>(SpinDisplayMode::NonCollinear));
    }

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
    if (standardizeButton_)
        standardizeButton_->setEnabled(defined);
    if (primitiveButton_)
        primitiveButton_->setEnabled(defined);
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

std::vector<StructureEditorDialog::PropertyColumn>
StructureEditorDialog::propertyColumns() const
{
    std::vector<PropertyColumn> columns;
    if (!working_)
        return columns;
    const auto n = working_->size();

    // Scalars first, then vectors: a charge is one number and a velocity is
    // three, and interleaving them by name would scatter the components of a
    // vector across the table.
    for (const auto& [name, values] : working_->scalarFields()) {
        if (name == kInitialMagmoms || values.size() != n)
            continue;
        columns.push_back({QString::fromStdString(name),
                           name, /*vector=*/false, 0});
    }
    for (const auto& [name, values] : working_->vectorFields()) {
        // A collinear structure carries initial_magmoms as BOTH a scalar and a
        // promoted vector; the spin-mode columns own it either way.
        if (name == kInitialMagmoms || values.size() != n)
            continue;
        // Velocities and forces are input as much as output (MD initial
        // conditions; hand-built training forces) — the only editable arrays.
        const bool editable = name == kVelocities || name == kForces;
        const QString base = QString::fromStdString(name);
        for (int component = 0; component < 3; ++component)
            columns.push_back(
                {QStringLiteral("%1 %2")
                     .arg(base, QStringLiteral("xyz").at(component)),
                 name, /*vector=*/true, component, editable});
    }
    return columns;
}

int StructureEditorDialog::firstPropertyColumn() const
{
    return AtomColumnCount + momentColumnCount();
}

void StructureEditorDialog::refreshAtomTable()
{
    // cellChanged fires for every setItem; suppress it or each refresh would
    // recurse back through onAtomCellChanged.
    updating_ = true;
    const bool defined = hasCell();
    // Fractional coordinates are undefined without a cell, so the toggle is
    // disabled AND forced off there — a checked-but-inapplicable box would
    // leave three columns of dashes and no way to read a coordinate at all.
    if (fractionalCheck_) {
        fractionalCheck_->setEnabled(defined);
        if (!defined && fractionalCheck_->isChecked()) {
            const QSignalBlocker block(fractionalCheck_);
            fractionalCheck_->setChecked(false);
        }
    }
    const bool fractional =
        defined && fractionalCheck_ && fractionalCheck_->isChecked();

    const int momentColumns = momentColumnCount();
    const std::vector<PropertyColumn> properties = propertyColumns();
    atomTable_->setColumnCount(AtomColumnCount + momentColumns
                               + static_cast<int>(properties.size()));

    QStringList headers = {tr("Element")};
    if (fractional)
        headers << tr("u") << tr("v") << tr("w");
    else
        headers << tr("x (Å)") << tr("y (Å)") << tr("z (Å)");
    if (momentColumns == 1)
        headers << tr("m (μB)");
    else if (momentColumns == 3)
        headers << tr("mx (μB)") << tr("my (μB)") << tr("mz (μB)");
    for (const PropertyColumn& property : properties)
        headers << property.header;
    atomTable_->setHorizontalHeaderLabels(headers);

    const std::vector<core::Vec3> moments = momentsFromStructure();
    atomTable_->setRowCount(static_cast<int>(working_->size()));
    const int propertyBase = firstPropertyColumn();
    for (int row = 0; row < static_cast<int>(working_->size()); ++row) {
        const auto& atom = working_->atoms()[static_cast<std::size_t>(row)];
        atomTable_->setItem(row, ColElement,
                            new QTableWidgetItem(QLatin1String(atom.symbol())));

        const core::Vec3 shown = fractional
            ? working_->cell().cartesianToFractional(atom.position)
            : atom.position;
        atomTable_->setItem(row, ColC1, numberCell(shown.x));
        atomTable_->setItem(row, ColC2, numberCell(shown.y));
        atomTable_->setItem(row, ColC3, numberCell(shown.z));

        if (momentColumns > 0) {
            const core::Vec3& m = moments[static_cast<std::size_t>(row)];
            if (momentColumns == 1) {
                atomTable_->setItem(row, kFirstMomentColumn, numberCell(m.z, 3));
            } else {
                atomTable_->setItem(row, kFirstMomentColumn, numberCell(m.x, 3));
                atomTable_->setItem(row, kFirstMomentColumn + 1,
                                    numberCell(m.y, 3));
                atomTable_->setItem(row, kFirstMomentColumn + 2,
                                    numberCell(m.z, 3));
            }
        }

        for (std::size_t p = 0; p < properties.size(); ++p) {
            const PropertyColumn& property = properties[p];
            double value = 0.0;
            if (property.vector) {
                const core::Vec3& v =
                    working_->vectorFields().at(property.field)
                        [static_cast<std::size_t>(row)];
                value = property.component == 0 ? v.x
                      : property.component == 1 ? v.y
                                                : v.z;
            } else {
                value = working_->scalarFields().at(property.field)
                    [static_cast<std::size_t>(row)];
            }
            // Editable columns are formatted like the coordinates (6
            // decimals): a thermal velocity is ~1e-3 in ASE units, and four
            // decimals would round a typed value visibly on the rewrite.
            auto* item = numberCell(value, property.editable ? 6 : 4);
            if (!property.editable) {
                // Greyed as well as flagged, so "cannot be typed over" is
                // visible before the first rejected double-click.
                item->setFlags(item->flags() & ~Qt::ItemIsEditable);
                item->setForeground(QApplication::palette().brush(
                    QPalette::Disabled, QPalette::Text));
            }
            atomTable_->setItem(row, propertyBase + static_cast<int>(p), item);
        }
    }
    updating_ = false;
}

void StructureEditorDialog::refreshSummary()
{
    summaryLabel_->setStyleSheet(QString());
    QString spinNote;
    if (momentColumnCount() > 0) {
        // The NET moment, because that is the number that says whether the
        // seed is ferromagnetic or antiferromagnetic — a column of 2.2 and a
        // column of ±2.2 look nearly identical and mean opposite things.
        double net = 0.0;
        double magnitude = 0.0;
        for (const core::Vec3& m : momentsFromStructure()) {
            net += m.z;
            magnitude += m.norm();
        }
        spinNote = tr(" · net moment %1 μB (Σ|m| %2)")
                       .arg(net, 0, 'f', 2)
                       .arg(magnitude, 0, 'f', 2);
    }
    if (hasCell()) {
        summaryLabel_->setText(tr("%1 · %2 atoms · cell volume %3 Å³%4")
                                   .arg(QString::fromStdString(
                                       working_->chemicalFormula()))
                                   .arg(working_->size())
                                   .arg(working_->cell().volume(), 0, 'f', 3)
                                   .arg(spinNote));
    } else {
        summaryLabel_->setText(tr("%1 · %2 atoms · no unit cell%3")
                                   .arg(QString::fromStdString(
                                       working_->chemicalFormula()))
                                   .arg(working_->size())
                                   .arg(spinNote));
    }
}

} // namespace calango::gui
