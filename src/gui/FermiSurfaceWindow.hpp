#pragma once

#include "core/Vec3.hpp"

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
class QSpinBox;

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
/// is which is the electron/hole distinction, so they are listed and coloured
/// separately rather than merged into one surface.
class FermiSurfaceWindow : public QDialog {
    Q_OBJECT

public:
    explicit FermiSurfaceWindow(QWidget* parent = nullptr);

    /// Parse a `fermi_surface.json` and build the sheets. False (showing
    /// nothing) when the file is missing or malformed.
    bool loadResults(const QString& jsonPath);

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
    /// Half-spaces bounding the first Brillouin zone: a point k is inside when
    /// k·n̂ ≤ d for every entry (n̂x, n̂y, n̂z, d).
    std::vector<std::array<double, 4>> zoneHalfSpaces() const;
    void rebuild();
    void populateBandList();
    void exportImage();
    /// Ask where, then write. Split from writeCsv() so the file format has a
    /// seam a test can reach without a modal dialog in the way.
    void exportData();

public:
    /// Write the interpolated grid to `path` as CSV: one row per k-point, one
    /// energy column per band, k₃ fastest. Reconstructable as a structured
    /// grid in ParaView (Table To Structured Grid) or Mayavi (reshape).
    /// False when the file could not be written or there is no grid loaded.
    bool writeCsv(const QString& path) const;

private:
    /// The colour a band's sheet is drawn in — the shared source of truth for
    /// the canvas, the band list swatches and the CSV header.
    QColor bandColor(int index) const;

    VolumeViewWidget* canvas_ = nullptr;
    QListWidget* bandList_ = nullptr;
    QCheckBox* clipCheck_ = nullptr;
    QCheckBox* zoneCheck_ = nullptr;
    QCheckBox* labelsCheck_ = nullptr;
    QDoubleSpinBox* energySpin_ = nullptr;
    QLabel* summary_ = nullptr;

    // --- Interpolation ------------------------------------------------------
    // Marching cubes reproduces the grid it is given, so a coarse grid gives a
    // faceted surface however it is shaded. Refining the field BEFORE
    // extraction is what actually smooths the sheet, and it is the same
    // refinement the volumetric renderer offers.
    QComboBox* interpolationCombo_ = nullptr;
    QSpinBox* refineSpin_ = nullptr;

    // --- Appearance ---------------------------------------------------------
    QComboBox* gradientCombo_ = nullptr;
    QDoubleSpinBox* opacitySpin_ = nullptr;
    QCheckBox* litCheck_ = nullptr;

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
