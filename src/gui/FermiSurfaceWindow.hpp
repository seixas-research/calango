#pragma once

#include "core/Vec3.hpp"
#include "gui/FermiSurfaceStyle.hpp"

#include <QDialog>
#include <QJsonObject>
#include <QString>

#include <array>
#include <vector>

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QListWidget;
class QPushButton;
class QSpinBox;
class QWidget;

namespace calango::gui {

class VolumeViewWidget;

/// Results viewer for a Wannier-interpolated Fermi surface: the sheets
/// E_n(k) = E_F drawn inside the first Brillouin zone.
///
/// Two things make this more than an isosurface. The grid the bands were
/// interpolated on spans the reciprocal UNIT CELL — a parallelepiped, because
/// that is what a regular grid can be laid on — while the object a Fermi
/// surface is conventionally drawn in is the WIGNER-SEITZ cell, which is a
/// different region of the same volume. The sheets are therefore clipped to
/// the zone here, where they already exist as triangles. And a Fermi surface
/// is per band: each crossing band contributes its own sheet, and which sheet
/// is which is the electron/hole distinction — kept apart by default
/// (FermiSurfaceMeshMode::Separate) so that stays visible, though the
/// physics never depends on it: extraction always runs per band regardless
/// of whether the meshes end up merged for display (see
/// FermiSurfaceMeshMode::Combined).
///
/// Appearance is FermiSurfaceStyle, and — unlike VolumetricStyle, live
/// workspace-tab state lost on close — it persists in a sidecar JSON file
/// beside fermi_surface.json (see loadResults() / closeEvent()): a results
/// viewer has no workspace tab of its own to keep it alive between
/// reopenings.
class FermiSurfaceWindow : public QDialog {
    Q_OBJECT

public:
    explicit FermiSurfaceWindow(QWidget* parent = nullptr);

    /// Parse a `fermi_surface.json` and build the sheets. False (showing
    /// nothing) when the file is missing or malformed.
    bool loadResults(const QString& jsonPath);

protected:
    void closeEvent(QCloseEvent* event) override;

private:
    struct Band {
        int index = 0;
        double minEv = 0.0;
        double maxEv = 0.0;
        bool crosses = false;
        std::vector<double> energies; ///< nx*ny*nz, z fastest
    };

    /// Grid points per band: nx·ny·nz. The length every band's energy array
    /// must have, and the row count of the CSV export.
    std::size_t pointCount() const;
    void rebuild();
    void populateBandList();
    void exportImage();
    /// Ask where, then write. Split from writeCsv() so the file format has a
    /// seam a test can reach without a modal dialog in the way.
    void exportData();
    /// Path of the appearance sidecar next to sourcePath_'s fermi_surface.json
    /// — empty when nothing is loaded yet.
    QString viewStylePath() const;
    /// Refresh every control from style_ (no signal emitted) — the counterpart
    /// of the constructor's own one-shot build, needed because loadResults()
    /// applies a style read from disk onto controls already built with
    /// defaults.
    void applyStyleToControls();
    /// Grey out / enable controls that only apply to the current mesh mode or
    /// color mode — e.g. per-band recoloring only means something in
    /// Separate + ByBand.
    void syncControlAvailability();
    QWidget* buildSheetsSection(QWidget* parent);
    QWidget* buildMaterialSection(QWidget* parent);
    QWidget* buildColorSection(QWidget* parent);
    QWidget* buildZoneSection(QWidget* parent);
    QWidget* buildQualitySection(QWidget* parent);
    /// Let the user pick a color for the currently-selected band row.
    void recolorSelectedBand();

public:
    /// Write the interpolated grid to `path` as CSV: one row per k-point, one
    /// energy column per band, k₃ fastest. Reconstructable as a structured
    /// grid in ParaView (Table To Structured Grid) or Mayavi (reshape).
    /// False when the file could not be written or there is no grid loaded.
    bool writeCsv(const QString& path) const;

private:
    /// The colour a band's sheet is drawn in: style_.perBandColors[index] if
    /// that slot holds a valid override, else grainCastColor(index) sampled
    /// through style_.bandGradient — the shared source of truth for the
    /// canvas, the band list swatches and the CSV header.
    QColor bandColor(int index) const;

    VolumeViewWidget* canvas_ = nullptr;
    QListWidget* bandList_ = nullptr;
    QPushButton* recolorBandButton_ = nullptr;
    QCheckBox* clipCheck_ = nullptr;
    QCheckBox* zoneCheck_ = nullptr;
    QCheckBox* labelsCheck_ = nullptr;
    QDoubleSpinBox* energySpin_ = nullptr;
    QLabel* summary_ = nullptr;

    // --- Mesh / color mode --------------------------------------------------
    QComboBox* meshModeCombo_ = nullptr;
    QComboBox* colorModeCombo_ = nullptr;
    QPushButton* combinedColorButton_ = nullptr;

    // --- Interpolation / quality --------------------------------------------
    QComboBox* interpolationCombo_ = nullptr;
    QSpinBox* refineSpin_ = nullptr;
    QSpinBox* smoothingSpin_ = nullptr;

    // --- Material -------------------------------------------------------
    QComboBox* shadingCombo_ = nullptr;
    QDoubleSpinBox* ambientSpin_ = nullptr;
    QDoubleSpinBox* specularSpin_ = nullptr;
    QDoubleSpinBox* opacitySpin_ = nullptr;
    QCheckBox* wireframeCheck_ = nullptr;

    // --- Coloring -------------------------------------------------------
    QComboBox* gradientCombo_ = nullptr;
    QCheckBox* invertGradientCheck_ = nullptr;
    QCheckBox* velocityBoundsCheck_ = nullptr;
    QDoubleSpinBox* velocityMinSpin_ = nullptr;
    QDoubleSpinBox* velocityMaxSpin_ = nullptr;

    // --- Brillouin zone ---------------------------------------------------
    QPushButton* zoneColorButton_ = nullptr;
    QDoubleSpinBox* zoneWidthSpin_ = nullptr;

    FermiSurfaceStyle style_;
    /// Guards every applyStyleToControls() sweep, same reason
    /// EditVolumetricRenderDialog's updating_ exists: without it each
    /// setValue() below would fire its own handler and write the value it
    /// was just given straight back into style_, and rebuild() N times over.
    bool updating_ = false;

    QJsonObject data_;
    QString sourcePath_;
    double fermiEv_ = 0.0;
    /// Samples along b1, b2, b3. Read from a three-element "samples" array, or
    /// from the single int older runs wrote.
    std::array<int, 3> samples_{0, 0, 0};
    std::array<core::Vec3, 3> reciprocal_{};
    std::vector<Band> bands_;
};

} // namespace calango::gui
