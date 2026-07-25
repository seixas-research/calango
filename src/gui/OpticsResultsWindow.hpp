#pragma once

#include <QDialog>
#include <QList>
#include <QPair>
#include <QString>

#include <vector>

class QComboBox;

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

private Q_SLOTS:
    /// Rebuild the plotted series from the Quantity + Direction combos.
    void updatePlot();
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
        std::vector<double> eps1, eps2, absorption, reflectivity, n, k, loss;
        std::vector<double> alpha2dRe, alpha2dIm, absorbance, sigma2dRe,
            sigma2dIm;
        bool twoDimensional = false;
    };
    const DirectionData* currentDirection() const;

    std::vector<double> energy_;                       ///< ħω grid, eV
    QList<QPair<QString, DirectionData>> directions_;  ///< in xx, yy, zz order

    class OpticsPlotWidget* plot_ = nullptr;
    QComboBox* quantityCombo_ = nullptr;
    QComboBox* directionCombo_ = nullptr;
    QComboBox* unitCombo_ = nullptr;
    bool hasData_ = false;
};

} // namespace calango::gui
