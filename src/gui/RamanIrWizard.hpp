#pragma once

#include "core/CalculatorConfig.hpp"
#include "core/RamanIrScriptGenerator.hpp"
#include "gui/SimulationWizardBase.hpp"

#include <QList>
#include <QPair>
#include <QString>

#include <memory>
#include <optional>

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QSpinBox;

namespace calango::core {
class Structure;
}

namespace calango::gui {

/// Electronics → "Raman and IR Spectroscopy…": the standardized wizard for the
/// vibrational-spectroscopy post-process.
///
/// It is the one wizard here that inherits from THREE upstream runs, because
/// the two spectra it produces are built from three different quantities:
///
///   • a Single-Point `.gpw`      — the converged geometry and the calculator
///                                  every displaced run is rebuilt from;
///   • a Born Effective Charges run — Z*, without which an IR intensity in a
///                                  periodic crystal cannot be formed at all
///                                  (there is no molecular dipole to
///                                  differentiate). OPTIONAL: it is what turns
///                                  the IR column on, and nothing else here
///                                  depends on it — the phonons come from
///                                  finite displacements and the Raman
///                                  activities from dchi/dQ;
///   • an Optics run              — optional, and used for its SETTINGS: the
///                                  broadening the dielectric response was
///                                  validated with, so the static
///                                  polarizability differentiated here is
///                                  consistent with the spectrum already
///                                  inspected.
///
/// Stage 1 is those selections plus the spectrum settings; the Calculator
/// Settings stage is dropped entirely (the calculator is restored whole from
/// the baseline, exactly as in Optics and Born Charges), leaving Stage 2 as the
/// shared ASE Script Review.
class RamanIrWizard : public SimulationWizardBase {
    Q_OBJECT

public:
    RamanIrWizard(std::shared_ptr<const core::Structure> structure,
                  QWidget* parent = nullptr);

    /// Completed Single-Point runs that saved their wavefunctions, as
    /// (label, absolute path to the .gpw). MANDATORY — call before exec().
    void setDensityBaselines(const QList<QPair<QString, QString>>& baselines);
    /// Completed Born Effective Charges runs, as (label, absolute path to
    /// born_charges.json).
    ///
    /// Optional; an empty list leaves the selector on "(none)", and the run
    /// then reports every IR intensity as zero with `ir.computed = false` in
    /// raman_ir.json rather than failing or inventing one.
    void setBornChargesResults(const QList<QPair<QString, QString>>& results);
    /// Completed Optics runs, as (label, absolute path to optics.json).
    /// Optional; an empty list simply leaves the selector on "(none)".
    void setOpticsResults(const QList<QPair<QString, QString>>& results);

    /// The run binds to the baseline's own interpreter when its provenance
    /// records one, so the displaced runs execute against the same GPAW build
    /// that produced the ground state.
    QString pythonExecutable() const override;

protected:
    QString wizardTitle() const override;
    QString settingsHeader() const override;
    QWidget* buildSettingsPage() override;
    QString generateScript() const override;
    QString exportFileName() const override
    {
        return QStringLiteral("raman_ir.py");
    }
    /// Berry-phase Z* and the response function are GPAW capabilities here.
    bool calculatorAllowed(core::CalculatorKind kind) const override
    {
        return kind == core::CalculatorKind::Gpaw;
    }
    QStringList calculatorElements() const override;
    /// The calculator is restored whole from the baseline; there is nothing
    /// left for a Calculator Settings stage to ask.
    bool showsCalculatorStage() const override { return false; }

private Q_SLOTS:
    /// Re-derive the "N atoms × … runs" estimate, which changes by a large
    /// factor with the Raman toggle.
    void updateCostEstimate();
    /// Re-read the selected baseline's calculator.json and refresh the note.
    void onBaselineChanged();

private:
    core::RamanIrConfig config() const;

    std::shared_ptr<const core::Structure> structure_;

    QComboBox* baselineCombo_ = nullptr;
    QComboBox* bornCombo_ = nullptr;
    QComboBox* opticsCombo_ = nullptr;
    QLabel* inheritanceNote_ = nullptr;
    std::optional<InheritedCalculator> inherited_;

    QCheckBox* ramanCheck_ = nullptr;
    QDoubleSpinBox* displacementSpin_ = nullptr;
    QDoubleSpinBox* laserSpin_ = nullptr;
    QDoubleSpinBox* temperatureSpin_ = nullptr;
    QDoubleSpinBox* broadeningSpin_ = nullptr;
    QDoubleSpinBox* freqMinSpin_ = nullptr;
    QDoubleSpinBox* freqMaxSpin_ = nullptr;
    QSpinBox* npointsSpin_ = nullptr;
    QLabel* costLabel_ = nullptr;
};

} // namespace calango::gui
