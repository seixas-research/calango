#pragma once

#include "core/Structure.hpp"
#include "core/VolumetricData.hpp"

#include <QDialog>

#include <memory>
#include <vector>

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QPushButton;
class QSlider;
class QSpinBox;

namespace calango::gui {

class ViewportWidget;

/// Viewport → "Lattice Plane…": an interactive crystallographic plane overlay.
///
/// The plane is oriented by Miller indices (h,k,l) — its normal is the
/// reciprocal-lattice vector G = h·b1 + k·b2 + l·b3 — and displaced along that
/// normal by an offset. It renders as a translucent, edge-outlined quad in the
/// main 3D viewport. Optionally, a loaded volumetric scalar field (charge
/// density, ELF, …) color-maps the plane, turning it into a 2D color-slice
/// through the 3D grid, sampled and drawn inside the OpenGL canvas.
///
/// Modeless, like PartialChargeDialog: the user orbits the structure while
/// adjusting the plane. The overlay is cleared when the dialog closes.
class LatticePlaneDialog : public QDialog {
    Q_OBJECT

public:
    LatticePlaneDialog(std::shared_ptr<const core::Structure> structure,
                       ViewportWidget* viewport, QWidget* parent = nullptr);

protected:
    void closeEvent(QCloseEvent* event) override;

private Q_SLOTS:
    void chooseColor();
    void loadField();
    /// Recompute the plane geometry and push it to the viewport.
    void rebuild();

private:
    /// Build the interleaved pos(3)+color(3) triangle + edge streams for the
    /// current Miller indices / offset / styling / (optional) field slice.
    void buildGeometry(std::vector<float>& faceTris,
                       std::vector<float>& edgeLines) const;
    void updateColorButton();

    std::shared_ptr<const core::Structure> structure_;
    ViewportWidget* viewport_;
    std::shared_ptr<const core::VolumetricData> field_;

    QSpinBox* hSpin_ = nullptr;
    QSpinBox* kSpin_ = nullptr;
    QSpinBox* lSpin_ = nullptr;
    QSlider* offsetSlider_ = nullptr;
    QDoubleSpinBox* offsetSpin_ = nullptr;
    QPushButton* colorButton_ = nullptr;
    QSlider* opacitySlider_ = nullptr;
    QCheckBox* edgesCheck_ = nullptr;
    QCheckBox* visibleCheck_ = nullptr;
    QCheckBox* sliceCheck_ = nullptr;
    QComboBox* gradientCombo_ = nullptr;
    QPushButton* loadFieldButton_ = nullptr;
    QLabel* fieldLabel_ = nullptr;

    QColor planeColor_{80, 190, 230};
    double planeExtent_ = 10.0; ///< half-size of the plane quad (Å)
};

} // namespace calango::gui
