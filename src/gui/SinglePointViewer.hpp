#pragma once

#include <QDialog>
#include <QJsonObject>
#include <QString>

class QLabel;
class QTableWidget;

namespace calango::gui {

/// Results → "Single-Point Viewer": the dedicated read-out for a completed
/// Single-Point calculation. It parses the `single_point.json` the generated
/// ASE script writes (see AseScriptGenerator, TaskKind::SinglePoint) and shows
/// the physical summary — total energy (eV and Hartree), Fermi level, maximum
/// atomic force with the offending site highlighted, and the SCF convergence
/// summary — plus copy-to-clipboard and JSON/CSV export.
///
/// Opened automatically when a single-point job finishes (MainWindow::
/// onJobFinished), on demand from the Processes panel, and from the Results
/// menu. Purely a viewer: it never launches a calculation.
class SinglePointViewer : public QDialog {
    Q_OBJECT

public:
    explicit SinglePointViewer(QWidget* parent = nullptr);

    /// Parse a `single_point.json` summary and fill the read-outs. Returns
    /// false (and shows nothing) when the file is missing or malformed.
    bool loadResults(const QString& jsonPath);

private Q_SLOTS:
    void copyToClipboard();
    void exportJson();
    void exportCsv();

private:
    /// Compact, human-readable multi-line rendering of the summary (also the
    /// clipboard payload).
    QString plainTextSummary() const;

    QJsonObject data_;   ///< parsed single_point.json
    QString sourcePath_; ///< where it was loaded from (for export defaults)

    QLabel* energyLabel_ = nullptr;
    QLabel* fermiLabel_ = nullptr;
    QLabel* forceLabel_ = nullptr;
    QLabel* scfLabel_ = nullptr;
    QTableWidget* forcesTable_ = nullptr;
};

} // namespace calango::gui
