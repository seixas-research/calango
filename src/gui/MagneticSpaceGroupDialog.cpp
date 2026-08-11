#include "gui/MagneticSpaceGroupDialog.hpp"

#include "core/Element.hpp"
#include "gui/GuiUtils.hpp"

#include <QApplication>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QFrame>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QPushButton>
#include <QTabWidget>
#include <QTableWidget>
#include <QVBoxLayout>

namespace calango::gui {

namespace {

using MSG = pybridge::MagneticSpaceGroup;

enum MomentColumn {
    kIndexColumn = 0,
    kElementColumn,
    kXColumn, kYColumn, kZColumn,
    kMxColumn, kMyColumn, kMzColumn,
    kClassColumn,
};

/// The four Belov-Neronova-Smirnova types, spelled out. The number alone
/// ("type 4") says nothing; what a reader needs is what it MEANS for the
/// crystal, which is a one-line statement in each case.
QString typeDescription(int type)
{
    switch (type) {
    case 1:
        return QObject::tr(
            "<b>Type I</b> — every symmetry operation is unitary "
            "(<i>M</i> = <i>G</i>). Time reversal is broken outright: no "
            "operation of the group needs it. One such group exists for each "
            "of the 230 ordinary space groups.");
    case 2:
        return QObject::tr(
            "<b>Type II</b> (grey group) — time reversal is a symmetry ON ITS "
            "OWN (<i>M</i> = <i>G</i> + <i>T</i>·<i>G</i>). That is only "
            "possible when every moment vanishes, so this is the "
            "<b>non-magnetic</b> case.");
    case 3:
        return QObject::tr(
            "<b>Type III</b> — half of the parent group stays unitary; the "
            "rest survives only in combination with time reversal "
            "(<i>M</i> = <i>G</i> + <i>T</i>·<i>g</i>₀·<i>G</i>, with "
            "<i>g</i>₀ <b>not</b> a pure translation). The magnetic cell is "
            "the crystallographic one. 674 of the 1651 groups.");
    case 4:
        return QObject::tr(
            "<b>Type IV</b> — the halving operation <i>g</i>₀ <b>is a pure "
            "translation</b>, so the group contains an "
            "<b>anti-translation</b>: a lattice translation that is a symmetry "
            "only when combined with time reversal. The magnetic unit cell is "
            "a supercell of the crystallographic one — the classic "
            "two-sublattice antiferromagnet. 517 of the 1651 groups.");
    default:
        return QObject::tr("Type not determined.");
    }
}

QString romanType(int type)
{
    switch (type) {
    case 1: return QStringLiteral("I");
    case 2: return QStringLiteral("II");
    case 3: return QStringLiteral("III");
    case 4: return QStringLiteral("IV");
    default: return QStringLiteral("—");
    }
}

/// "-1 0 0 / 0 -1 0 / 0 0 1" — a 3×3 integer matrix on one line, which is
/// what fits in a table cell and still reads as a matrix.
QString rotationText(const MSG::Operation& op)
{
    QStringList rows;
    for (const auto& row : op.rotation) {
        rows << QStringLiteral("%1 %2 %3")
                    .arg(row[0], 2).arg(row[1], 3).arg(row[2], 3);
    }
    return rows.join(QStringLiteral("  /  "));
}

QString translationText(const MSG::Operation& op)
{
    return QStringLiteral("(%1, %2, %3)")
        .arg(op.translation[0], 0, 'f', 4)
        .arg(op.translation[1], 0, 'f', 4)
        .arg(op.translation[2], 0, 'f', 4);
}

} // namespace

MagneticSpaceGroupDialog::MagneticSpaceGroupDialog(
    std::shared_ptr<const core::Structure> structure, QWidget* parent)
    : QDialog(parent), structure_(std::move(structure))
{
    setWindowTitle(tr("Magnetic Space Group"));
    resize(760, 760);

    auto* layout = new QVBoxLayout(this);

    // --- inputs ----------------------------------------------------------
    auto* inputForm = new QFormLayout;
    layout->addLayout(inputForm);

    sourceCombo_ = new QComboBox(this);
    sourceCombo_->addItem(tr("Automatic (computed, else initial)"),
                          static_cast<int>(MSG::MomentSource::Auto));
    sourceCombo_->addItem(tr("Computed moments (magmoms)"),
                          static_cast<int>(MSG::MomentSource::Computed));
    sourceCombo_->addItem(tr("Initial moments (initial_magmoms)"),
                          static_cast<int>(MSG::MomentSource::Initial));
    sourceCombo_->setToolTip(
        tr("Which per-atom moments to load into the table below.\n\n"
           "A structure can carry both the moments a calculation CONVERGED to "
           "and the ones it was SEEDED with, and they answer different "
           "questions — an ordering that collapsed during the SCF is exactly "
           "the case where they disagree. Whichever is loaded, the table stays "
           "editable."));
    inputForm->addRow(tr("Magnetic moments from:"), sourceCombo_);
    connect(sourceCombo_, &QComboBox::currentIndexChanged, this, [this] {
        reloadMoments();
        detect();
    });

    tolSpin_ = new QDoubleSpinBox(this);
    tolSpin_->setDecimals(5);
    tolSpin_->setRange(1e-5, 1.0);
    tolSpin_->setSingleStep(1e-4);
    tolSpin_->setValue(1e-4);
    tolSpin_->setSuffix(tr(" Å"));
    tolSpin_->setToolTip(tr("Positional symmetry-finding tolerance (symprec)."));

    magTolSpin_ = new QDoubleSpinBox(this);
    magTolSpin_->setDecimals(4);
    magTolSpin_->setRange(1e-4, 5.0);
    magTolSpin_->setSingleStep(0.001);
    magTolSpin_->setValue(0.001);
    magTolSpin_->setSuffix(tr(" μB"));
    magTolSpin_->setToolTip(
        tr("Tolerance on the MOMENTS, separate from the positional one.\n\n"
           "A moment converged to 1.98 μB against its neighbour's −2.02 μB is "
           "one antiferromagnet, not two inequivalent sublattices, and only a "
           "tolerance in μB can say so. Raise it to absorb SCF noise; lower it "
           "to resolve a genuine ferrimagnetic inequivalence."));

    auto* tolRow = new QHBoxLayout;
    tolRow->addWidget(tolSpin_, 1);
    tolRow->addWidget(magTolSpin_, 1);
    auto* detectButton = new QPushButton(tr("Determine"), this);
    tolRow->addWidget(detectButton);
    inputForm->addRow(tr("Tolerances (position / moment):"), tolRow);
    connect(detectButton, &QPushButton::clicked, this,
            &MagneticSpaceGroupDialog::detect);

    // --- result summary ---------------------------------------------------
    auto* summary = new QGroupBox(tr("Magnetic space group"), this);
    auto* summaryForm = new QFormLayout(summary);
    const auto addRow = [&](const QString& caption, QLabel*& label) {
        label = new QLabel(QStringLiteral("—"), summary);
        label->setTextInteractionFlags(Qt::TextSelectableByMouse);
        label->setTextFormat(Qt::RichText);
        summaryForm->addRow(caption, label);
    };
    addRow(tr("BNS number:"), bnsLabel_);
    bnsLabel_->setToolTip(
        tr("Belov-Neronova-Smirnova label <i>S</i>.<i>L</i>: <i>S</i> is one "
           "of the 230 ordinary space groups and <i>L</i> distinguishes its "
           "magnetic descendants. This is the identifier the magnetic "
           "space-group tables are keyed by."));
    addRow(tr("Type:"), typeLabel_);
    addRow(tr("OG number:"), ogLabel_);
    ogLabel_->setToolTip(
        tr("Opechowski-Guccione label — the other convention in the "
           "literature, listed so either can be cross-referenced."));
    addRow(tr("Parent space group:"), parentLabel_);
    addRow(tr("Crystallographic space group:"), crystalLabel_);
    crystalLabel_->setToolTip(
        tr("What the same structure would be with the moments IGNORED. The "
           "comparison is the physics: magnetic order can only lower the "
           "symmetry, and the difference between this and the unitary group "
           "is exactly what the magnetism broke."));
    addRow(tr("Point group (crystal / unitary):"), pointGroupLabel_);
    addRow(tr("Operations (unitary + antiunitary):"), orderLabel_);
    addRow(tr("Magnetic ordering:"), orderingLabel_);
    layout->addWidget(summary);

    classificationLabel_ = new QLabel(this);
    classificationLabel_->setTextFormat(Qt::RichText);
    classificationLabel_->setWordWrap(true);
    classificationLabel_->setFrameShape(QFrame::StyledPanel);
    classificationLabel_->setContentsMargins(8, 6, 8, 6);
    layout->addWidget(classificationLabel_);

    // --- tables -----------------------------------------------------------
    auto* tabs = new QTabWidget(this);
    layout->addWidget(tabs, 1);

    momentTable_ = new QTableWidget(0, 9, tabs);
    momentTable_->setHorizontalHeaderLabels(
        {tr("#"), tr("Element"), QStringLiteral("x"), QStringLiteral("y"),
         QStringLiteral("z"), QStringLiteral("mx"), QStringLiteral("my"),
         QStringLiteral("mz"), tr("Magnetic class")});
    momentTable_->horizontalHeader()->setSectionResizeMode(
        QHeaderView::Stretch);
    momentTable_->verticalHeader()->setVisible(false);
    disableTypeToEdit(momentTable_);
    momentTable_->setToolTip(
        tr("The moments the determination runs on, in μB and in Cartesian "
           "components. The mx / my / mz cells are EDITABLE — flip a "
           "sublattice, cant a moment out of the axis, or type in an ordering "
           "the file never carried, then press Determine.\n\n"
           "A collinear structure (all moments along z) is analysed as "
           "collinear; any transverse component switches spglib to the "
           "non-collinear treatment, in which the moments are axial vectors "
           "that rotate with the operations.\n\n"
           "\"Magnetic class\" is the symmetry-equivalence class under the "
           "MAGNETIC group: two atoms of the same element in different classes "
           "are the two sublattices of an antiferromagnet."));
    tabs->addTab(momentTable_, tr("Magnetic moments"));

    operationTable_ = new QTableWidget(0, 4, tabs);
    operationTable_->setHorizontalHeaderLabels(
        {tr("#"), tr("Rotation"), tr("Translation"), tr("Time reversal")});
    operationTable_->horizontalHeader()->setSectionResizeMode(
        QHeaderView::ResizeToContents);
    operationTable_->horizontalHeader()->setStretchLastSection(true);
    operationTable_->verticalHeader()->setVisible(false);
    operationTable_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    operationTable_->setToolTip(
        tr("Every element of the magnetic group. Those marked 1′ are the "
           "ANTIUNITARY ones — the spatial operation alone is not a symmetry, "
           "only its combination with time reversal is."));
    tabs->addTab(operationTable_, tr("Symmetry operations"));

    statusLabel_ = new QLabel(this);
    statusLabel_->setWordWrap(true);
    layout->addWidget(statusLabel_);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    reloadMoments();
    detect();
}

void MagneticSpaceGroupDialog::reloadMoments()
{
    if (!structure_ || !momentTable_)
        return;
    const auto source = static_cast<MSG::MomentSource>(
        sourceCombo_->currentData().toInt());
    std::string sourceName;
    const auto moments = MSG::momentsFor(*structure_, source, &sourceName);

    const auto& atoms = structure_->atoms();
    const auto& cell = structure_->cell();
    momentTable_->setRowCount(static_cast<int>(atoms.size()));
    for (int i = 0; i < static_cast<int>(atoms.size()); ++i) {
        const core::Vec3 frac = cell.cartesianToFractional(atoms[i].position);
        const core::Vec3 m = moments[static_cast<std::size_t>(i)];
        const auto set = [&](int column, const QString& text, bool editable) {
            auto* item = new QTableWidgetItem(text);
            if (!editable)
                item->setFlags(item->flags() & ~Qt::ItemIsEditable);
            momentTable_->setItem(i, column, item);
        };
        set(kIndexColumn, QString::number(i), false);
        set(kElementColumn,
            QLatin1String(core::Elements::data(atoms[i].atomicNumber).symbol),
            false);
        set(kXColumn, QString::number(frac.x, 'f', 4), false);
        set(kYColumn, QString::number(frac.y, 'f', 4), false);
        set(kZColumn, QString::number(frac.z, 'f', 4), false);
        set(kMxColumn, QString::number(m.x, 'f', 4), true);
        set(kMyColumn, QString::number(m.y, 'f', 4), true);
        set(kMzColumn, QString::number(m.z, 'f', 4), true);
        set(kClassColumn, QStringLiteral("—"), false);
    }

    if (sourceName == "none") {
        statusLabel_->setText(
            tr("<span style='color:#c08040'>This structure carries no "
               "magnetic moments, so every one is zero and the answer is a "
               "grey (type II) group. Type moments into the table and press "
               "Determine to classify an ordering.</span>"));
    } else {
        statusLabel_->setText(
            tr("Moments loaded from <b>%1</b>.")
                .arg(QString::fromStdString(sourceName)));
    }
}

std::vector<core::Vec3> MagneticSpaceGroupDialog::tableMoments() const
{
    std::vector<core::Vec3> moments;
    if (!momentTable_)
        return moments;
    moments.reserve(static_cast<std::size_t>(momentTable_->rowCount()));
    for (int row = 0; row < momentTable_->rowCount(); ++row) {
        core::Vec3 m;
        const auto read = [&](int column) {
            const QTableWidgetItem* item = momentTable_->item(row, column);
            return item ? item->text().toDouble() : 0.0;
        };
        m.x = read(kMxColumn);
        m.y = read(kMyColumn);
        m.z = read(kMzColumn);
        moments.push_back(m);
    }
    return moments;
}

void MagneticSpaceGroupDialog::detect()
{
    if (!structure_)
        return;
    const auto moments = tableMoments();
    if (moments.size() != structure_->size()) {
        clearResult(tr("The moment table is out of step with the structure."));
        return;
    }

    QApplication::setOverrideCursor(Qt::WaitCursor);
    const auto result = MSG::analyze(*structure_, moments, tolSpin_->value(),
                                     magTolSpin_->value());
    QApplication::restoreOverrideCursor();

    if (!result.error.empty()) {
        clearResult(QString::fromStdString(result.error));
        return;
    }
    showResult(result);
}

void MagneticSpaceGroupDialog::clearResult(const QString& reason)
{
    for (QLabel* label : {bnsLabel_, typeLabel_, ogLabel_, parentLabel_,
                          crystalLabel_, pointGroupLabel_, orderLabel_,
                          orderingLabel_}) {
        label->setText(QStringLiteral("—"));
    }
    classificationLabel_->clear();
    operationTable_->setRowCount(0);
    statusLabel_->setText(reason);
}

void MagneticSpaceGroupDialog::showResult(const MSG::Result& result)
{
    bnsLabel_->setText(
        result.bnsNumber.empty()
            ? QStringLiteral("—")
            : QStringLiteral("<b>%1</b>")
                  .arg(QString::fromStdString(result.bnsNumber)));
    typeLabel_->setText(tr("<b>%1</b> &nbsp; (BNS type %2 of 4)")
                            .arg(romanType(result.type))
                            .arg(result.type));
    ogLabel_->setText(result.ogNumber.empty()
                          ? QStringLiteral("—")
                          : QString::fromStdString(result.ogNumber));
    parentLabel_->setText(
        QStringLiteral("%1 (No. %2)")
            .arg(result.parentSymbol.empty()
                     ? QStringLiteral("—")
                     : QString::fromStdString(result.parentSymbol))
            .arg(result.parentNumber));
    crystalLabel_->setText(
        QStringLiteral("%1 (No. %2)")
            .arg(QString::fromStdString(result.crystalSpaceGroup))
            .arg(result.crystalSpaceGroupNumber));
    pointGroupLabel_->setText(
        QStringLiteral("%1 &nbsp;/&nbsp; %2")
            .arg(pointGroupDisplay(
                     QString::fromStdString(result.crystalPointGroup)),
                 pointGroupDisplay(
                     QString::fromStdString(result.unitaryPointGroup))));
    orderLabel_->setText(tr("%1 &nbsp;=&nbsp; %2 unitary + %3 antiunitary")
                             .arg(result.operations)
                             .arg(result.unitaryOperations)
                             .arg(result.antiunitaryOperations));
    orderingLabel_->setText(
        tr("%1 — |Σm| = %2 μB, Σ|m| = %3 μB (%4)")
            .arg(QString::fromStdString(result.ordering))
            .arg(result.totalMoment, 0, 'f', 3)
            .arg(result.absoluteMoment, 0, 'f', 3)
            .arg(result.collinear ? tr("collinear") : tr("non-collinear")));

    QString classification = typeDescription(result.type);
    if (result.hasAntiTranslation) {
        classification += tr("<br><br><b>Anti-translation:</b> (%1, %2, %3) in "
                             "fractional coordinates — the translation that is "
                             "a symmetry only together with time reversal, and "
                             "so the vector by which the magnetic cell exceeds "
                             "the crystallographic one.")
                              .arg(result.antiTranslation[0], 0, 'f', 4)
                              .arg(result.antiTranslation[1], 0, 'f', 4)
                              .arg(result.antiTranslation[2], 0, 'f', 4);
    }
    // The symmetry actually LOST is the number worth stating: it is what
    // distinguishes "the magnetism barely matters" from "the magnetism halves
    // the group".
    const int crystalOps = result.unitaryOperations
        + result.antiunitaryOperations;
    if (result.type != 2 && result.unitaryOperations < crystalOps) {
        classification +=
            tr("<br><br>%1 of the %2 operations survive only in combination "
               "with time reversal: the magnetic order broke them as ordinary "
               "symmetries.")
                .arg(result.antiunitaryOperations)
                .arg(crystalOps);
    }
    classificationLabel_->setText(classification);

    // Magnetic equivalence classes back into the moment table.
    for (int row = 0; row < momentTable_->rowCount(); ++row) {
        QTableWidgetItem* item = momentTable_->item(row, kClassColumn);
        if (!item)
            continue;
        item->setText(
            row < static_cast<int>(result.equivalentAtoms.size())
                ? QString::number(result.equivalentAtoms[
                      static_cast<std::size_t>(row)])
                : QStringLiteral("—"));
    }

    operationTable_->setRowCount(
        static_cast<int>(result.symmetryOperations.size()));
    for (int row = 0; row < static_cast<int>(result.symmetryOperations.size());
         ++row) {
        const auto& op = result.symmetryOperations[static_cast<std::size_t>(row)];
        const auto set = [&](int column, const QString& text) {
            operationTable_->setItem(row, column, new QTableWidgetItem(text));
        };
        set(0, QString::number(row));
        set(1, rotationText(op));
        set(2, translationText(op));
        // 1' is the standard notation for the time-reversal operation, and a
        // primed entry is how a magnetic group is read on paper.
        set(3, op.timeReversal ? QStringLiteral("1′  (antiunitary)")
                               : QStringLiteral("—"));
    }

    statusLabel_->setText(
        tr("%n symmetry-inequivalent magnetic site(s) under the magnetic "
           "group; UNI number %1 of 1651, Litvin %2.",
           nullptr, result.uniqueSites)
            .arg(result.uniNumber)
            .arg(result.litvinNumber));
}

} // namespace calango::gui
