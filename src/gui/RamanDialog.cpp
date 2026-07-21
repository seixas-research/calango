#include "gui/RamanDialog.hpp"

#include "python_bridge/RamanAnalysis.hpp"

#include <QApplication>
#include <QDialogButtonBox>
#include <QHeaderView>
#include <QLabel>
#include <QTableWidget>
#include <QVBoxLayout>

namespace calango::gui {

RamanDialog::RamanDialog(std::shared_ptr<const core::Structure> structure,
                         QWidget* parent)
    : QDialog(parent)
    , structure_(std::move(structure))
{
    setWindowTitle(tr("Raman Modes"));
    resize(520, 440);

    auto* layout = new QVBoxLayout(this);
    summaryLabel_ = new QLabel(this);
    summaryLabel_->setWordWrap(true);
    layout->addWidget(summaryLabel_);

    table_ = new QTableWidget(this);
    table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table_->setColumnCount(4);
    table_->setHorizontalHeaderLabels({tr("Irrep"), tr("Degeneracy"),
                                       tr("Optical modes"), tr("Activity")});
    table_->horizontalHeader()->setStretchLastSection(true);
    layout->addWidget(table_, 1);

    auto* note = new QLabel(
        tr("Factor-group (nuclear-site) analysis at the Γ point. Raman "
           "activity from the symmetric polarizability representation, IR "
           "activity from the dipole (vector) representation; acoustic "
           "translations are subtracted. Mulliken subscripts in "
           "low-symmetry orthorhombic groups may be permuted relative to "
           "a specific textbook axis convention."),
        this);
    note->setWordWrap(true);
    note->setStyleSheet(QStringLiteral("color: gray;"));
    layout->addWidget(note);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);

    compute();
}

void RamanDialog::compute()
{
    QApplication::setOverrideCursor(Qt::WaitCursor);
    const auto result = pybridge::RamanAnalysis::analyze(*structure_);
    QApplication::restoreOverrideCursor();

    if (!result.error.empty()) {
        summaryLabel_->setText(QString::fromStdString(result.error));
        return;
    }

    int ramanSets = 0, irSets = 0, silentSets = 0;
    for (const auto& mode : result.modes) {
        if (mode.opticalCount <= 0)
            continue;
        if (mode.ramanActive)
            ramanSets += mode.opticalCount;
        else if (mode.irActive)
            irSets += mode.opticalCount;
        else
            silentSets += mode.opticalCount;
    }
    summaryLabel_->setText(
        tr("<b>%1 (#%2)</b>, point group <b>%3</b> — %4 atoms in the "
           "primitive cell: %5 modes (3 acoustic, %6 optical).<br>"
           "Optical irrep copies: <b>%7 Raman-active</b>, %8 IR-only, "
           "%9 silent.")
            .arg(QString::fromStdString(result.spaceGroupSymbol))
            .arg(result.spaceGroupNumber)
            .arg(QString::fromStdString(result.pointGroup))
            .arg(result.atomsPrimitive)
            .arg(3 * result.atomsPrimitive)
            .arg(3 * result.atomsPrimitive - 3)
            .arg(ramanSets)
            .arg(irSets)
            .arg(silentSets));

    table_->setRowCount(static_cast<int>(result.modes.size()));
    int row = 0;
    for (const auto& mode : result.modes) {
        QString activity;
        if (mode.ramanActive && mode.irActive)
            activity = tr("Raman + IR");
        else if (mode.ramanActive)
            activity = tr("Raman");
        else if (mode.irActive)
            activity = mode.opticalCount > 0 ? tr("IR") : tr("acoustic");
        else
            activity = tr("silent");
        if (mode.acousticCount > 0 && mode.opticalCount > 0)
            activity += tr(" (+acoustic)");

        const auto item = [](const QString& text) {
            return new QTableWidgetItem(text);
        };
        table_->setItem(row, 0, item(QString::fromStdString(mode.label)));
        table_->setItem(row, 1, item(QString::number(mode.degeneracy)));
        table_->setItem(row, 2, item(QString::number(mode.opticalCount)));
        table_->setItem(row, 3, item(activity));
        ++row;
    }
    table_->resizeColumnsToContents();
}

} // namespace calango::gui
