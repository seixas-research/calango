#pragma once

#include "core/MarchingCubes.hpp"
#include "core/Structure.hpp"
#include "core/VolumetricData.hpp"

#include <QDialog>
#include <QFutureWatcher>
#include <QList>
#include <QPair>
#include <QString>

#include <memory>

class QComboBox;
class QDoubleSpinBox;
class QGroupBox;
class QLabel;
class QSlider;
class QSpinBox;
class QTableWidget;

namespace calango::gui {

class VolumeViewWidget;

/// Analysis → "Maximally Localized Wannier Functions (MLWF)…": run the
/// Marzari-Vanderbilt localization on top of a converged GPAW calculation,
/// tabulate the resulting Wannier centres and spreads, and visualise each
/// real-space orbital ψ_n(r) as an isosurface (plus an axis-aligned slice).
///
/// MLWF is a DFT post-process, so — exactly like ElfDialog — the dialog only
/// *generates* the ASE/GPAW script and emits runRequested() for the controller
/// to stage & run. When the job's wannier.json + wannier_<n>.cube files are
/// ready the user loads the JSON back in ("Load results…") to fill the table
/// and pick which orbital to render. A completed single-point that already
/// holds the Bloch wavefunctions (.gpw) is reused as the baseline so no fresh
/// SCF is needed.
class WannierDialog : public QDialog {
    Q_OBJECT

public:
    explicit WannierDialog(std::shared_ptr<core::Structure> structure,
                           QWidget* parent = nullptr);
    ~WannierDialog() override;

    /// Populate the baseline source selector with completed single-point
    /// processes that hold the calculated Bloch wavefunctions (GPAW .gpw). Each
    /// entry is (display label, absolute path to the origin process directory);
    /// the generated script restarts GPAW from the auto-detected .gpw there.
    void setDensityBaselines(const QList<QPair<QString, QString>>& baselines);

    /// Parse a wannier.json results file (centres, spreads, total spread, cube
    /// list) to fill the table and the per-orbital selector. The cubes are
    /// resolved relative to the JSON's directory. Used by the "Load results…"
    /// button and callable externally once a job finishes.
    void loadResults(const QString& jsonPath);

Q_SIGNALS:
    /// Generated analysis script + a task label; the controller stages and
    /// runs it through the normal local-job path.
    void runRequested(const QString& script, const QString& label);

private Q_SLOTS:
    void computeMlwf();
    void loadResultsDialog();
    /// Load wannier_<n>.cube (from the results directory) into the viewer.
    void orbitalSelected(int index);
    /// Request an isosurface for the current isovalue. Cheap and non-blocking:
    /// it schedules the extraction (see isoWatcher_) rather than performing it.
    void rebuildIso();
    void rebuildSlice();

private:
    /// Shared immutable grid: the worker thread holds a reference that stays
    /// valid even if the user loads a different orbital mid-extraction, and
    /// neither side ever mutates it.
    using FieldPtr = std::shared_ptr<const core::VolumetricData>;

    /// ASE/GPAW script running the MLWF localization and writing wannier.json
    /// plus the per-orbital wannier_<n>.cube files into the job directory.
    QString generateScript() const;

    /// Load one Wannier orbital cube into the viewer (shared by the orbital
    /// selector). Rebuilds the isosurface / slice for the new grid.
    void loadCube(const QString& path);

    double isovalueFromSlider() const;

    /// Kick off the queued extraction if the worker is idle.
    void startIsoExtraction();
    void onIsoExtractionFinished();

    std::shared_ptr<core::Structure> structure_;

    QString jobDir_;        ///< directory of the loaded wannier.json (holds cubes)
    FieldPtr field_;        ///< currently displayed Wannier orbital grid
    double fieldMax_ = 1.0; ///< |value| ceiling for the isovalue slider mapping
    core::IsoMesh isoMesh_; ///< last extracted surface

    // -- Isosurface extraction, off the GUI thread -------------------------
    //
    // Mirrors ElfDialog: marching cubes runs on a QtConcurrent worker so a
    // slider drag never freezes the window. Requests coalesce (at most one in
    // flight, at most one queued) and `isoGeneration_` tags each launch so a
    // result that arrives after its inputs changed is discarded, not drawn.
    QFutureWatcher<core::IsoMesh> isoWatcher_;
    bool isoRequestPending_ = false;
    unsigned isoGeneration_ = 0;
    unsigned isoRunningGeneration_ = 0;

    VolumeViewWidget* view_ = nullptr;
    QComboBox* baselineCombo_ = nullptr; ///< wavefunction source (origin process)
    QSpinBox* nWannier_ = nullptr;       ///< number of Wannier functions
    QComboBox* projectionCombo_ = nullptr; ///< trial-orbital initialization
    QLabel* infoLabel_ = nullptr;

    QTableWidget* table_ = nullptr;         ///< centres + spreads
    QLabel* totalSpreadLabel_ = nullptr;
    QComboBox* orbitalCombo_ = nullptr;     ///< which wannier_<n>.cube to show

    QGroupBox* isoGroup_ = nullptr;
    QSlider* isoSlider_ = nullptr;
    QDoubleSpinBox* isoSpin_ = nullptr;
    QComboBox* isoColormapCombo_ = nullptr;
    QGroupBox* sliceGroup_ = nullptr;
    QComboBox* planeCombo_ = nullptr;
    QSlider* offsetSlider_ = nullptr;
    QComboBox* sliceColormapCombo_ = nullptr;
    QLabel* gridLabel_ = nullptr;
};

} // namespace calango::gui
