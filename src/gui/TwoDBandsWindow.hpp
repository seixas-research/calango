#pragma once

#include "render/ColorMap.hpp"

#include <QDialog>
#include <QString>

#include <array>
#include <vector>

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QListWidget;
class QPushButton;
class QSlider;
class QSpinBox;
class QStackedWidget;

namespace calango::gui {

class BzMapView;
class VolumeViewWidget;

/// Results viewer for a 2D Bands run: each selected band drawn as a surface
/// E_n(k_x, k_y) over the k_x-k_y plane, in an orbitable 3D canvas.
///
/// The canvas is the Volumetric viewer's — same orbit camera, same lit shader,
/// same interaction — because a band surface and an isosurface are the same
/// kind of object once triangulated, and two near-identical OpenGL widgets is
/// one too many.
///
/// The vertical axis is energy, and it has to be RESCALED to be legible: k runs
/// over a few Å⁻¹ while energies span tens of eV, so plotted 1:1 the surfaces
/// are a vertical wall. "Energy scale" sets that exaggeration and is reported
/// in the axis label, so nothing about the picture is silently distorted.
///
/// A second, flat view sits behind a selector: ONE band's E(k_x, k_y) painted
/// as colour over the exact first Brillouin zone, folded periodically from the
/// run's optional Monkhorst-Pack "bz_map" sampling. Runs that predate the
/// option keep the 3D view; the map entry stays visible but disabled, with a
/// tooltip saying how to compute one — a vanished option looks like a
/// regression, a greyed one is an instruction.
class TwoDBandsWindow : public QDialog {
    Q_OBJECT

public:
    explicit TwoDBandsWindow(QWidget* parent = nullptr);

    /// Parse a `bands_2d.json` and build the surfaces. False (showing nothing)
    /// when the file is missing or malformed.
    bool loadResults(const QString& jsonPath);

private:
    /// One band's sampled surface, in the units the file carries.
    struct Surface {
        int band = 0;
        int spin = 0;
        double minEv = 0.0;
        double maxEv = 0.0;
        std::vector<std::vector<double>> energies; ///< [ix][iy], eV
    };

    /// A labelled high-symmetry point of the 2D zone (Γ, M, K, …).
    struct SpecialPoint {
        QString label;
        double kx = 0.0; ///< Å⁻¹
        double ky = 0.0;
    };

    /// Rebuild the geometry from the current selection and settings.
    void rebuild();
    /// The half-planes bounding the first Brillouin zone (Wigner-Seitz cell of
    /// the reciprocal lattice): a point k is inside when k·n̂ ≤ d for all of
    /// them. Empty when the reciprocal cell is unusable.
    std::vector<std::array<double, 3>> brillouinHalfPlanes() const;
    /// Vertices of the first-BZ polygon, counter-clockwise, for its outline.
    std::vector<std::array<double, 2>> brillouinPolygon() const;
    /// Upsample the k-grid and one band by the selected scheme and factor.
    /// Returns false (leaving the outputs untouched) when no refinement
    /// applies, so the caller can use the original arrays without copying.
    bool refine(const std::vector<std::vector<double>>& energies,
                std::vector<std::vector<double>>& outKx,
                std::vector<std::vector<double>>& outKy,
                std::vector<std::vector<double>>& outEnergies) const;
    /// Repopulate the band list (called once per load).
    void populateBandList();
    /// Build the settings panel that sits to the right of the canvas.
    QWidget* buildSettingsPanel();
    /// Build the page holding the flat first-Brillouin-zone map view.
    QWidget* buildMapPage();
    /// Refresh the orientation read-out from the canvas after any view change.
    void syncOrientationReadout();
    void exportImage();
    void exportData();

    VolumeViewWidget* canvas_ = nullptr;
    QListWidget* bandList_ = nullptr;
    // View selector and the two stacked pages. A selector rather than a split
    // view: each projection wants the whole canvas, and they answer different
    // questions — the surfaces show dispersion as shape, the map shows one
    // band's energy over the zone's true geometry.
    QComboBox* viewCombo_ = nullptr;
    QStackedWidget* viewStack_ = nullptr;
    BzMapView* mapView_ = nullptr;
    QSpinBox* mapBandSpin_ = nullptr;
    QCheckBox* mapShiftFermiCheck_ = nullptr;
    QComboBox* mapGradientCombo_ = nullptr;
    QComboBox* gradientCombo_ = nullptr;
    /// Colormap vs. one solid colour. A colormap encodes energy as hue, which
    /// is what a single surface wants; several surfaces at once are better
    /// told apart by giving each a flat colour, and a figure often wants one
    /// deliberate colour rather than a rainbow.
    QComboBox* colorModeCombo_ = nullptr;
    QPushButton* solidColorButton_ = nullptr;
    QColor solidColor_{255, 170, 60};
    // Explicit view controls beside the canvas. The arcball covers free
    // rotation; these cover the things a pointer gesture is bad at — landing
    // exactly on an axis, and repeating an orientation for a figure.
    QSlider* rollSlider_ = nullptr;
    QLabel* orientationLabel_ = nullptr;
    QCheckBox* shiftFermiCheck_ = nullptr;
    QCheckBox* fermiPlaneCheck_ = nullptr;
    QCheckBox* axesCheck_ = nullptr;
    QCheckBox* brillouinCheck_ = nullptr;
    QCheckBox* labelsCheck_ = nullptr;
    QComboBox* interpolationCombo_ = nullptr;
    QSpinBox* refineSpin_ = nullptr;
    QDoubleSpinBox* energyScaleSpin_ = nullptr;
    QLabel* summary_ = nullptr;

    QString sourcePath_;
    double fermiEv_ = 0.0;
    int samples_ = 0;
    bool spinOrbit_ = false;
    /// Cartesian k of each grid node, Å⁻¹ (2π included), [ix][iy].
    std::vector<std::vector<double>> kx_;
    std::vector<std::vector<double>> ky_;
    std::vector<Surface> surfaces_;
    std::vector<SpecialPoint> specialPoints_;
    /// Reciprocal lattice rows b1, b2, b3 in Å⁻¹ (2π included) — the basis the
    /// first-Brillouin-zone construction is built from.
    std::array<std::array<double, 3>, 3> reciprocal_{};
    /// Half-extent of the k-grid, used to set the default energy exaggeration
    /// so the first view is legible without touching the control.
    double kExtent_ = 1.0;
};

} // namespace calango::gui
