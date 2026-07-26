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

/// Simulation → "Phonon Calculator…": a 4-stage wizard.
///   Stage 1  Calculator Settings — the shared engine page, identical to the
///            Single-Point and Geometry Optimization setups.
///   Stage 2  Phonon Settings — supercell nx×ny×nz, displacement δ, the
///            symmetry-reduction and residual-force toggles, and the DOS mesh.
///   Stage 3  q-Path Definition — the interactive Brillouin-zone builder for
///            the dispersion ω(q).
///   Stage 4  ASE Script Review.
///
/// The displacement settings and the q-path used to share one page, which
/// conflated two separate decisions: how the force constants are sampled, and
/// where the dispersion is read out. `periodic` selects finite-displacement
/// phonons vs molecular Γ-point normal modes (which collapse stages 2–3 to the
/// handful of controls that still apply).
class PhononWizard : public SimulationWizardBase {
    Q_OBJECT

public:
    PhononWizard(bool periodic, std::shared_ptr<const core::Structure> structure,
                 QWidget* parent = nullptr);

protected:
    QString wizardTitle() const override;
    QString settingsHeader() const override;
    QWidget* buildSettingsPage() override;
    /// Stage 2 — the displacement / supercell settings, between Calculator
    /// Settings and the q-path page.
    QString secondSettingsHeader() const override;
    QWidget* buildSecondSettingsPage() override;
    /// Calculator Settings lead; the phonon settings and q-path follow.
    /// Dispersion is offered here: force constants are second derivatives of the energy, so a missing dispersion term shifts every frequency.
    bool showsDispersionToggle() const override { return true; }
    bool settingsStageFirst() const override { return false; }
    QString generateScript() const override;
    QString exportFileName() const override { return QStringLiteral("phonon.py"); }

private:
    bool periodic_;
    std::shared_ptr<const core::Structure> structure_;

    QDoubleSpinBox* deltaSpin_ = nullptr;
    QSpinBox* supercellSpins_[3] = {nullptr, nullptr, nullptr};
    QCheckBox* acousticCheck_ = nullptr;
    QCheckBox* symmetryCheck_ = nullptr;  ///< symmetry-reduced displacements
    QCheckBox* residualCheck_ = nullptr;  ///< remove residual forces
    QSpinBox* meshSpin_ = nullptr;
    QDoubleSpinBox* dosWidthSpin_ = nullptr;
    EmbeddedKPathEditor* kpath_ = nullptr;
};

} // namespace calango::gui
