#pragma once

#include "core/CalculatorConfig.hpp"
#include "gui/SimulationWizardBase.hpp"

#include <QList>
#include <QPair>
#include <QString>

#include <memory>

class QComboBox;

namespace calango::core {
class Structure;
}

namespace calango::gui {

/// Simulation → "Electron Localization Function (ELF)…": the standardized
/// multi-stage wizard for the ELF η(r) post-process. Stage 1 chooses the
/// wavefunction/density source (a completed GPAW `.gpw` baseline, or a fresh
/// ground state); Stages 2–3 are the shared Calculator Settings (engine
/// selection + backend knobs) and ASE Script Review. The Conda environment is
/// bound silently per engine from Preferences → "Python & Environments".
///
/// The ELF is a kinetic-energy-density post-process evaluated through GPAW, so
/// the engine combo is limited to the DFT backends that can drive it (GPAW is
/// the fully supported one; Quantum ESPRESSO / SIESTA select their own env and
/// SCF).
class ElfWizard : public SimulationWizardBase {
    Q_OBJECT

public:
    ElfWizard(std::shared_ptr<core::Structure> structure,
              QWidget* parent = nullptr);

    /// Populate the baseline source selector with completed processes that hold
    /// a calculated wavefunction/density (GPAW `.gpw`). Each entry is (display
    /// label, absolute path to the origin process directory). Call after
    /// construction, before exec().
    void setDensityBaselines(const QList<QPair<QString, QString>>& baselines);

protected:
    QString wizardTitle() const override;
    QString settingsHeader() const override;
    QWidget* buildSettingsPage() override;
    QString generateScript() const override;
    QString exportFileName() const override { return QStringLiteral("elf.py"); }
    bool calculatorAllowed(core::CalculatorKind kind) const override;
    bool hasTaskSettingsStage() const override { return true; }
    bool settingsStageFirst() const override { return true; }

private:
    std::shared_ptr<core::Structure> structure_;
    QComboBox* baselineCombo_ = nullptr; ///< wavefunction source (origin process)
};

} // namespace calango::gui
