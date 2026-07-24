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

namespace calango::gui {

class VolumeViewWidget;

/// Analysis → "Electron Localization Function (ELF)…": compute the ELF η(r)
/// with GPAW as a background job, then visualise the resulting grid as an
/// isosurface (η ∈ [0, 1]) plus an axis-aligned color-mapped slice.
///
/// The ELF is a DFT post-process, so — exactly like PartialChargeDialog — the
/// dialog only *generates* the ASE/GPAW script and emits runRequested() for the
/// controller to stage & run. When the job's elf.cube is ready the user loads
/// it back in ("Load ELF grid…") to populate the viewer. A completed process
/// that already holds a wavefunction/density (.gpw) can be reused as the
/// baseline so no fresh single-point is needed.
class ElfDialog : public QDialog {
    Q_OBJECT

public:
    explicit ElfDialog(std::shared_ptr<core::Structure> structure,
                       QWidget* parent = nullptr);
    ~ElfDialog() override;

    /// Populate the baseline source selector with completed processes that hold
    /// a calculated wavefunction/density (GPAW .gpw). Each entry is (display
    /// label, absolute path to the origin process directory); the generated
    /// script restarts GPAW from the auto-detected .gpw in that directory.
    void setDensityBaselines(const QList<QPair<QString, QString>>& baselines);

    /// Load an ELF grid file (.cube / ELFCAR / .xsf) into the viewer. Used by
    /// the "Load ELF grid…" button and callable externally once a job finishes.
    void loadGrid(const QString& path);

Q_SIGNALS:
    /// Generated analysis script + a task label; the controller stages and
    /// runs it through the normal local-job path.
    void runRequested(const QString& script, const QString& label);

private Q_SLOTS:
    void computeElf();
    void loadGridDialog();
    /// Request an isosurface for the current isovalue. Cheap and non-blocking:
    /// it schedules the extraction (see isoWatcher_) rather than performing it.
    void rebuildIso();
    void rebuildSlice();
    void exportIso();
    void exportSlice();

private:
    /// Shared immutable grid: the worker thread holds a reference that stays
    /// valid even if the user loads a different file mid-extraction, and neither
    /// side ever mutates it.
    using FieldPtr = std::shared_ptr<const core::VolumetricData>;

    /// GPAW script writing elf.cube into the job directory.
    QString generateScript() const;

    double isovalueFromSlider() const;

    /// Kick off the queued extraction if the worker is idle.
    void startIsoExtraction();
    void onIsoExtractionFinished();

    std::shared_ptr<core::Structure> structure_;

    FieldPtr field_;        ///< loaded ELF grid
    core::IsoMesh isoMesh_; ///< last extracted surface (export)
    std::vector<std::array<double, 4>> sliceSamples_; ///< x,y,z,value (export)

    // -- Isosurface extraction, off the GUI thread -------------------------
    //
    // extractIsosurface() is O(nx·ny·nz) marching cubes; running it inline
    // froze the window for a whole slider drag. It now runs on a QtConcurrent
    // worker, matching VolumetricDialog. Drags emit more requests than can be
    // served, so requests coalesce: at most one extraction is in flight and at
    // most one is queued (the newest). `isoGeneration_` tags each launch so a
    // result that arrives after its inputs changed is discarded, not drawn.
    QFutureWatcher<core::IsoMesh> isoWatcher_;
    bool isoRequestPending_ = false;
    unsigned isoGeneration_ = 0;
    unsigned isoRunningGeneration_ = 0;

    VolumeViewWidget* view_ = nullptr;
    QComboBox* baselineCombo_ = nullptr; ///< wavefunction source (origin process)
    QLabel* infoLabel_ = nullptr;
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
