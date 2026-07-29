#pragma once

#include "core/Structure.hpp"

#include <QDialog>
#include <QString>

#include <memory>
#include <vector>

class QLabel;
class QTabWidget;

namespace calango::gui {

class SeriesPlotWidget;

/// Results → "Molecular Dynamics Viewer": the read-out for a completed MD run.
///
/// It reads the `metrics.json` the generated script logs live (temperature,
/// potential and kinetic energy, pressure, volume) and the saved trajectory,
/// and presents them as time series plus a g(r) computed from the final
/// frame.
///
/// Deliberately WITHOUT its own frame player: trajectory playback belongs to
/// the main viewport's timeline, where every loaded trajectory is scrubbed
/// and played. The host loads this run's frames (frames()) into a workspace
/// tab, so one set of playback controls serves MD runs and file-loaded
/// trajectories alike — the second slider this dialog used to carry fought
/// the global one over the same viewport.
///
/// The quantity that actually says whether the run was sound is the TOTAL
/// energy: in NVE it should be conserved, and its drift is the integrator's
/// health check. The script logs potential and kinetic separately so this
/// viewer can form E_tot rather than showing only the potential term, which
/// drifts for perfectly good physical reasons and tells you nothing.
class MolecularDynamicsViewer : public QDialog {
    Q_OBJECT

public:
    explicit MolecularDynamicsViewer(QWidget* parent = nullptr);

    /// Load a finished MD job directory (metrics.json + trajectory). Returns
    /// false when neither is present.
    bool loadDirectory(const QString& directory);

    /// The trajectory read from the job directory, for the host to open as
    /// a scrubbable workspace tab on the main timeline. Empty when the run
    /// kept no trajectory.
    const std::vector<std::shared_ptr<core::Structure>>& frames() const
    {
        return frames_;
    }

private Q_SLOTS:
    void recomputeRdf();
    void exportCsv();
    void exportImage();

private:
    void buildUi();
    /// Mean and RMS fluctuation of a series, the pair that says whether a
    /// thermostat is holding its setpoint.
    static void statistics(const std::vector<double>& values, double& mean,
                           double& rms);

    QString directory_;

    std::vector<double> time_;        ///< ps
    std::vector<double> temperature_; ///< K
    std::vector<double> potential_;   ///< eV
    std::vector<double> kinetic_;     ///< eV
    std::vector<double> total_;       ///< eV
    std::vector<double> pressure_;    ///< GPa
    std::vector<double> volume_;      ///< Å³
    std::vector<std::shared_ptr<core::Structure>> frames_;

    QTabWidget* tabs_ = nullptr;
    SeriesPlotWidget* temperaturePlot_ = nullptr;
    SeriesPlotWidget* energyPlot_ = nullptr;
    SeriesPlotWidget* pressurePlot_ = nullptr;
    SeriesPlotWidget* rdfPlot_ = nullptr;
    QLabel* summaryLabel_ = nullptr;
};

} // namespace calango::gui
