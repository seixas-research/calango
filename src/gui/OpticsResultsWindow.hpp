#pragma once

#include "gui/OpticsPlotStyleDialog.hpp"

#include <QDialog>
#include <QList>
#include <QPair>
#include <QString>

#include <vector>

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QSlider;

namespace calango::gui {

/// Post-processing viewer for a finished optical-properties job. Reads the
/// `optics.json` written by OpticsScriptGenerator (a photon-energy grid plus,
/// per direction, ε₁/ε₂, absorption, reflectivity, n/k and the loss function)
/// and plots any one quantity against ħω for a chosen direction. A small
/// self-contained multi-series QPainter widget draws the curves; the data can
/// be exported as CSV or the plot as a high-resolution image. hasData()
/// reports whether a usable optics.json was found and parsed.
///
/// A job run through Modules → 2D Materials → "2D Optics…" additionally stores
/// the sheet observables (absorbance A(ω), polarizability α₂D, conductivity
/// σ₂D). Those quantities are offered only when the file actually carries them:
/// they are meaningless for a bulk run, where ε₃D is the property and there is
/// no vacuum thickness to divide back out.
class OpticsResultsWindow : public QDialog {
    Q_OBJECT

public:
    explicit OpticsResultsWindow(const QString& directory,
                                 QWidget* parent = nullptr);

    bool hasData() const { return hasData_; }

    /// The spectra currently DERIVED for one direction, at the broadening the
    /// viewer is showing.
    ///
    /// Public because the Lorentzian re-derivation is the only physics in this
    /// window, and physics that can only be checked by looking at a plot is not
    /// checked. Index into directions() order; empty for an out-of-range one.
    struct DerivedSpectra {
        std::vector<double> energy, eps1, eps2, absorption, reflectivity;
        std::vector<double> n, k, loss, absorbance;
    };
    DerivedSpectra derivedSpectra(int direction = 0) const;
    /// The broadening currently applied (eV), and the floor it cannot go below.
    double broadening() const { return broadening_; }
    double storedBroadening() const { return etaStored_; }

private Q_SLOTS:
    /// Rebuild the plotted series from the Quantity + Direction combos.
    void updatePlot();
    /// "Customize Appearance…": open the styling dialog, applying live.
    void customizeAppearance();
    /// Re-label and re-scale the range spin boxes after a unit change, keeping
    /// the window pointing at the same physical span where possible.
    void retuneRangeForUnit();
    /// Energy + every quantity for the current direction, one row per sample.
    void exportCsv();
    /// Render the current plot to a high-resolution PNG / JPEG.
    void exportImage();

private:
    void loadDirectory(const QString& directory);

    /// X-axis unit. The data is always stored as photon energy in eV (that is
    /// what the generator writes); wavelength is a view of it.
    enum class XAxisUnit { EnergyEv, WavelengthNm };

    /// Photon energy (eV) → vacuum wavelength (nm): λ = hc/E with
    /// hc = 1239.84197 eV·nm.
    static constexpr double kHcEvNm = 1239.84197;

    /// The plotted abscissa in the selected unit, together with `series`
    /// filtered to match it index for index.
    ///
    /// Converting to wavelength is singular at E = 0 — the grid GPAW writes
    /// starts there — so those samples are DROPPED rather than clamped. A
    /// clamp would place a fabricated finite wavelength on the axis; dropping
    /// removes the sample from the abscissa and from every curve together, so
    /// no series is silently shifted against the others.
    std::vector<double> abscissa(
        std::vector<QPair<QString, std::vector<double>>>& series) const;

    /// Axis title for the current unit.
    QString xAxisLabel() const;

    /// Which spectrum the plot shows. Stored as combo item DATA, not as an
    /// index: the 2D entries only exist for a 2D job, so index-based dispatch
    /// would silently plot the wrong quantity for bulk runs.
    enum class Quantity {
        Dielectric,
        Absorption,
        Reflectivity,
        RefractiveIndex,
        Loss,
        Absorbance,      ///< A(ω), 2D only
        Polarizability,  ///< α₂D(ω), 2D only
        Conductivity,    ///< σ₂D(ω), 2D only
    };

    /// The spectra stored for one polarization direction. The 2D block is
    /// present only when the job ran as a sheet (the generator emits `twod_*`
    /// alongside each direction); those vectors stay empty otherwise.
    struct DirectionData {
        /// The dielectric function AS STORED, at the run's own η. Everything
        /// below is derived from these two and the current broadening; these
        /// are never overwritten.
        std::vector<double> rawEps1, rawEps2;
        std::vector<double> eps1, eps2, absorption, reflectivity, n, k, loss;
        std::vector<double> alpha2dRe, alpha2dIm, absorbance, sigma2dRe,
            sigma2dIm;
        bool twoDimensional = false;
    };
    const DirectionData* currentDirection() const;

    /// Re-derive every displayed spectrum from the stored ε at the current
    /// broadening.
    ///
    /// Optical broadening is LORENTZIAN, not Gaussian — η enters the response
    /// function as 1/(E − ω − iη), so it is a lifetime, not a resolution
    /// blur. That is what makes this possible at all: Lorentzian widths ADD
    /// under convolution, L(η₁) ∗ L(η₂) = L(η₁+η₂), so convolving a spectrum
    /// stored at η_stored with a Lorentzian of width (η − η_stored) gives
    /// exactly the spectrum GPAW would have produced at η. Verified against
    /// the analytic two-level resolvent to ~0.1 % on a realistic 0–20 eV grid
    /// (tests/OpticsBroadeningTest.cpp).
    ///
    /// It is one-way: η can only be INCREASED. Narrowing would be a
    /// deconvolution, which is not a filter but an inverse problem, and on a
    /// spectrum with any noise in it an unstable one.
    void rebuildSpectra();

    std::vector<double> energy_;                       ///< ħω grid, eV
    QList<QPair<QString, DirectionData>> directions_;  ///< in xx, yy, zz order

    /// "Show visible spectrum" — shades 380-750 nm behind the curves.
    QCheckBox* visibleSpectrumCheck_ = nullptr;
    class SpectrumPlotWidget* plot_ = nullptr;
    QComboBox* quantityCombo_ = nullptr;
    QComboBox* directionCombo_ = nullptr;
    QComboBox* unitCombo_ = nullptr;
    /// Display window on the abscissa. Both zero means "fit the data"; the
    /// suffix and sensible range follow the selected unit.
    QDoubleSpinBox* xMinSpin_ = nullptr;
    QDoubleSpinBox* xMaxSpin_ = nullptr;
    OpticsPlotStyle style_;
    bool hasData_ = false;

    // -- Live broadening ----------------------------------------------------
    /// η the run computed at — the floor this viewer can only add to.
    double etaStored_ = 0.0;
    /// η currently displayed (eV); never below etaStored_.
    double broadening_ = 0.0;
    /// Vacuum-direction cell length (Å), needed to re-derive the sheet
    /// observables. Zero when the run was not a 2D one.
    double vacuumLengthA_ = 0.0;
    QSlider* broadeningSlider_ = nullptr;
    QDoubleSpinBox* broadeningSpin_ = nullptr;
    QLabel* broadeningNote_ = nullptr;
};

} // namespace calango::gui
