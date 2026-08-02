#include "gui/PhaseColorsDialog.hpp"

#include "gui/GuiUtils.hpp"
#include "gui/ViewportWidget.hpp"
#include "render/StructureRenderer.hpp"

#include <QCheckBox>
#include <QColorDialog>
#include <QDialogButtonBox>
#include <QHeaderView>
#include <QLabel>
#include <QPushButton>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QVBoxLayout>

namespace calango::gui {

namespace {

enum Column { ColPhase = 0, ColAtoms, ColFraction, ColColor, ColReset,
              ColumnCount };

/// Display name of a phase. Translated here — core::toString() is the stable
/// machine-readable name that goes into exported files, and the two must not be
/// the same function or a locale would change what is written to disk.
QString phaseName(core::StructuralPhase phase)
{
    switch (phase) {
    case core::StructuralPhase::Fcc:
        return QObject::tr("FCC");
    case core::StructuralPhase::Hcp:
        return QObject::tr("HCP");
    case core::StructuralPhase::Bcc:
        return QObject::tr("BCC");
    case core::StructuralPhase::Icosahedral:
        return QObject::tr("Icosahedral");
    case core::StructuralPhase::CubicDiamond:
        return QObject::tr("Cubic diamond");
    case core::StructuralPhase::HexagonalDiamond:
        return QObject::tr("Hexagonal diamond");
    case core::StructuralPhase::Other:
        break;
    }
    return QObject::tr("Other / unidentified");
}

QString phaseTooltip(core::StructuralPhase phase)
{
    switch (phase) {
    case core::StructuralPhase::Fcc:
        return QObject::tr("Twelve neighbours, all with CNA signature (4,2,1) "
                           "— cubic close packing.");
    case core::StructuralPhase::Hcp:
        return QObject::tr("Twelve neighbours, half (4,2,1) and half (4,2,2). "
                           "Inside an fcc grain these are the STACKING FAULTS "
                           "and twin boundaries: a coherent twin shows as two "
                           "adjacent hcp planes.");
    case core::StructuralPhase::Bcc:
        return QObject::tr("Fourteen neighbours (8 first + 6 second), "
                           "6 x (4,4,4) and 8 x (6,6,6).");
    case core::StructuralPhase::Icosahedral:
        return QObject::tr("Twelve neighbours, all (5,5,5) — the shell small "
                           "metal clusters and many metallic glasses adopt.");
    case core::StructuralPhase::CubicDiamond:
        return QObject::tr("Four neighbours whose twelve SECOND neighbours "
                           "form an fcc shell — the Si / Ge / diamond lattice.");
    case core::StructuralPhase::HexagonalDiamond:
        return QObject::tr("As cubic diamond, but with an hcp second shell — "
                           "lonsdaleite, and what a diamond stacking fault "
                           "reads as.");
    case core::StructuralPhase::Other:
        break;
    }
    return QObject::tr("No signature matched. Surfaces, defect cores, liquids "
                       "and glasses all land here — in a melt this is the "
                       "correct answer for nearly every atom, not a failure.");
}

} // namespace

PhaseColorsDialog::PhaseColorsDialog(ViewportWidget* viewport, QWidget* parent)
    : QDialog(parent)
    , viewport_(viewport)
{
    setWindowTitle(tr("Phase Colors"));
    resize(520, 380);

    initialColors_ = viewport_->style().phaseColors;
    initialOptions_ = viewport_->structuralPhaseOptions();

    auto* layout = new QVBoxLayout(this);

    auto* note = new QLabel(
        tr("Under <b>Color by: Phase</b> every atom takes the color of the "
           "local crystal structure its neighbour topology matches "
           "(adaptive common-neighbour analysis). The label is <i>per atom</i>, "
           "not per cell: in a nanoparticle the core reads fcc while the {111} "
           "facets read hcp, and inside a deformed metal the hcp-labelled "
           "planes <i>are</i> the stacking faults."),
        this);
    note->setWordWrap(true);
    note->setTextFormat(Qt::RichText);
    layout->addWidget(note);

    table_ = new QTableWidget(this);
    table_->setColumnCount(ColumnCount);
    table_->setHorizontalHeaderLabels({tr("Structure"), tr("Atoms"),
                                       tr("Fraction"), tr("Color"), QString()});
    table_->horizontalHeader()->setStretchLastSection(true);
    table_->verticalHeader()->setVisible(false);
    table_->setSelectionMode(QAbstractItemView::NoSelection);
    table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    layout->addWidget(table_, 1);

    diamondCheck_ = new QCheckBox(tr("Detect diamond structures"), this);
    diamondCheck_->setChecked(initialOptions_.detectDiamond);
    diamondCheck_->setToolTip(
        tr("Diamond has no CNA signature of its own — a four-fold-coordinated "
           "atom shares almost no neighbours with anyone — so it is identified "
           "from its SECOND shell, which costs one extra pass over the "
           "four-fold atoms.\n\n"
           "Leave it on unless you are analysing a pure-metal trajectory, "
           "where no diamond phase can appear and the pass is wasted work."));
    layout->addWidget(diamondCheck_);
    connect(diamondCheck_, &QCheckBox::toggled, this, [this](bool on) {
        auto options = viewport_->structuralPhaseOptions();
        options.detectDiamond = on;
        viewport_->setStructuralPhaseOptions(options);
        populate(); // the counts move when the diamond pass is switched off
    });

    auto* buttons = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    populate();
}

void PhaseColorsDialog::populate()
{
    const auto counts = viewport_->phaseCounts();
    int total = 0;
    for (const int count : counts)
        total += count;

    table_->setRowCount(core::kStructuralPhaseCount);
    for (int row = 0; row < core::kStructuralPhaseCount; ++row) {
        const auto phase = static_cast<core::StructuralPhase>(row);
        const int count = counts[static_cast<std::size_t>(row)];

        const auto readOnly = [](const QString& text) {
            auto* item = new QTableWidgetItem(text);
            item->setFlags(item->flags() & ~Qt::ItemIsEditable);
            return item;
        };
        auto* nameItem = readOnly(phaseName(phase));
        nameItem->setToolTip(phaseTooltip(phase));
        table_->setItem(row, ColPhase, nameItem);
        // Blank rather than "0" when nothing has been analysed: zero fcc atoms
        // and "the analysis has not run" are different facts, and printing 0
        // for the second reads as a definite (and wrong) answer.
        table_->setItem(row, ColAtoms,
                        readOnly(total > 0 ? QString::number(count)
                                           : QStringLiteral("—")));
        table_->setItem(
            row, ColFraction,
            readOnly(total > 0
                         ? QStringLiteral("%1 %").arg(
                               100.0 * count / total, 0, 'f', 1)
                         : QStringLiteral("—")));

        auto* swatch =
            qobject_cast<QPushButton*>(table_->cellWidget(row, ColColor));
        if (!swatch) {
            swatch = new QPushButton(table_);
            table_->setCellWidget(row, ColColor, swatch);
            connect(swatch, &QPushButton::clicked, this,
                    [this, row] { pickColor(row); });
        }
        // The EFFECTIVE colour, defaults included: the swatch has to show what
        // the viewport draws, and phaseColor() is the renderer's own answer.
        setButtonColor(swatch, viewport_->phaseColor(phase));

        auto* reset =
            qobject_cast<QPushButton*>(table_->cellWidget(row, ColReset));
        if (!reset) {
            reset = new QPushButton(tr("Reset"), table_);
            reset->setToolTip(
                tr("Drop the explicit color; the structure returns to the "
                   "standard CNA palette."));
            table_->setCellWidget(row, ColReset, reset);
            connect(reset, &QPushButton::clicked, this,
                    [this, row] { resetColor(row); });
        }
    }
    table_->resizeColumnsToContents();
}

void PhaseColorsDialog::pickColor(int phase)
{
    const auto value = static_cast<core::StructuralPhase>(phase);
    const QColor chosen =
        QColorDialog::getColor(viewport_->phaseColor(value), this,
                               tr("Color for %1").arg(phaseName(value)));
    if (!chosen.isValid())
        return; // picker cancelled — not a reset, which has its own button
    viewport_->setPhaseColor(value, chosen);
    populate();
}

void PhaseColorsDialog::resetColor(int phase)
{
    // An invalid colour means "no explicit pick" — a meaning the picker itself
    // cannot express, which is why Reset is a button rather than an entry in it.
    viewport_->setPhaseColor(static_cast<core::StructuralPhase>(phase),
                             QColor());
    populate();
}

void PhaseColorsDialog::reject()
{
    for (int row = 0; row < core::kStructuralPhaseCount; ++row)
        viewport_->setPhaseColor(static_cast<core::StructuralPhase>(row),
                                 initialColors_[static_cast<std::size_t>(row)]);
    viewport_->setStructuralPhaseOptions(initialOptions_);
    QDialog::reject();
}

} // namespace calango::gui
