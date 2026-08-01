#pragma once

#include "core/NonlinearOpticsScriptGenerator.hpp"
#include "gui/SimulationWizardBase.hpp"

#include <QString>

#include <memory>

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QFormLayout;
class QLabel;
class QLineEdit;
class QSpinBox;

namespace calango::core {
class Structure;
}

namespace calango::gui {

/// Electronics → "Nonlinear Optics…": second-harmonic generation, the shift
/// current and the linear susceptibility tensor, through GPAW's `gpaw.nlopt`.
///
/// The one response module here that is NOT a post-process. Every other one
/// inherits a completed Single-Point Calculation; this one converges its own
/// ground state, because `gpaw.nlopt.matrixel.make_nlodata` asserts that
/// point-group symmetry is off and the sums over intermediate states need a
/// converged empty manifold and a dense mesh. An ordinary baseline has none of
/// those, and the symmetry check fails as a bare AssertionError — after the
/// run has already been paid for.
///
/// So the flow is Calculator Settings → Nonlinear Response → Script Review:
/// the ground state first, since what the response can resolve is decided
/// there, and the response parameters second.
class NonlinearOpticsWizard : public SimulationWizardBase {
    Q_OBJECT

public:
    explicit NonlinearOpticsWizard(std::shared_ptr<const core::Structure> structure,
                                   QWidget* parent = nullptr);

protected:
    QString wizardTitle() const override;
    QString settingsHeader() const override;
    QWidget* buildSettingsPage() override;
    QString generateScript() const override;
    QString exportFileName() const override
    {
        return QStringLiteral("nlopt.py");
    }
    /// gpaw.nlopt has no counterpart in the other engines.
    bool calculatorAllowed(core::CalculatorKind kind) const override
    {
        return kind == core::CalculatorKind::Gpaw;
    }
    QStringList calculatorElements() const override;
    /// The ground state is converged by this job, so its settings are a stage
    /// of this wizard rather than something inherited.
    bool showsCalculatorStage() const override { return true; }
    /// Calculator Settings first: the band count and k-mesh chosen there are
    /// what decide whether the response parameters on the next page can be
    /// resolved at all.
    bool settingsStageFirst() const override { return false; }

private Q_SLOTS:
    /// Show only the controls the selected responses use (the gauge is an SHG
    /// concept), refresh the component validation and the cost note.
    void updateResponseRows();

private:
    core::NonlinearOpticsConfig config() const;
    /// The component list as typed, split on commas/whitespace. Invalid
    /// entries are kept here and reported by the note — dropping them silently
    /// would make a typo look like a component that produced nothing.
    QStringList typedComponents() const;

    std::shared_ptr<const core::Structure> structure_;

    QCheckBox* shgCheck_ = nullptr;
    QCheckBox* shiftCheck_ = nullptr;
    QCheckBox* linearCheck_ = nullptr;
    QComboBox* gaugeCombo_ = nullptr;
    /// Owner of the gauge row, so it can be hidden outright when SHG is off:
    /// the gauge is a choice within that sum, and get_shift / get_chi_tensor
    /// take no such argument.
    QFormLayout* responseForm_ = nullptr;
    QLineEdit* componentsEdit_ = nullptr;
    QLabel* componentsNote_ = nullptr;
    QDoubleSpinBox* etaSpin_ = nullptr;
    QDoubleSpinBox* omegaMinSpin_ = nullptr;
    QDoubleSpinBox* omegaMaxSpin_ = nullptr;
    QSpinBox* npointsSpin_ = nullptr;
    QDoubleSpinBox* scissorsSpin_ = nullptr;
    QSpinBox* bandFirstSpin_ = nullptr;
    QSpinBox* bandLastSpin_ = nullptr;
    QComboBox* vacuumAxisCombo_ = nullptr;
};

} // namespace calango::gui
