#pragma once

#include "core/CalculatorConfig.hpp"
#include "gui/SimulationWizardBase.hpp"

#include <QList>
#include <QPair>
#include <QString>

#include <memory>

class QComboBox;
class QSpinBox;

namespace calango::core {
class Structure;
}

namespace calango::gui {

/// Simulation → "Maximally Localized Wannier Functions (MLWF)…": the
/// standardized multi-stage wizard for the Marzari-Vanderbilt localization.
/// Stage 1 collects the MLWF settings (wavefunction source, number of Wannier
/// functions, trial-orbital initialization); Stages 2–3 are the shared
/// Calculator Settings (engine selection + backend knobs) and ASE Script
/// Review. The Conda environment is bound silently per engine from
/// Preferences → "Python & Environments".
///
/// The localization runs through ASE's `ase.dft.wannier`, which is driven by a
/// GPAW ground state, so the engine combo is limited to the DFT backends (GPAW
/// is the fully supported one; Quantum ESPRESSO / SIESTA select their own env
/// and SCF).
class WannierWizard : public SimulationWizardBase {
    Q_OBJECT

public:
    WannierWizard(std::shared_ptr<core::Structure> structure,
                  QWidget* parent = nullptr);

    /// Populate the baseline source selector with completed single-points that
    /// hold the Bloch wavefunctions (GPAW `.gpw`). Each entry is (display
    /// label, absolute path to the origin process directory). Call after
    /// construction, before exec().
    void setDensityBaselines(const QList<QPair<QString, QString>>& baselines);

protected:
    QString wizardTitle() const override;
    QString settingsHeader() const override;
    QWidget* buildSettingsPage() override;
    QString generateScript() const override;
    QString exportFileName() const override
    {
        return QStringLiteral("wannier.py");
    }
    bool calculatorAllowed(core::CalculatorKind kind) const override;
    bool hasTaskSettingsStage() const override { return true; }
    bool settingsStageFirst() const override { return true; }

private:
    std::shared_ptr<core::Structure> structure_;
    QComboBox* baselineCombo_ = nullptr;   ///< wavefunction source (origin proc)
    QSpinBox* nWannier_ = nullptr;         ///< number of Wannier functions
    QComboBox* projectionCombo_ = nullptr; ///< trial-orbital initialization
};

} // namespace calango::gui
