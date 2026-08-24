#pragma once

#include "gui/SimulationWizardBase.hpp"

#include <memory>

class QCheckBox;
class QComboBox;
class QLabel;
class QDoubleSpinBox;
class QGroupBox;
class QSpinBox;
class QTableWidget;

#include <QList>
#include <QPair>
#include <QString>

#include <vector>

namespace calango::core {
class Structure;
struct FatbandProjection;
}

namespace calango::gui {

/// Simulation → "Electronic Structure…": a bands + PDOS wizard built on a
/// mandatory prior single-point (SCF) baseline. The run restarts from the
/// baseline's saved charge density (.gpw or, for VASP, CHGCAR via ICHARG=11)
/// and evaluates the bands / PDOS non-self-consistently, so the plane-wave
/// cutoff, XC functional and mode are inherited from that baseline and
/// hidden. Stages: Calculator Settings (own engineCombo_ + PDOS k-mesh /
/// energy points / smearing — showsEngineAndDftControls() stays off, so the
/// standard chrome is not what is on screen here) → k-Path Definition
/// (embedded Brillouin-zone builder) → ASE script review. The engine choice
/// maps to the electronic backend: GPAW → GPAW, VASP → Vasp, Quantum
/// ESPRESSO → Espresso, SIESTA → Siesta.
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

    /// The selected baseline's WAVECAR, staged as `baseline.WAVECAR` so a
    /// REMOTE hybrid run finds the orbitals it restarts from. Empty on every
    /// other route — only the hybrid KPOINTS_OPT branch reads one.
    QString baselineWavecarPathToStage() const override;

    /// This wizard already picks its parent run — the "Baseline density"
    /// combo on the settings page — and derives the WAVECAR from it. A
    /// second, independent picker in the calculator group could only
    /// disagree with that choice, so it is not offered here.
    bool showsVaspWavecarRow() const override { return false; }
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
    /// The selected baseline density file, for remote staging (see the base
    /// class's own doc comment).
    QString baselineDensityPathToStage() const override;

private:
    /// Set the PDOS k-mesh spinboxes to 2× the SCF k-grid along each
    /// non-vacuum direction (a direction sampled with a single k-point stays 1),
    /// unless the user has already edited them.
    void applyPdosKmeshDefault();

    /// "Band symmetry" — irreducible-representation labels at the
    /// high-symmetry points of the path.
    QGroupBox* buildSymmetryGroup();
    /// "Orbital projections (fatbands)" — the per-channel atom/orbital table.
    QGroupBox* buildFatbandGroup();
    /// Append an empty channel row (atoms = all, orbital = the first shell).
    void addFatbandRow(const QString& atoms, int orbitalIndex,
                       const QString& label);
    /// One channel per element in the structure, summed over its p shell (or
    /// s, for hydrogen) — the selection a first look almost always wants.
    void seedFatbandRows();
    /// The table's rows as core projections; an empty result means "let the
    /// script derive one channel per element and shell".
    std::vector<core::FatbandProjection> fatbandProjections() const;
    /// 0-based atom indices from a "0, 2, 5-8" selection string; empty means
    /// every atom (of the row's element filter, if any). Same spelling as the
    /// Born-charges wizard's atom field.
    std::vector<int> parseAtomSelection(const QString& text) const;
    /// Spin-orbit coupling and the two scalar-state post-processes are
    /// mutually exclusive; keep the checkboxes honest about it.
    void updateSpinOrbitExclusions();

    std::shared_ptr<const core::Structure> structure_;

    class EmbeddedKPathEditor* kpath_ = nullptr;
    /// This wizard's own engine picker — see the class doc for why it is
    /// not the standard one (showsEngineAndDftControls() stays off). Same
    /// pattern as TwoDBandsWizard::engineCombo_.
    QComboBox* engineCombo_ = nullptr;
    QComboBox* baselineCombo_ = nullptr; ///< charge-density baseline selector
    /// "Spin Configurations" — the spin treatment of the BAND evaluation, as
    /// opposed to the collinear polarization of the SCF, which is inherited
    /// from the baseline along with the density.
    QGroupBox* spinGroup_ = nullptr;
    QCheckBox* spinOrbitCheck_ = nullptr;
    QGroupBox* pdosGroup_ = nullptr;
    QCheckBox* pdosCheck_ = nullptr;
    QSpinBox* pdosKptsSpin_[3] = {nullptr, nullptr, nullptr};
    QComboBox* dosIntegrationCombo_ = nullptr;
    QSpinBox* energyPointsSpin_ = nullptr;
    /// Once the user edits the PDOS k-mesh, stop auto-rescaling it.
    bool pdosKptsUserEdited_ = false;

    // -- Band symmetry ------------------------------------------------------
    QGroupBox* symmetryGroup_ = nullptr;
    QCheckBox* symmetryCheck_ = nullptr;
    QCheckBox* symmetryLinesCheck_ = nullptr;
    QDoubleSpinBox* symmetryTolSpin_ = nullptr;
    QDoubleSpinBox* symmetryDegenSpin_ = nullptr;
    QDoubleSpinBox* symmetryWindowSpin_ = nullptr;

    // -- Orbital projections (fatbands) -------------------------------------
    QGroupBox* fatbandGroup_ = nullptr;
    QCheckBox* fatbandCheck_ = nullptr;
    /// Columns: Atoms | Orbital | Label. An empty table means "one channel
    /// per element and shell", which the generated script derives itself.
    QTableWidget* fatbandTable_ = nullptr;
};

} // namespace calango::gui
