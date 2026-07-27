#pragma once

#include <QDialog>
#include <QJsonObject>
#include <QString>

class QLabel;
class QTableWidget;

namespace calango::gui {

/// The dedicated read-out for a completed Born Effective Charges run. Parses
/// the `born_charges.json` the generated script writes and shows one row per
/// atom: the isotropic charge Z*_iso = tr(Z*)/3, the three eigenvalues of the
/// symmetric part, and the full 3×3 tensor.
///
/// The isotropic number is what gets quoted, but the tensor is what the physics
/// uses — a strongly anisotropic Z* on a bridging oxygen is a real, reportable
/// feature, not noise — so both are shown rather than collapsing the tensor to
/// its trace.
///
/// The acoustic-sum-rule residual is reported prominently: it is the one number
/// in the file that says whether the calculation is converged, since Σ_k Z*_k
/// must vanish exactly.
///
/// Opened from the Processes panel (its "Open Viewer" button or context menu)
/// and automatically when the job finishes. Purely a viewer.
class BornChargesViewer : public QDialog {
    Q_OBJECT

public:
    explicit BornChargesViewer(QWidget* parent = nullptr);

    /// Parse a `born_charges.json` summary and fill the table. Returns false
    /// (showing nothing) when the file is missing or malformed.
    bool loadResults(const QString& jsonPath);

private Q_SLOTS:
    void copyToClipboard();
    void exportCsv();

private:
    QString plainTextSummary() const;

    QJsonObject data_;
    QString sourcePath_;

    QLabel* summaryLabel_ = nullptr;
    QLabel* sumRuleLabel_ = nullptr;
    QTableWidget* table_ = nullptr;
};

} // namespace calango::gui
