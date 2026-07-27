#pragma once

#include <QDialog>
#include <QJsonObject>
#include <QString>

class QComboBox;
class QLabel;
class QTableWidget;

namespace calango::gui {

class LinePlotWidget;

/// The read-out for a completed Raman & IR Spectroscopy run: parses the
/// `raman_ir.json` the generated script writes and shows the broadened spectra
/// alongside the discrete mode table.
///
/// Both are shown because they answer different questions. The spectrum is what
/// is compared against an experiment; the mode table is what says WHICH
/// vibration a peak belongs to — and a broadened curve has already thrown that
/// away by summing overlapping modes into one band.
///
/// Opened from the Processes panel (its "Open Viewer" button or context menu)
/// and automatically when the job finishes. Purely a viewer.
class RamanIrViewer : public QDialog {
    Q_OBJECT

public:
    explicit RamanIrViewer(QWidget* parent = nullptr);

    /// Parse a `raman_ir.json` summary and fill the plot + table. Returns
    /// false (showing nothing) when the file is missing or malformed.
    bool loadResults(const QString& jsonPath);

private Q_SLOTS:
    void showSelectedSpectrum();
    void copyToClipboard();
    void exportCsv();

private:
    QString plainTextSummary() const;

    QJsonObject data_;
    QString sourcePath_;

    QLabel* summaryLabel_ = nullptr;
    QComboBox* spectrumCombo_ = nullptr;
    LinePlotWidget* plot_ = nullptr;
    QTableWidget* table_ = nullptr;
};

} // namespace calango::gui
