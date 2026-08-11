#include "gui/WorkflowReportDialog.hpp"

#include "ui/IconManager.hpp"

#include <QApplication>
#include <QClipboard>
#include <QDesktopServices>
#include <QDialogButtonBox>
#include <QFont>
#include <QHeaderView>
#include <QLabel>
#include <QPushButton>
#include <QTreeWidget>
#include <QUrl>
#include <QVBoxLayout>

#include <algorithm>

namespace calango::gui {

namespace {

constexpr int kDirRole = Qt::UserRole + 1;

QColor statusColor(const QString& status)
{
    if (status == QLatin1String("done"))
        return QColor(0x2E, 0x9E, 0x5B);
    if (status == QLatin1String("failed"))
        return QColor(0xD1, 0x4A, 0x3F);
    return QColor(0x9A, 0x9A, 0xA6); // skipped / pending
}

} // namespace

WorkflowReportDialog::WorkflowReportDialog(const core::WorkflowReport& report,
                                           QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle(tr("Workflow Execution Report"));
    resize(860, 560);

    auto* layout = new QVBoxLayout(this);

    // -- The one line that answers the first question ----------------------
    auto* headline = new QLabel(report.headline(), this);
    QFont headlineFont = headline->font();
    headlineFont.setPointSizeF(headlineFont.pointSizeF() * 1.25);
    headlineFont.setBold(true);
    headline->setFont(headlineFont);
    const core::WorkflowReport::Tally counts = report.tally();
    headline->setStyleSheet(
        QStringLiteral("color: %1;")
            .arg(statusColor(counts.failed > 0 ? QStringLiteral("failed")
                                               : QStringLiteral("done"))
                     .name()));
    layout->addWidget(headline);

    auto* subtitle = new QLabel(this);
    subtitle->setWordWrap(true);
    subtitle->setTextFormat(Qt::RichText);
    QStringList facts;
    if (report.batchTotal > 1) {
        facts << tr("%n structure(s) processed", nullptr, report.batchTotal);
    }
    if (!report.finishedUtc.isEmpty())
        facts << tr("finished %1 UTC").arg(report.finishedUtc);
    if (!report.completed) {
        facts << tr("<b>the run was stopped before it finished</b> — Resume "
                    "continues it");
    }
    subtitle->setText(facts.join(QStringLiteral(" &nbsp;·&nbsp; ")));
    layout->addWidget(subtitle);

    // -- The table ---------------------------------------------------------
    tree_ = new QTreeWidget(this);
    tree_->setRootIsDecorated(report.batchTotal > 1);
    tree_->setAlternatingRowColors(true);
    tree_->setSelectionMode(QAbstractItemView::SingleSelection);
    layout->addWidget(tree_, 1);
    populate(report);

    auto* note = new QLabel(
        tr("<i>Double-click a row to open its folder. The same report is "
           "written beside the run as <b>workflow_report.json</b> and "
           "<b>workflow_report.txt</b>.</i>"),
        this);
    note->setWordWrap(true);
    note->setTextFormat(Qt::RichText);
    layout->addWidget(note);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    auto* copy = buttons->addButton(tr("Copy as text"),
                                    QDialogButtonBox::ActionRole);
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(copy, &QPushButton::clicked, this, [report] {
        // The plain-text form, not a re-render of the table: it is the same
        // text the run wrote to disk, so what is pasted into a notebook and
        // what is in the folder are the same account.
        QApplication::clipboard()->setText(report.toPlainText());
    });
    connect(tree_, &QTreeWidget::itemDoubleClicked, this,
            [this](QTreeWidgetItem* item, int) {
                const QString directory = item->data(0, kDirRole).toString();
                if (directory.isEmpty())
                    return;
                Q_EMIT openDirectoryRequested(directory);
                QDesktopServices::openUrl(QUrl::fromLocalFile(directory));
            });
}

void WorkflowReportDialog::populate(const core::WorkflowReport& report)
{
    // Columns are decided by the metrics that were actually EXTRACTED, in the
    // order they first appear. A fixed column set would mean an energy column
    // full of blanks for a pipeline of transforms, and no column at all for the
    // one number a new module reports.
    QStringList metricKeys;
    QStringList metricLabels;
    for (const core::NodeOutcome& outcome : report.outcomes) {
        for (const core::ReportMetric& metric : outcome.metrics) {
            if (metricKeys.contains(metric.key))
                continue;
            metricKeys << metric.key;
            metricLabels << metric.label;
        }
    }

    QStringList headers{tr("Node"), tr("Status")};
    headers += metricLabels;
    tree_->setColumnCount(headers.size());
    tree_->setHeaderLabels(headers);

    const QStringList labels = report.batchLabels();
    const bool grouped = report.batchTotal > 1;

    for (int pass = 0; pass < std::max(1, report.batchTotal); ++pass) {
        const QList<core::NodeOutcome> outcomes = report.outcomesFor(pass);
        if (outcomes.isEmpty())
            continue;

        QTreeWidgetItem* parent = nullptr;
        if (grouped) {
            const core::WorkflowReport::Tally counts = report.tallyFor(pass);
            const QString label =
                pass < labels.size() && !labels[pass].isEmpty()
                ? labels[pass]
                : tr("Pass %1").arg(pass + 1);
            parent = new QTreeWidgetItem(tree_);
            parent->setText(0, label);
            parent->setText(1, counts.failed > 0
                                   ? tr("%1 ok, %2 failed")
                                         .arg(counts.succeeded)
                                         .arg(counts.failed)
                                   : tr("%1 ok").arg(counts.succeeded));
            parent->setForeground(1, QBrush(statusColor(
                counts.failed > 0 ? QStringLiteral("failed")
                                  : QStringLiteral("done"))));
            QFont bold = parent->font(0);
            bold.setBold(true);
            parent->setFont(0, bold);
            parent->setExpanded(true);
        }

        for (const core::NodeOutcome& outcome : outcomes) {
            auto* row = parent ? new QTreeWidgetItem(parent)
                               : new QTreeWidgetItem(tree_);
            row->setText(0, outcome.title);
            row->setText(1, outcome.status);
            row->setForeground(1, QBrush(statusColor(outcome.status)));
            row->setData(0, kDirRole, outcome.directory);
            // The reason a node failed belongs where the failure is, not in a
            // log the reader has to go and find.
            QString tip = outcome.directory;
            if (!outcome.note.isEmpty())
                tip = outcome.note + QStringLiteral("\n") + tip;
            row->setToolTip(0, tip);
            row->setToolTip(1, outcome.note);
            if (!outcome.engine.isEmpty())
                row->setText(0, tr("%1  (%2)").arg(outcome.title,
                                                   outcome.engine));

            for (const core::ReportMetric& metric : outcome.metrics) {
                const int column = 2 + metricKeys.indexOf(metric.key);
                if (column >= 2)
                    row->setText(column, metric.display());
            }
        }
    }

    tree_->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    for (int column = 1; column < tree_->columnCount(); ++column)
        tree_->resizeColumnToContents(column);
}

} // namespace calango::gui
