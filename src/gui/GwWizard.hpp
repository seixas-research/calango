#pragma once

#include "core/GwScriptGenerator.hpp"
#include "gui/SimulationWizardBase.hpp"

#include <QList>
#include <QPair>

class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QSpinBox;

namespace calango::gui {

/// Simulation → "GW Calculations…": one-shot G₀W₀ quasiparticle corrections.
///
/// Two engines, each bound to the DFT code that produced its baseline — GPAW
/// corrects a `.gpw`, Yambo corrects a Quantum ESPRESSO `.save`. They are not
/// interchangeable: G₀W₀ perturbs a specific DFT solution, so the engine is
/// determined by which baseline exists, not by preference.
///
/// The wizard hides the shared Calculator Settings stage entirely. Every
/// ground-state parameter is inherited from the baseline, so offering an engine
/// dropdown and a cutoff here would present knobs that either do nothing or
/// silently contradict the calculation being corrected.
class GwWizard : public SimulationWizardBase {
    Q_OBJECT

public:
    explicit GwWizard(QWidget* parent = nullptr);

    /// Baselines by engine. GPAW entries are `.gpw` files; Yambo entries are
    /// directories holding a Quantum ESPRESSO `*.save`.
    void setBaselines(const QList<QPair<QString, QString>>& gpaw,
                      const QList<QPair<QString, QString>>& espresso);

    /// Interpreter the run binds to: the baseline's own environment when it
    /// recorded one. This is not cosmetic for Yambo — the pipeline shells out
    /// to `p2y` and `yambo`, which are found on the PATH of the environment the
    /// DFT baseline ran under, not the default one.
    QString pythonExecutable() const override;

protected:
    QString wizardTitle() const override;
    QString settingsHeader() const override;
    QWidget* buildSettingsPage() override;
    QString generateScript() const override;
    QString exportFileName() const override { return QStringLiteral("gw.py"); }
    /// Everything about the ground state comes from the baseline, so the
    /// shared engine/cutoff stage would only offer contradictory knobs.
    bool showsCalculatorStage() const override { return false; }

private Q_SLOTS:
    /// Repopulate the baseline list and retune the notes for the engine.
    void updateEngine();

private:
    core::GwConfig config() const;
    /// Provenance of the selected baseline, or empty when it has no sidecar.
    /// GPAW entries name a `.gpw` file, Yambo entries a job directory — both
    /// resolve to the job directory that holds `calculator.json`.
    std::optional<InheritedCalculator> selectedProvenance() const;

    QList<QPair<QString, QString>> gpawBaselines_;
    QList<QPair<QString, QString>> espressoBaselines_;

    QComboBox* engineCombo_ = nullptr;
    QComboBox* baselineCombo_ = nullptr;
    QComboBox* frequencyCombo_ = nullptr;
    QDoubleSpinBox* cutoffSpin_ = nullptr;
    QSpinBox* bandsSpin_ = nullptr;
    QSpinBox* belowSpin_ = nullptr;
    QSpinBox* aboveSpin_ = nullptr;
    QSpinBox* coresSpin_ = nullptr;
    QLabel* engineNoteLabel_ = nullptr;
};

} // namespace calango::gui
