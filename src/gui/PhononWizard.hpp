#pragma once

#include "gui/SimulationWizardBase.hpp"

class QCheckBox;
class QDoubleSpinBox;
class QSpinBox;

namespace calango::gui {

/// Simulation → "Phonon Calculator…": the standardized 4-stage wizard. Stage 1
/// is the phonon settings (finite-displacement δ, supercell expansion, acoustic
/// sum rule, and the mesh density for the DOS / band-structure interpolation);
/// Stages 2–4 are the shared calculator/environment, calculator settings and
/// ASE script review. `periodic` selects finite-displacement phonons (periodic)
/// vs molecular normal modes.
class PhononWizard : public SimulationWizardBase {
    Q_OBJECT

public:
    explicit PhononWizard(bool periodic, QWidget* parent = nullptr);

protected:
    QString wizardTitle() const override;
    QString settingsHeader() const override;
    QWidget* buildSettingsPage() override;
    QString generateScript() const override;
    QString exportFileName() const override { return QStringLiteral("phonon.py"); }

private:
    bool periodic_;

    QDoubleSpinBox* deltaSpin_;
    QSpinBox* supercellSpins_[3];
    QCheckBox* acousticCheck_;
    QSpinBox* meshSpin_;
    QSpinBox* bandPointsSpin_;
};

} // namespace calango::gui
