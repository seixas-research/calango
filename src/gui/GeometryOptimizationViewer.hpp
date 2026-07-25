#pragma once

#include <QDialog>
#include <QJsonObject>
#include <QString>

class QLabel;
class QPushButton;

namespace calango::gui {

/// Results → "Geometry Optimization Viewer": the read-out for a completed
/// relaxation. It parses the `geometry_optimization.json` the generated ASE
/// script writes (AseScriptGenerator, TaskKind::GeometryOptimization) and
/// shows the physical summary — final total energy and the change over the
/// run, the maximum and RMS atomic force against the convergence criterion,
/// the optimizer used and how many steps it took.
///
/// Opened automatically when a relaxation finishes (MainWindow::onJobFinished),
/// on demand from the Processes panel, and from the Results menu. Purely a
/// viewer: it never launches a calculation.
class GeometryOptimizationViewer : public QDialog {
    Q_OBJECT

public:
    explicit GeometryOptimizationViewer(QWidget* parent = nullptr);

    /// Parse a `geometry_optimization.json` summary and fill the read-outs.
    /// Returns false (showing nothing) when the file is missing or malformed.
    /// The JSON's directory is remembered for the volumetric export.
    bool loadResults(const QString& jsonPath);

Q_SIGNALS:
    /// "Get Volumetric Data": the host should export the final electron
    /// density / potential from `directory` into the Volumetric Data dock.
    void getVolumetricDataRequested(const QString& directory);

private Q_SLOTS:
    void copyToClipboard();

private:
    QString plainTextSummary() const;

    QJsonObject data_;
    QString directory_;

    QLabel* energyLabel_ = nullptr;
    QLabel* energyChangeLabel_ = nullptr;
    QLabel* forceLabel_ = nullptr;
    QLabel* rmsForceLabel_ = nullptr;
    QLabel* optimizerLabel_ = nullptr;
    QLabel* convergedLabel_ = nullptr;
    QPushButton* volumetricButton_ = nullptr;
};

} // namespace calango::gui
