#include "gui/JobLogWidget.hpp"

#include <QFontDatabase>
#include <QHBoxLayout>
#include <QTextCursor>
#include <QVBoxLayout>

namespace calango::gui {

JobLogWidget::JobLogWidget(QWidget* parent)
    : QWidget(parent)
    , statusLabel_(new QLabel(tr("Idle"), this))
    , progressBar_(new QProgressBar(this))
    , killButton_(new QPushButton(tr("Kill"), this))
    , logView_(new QPlainTextEdit(this))
{
    logView_->setReadOnly(true);
    logView_->setMaximumBlockCount(20000);
    logView_->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));

    progressBar_->setRange(0, 1);
    progressBar_->setValue(0);
    killButton_->setEnabled(false);
    connect(killButton_, &QPushButton::clicked, this, &JobLogWidget::terminateRequested);

    auto* topRow = new QHBoxLayout;
    topRow->addWidget(statusLabel_, 1);
    topRow->addWidget(progressBar_, 2);
    topRow->addWidget(killButton_);

    auto* layout = new QVBoxLayout(this);
    layout->addLayout(topRow);
    layout->addWidget(logView_);
}

QString JobLogWidget::logText() const
{
    return logView_->toPlainText();
}

void JobLogWidget::restoreLog(const QString& text)
{
    logView_->setPlainText(text);
    logView_->moveCursor(QTextCursor::End);
    statusLabel_->setText(tr("Restored from project"));
    progressBar_->setRange(0, 1);
    progressBar_->setValue(0);
}

void JobLogWidget::onJobStarted(const QString& description)
{
    logView_->clear();
    appendColored(tr("Starting: %1").arg(description), QStringLiteral("#6699ff"));
    statusLabel_->setText(tr("Running…"));
    progressBar_->setRange(0, 0); // indeterminate until first CALANGO_PROGRESS
    killButton_->setEnabled(true);
}

void JobLogWidget::onOutputLine(const QString& line)
{
    logView_->appendPlainText(line);
}

void JobLogWidget::onErrorLine(const QString& line)
{
    appendColored(line, QStringLiteral("#ff6b6b"));
}

void JobLogWidget::onProgress(int step, int total)
{
    progressBar_->setRange(0, total);
    progressBar_->setValue(step);
}

void JobLogWidget::onJobFinished(int exitCode, bool crashed)
{
    if (progressBar_->maximum() == 0)
        progressBar_->setRange(0, 1); // stop the indeterminate animation
    if (crashed) {
        statusLabel_->setText(tr("Crashed"));
        appendColored(tr("Calculation crashed."), QStringLiteral("#ff6b6b"));
    } else {
        statusLabel_->setText(tr("Finished (exit %1)").arg(exitCode));
        appendColored(tr("Calculation finished with exit code %1.").arg(exitCode),
                      exitCode == 0 ? QStringLiteral("#51cf66") : QStringLiteral("#ff6b6b"));
        if (exitCode == 0)
            progressBar_->setValue(progressBar_->maximum());
    }
    killButton_->setEnabled(false);
}

void JobLogWidget::appendColored(const QString& line, const QString& cssColor)
{
    logView_->appendHtml(QStringLiteral("<span style=\"color:%1\">%2</span>")
                             .arg(cssColor, line.toHtmlEscaped()));
}

} // namespace calango::gui
