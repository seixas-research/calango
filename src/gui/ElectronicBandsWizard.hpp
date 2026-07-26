#pragma once

#include "gui/SimulationWizardBase.hpp"

#include <memory>

class QCheckBox;
class QComboBox;
class QLabel;
class QDoubleSpinBox;
class QGroupBox;
class QSpinBox;

#include <QList>
#include <QPair>
#include <QString>

namespace calango::core {
class Structure;
}

namespace calango::gui {

/// Simulation → "Electronic Structure…": a bands + PDOS wizard built on a
/// mandatory prior single-point (SCF) baseline. The run restarts from the
/// baseline's saved charge density (.gpw) and evaluates the bands / PDOS
/// non-self-consistently, so the plane-wave cutoff, XC functional and mode are
/// inherited from that baseline and hidden. Stages: Calculator Settings (engine
/// + PDOS k-mesh / energy points / smearing) → k-Path Definition (embedded
/// Brillouin-zone builder) → ASE script review. The engine choice maps to the
/// electronic backend: GPAW → GPAW, Quantum ESPRESSO → Espresso, etc.
class ElectronicBandsWizard : public SimulationWizardBase {
    Q_OBJECT

public:
    ElectronicBandsWizard(std::shared_ptr<const core::Structure> structure,
                          QWidget* parent = nullptr);

    /// Populate the Stage-1 charge-density baseline selector with completed
    /// Single-Point Calculations that saved a density (`.gpw`). Each entry is
    /// (display label, absolute path to the density file). When the GPAW
    /// backend is chosen, selecting one runs the bands/PDOS non-self-
    /// consistently off that fixed density.
    void setDensityBaselines(const QList<QPair<QString, QString>>& baselines);

protected:
    QString wizardTitle() const override;
    /// Species present in the structure, seeding the Hubbard editor's
    /// element completer.
    QStringList calculatorElements() const override;
    QString settingsHeader() const override { return QString(); } // unused (merged)
    QWidget* buildSettingsPage() override { return nullptr; }      // unused (merged)
    /// The former k-Path Definition stage is merged into the calculator page:
    /// there is no separate task-settings stage.
    bool hasTaskSettingsStage() const override { return false; }
    QString generateScript() const override;
    QString exportFileName() const override { return QStringLiteral("bands.py"); }
    /// Only DFT-capable electronic-structure engines (GPAW, SIESTA, VASP,
    /// Quantum ESPRESSO); the empirical/ML calculators can't produce bands.
    bool calculatorAllowed(core::CalculatorKind kind) const override;
    /// One merged setup stage: baseline selection + PDOS settings + k-path.
    QString calculatorSettingsHeader() const override
    {
        return tr("Configuration & k-Path Definition");
    }
    /// The whole engine/DFT/GPAW chrome is hidden: the SCF state (cutoff, XC,
    /// mode, SCF k-grid, execution mode) is inherited from the baseline and
    /// never shown. Only baseline selection + PDOS + k-path appear.
    bool showsEngineAndDftControls() const override { return false; }
    /// Baseline + PDOS + the embedded k-path builder, all on the merged stage.
    QWidget* buildCalculatorExtras() override;
    void updateCalculatorExtras(core::CalculatorKind kind) override;
    /// Cutoff / XC / mode are locked to the selected baseline SCF density.
    bool inheritsCalculatorFromBaseline() const override { return true; }
    /// Rescale the PDOS k-mesh default to 2× the (baseline) SCF k-grid.
    void calculatorKgridChanged() override;

private:
    /// Set the PDOS k-mesh spinboxes to 2× the SCF k-grid along each
    /// non-vacuum direction (a direction sampled with a single k-point stays 1),
    /// unless the user has already edited them.
    void applyPdosKmeshDefault();

    std::shared_ptr<const core::Structure> structure_;

    class EmbeddedKPathEditor* kpath_ = nullptr;
    QComboBox* baselineCombo_ = nullptr; ///< charge-density baseline selector
    /// "Spin Configurations" — the spin treatment of the BAND evaluation, as
    /// opposed to the collinear polarization of the SCF, which is inherited
    /// from the baseline along with the density.
    QGroupBox* spinGroup_ = nullptr;
    QCheckBox* spinOrbitCheck_ = nullptr;
    QGroupBox* pdosGroup_ = nullptr;
    QCheckBox* pdosCheck_ = nullptr;
    QDoubleSpinBox* pdosWidthSpin_ = nullptr;
    QSpinBox* pdosKptsSpin_[3] = {nullptr, nullptr, nullptr};
    QSpinBox* energyPointsSpin_ = nullptr;
    /// Once the user edits the PDOS k-mesh, stop auto-rescaling it.
    bool pdosKptsUserEdited_ = false;
};

} // namespace calango::gui
