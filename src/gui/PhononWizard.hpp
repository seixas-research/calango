#pragma once

#include "gui/SimulationWizardBase.hpp"

#include <memory>

class QCheckBox;
class QDoubleSpinBox;
class QSpinBox;

namespace calango::core {
class Structure;
}

namespace calango::gui {

class EmbeddedKPathEditor;

/// Simulation → "Phonon Calculator…": the standardized 4-stage wizard. Stage 1
/// is the phonon settings (finite-displacement δ, supercell expansion, acoustic
/// sum rule, the mesh density for the DOS / band-structure interpolation, and —
/// for periodic systems — a q-path definition with the interactive Brillouin
/// Zone Builder for the dispersion plot). Stages 2–4 are the shared
/// calculator/environment, calculator settings and ASE script review.
/// `periodic` selects finite-displacement phonons vs molecular normal modes.
class PhononWizard : public SimulationWizardBase {
    Q_OBJECT

public:
    PhononWizard(bool periodic, std::shared_ptr<const core::Structure> structure,
                 QWidget* parent = nullptr);

protected:
    QString wizardTitle() const override;
    QString settingsHeader() const override;
    QWidget* buildSettingsPage() override;
    QWidget* buildCalculatorExtras() override;
    /// Same flow as Electronic Structure: the q-path is defined once the
    /// engine is chosen, so this page is Stage 3.
    bool settingsStageFirst() const override { return false; }
    QString generateScript() const override;
    QString exportFileName() const override { return QStringLiteral("phonon.py"); }

private:
    bool periodic_;
    std::shared_ptr<const core::Structure> structure_;

    QDoubleSpinBox* deltaSpin_;
    QSpinBox* supercellSpins_[3];
    QCheckBox* acousticCheck_;
    QSpinBox* meshSpin_;
    QDoubleSpinBox* dosWidthSpin_;
    EmbeddedKPathEditor* kpath_ = nullptr;
};

} // namespace calango::gui
