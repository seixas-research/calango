#pragma once

#include "core/Structure.hpp"

#include <QCheckBox>
#include <QDoubleSpinBox>
#include <QLabel>
#include <QSpinBox>
#include <QTimer>
#include <QWizard>
#include <QWizardPage>

#include <array>
#include <memory>
#include <vector>

namespace calango::gui {

class ViewportWidget;
class SlabWizard;

/// Stage-1 canvas: axonometric view of the bulk lattice with the current
/// surface plane drawn as the parallelogram spanned by the in-plane cell
/// vectors u and v. The vector tips are draggable handles that snap to
/// lattice points; because snapped vectors are exact integer combinations
/// u = p·(a1,a2,a3), v = q·(a1,a2,a3), the Miller indices of the plane
/// they span are the integer cross product (h,k,l) = p × q (gcd-reduced)
/// — emitted via millerDragged() on release.
class OrientationCanvas : public QWidget {
    Q_OBJECT

public:
    explicit OrientationCanvas(QWidget* parent = nullptr);

    void setBulk(const core::Structure& bulk);
    /// Canonical in-plane vectors for the current (hkl), as lattice
    /// vectors of the bulk cell (integer fractional coordinates).
    void setSurfaceVectors(const core::Vec3& u, const core::Vec3& v);

Q_SIGNALS:
    void millerDragged(int h, int k, int l);

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;

private:
    struct LatticePoint {
        std::array<int, 3> n; ///< integer coefficients in the bulk basis
        core::Vec3 pos;       ///< Cartesian Å
    };

    QPointF project(const core::Vec3& p) const; ///< axonometric + fit
    void updateProjectionFit();
    core::Vec3 fromCoeff(const std::array<int, 3>& n) const;
    std::array<int, 3> currentMiller() const; ///< p × q, gcd-reduced

    core::UnitCell cell_;
    std::vector<core::Atom> atoms_;
    std::vector<LatticePoint> lattice_;
    std::array<int, 3> coeffU_{1, 0, 0};
    std::array<int, 3> coeffV_{0, 1, 0};
    int dragging_ = 0; ///< 0 = none, 1 = u handle, 2 = v handle
    double scale_ = 1.0;
    QPointF offset_;
};

/// Stage-2 canvas: orthogonal cross-section perpendicular to the surface
/// plane (horizontal = in-plane position, vertical = height along the
/// surface normal). Atomic layers are drawn as horizontal lines; clicking
/// a layer assigns it as the top or bottom termination (whichever current
/// boundary is closer to the click).
class CrossSectionCanvas : public QWidget {
    Q_OBJECT

public:
    explicit CrossSectionCanvas(QWidget* parent = nullptr);

    void setSlab(const core::Structure& slab, const std::vector<double>& layerZ);
    void setSelection(int bottomLayer, int topLayer);

Q_SIGNALS:
    void terminationPicked(int bottomLayer, int topLayer);

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;

private:
    double zToY(double z) const;

    std::vector<core::Atom> atoms_;
    std::vector<double> layerZ_;
    int bottom_ = 0;
    int top_ = 0;
    double xMin_ = 0.0, xMax_ = 1.0, zMin_ = 0.0, zMax_ = 1.0;
};

/// Stage 1: surface orientation. Miller spinboxes synchronized with the
/// interactive vector canvas; "Next" locks the orientation.
class OrientationPage : public QWizardPage {
    Q_OBJECT

public:
    explicit OrientationPage(SlabWizard* wizard);

    void initializePage() override;
    bool isComplete() const override;

private Q_SLOTS:
    void onMillerEdited();
    void recomputeSurfaceVectors();

private:
    SlabWizard* wizard_;
    QSpinBox* millerSpins_[3];
    OrientationCanvas* canvas_;
    QLabel* infoLabel_;
    QTimer debounce_;
    bool valid_ = false;
};

/// Stage 2: out-of-plane cut. Cross-section view with clickable layer
/// terminations plus layer-count and Ångström thickness controls.
class TerminationPage : public QWizardPage {
    Q_OBJECT

public:
    explicit TerminationPage(SlabWizard* wizard);

    void initializePage() override;
    bool isComplete() const override;

private Q_SLOTS:
    void onTerminationPicked(int bottom, int top);
    void onLayerCountEdited(int count);
    void onThicknessEdited(double angstrom);

private:
    void syncControls();

    SlabWizard* wizard_;
    CrossSectionCanvas* canvas_;
    QSpinBox* layerCountSpin_;
    QDoubleSpinBox* thicknessSpin_;
    QLabel* infoLabel_;
    QLabel* statusLabel_;
    bool updating_ = false;
    bool valid_ = false;
};

/// Stage 3: vacuum configuration and final generation, with a live 3D
/// preview of the finished slab.
class VacuumPage : public QWizardPage {
    Q_OBJECT

public:
    explicit VacuumPage(SlabWizard* wizard);

    void initializePage() override;
    bool isComplete() const override;

private Q_SLOTS:
    void rebuild();

private:
    SlabWizard* wizard_;
    QDoubleSpinBox* topVacuumSpin_;
    QDoubleSpinBox* bottomVacuumSpin_;
    QCheckBox* centeredCheck_;
    QLabel* infoLabel_;
    ViewportWidget* preview_;
    bool updating_ = false;
};

/// "Build → Surface Slab": interactive 3-stage slab construction.
///   1. Orientation — Miller indices + draggable in-plane cell vectors
///   2. Cut — termination layers and thickness on a cross-section view
///   3. Vacuum — top/bottom spacing (or centered) + full 3D preview
/// The tall reference stack comes from ase.build.surface; the cut and
/// vacuum stages slice and re-cell it in C++.
class SlabWizard : public QWizard {
    Q_OBJECT

public:
    explicit SlabWizard(std::shared_ptr<const core::Structure> bulk,
                        QWidget* parent = nullptr);

    std::shared_ptr<core::Structure> result() const { return result_; }
    QString resultLabel() const;

    // -- Shared wizard state (owned here, edited by the pages) -------------
    std::shared_ptr<const core::Structure> bulk;
    int h = 0, k = 0, l = 1;
    core::Structure tallSlab;      ///< many-layer stack, no vacuum
    bool tallSlabValid = false;
    std::vector<double> layerZ;    ///< sorted distinct layer heights (Å)
    int bottomLayer = 0;
    int topLayer = 0;
    double vacuumTop = 10.0;
    double vacuumBottom = 10.0;

    /// Rebuild `tallSlab` + `layerZ` for the current (h, k, l); returns
    /// false (with tallSlabValid unset) if ASE rejects the cut.
    bool buildTallSlab();

    /// Slice the selected layers out of the tall stack and apply the
    /// vacuum: the final slab, stored in result() (null on failure).
    std::shared_ptr<core::Structure> buildResult();

private:
    std::shared_ptr<core::Structure> result_;
};

} // namespace calango::gui
