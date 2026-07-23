#pragma once

#include "core/MarchingCubes.hpp"
#include "core/VolumetricData.hpp"

#include <QDialog>
#include <QFutureWatcher>

#include <memory>

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QGroupBox;
class QLabel;
class QSlider;

namespace calango::gui {

class VolumeViewWidget;

/// Analysis → "Volumetric Data": isosurfaces (marching-cubes family,
/// tetrahedral variant) with a live isovalue control, color-mapped slice
/// planes (axis-aligned or custom normal), and dual-field electrostatic
/// potential maps — Field A shapes the surface, Field B colors it.
/// Reads Gaussian .cube, VASP CHGCAR/LOCPOT/PARCHG/ELFCAR and .xsf
/// grids; exports the isosurface as OBJ and slices as CSV.
class VolumetricDialog : public QDialog {
    Q_OBJECT

public:
    explicit VolumetricDialog(QWidget* parent = nullptr);
    ~VolumetricDialog() override;

private Q_SLOTS:
    void loadFieldA();
    void loadFieldB();
    /// Request an isosurface for the current isovalue. Cheap and
    /// non-blocking: it schedules the extraction (see the threading note on
    /// isoWatcher_ below) rather than performing it.
    void rebuildIso();
    void rebuildSlice();
    void exportIso();
    void exportSlice();

private:
    /// Shared immutable grid. Shared, not copied, because a CHGCAR grid is
    /// routinely 100³–200³ doubles (8–64 MB): the worker thread holds a
    /// reference that stays valid even if the user loads a different file
    /// mid-extraction, and neither side ever mutates it.
    using FieldPtr = std::shared_ptr<const core::VolumetricData>;

    void loadField(FieldPtr& field, QLabel* label, const QString& role);
    double isovalueFromSlider() const;
    const core::VolumetricData* sliceField() const;

    /// Kick off the queued extraction if the worker is idle.
    void startIsoExtraction();
    void onIsoExtractionFinished();

    FieldPtr fieldA_;
    FieldPtr fieldB_;
    core::IsoMesh isoMesh_; ///< last extracted surface (export)

    // -- Isosurface extraction, off the GUI thread -------------------------
    //
    // extractIsosurface() is O(nx·ny·nz) marching cubes. Running it inline
    // froze the window for the whole sweep of a slider drag; it now runs on a
    // QtConcurrent worker, matching the RDF / coordination / distribution
    // dialogs.
    //
    // Drags emit far more requests than can be served, so requests coalesce:
    // at most one extraction is in flight and at most one is queued (the
    // newest), and intermediate isovalues are simply dropped. `isoGeneration_`
    // tags each launch so a result that arrives after its inputs changed is
    // discarded instead of being drawn.
    QFutureWatcher<core::IsoMesh> isoWatcher_;
    bool isoRequestPending_ = false;
    unsigned isoGeneration_ = 0;
    unsigned isoRunningGeneration_ = 0;
    std::vector<std::array<double, 4>> sliceSamples_; ///< x,y,z,value (export)

    VolumeViewWidget* view_;
    QLabel* fieldALabel_;
    QLabel* fieldBLabel_;
    QGroupBox* isoGroup_;
    QSlider* isoSlider_;
    QDoubleSpinBox* isoSpin_;
    QCheckBox* epmCheck_;
    QComboBox* isoColormapCombo_;
    QLabel* epmRangeLabel_;
    QGroupBox* sliceGroup_;
    QComboBox* planeCombo_;
    QDoubleSpinBox* normalSpin_[3];
    QSlider* offsetSlider_;
    QComboBox* sliceColormapCombo_;
    QComboBox* sliceFieldCombo_;
};

} // namespace calango::gui
