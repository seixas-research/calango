#pragma once

// Required on Linux: libstdc++ (GCC 13+) no longer pulls <cstdint> in
// transitively. Do not remove even if clangd reports it unused on macOS.
#include <cstdint>

#include "core/ClusterVariation.hpp"
#include "core/SublatticeClusterVariation.hpp"

#include <QString>
#include <QWidget>
#include <vector>

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QSpinBox;
class QTableWidget;

namespace calango::gui {

/// One approximation's curve, ready to draw.
struct CvmCurve {
    QString label;
    QColor colour;
    /// Drawn as a horizontal line across the whole axis rather than as a
    /// polyline. The ideal entropy does not depend on temperature, and
    /// plotting it as a flat series of points invites the reader to look for
    /// structure in it that cannot be there.
    bool horizontal = false;
    double constantValue = 0.0;
    std::vector<double> xs;
    std::vector<double> ys;
};

/// The comparison canvas: configurational entropy against temperature or
/// composition, with the ideal baseline and both CVM approximations on the
/// same axes.
///
/// The comparison IS the deliverable. A single S_conf from any one
/// approximation cannot tell you whether short-range order mattered — only
/// the SPREAD between the ideal bound, the pair result and the tetrahedron
/// result does. So all three are drawn together by construction rather than
/// offered as alternative views, and the gap between the ideal line and the
/// curves is shaded to make the size of the correction the first thing the
/// eye lands on.
class CvmComparisonPlot : public QWidget {
    Q_OBJECT

public:
    explicit CvmComparisonPlot(QWidget* parent = nullptr);

    void setCurves(std::vector<CvmCurve> curves, const QString& xLabel,
                   const QString& yLabel);
    /// Draw a vertical rule at T_c. Zero removes it.
    void setTransitionTemperature(double kelvin, const QString& label);
    /// Render at an arbitrary size, so the exported figure is the figure on
    /// screen rather than a second drawing path that can drift from it.
    void render(QPainter& painter, const QRectF& bounds) const;
    bool exportImage(const QString& path, double scale) const;

    QSize sizeHint() const override { return {720, 460}; }

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    std::vector<CvmCurve> curves_;
    QString xLabel_;
    QString yLabel_;
    double transitionK_ = 0.0;
    QString transitionLabel_;
};

/// The CVM / Alloy Thermodynamics module window.
///
/// Composition, lattice and pair interactions in; three entropy curves out.
/// Deliberately a plain window rather than a wizard: nothing here launches a
/// job, every solve is milliseconds, and a wizard's page-at-a-time flow would
/// hide the one interaction that matters — changing an interaction strength
/// and watching the three curves separate.
class CvmComparisonWindow : public QWidget {
    Q_OBJECT

public:
    explicit CvmComparisonWindow(QWidget* parent = nullptr);

    /// Seed the interactions from a fitted pair ECI (ClusterExpansionFit).
    void setPairEci(double eci);

private Q_SLOTS:
    void recompute();
    void exportImage();
    void exportData();

private:
    void buildControls(QWidget* panel);
    core::CvmInput currentInput(core::CvmApproximation approximation) const;

    /// Marks T_c with a vertical rule. Only meaningful when the four-
    /// sublattice solver ran and actually found a transition.
    QComboBox* latticeCombo_ = nullptr;
    QCheckBox* longRangeCheck_ = nullptr;
    QComboBox* orderCombo_ = nullptr;
    QComboBox* axisCombo_ = nullptr;
    QSpinBox* speciesSpin_ = nullptr;
    QTableWidget* compositionTable_ = nullptr;
    QDoubleSpinBox* interactionSpin_ = nullptr;
    QDoubleSpinBox* minTemperatureSpin_ = nullptr;
    QDoubleSpinBox* maxTemperatureSpin_ = nullptr;
    QSpinBox* stepsSpin_ = nullptr;
    QLabel* provenanceLabel_ = nullptr;
    QLabel* summaryLabel_ = nullptr;
    CvmComparisonPlot* plot_ = nullptr;
};

} // namespace calango::gui
