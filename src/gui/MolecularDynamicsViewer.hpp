#pragma once

#include "core/Structure.hpp"

#include <QDialog>
#include <QString>

#include <memory>
#include <vector>

class QLabel;
class QPushButton;
class QSlider;
class QTabWidget;
class QTimer;

namespace calango::gui {

class ViewportWidget;
class SeriesPlotWidget;

/// Results → "Molecular Dynamics Viewer": the read-out for a completed MD run.
///
/// It reads the `metrics.json` the generated script logs live (temperature,
/// potential and kinetic energy, pressure, volume) and the saved trajectory,
/// and presents them as time series plus a g(r) computed from a chosen frame.
///
/// The quantity that actually says whether the run was sound is the TOTAL
/// energy: in NVE it should be conserved, and its drift is the integrator's
/// health check. The script logs potential and kinetic separately so this
/// viewer can form E_tot rather than showing only the potential term, which
/// drifts for perfectly good physical reasons and tells you nothing.
class MolecularDynamicsViewer : public QDialog {
    Q_OBJECT

public:
    /// `viewport` receives the scrubbed frames; null disables the player.
    explicit MolecularDynamicsViewer(ViewportWidget* viewport,
                                     QWidget* parent = nullptr);
    ~MolecularDynamicsViewer() override;

    /// Load a finished MD job directory (metrics.json + trajectory). Returns
    /// false when neither is present.
    bool loadDirectory(const QString& directory);

private Q_SLOTS:
    void showFrame(int index);
    void togglePlay();
    void advanceFrame();
    void recomputeRdf();
    void exportCsv();
    void exportImage();

private:
    void buildUi();
    void restoreViewport();
    /// Mean and RMS fluctuation of a series, the pair that says whether a
    /// thermostat is holding its setpoint.
    static void statistics(const std::vector<double>& values, double& mean,
                           double& rms);

    ViewportWidget* viewport_ = nullptr;
    QString directory_;

    std::vector<double> time_;        ///< ps
    std::vector<double> temperature_; ///< K
    std::vector<double> potential_;   ///< eV
    std::vector<double> kinetic_;     ///< eV
    std::vector<double> total_;       ///< eV
    std::vector<double> pressure_;    ///< GPa
    std::vector<double> volume_;      ///< Å³
    std::vector<std::shared_ptr<core::Structure>> frames_;
    std::shared_ptr<const core::Structure> viewportStructureBefore_;

    QTabWidget* tabs_ = nullptr;
    SeriesPlotWidget* temperaturePlot_ = nullptr;
    SeriesPlotWidget* energyPlot_ = nullptr;
    SeriesPlotWidget* pressurePlot_ = nullptr;
    SeriesPlotWidget* rdfPlot_ = nullptr;
    QLabel* summaryLabel_ = nullptr;
    QLabel* frameLabel_ = nullptr;
    QSlider* frameSlider_ = nullptr;
    QPushButton* playButton_ = nullptr;
    QTimer* timer_ = nullptr;
};

} // namespace calango::gui
