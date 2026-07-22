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

class KPathSelector;

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
    QString generateScript() const override;
    QString exportFileName() const override { return QStringLiteral("phonon.py"); }

private:
    bool periodic_;
    std::shared_ptr<const core::Structure> structure_;

    QDoubleSpinBox* deltaSpin_;
    QSpinBox* supercellSpins_[3];
    QCheckBox* acousticCheck_;
    QSpinBox* meshSpin_;
    QSpinBox* bandPointsSpin_;
    KPathSelector* kpath_ = nullptr;
};

} // namespace calango::gui
