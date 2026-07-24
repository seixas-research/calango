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

/// Result viewer for the Electron Localization Function η(r): an isosurface
/// (η ∈ [0, 1]) plus an axis-aligned color-mapped slice of the ELF grid.
///
/// The ELF is a DFT post-process set up and launched from the ELF *wizard*
/// (Simulation → "Electron Localization Function (ELF)…"); this dialog only
/// *visualises* the resulting grid. It is opened automatically when the job's
/// elf.cube is ready (see MainWindow::onJobFinished) and can also load any grid
/// on demand ("Load ELF grid…").
class ElfDialog : public QDialog {
    Q_OBJECT

public:
    explicit ElfDialog(std::shared_ptr<core::Structure> structure,
                       QWidget* parent = nullptr);
    ~ElfDialog() override;

    /// Load an ELF grid file (.cube / ELFCAR / .xsf) into the viewer. Used by
    /// the "Load ELF grid…" button and callable externally once a job finishes.
    void loadGrid(const QString& path);

private Q_SLOTS:
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
