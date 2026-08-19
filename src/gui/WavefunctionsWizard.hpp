#pragma once

#include "core/CalculatorConfig.hpp"
#include "gui/SimulationWizardBase.hpp"

#include <QList>
#include <QPair>
#include <QString>

#include <memory>
#include <optional>
#include <vector>

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QTableWidget;

namespace calango::core {
class Structure;
}

namespace calango::gui {

/// Electronics -> "Wavefunctions…": real-space Kohn-Sham orbitals
/// (pseudo, or all-electron via the PAW reconstruction) as volumetric data,
/// one cube per selected state, in a single pass.
///
/// Shares its wavefunction-access layer with LdosWizard — both generators
/// go through AseScriptGenerator::gpawRestartFromBaselineScript /
/// gpawWaveFunctionHelperScript — and its baseline-peek / state-list data
/// source with EnergyDiagramWizard (gui::peekGpawEigenvalues), though the
/// UI here is a multi-select table rather than a diagram: picking several
/// states at once to batch-export is a list operation, not something a
/// level diagram helps with.
class WavefunctionsWizard : public SimulationWizardBase {
    Q_OBJECT

public:
    WavefunctionsWizard(std::shared_ptr<core::Structure> structure,
                       QWidget* parent = nullptr);

    /// Same shape as LdosWizard::setDensityBaselines().
    void setDensityBaselines(const QList<QPair<QString, QString>>& baselines);

    QString pythonExecutable() const override;

protected:
    QString wizardTitle() const override;
    QString settingsHeader() const override;
    QWidget* buildSettingsPage() override;
    QString generateScript() const override;
    QString exportFileName() const override
    {
        return QStringLiteral("wavefunctions.py");
    }
    bool calculatorAllowed(core::CalculatorKind kind) const override
    {
        return kind == core::CalculatorKind::Gpaw;
    }
    bool hasTaskSettingsStage() const override { return true; }
    bool settingsStageFirst() const override { return true; }
    bool showsCalculatorStage() const override { return false; }

private Q_SLOTS:
    void onBaselineChanged();
    /// Keeps selectionWarningLabel_ in sync with how many rows are ticked.
    /// Advisory, like every other pre-flight note in this app (Wannier's
    /// symmetry warning, Energy Diagrams' periodicity warning) — it does
    /// not disable Run; the generated script is what actually refuses (see
    /// WavefunctionScriptGenerator.cpp) when nothing is selected.
    void updateSelectionWarning();

private:
    void rebuildStateTable();

    std::shared_ptr<core::Structure> structure_;

    QComboBox* baselineCombo_ = nullptr;
    QLabel* inheritedLabel_ = nullptr;
    QLabel* baselineSummaryLabel_ = nullptr;
    QLabel* peekErrorLabel_ = nullptr;
    QLabel* selectionWarningLabel_ = nullptr;

    QTableWidget* stateTable_ = nullptr;
    QComboBox* quantityCombo_ = nullptr;
    QCheckBox* allElectronCheck_ = nullptr;
    QDoubleSpinBox* gridSpacingSpin_ = nullptr;

    std::optional<InheritedCalculator> inherited_;
    /// Every state the last peek found — stateTable_'s rows index into
    /// this 1:1, so generateScript() reads selections straight off it.
    struct PeekedState {
        int spin = 0;
        int kpt = 0;
        int band = 0;
        double energyEv = 0.0;
        double occupation = -1.0;
    };
    std::vector<PeekedState> peekedStates_;
};

} // namespace calango::gui
