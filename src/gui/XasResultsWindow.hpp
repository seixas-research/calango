#pragma once

#include "gui/OpticsPlotStyleDialog.hpp"

#include <QDialog>
#include <QJsonObject>

#include <vector>

class QCheckBox;
class QDoubleSpinBox;
class QLabel;
class QSlider;

namespace calango::gui {

class SpectrumPlotWidget;

/// The X-ray absorption spectrum written by an XAS run (`xas.json`).
///
/// Shows the isotropic spectrum — the average over the three Cartesian
/// polarizations, which is what a powder or solution measurement sees — with
/// the individual polarizations available alongside it. For an oriented sample
/// the difference between them IS the result, so they are one checkbox away
/// rather than hidden.
///
/// Brought up to the Optics viewer's own standard: a "Customize Appearance…"
/// dialog (the same OpticsPlotStyleDialog/OpticsPlotStyle Optics uses —
/// SpectrumPlotWidget was factored out of the Optics viewer specifically so
/// both windows draw through one styled widget), an energy window, and
/// Export CSV…/Export Image… buttons in the same place Optics puts them.
///
/// One thing XAS can do that Optics cannot: live re-broadening from the RAW
/// discrete transitions (`stick_energy_eV` and, per polarization,
/// `stick_isotropic`/`stick_polarization_x/y/z`), not merely a widening
/// convolution of an already-broadened curve. A stick is a delta function
/// with no width of its own to deconvolve, so re-folding it at ANY FWHM —
/// narrower or wider than the run's own — is a well-defined forward
/// computation (see foldSticks()), unlike Optics's Lorentzian η, which can
/// only be raised because the stored curve already carries a width.
class XasResultsWindow : public QDialog {
    Q_OBJECT

public:
    explicit XasResultsWindow(QWidget* parent = nullptr);

    /// Load `xas.json`. False (with nothing shown) when it is missing or
    /// unreadable.
    bool loadResults(const QString& path);

    /// Gaussian-fold `stickEnergy`/`stickIntensity` onto `grid` at `fwhm` —
    /// gpaw.xas.XAS.constant_broadening(), reproduced here (see
    /// ~/Codes/gpaw/gpaw/xas.py) so the viewer can re-derive the spectrum at
    /// any width directly from the transitions GPAW itself folds, rather
    /// than approximating a re-broadening by convolving an already-broadened
    /// curve the way the Optics viewer has to.
    ///
    /// Public, like OpticsResultsWindow::derivedSpectra(): this is the only
    /// physics in this window, and physics that can only be checked by
    /// looking at a plot is not checked (tests/XasBroadeningTest.cpp).
    static std::vector<double> foldSticks(const std::vector<double>& grid,
                                          const std::vector<double>& stickEnergy,
                                          const std::vector<double>& stickIntensity,
                                          double fwhm);

private Q_SLOTS:
    void refreshPlot();
    /// "Customize Appearance…": open the styling dialog, applying live.
    void customizeAppearance();
    /// Energy + every displayed curve, one row per sample.
    void exportCsv();
    /// Render the current plot to a high-resolution PNG / JPEG.
    void exportImage();

private:
    QJsonObject data_;
    SpectrumPlotWidget* plot_ = nullptr;
    QCheckBox* polarizationCheck_ = nullptr;
    QCheckBox* sticksCheck_ = nullptr;
    QLabel* summaryLabel_ = nullptr;
    OpticsPlotStyle style_;

    // -- Energy window, like the Optics viewer's own Range: row -------------
    QDoubleSpinBox* xMinSpin_ = nullptr;
    QDoubleSpinBox* xMaxSpin_ = nullptr;

    // -- Live re-broadening ---------------------------------------------
    QSlider* broadeningSlider_ = nullptr;
    QDoubleSpinBox* broadeningSpin_ = nullptr;
    QLabel* broadeningNote_ = nullptr;
    /// FWHM (eV) the run itself computed at — the slider opens here.
    double fwhmStored_ = 0.0;
    /// FWHM currently displayed; equals fwhmStored_ until the control is
    /// touched.
    double broadening_ = 0.0;
    /// Whether the RUN used the linear broadening ramp — if so, the stored
    /// continuous curve (shown at broadening_ == fwhmStored_) has an
    /// energy-DEPENDENT width the uniform re-fold this window offers cannot
    /// reproduce; broadeningNote_ says so rather than silently disagreeing.
    bool linearBroadeningStored_ = false;

    // Raw data for the live re-fold. energyGrid_ is the grid every curve —
    // stored or re-folded — is plotted and exported on.
    std::vector<double> energyGrid_;
    std::vector<double> stickEnergy_;
    std::vector<double> stickIsotropic_;
    std::vector<double> stickX_, stickY_, stickZ_;
    /// False for a job directory written before per-polarization sticks were
    /// recorded — the isotropic curve can still be re-folded, x/y/z cannot
    /// and stay at their stored values (with a note) until the job is re-run.
    bool hasPolarizationSticks_ = false;
};

} // namespace calango::gui
