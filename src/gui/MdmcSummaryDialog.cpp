#include "gui/MdmcSummaryDialog.hpp"

#include <QDialogButtonBox>
#include <QFile>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QTableWidget>
#include <QTextStream>
#include <QVBoxLayout>

namespace calango::gui {

MdmcSummaryDialog::MdmcSummaryDialog(QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle(tr("MDMC Summary"));
    // Modeless: the whole point is to keep the counters beside the viewport
    // while the run moves. Not WA_DeleteOnClose — MainWindow keeps the one
    // instance and re-shows it, so closing the window does not throw away
    // which run it was showing.
    setModal(false);
    setSizeGripEnabled(true);

    auto* layout = new QVBoxLayout(this);

    subtitle_ = new QLabel(this);
    subtitle_->setWordWrap(true);
    layout->addWidget(subtitle_);

    // The RUN block: the five whole-run quantities. A separate two-column
    // table above the per-move-kind one rather than extra rows in it — the
    // two have different shapes (five named scalars against a row per move
    // kind), and folding either into the other's columns would cost a
    // diagnostic.
    runSummaryTable_ = new QTableWidget(0, 2, this);
    runSummaryTable_->setHorizontalHeaderLabels({tr("Quantity"), tr("Value")});
    runSummaryTable_->horizontalHeader()->setSectionResizeMode(
        QHeaderView::Stretch);
    runSummaryTable_->verticalHeader()->setVisible(false);
    runSummaryTable_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    runSummaryTable_->setSelectionMode(QAbstractItemView::NoSelection);
    // Five rows and a header, and it never grows: capped so the per-move-kind
    // table below it takes the space a resized window gains.
    runSummaryTable_->setMaximumHeight(170);
    layout->addWidget(runSummaryTable_, 0);

    moveBreakdownTable_ = new QTableWidget(0, 4, this);
    moveBreakdownTable_->setHorizontalHeaderLabels(
        {tr("Move kind"), tr("Attempts"), tr("Accepted"),
         tr("Acceptance ratio")});
    moveBreakdownTable_->horizontalHeader()->setSectionResizeMode(
        QHeaderView::Stretch);
    moveBreakdownTable_->verticalHeader()->setVisible(false);
    moveBreakdownTable_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    moveBreakdownTable_->setSelectionMode(QAbstractItemView::NoSelection);
    layout->addWidget(moveBreakdownTable_, 1);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    auto* exportButton =
        buttons->addButton(tr("Export CSV…"), QDialogButtonBox::ActionRole);
    connect(exportButton, &QPushButton::clicked, this,
            &MdmcSummaryDialog::exportCsv);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::close);
    layout->addWidget(buttons);

    resize(520, 480);
    refreshSubtitle();
}

void MdmcSummaryDialog::bindProcess(int id, const QString& label, bool running)
{
    processId_ = id;
    processLabel_ = label;
    running_ = running;
    // Cleared rather than left showing the previous run's numbers: MainWindow
    // repaints immediately after binding, but a record with nothing to show
    // (a run that has not completed its first cycle) leaves the tables as it
    // finds them, and "as it finds them" must not be another run's data.
    runSummaryTable_->setRowCount(0);
    moveBreakdownTable_->setRowCount(0);
    refreshSubtitle();
}

void MdmcSummaryDialog::setRunning(bool running)
{
    if (running_ == running)
        return;
    running_ = running;
    refreshSubtitle();
}

void MdmcSummaryDialog::refreshSubtitle()
{
    if (!subtitle_)
        return;
    if (processId_ < 0) {
        subtitle_->setText(tr("No MDMC run selected."));
        return;
    }
    const QString name =
        processLabel_.isEmpty() ? tr("GO-MDMC") : processLabel_;
    subtitle_->setText(running_
                           ? tr("Process #%1 — %2 · running, updating live")
                                 .arg(processId_)
                                 .arg(name)
                           : tr("Process #%1 — %2 · finished")
                                 .arg(processId_)
                                 .arg(name));
}

void MdmcSummaryDialog::exportCsv()
{
    const QString path = QFileDialog::getSaveFileName(
        this, tr("Export MDMC Summary"), QStringLiteral("mdmc_summary.csv"),
        tr("CSV (*.csv)"));
    if (path.isEmpty())
        return;
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QMessageBox::critical(this, tr("Export MDMC Summary"),
                              tr("Could not write %1").arg(path));
        return;
    }
    QTextStream out(&file);
    const auto writeTable = [&out](const QTableWidget* table) {
        for (int row = 0; row < table->rowCount(); ++row) {
            for (int col = 0; col < table->columnCount(); ++col) {
                if (col > 0)
                    out << ',';
                const auto* item = table->item(row, col);
                out << (item ? item->text() : QString());
            }
            out << '\n';
        }
    };
    out << "quantity,value\n";
    writeTable(runSummaryTable_);
    out << '\n';
    out << "move_kind,attempts,accepted,acceptance_ratio\n";
    writeTable(moveBreakdownTable_);
}

} // namespace calango::gui
