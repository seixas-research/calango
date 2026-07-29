#pragma once

#include "gui/OpticsPlotStyleDialog.hpp"

#include <QDialog>
#include <QString>
#include <QWidget>

#include <vector>

class QCheckBox;
class QDoubleSpinBox;
class QLabel;
class QPainter;

namespace calango::gui {

/// One convergence curve: a quantity against the swept parameter, drawn as a
/// line with a marker at every evaluated point, styled by OpticsPlotStyle
/// (the shared "pens, fonts and colors" struct — nothing optical about it).
///
/// Distinct from SpectrumPlotWidget because a convergence plot has two things
/// a spectrum does not: discrete evaluated points that deserve markers (the
/// curve is an interpolation between a handful of expensive SCFs, not a dense
/// signal), and an optional hatched horizontal band marking the region within
/// the convergence threshold of the reference value. Band color, pattern and
/// opacity come from the style's threshold-band fields.
class ConvergencePlotWidget : public QWidget {
public:
    explicit ConvergencePlotWidget(QWidget* parent = nullptr);

    void setData(std::vector<double> x, std::vector<double> y);
    void setLabels(const QString& xLabel, const QString& yLabel);
    void setStyle(const OpticsPlotStyle& style);
    /// The convergence corridor [low, high] around the reference value. When
    /// visible it is drawn as a hatched band and included in the y-range, so
    /// a tight threshold cannot vanish off-scale.
    void setThresholdBand(double low, double high, bool visible);
    /// Pin the y-axis to exactly [minimum, maximum] (no padding), instead of
    /// fitting the data. The σ×threshold zoom uses this: the far-from-
    /// converged early points would otherwise set the scale and flatten the
    /// interesting tail into a line. Off-range points draw clipped.
    void setFixedYRange(double minimum, double maximum, bool enabled);

    /// Draw the chart into `painter` filling a logical area of `size` — used
    /// by both paintEvent and the image export, so the file shows exactly
    /// what the screen does.
    bool renderTo(QPainter& painter, QSize size) const;

protected:
    void paintEvent(QPaintEvent*) override;

private:
    std::vector<double> x_;
    std::vector<double> y_;
    QString xLabel_;
    QString yLabel_;
    OpticsPlotStyle style_;
    double bandLow_ = 0.0;
    double bandHigh_ = 0.0;
    bool bandVisible_ = false;
    double fixedYMin_ = 0.0;
    double fixedYMax_ = 0.0;
    bool fixedYEnabled_ = false;
};

/// Result window shared by the Parameters Convergence modules. Three panels
/// against the swept parameter, all differenced against the best run in the
/// set (highest cutoff / densest mesh):
///
///   ΔE/atom = (E_total − E_ref) / N      — energy convergence, per atom so
///                                          the criterion transfers between
///                                          cells (plotted in meV/atom)
///   max_i |F_i − F_i,ref|                — force convergence, vector-wise
///                                          against the reference and NOT
///                                          divided by N (meV/Å)
///   mean_n |⟨ε_n⟩ − ⟨ε_n⟩_ref|           — eigenvalue convergence: MAD of
///                                          the k-averaged band energies
///                                          (meV)
///
/// Optional threshold corridors (on by default) hatch |ΔE| ≤ τ_E and
/// ΔF ≤ τ_F; band styling lives in Customize Appearance.
///
/// One class for both sweeps rather than one per module: everything that
/// makes a convergence study readable is identical, and only the swept
/// parameter's name, results file and x values differ.
class ConvergenceResultsWindow : public QDialog {
    Q_OBJECT

public:
    /// Which sweep produced the directory's results.
    enum class Sweep {
        PlaneWaveCutoff, ///< cutoff_convergence.json, x = ecut (eV)
        KpointGrid,      ///< kpoints_convergence.json, x = k-points per axis
    };

    /// The three plotted quantities, in panel order.
    enum class Quantity { EnergyDelta, ForceError, EigenvalueMad };

    ConvergenceResultsWindow(Sweep sweep, const QString& directory,
                             QWidget* parent = nullptr);

    /// False when the directory holds no readable results JSON — the host
    /// deletes the window instead of showing an empty shell.
    bool hasData() const { return hasData_; }

private Q_SLOTS:
    void customizeAppearance();
    void updateThresholdBands();

private:
    bool loadResults(const QString& jsonPath);
    QWidget* buildColumn(Quantity quantity, ConvergencePlotWidget*& plot);
    void exportCsv(Quantity quantity);
    void exportImage(Quantity quantity);
    const std::vector<double>& values(Quantity quantity) const;
    /// "560 eV" / "6×6×6" — the swept value at curve index `index`, for the
    /// threshold summary and the intro line.
    QString xValueLabel(std::size_t index) const;

    Sweep sweep_;
    bool hasData_ = false;
    QString directory_;

    // The curves, in evaluation order (ascending parameter). All three are
    // differences against the reference run, in meV so the numbers on screen
    // match the units convergence criteria are quoted in.
    std::vector<double> xValues_;
    /// Human-readable form of each x value ("6×6×6" for a mesh; empty for
    /// sweeps whose number speaks for itself).
    std::vector<QString> xTexts_;
    std::vector<double> deltaEnergyMevPerAtom_;
    std::vector<double> forceErrorMevPerA_;
    /// NaN where a run produced no eigenvalues — the plot skips the point.
    std::vector<double> eigenvalueMadMev_;
    QString referenceLabel_;

    OpticsPlotStyle style_;
    ConvergencePlotWidget* energyPlot_ = nullptr;
    ConvergencePlotWidget* forcePlot_ = nullptr;
    ConvergencePlotWidget* eigenPlot_ = nullptr;
    QCheckBox* thresholdCheck_ = nullptr;
    QDoubleSpinBox* energyThresholdSpin_ = nullptr;
    QDoubleSpinBox* forceThresholdSpin_ = nullptr;
    QDoubleSpinBox* eigenThresholdSpin_ = nullptr;
    /// σ×threshold y-axis zoom: each panel clamps to ±σ·τ of its own
    /// criterion, so the converged tail is inspectable at the scale that
    /// decides it.
    QCheckBox* scaleCheck_ = nullptr;
    QDoubleSpinBox* sigmaSpin_ = nullptr;
    QLabel* thresholdSummary_ = nullptr;
};

} // namespace calango::gui
