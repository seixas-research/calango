#pragma once

#include "gui/SimulationWizardBase.hpp"

#include <QList>
#include <QPair>
#include <QString>

#include <memory>

class QCheckBox;
class QComboBox;
class QLabel;
class QSpinBox;

namespace calango::core {
class Structure;
}

namespace calango::gui {

/// Modules → 2D Materials → "2D Bands…": the band structure of a sheet as a
/// SURFACE E_n(k_x, k_y) rather than as a path through the Brillouin zone.
///
/// Same workflow as the Electronic Structure wizard, and deliberately so: a
/// mandatory completed single point supplies the converged density, the run is
/// non-self-consistent on top of it, and cutoff / XC / mode are inherited
/// rather than re-asked. The difference is the sampling. A 1D band structure
/// walks a path Γ→M→K→Γ, which is a set of cuts through the dispersion; this
/// samples the plane itself, which is what makes a Dirac cone look like a cone
/// and a band touching visible as a touching rather than as a near-miss
/// between two lines.
///
/// GPAW only. The whole method is `calc.fixed_density(kpts=…)` on a saved
/// wavefunction file, which no other engine in this application exposes.
class TwoDBandsWizard : public SimulationWizardBase {
    Q_OBJECT

public:
    TwoDBandsWizard(std::shared_ptr<const core::Structure> structure,
                    QWidget* parent = nullptr);

    /// Populate the baseline selector with completed Single-Point Calculations
    /// that saved a density (`.gpw`): (display label, absolute path).
    void setDensityBaselines(const QList<QPair<QString, QString>>& baselines);

protected:
    QString wizardTitle() const override;
    QStringList calculatorElements() const override;
    QString settingsHeader() const override { return QString(); } // unused
    QWidget* buildSettingsPage() override { return nullptr; }     // unused
    bool hasTaskSettingsStage() const override { return false; }
    QString generateScript() const override;
    QString exportFileName() const override
    {
        return QStringLiteral("bands_2d.py");
    }
    /// GPAW alone — see the class note.
    bool calculatorAllowed(core::CalculatorKind kind) const override
    {
        return kind == core::CalculatorKind::Gpaw;
    }
    QString calculatorSettingsHeader() const override
    {
        return tr("Baseline & Brillouin-Zone Sampling");
    }
    /// The SCF state is inherited from the baseline, so the engine/DFT chrome
    /// is hidden exactly as it is in the Electronic Structure wizard.
    bool showsEngineAndDftControls() const override { return false; }
    bool inheritsCalculatorFromBaseline() const override { return true; }
    QWidget* buildCalculatorExtras() override;

private:
    /// Warn in-page when the selected structure is not a sheet: the k_z = 0
    /// plane is the whole Brillouin zone only for something periodic in x and
    /// y with vacuum along z. The generated script raises on a genuinely
    /// non-periodic cell, but a bulk crystal produces a plausible-looking
    /// surface that means something else — so it is worth saying up front.
    void refreshDimensionalityNote();
    /// Estimated cost of the current grid, which is quadratic and the one
    /// setting that can turn a two-minute job into an overnight one.
    void refreshCostNote();

    std::shared_ptr<const core::Structure> structure_;

    QComboBox* baselineCombo_ = nullptr;
    QSpinBox* samplesSpin_ = nullptr;
    QSpinBox* belowSpin_ = nullptr;
    QSpinBox* aboveSpin_ = nullptr;
    QSpinBox* totalBandsSpin_ = nullptr;
    QCheckBox* spinOrbitCheck_ = nullptr;
    QLabel* dimensionalityNote_ = nullptr;
    QLabel* costNote_ = nullptr;
};

} // namespace calango::gui
