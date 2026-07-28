#pragma once

#include <QDialog>
#include <QJsonObject>
#include <QString>

class QGroupBox;
class QLabel;
class QPushButton;
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
    /// false (and shows nothing) when the file is missing or malformed. The
    /// JSON's directory is remembered as the process directory for the
    /// "Get Volumetric Data" action.
    bool loadResults(const QString& jsonPath);

Q_SIGNALS:
    /// "Get Volumetric Data": the host should export the charge density from
    /// `directory` (or register an existing density.cube there) into the
    /// Volumetric Data dock.
    void getVolumetricDataRequested(const QString& directory);

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
    QString directory_;  ///< the process directory (for Get Volumetric Data)

    QLabel* energyLabel_ = nullptr;
    QLabel* fermiLabel_ = nullptr;
    QLabel* forceLabel_ = nullptr;
    QLabel* magmomLabel_ = nullptr;
    QLabel* scfLabel_ = nullptr;
    /// Titled from the run: the moment column is only meaningful, and only
    /// mentioned, for a spin-polarized result.
    QGroupBox* forcesGroup_ = nullptr;
    QTableWidget* forcesTable_ = nullptr;
    QPushButton* volumetricButton_ = nullptr;
};

} // namespace calango::gui
