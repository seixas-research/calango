#pragma once

#include <QLabel>
#include <QPlainTextEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QWidget>

namespace calango::gui {

/// Dockable job console: streams stdout/stderr of the running job,
/// shows a progress bar driven by CALANGO_PROGRESS markers, and offers
/// a kill button. Connect its slots to the JobRunner signals 1:1.
class JobLogWidget : public QWidget {
    Q_OBJECT

public:
    explicit JobLogWidget(QWidget* parent = nullptr);

public Q_SLOTS:
    void onJobStarted(const QString& description);
    void onOutputLine(const QString& line);
    void onErrorLine(const QString& line);
    void onProgress(int step, int total);
    void onJobFinished(int exitCode, bool crashed);

Q_SIGNALS:
    void terminateRequested();

private:
    void appendColored(const QString& line, const QString& cssColor);

    QLabel* statusLabel_;
    QProgressBar* progressBar_;
    QPushButton* killButton_;
    QPlainTextEdit* logView_;
};

} // namespace calango::gui
