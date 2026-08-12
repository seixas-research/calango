#pragma once

#include "core/ElectronPhononScriptGenerator.hpp"
#include "gui/SimulationWizardBase.hpp"

class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QSpinBox;

namespace calango::gui {

/// Simulation → "Electron-Phonon Coupling…": the `gpaw.elph` supercell
/// finite-difference workflow.
///
/// Three stages: the electron-phonon settings, the shared Calculator page, and
/// the ASE script review.
///
/// GPAW only, and locked that way rather than merely defaulted: `gpaw.elph` is
/// the implementation. The other engines' ASE calculators expose forces and
/// energies, not the change in the effective potential under a displacement,
/// which is the quantity this whole module is built on.
///
/// The wizard's job beyond collecting numbers is to keep the user out of the
/// two configurations that waste the expensive stage. The supercell bounds
/// which q-points exist at all, and the k-mesh must contain every k+q — both
/// are enforced live here, with the cost of the choice stated, because they
/// are otherwise discovered by GPAW hours in.
class ElectronPhononWizard : public SimulationWizardBase {
    Q_OBJECT

public:
    explicit ElectronPhononWizard(QWidget* parent = nullptr);

protected:
    QString wizardTitle() const override;
    QString settingsHeader() const override
    {
        return tr("Electron-Phonon Settings");
    }
    QWidget* buildSettingsPage() override;
    QString generateScript() const override;
    QString exportFileName() const override
    {
        return QStringLiteral("electron_phonon.py");
    }
    /// GPAW alone — see the class comment.
    bool calculatorAllowed(core::CalculatorKind kind) const override
    {
        return kind == core::CalculatorKind::Gpaw;
    }
    bool hasTaskSettingsStage() const override { return true; }
    bool settingsStageFirst() const override { return true; }
    /// The sweep page owns the k-mesh: it has to satisfy the k+q condition,
    /// and a second k-grid control on the calculator page would be a value the
    /// generated script ignores.
    bool showsKpointGridRow() const override { return false; }

private:
    core::ElectronPhononConfig runConfig() const;
    /// Restate the cost and the two commensurability conditions for the
    /// current numbers. Live, because "6N+1 SCF runs" is an abstraction until
    /// it says 25.
    void updateSummary();

    QSpinBox* supercellSpins_[3] = {nullptr, nullptr, nullptr};
    QSpinBox* qGridSpins_[3] = {nullptr, nullptr, nullptr};
    QSpinBox* kGridSpins_[3] = {nullptr, nullptr, nullptr};
    QComboBox* basisCombo_ = nullptr;
    QDoubleSpinBox* deltaSpin_ = nullptr;
    QDoubleSpinBox* phononSmearingSpin_ = nullptr;
    QDoubleSpinBox* muStarSpin_ = nullptr;
    QDoubleSpinBox* plasmaSpin_ = nullptr;
    QDoubleSpinBox* temperatureSpin_ = nullptr;
    QLabel* summaryLabel_ = nullptr;
};

} // namespace calango::gui
