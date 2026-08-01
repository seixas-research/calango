#pragma once

#include "gui/GpawElectronicWizard.hpp"

#include <QList>
#include <QPair>
#include <QString>

#include <memory>

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QGroupBox;
class QLabel;
class QSpinBox;

namespace calango::core {
class Structure;
}

class QFormLayout;

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
class PhononWizard : public GpawElectronicWizard {
    Q_OBJECT

public:
    PhononWizard(bool periodic, std::shared_ptr<const core::Structure> structure,
                 QWidget* parent = nullptr);

    /// Completed Born Effective Charges runs (label -> born_charges.json), for
    /// the LO-TO splitting selector. Empty leaves the correction unavailable
    /// and says why.
    void setBornChargeProcesses(const QList<QPair<QString, QString>>& processes);
    /// Completed Optics runs (label -> optics.json), which carry the
    /// high-frequency dielectric function the correction also needs.
    void setOpticsProcesses(const QList<QPair<QString, QString>>& processes);

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
    QSpinBox* meshSpins_[3] = {nullptr, nullptr, nullptr}; ///< qx, qy, qz
    QDoubleSpinBox* dosWidthSpin_ = nullptr;
    EmbeddedKPathEditor* kpath_ = nullptr;

    // -- LO-TO splitting ---------------------------------------------------
    /// Z* files adopted through "Load…" rather than discovered as workspace
    /// runs. Held so they can be re-added when setBornChargeProcesses()
    /// rebuilds the combo, which happens every time a job finishes.
    QList<QPair<QString, QString>> loadedBornFiles_;
    /// Fill eps_inf from a completed Optics run's optics.json, taking the
    /// zero-frequency limit of eps_1 along each axis. Returns false (and says
    /// why) when the file cannot supply it.
    bool loadDielectricFromOptics(const QString& file);
    /// Enable/disable the eps_inf controls with the Born selection, and warn
    /// when the tensor is still the physically impossible identity.
    /// "Load…" beside the Born-charges combo: adopt a born_charges.json from
    /// outside this workspace.
    void loadBornChargesFile();
    void updateLoToState();

    QGroupBox* loToGroup_ = nullptr;
    QComboBox* bornCombo_ = nullptr;
    QComboBox* opticsCombo_ = nullptr;
    QDoubleSpinBox* dielectricSpin_[3] = {nullptr, nullptr, nullptr};
    QLabel* loToNote_ = nullptr;
};

} // namespace calango::gui
