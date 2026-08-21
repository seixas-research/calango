#pragma once

#include "core/DsimScriptGenerator.hpp"
#include "core/Structure.hpp"
#include "gui/CellRelaxationControls.hpp"
#include "gui/ForceConvergenceControl.hpp"
#include "gui/SimulationWizardBase.hpp"

#include <QList>
#include <QPair>
#include <QString>

#include <memory>

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QPushButton;
class QSpinBox;

namespace calango::gui {

/// Modules -> Alloys -> "DSIM (Dilute Solution Interpolation)…": the
/// mixing enthalpy of an N-component alloy (N >= 2), interpolated between
/// its dilute-solution limits (subregular solution model — see
/// docs/sphinx/source/simulations/dsim.md, Eq. 4+6+9-10).
///
/// Stage 1 is DSIM's own settings: a list of N >= 2 pristine reference
/// structures — added from an open document or imported from a file, each
/// its own element's own native geometry, not a shared template relabeled
/// — plus the supercell repeat count. Stage 2 is the shared Calculator
/// Settings; Stage 3 is DSIM's own second page, Geometry Optimization
/// Settings (optimizer, force convergence, max steps, cell-relaxation mode
/// — see secondSettingsHeader()'s doc comment for why this is its own
/// stage rather than folded into Stage 1); Stage 4 is the shared ASE
/// Script Review.
/// Unlike every existing multi-calculation alloy module (EGQCA, CVM, the
/// Orchestration canvas's SqsGenerator/ClusterExpansionFit/CvmEntropy/
/// KkrCpa/TdbGenerator, all of which run in-process against a batch a
/// PRIOR job already computed), DSIM needs N + N(N-1) NEW ab initio/MLIP
/// calculations of its own — so it follows the Elastic/Piezoelectric
/// template instead: one wizard, one generated script that loops over all
/// calculation classes internally (core::generateDsimScript()), one
/// result file. There is deliberately no incoming-link/port fan-out on the
/// Orchestration canvas — this wizard IS the DSIM node's own configure-time
/// editor there too, exactly as an ordinary Simulation-family node's wizard
/// is (see docs/sphinx/source/simulations/dsim.md's "Orchestration" note).
///
/// Each of the N + N(N-1) supercells is built once, synchronously, in C++
/// — from its own input structure via the existing supercell builder
/// (pybridge::AseBridge::makeSupercell) plus a plain per-atom relabel for
/// each impurity (a single substitution needs no more machinery than
/// setting one atom's atomic number) — and baked into the generated
/// script as literal geometry, so DSIM needs no staged trajectory file.
class DsimWizard : public SimulationWizardBase {
    Q_OBJECT

public:
    /// (display name, structure) pairs — the open-document snapshot a
    /// structure can be added FROM. Identical shape to
    /// OrchestrationWindow::MaterialList (both are plain aliases for the
    /// same QList<QPair<...>>, interchangeable without conversion) so the
    /// same list MainWindow already builds for the Orchestration panel
    /// feeds this wizard too, whether it is opened from the menu or as a
    /// canvas node's configure-time editor.
    using MaterialList = QList<QPair<QString, std::shared_ptr<const core::Structure>>>;

    explicit DsimWizard(MaterialList openDocuments, QWidget* parent = nullptr);

    /// Appends structures directly, bypassing the "Add from Open
    /// Document…"/"Import from File…" modal dialogs — the same role
    /// setDensityBaselines() plays for other wizards. Used to restore a
    /// previously-configured list (an Orchestration DSIM node reopened for
    /// editing) and to drive the picker from a headless test.
    void addStructures(const MaterialList& structures);

    /// The current valid (single-species, non-duplicate) input structures,
    /// in list order — what an Orchestration DSIM node persists as its own
    /// configuration alongside the generated script.
    MaterialList validStructures() const;

protected:
    QString wizardTitle() const override;
    QString settingsHeader() const override;
    QWidget* buildSettingsPage() override;
    /// Its own stage (Stage 3, after Calculator Settings) rather than a
    /// group folded into Stage 1: the paper's protocol relaxes every
    /// supercell (ions AND cell, force criterion < 0.02 eV/Å), and the
    /// force convergence / optimizer / max-steps / cell-relaxation-mode
    /// choices that govern HOW deserve the same visible, configurable
    /// standing every other relaxation-capable wizard gives them —
    /// reusing ForceConvergenceControl/CellRelaxationControls, the SAME
    /// shared controls Geometry Optimization and Cluster Expansion's own
    /// batch relax use, rather than silently hardcoding the paper's
    /// defaults where nothing on screen said so.
    QString secondSettingsHeader() const override;
    QWidget* buildSecondSettingsPage() override;
    QString generateScript() const override;
    QString exportFileName() const override { return QStringLiteral("dsim.py"); }
    QStringList calculatorElements() const override;

    /// Builds every pristine/impurity supercell (once) on the way out of
    /// Stage 1, the same "build the ensemble on goNext()" hook the base
    /// class's own doc comment names as the precedent (Random Noise
    /// wizard). Must call the base implementation.
    void goNext() override;

private Q_SLOTS:
    void addFromOpenDocument();
    void addFromFile();
    void removeSelected();
    void updateSummary();
    /// Shows/hides the two phase-label fields and enables/disables the
    /// checkbox itself — multi-phase mode is only meaningful for exactly
    /// 2 valid components (see the checkbox's own tooltip / class doc
    /// comment's "Multi-phase alloys" paragraph).
    void updateMultiPhaseVisibility();

private:
    /// One candidate input structure: its display name, the structure
    /// itself, and — filled in by validateEntries() — the single element
    /// symbol it names, or an empty string when it is not single-species
    /// (shown in the list as a refusal, not silently dropped, so the user
    /// sees exactly which entry needs fixing).
    struct Entry {
        QString label;
        std::shared_ptr<const core::Structure> structure;
        QString species;
    };

    void validateEntries();
    void refillList();
    /// The valid (single-species) entries only, in list order — what
    /// goNext()/config() actually build from. Empty when fewer than two
    /// entries validate, or any two share a species.
    std::vector<Entry> validEntries() const;
    core::DsimConfig config() const;
    /// Stage 3's controls, read into a bare CalculatorConfig — the part
    /// config() and the multi-phase branch of generateScript() both need,
    /// factored out so multi-phase does not hand-copy it.
    core::CalculatorConfig builtCalculatorConfig() const;

    MaterialList openDocuments_;
    std::vector<Entry> entries_;

    QListWidget* listWidget_ = nullptr;
    QPushButton* addFileButton_ = nullptr;
    QPushButton* addDocumentButton_ = nullptr;
    QPushButton* removeButton_ = nullptr;
    QSpinBox* nxSpin_ = nullptr;
    QSpinBox* nySpin_ = nullptr;
    QSpinBox* nzSpin_ = nullptr;
    QLabel* summaryLabel_ = nullptr;

    // -- Multi-phase alloys (Fe(bcc)-Co(hcp) and similar) --------------------
    // A 2-component-only mode: each of the two input structures keeps its
    // OWN crystal structure (already true for every DSIM input — see the
    // class doc comment), and this mode ALSO builds the other element
    // relabeled onto each structure's template, producing two independent
    // binary DSIM branches (one per phase) whose curves are then shifted
    // onto a common energy reference — core::solveDsimMultiPhase's own doc
    // comment / core/Dsim.hpp's "Multi-phase alloys" note. Enabled only
    // when validEntries().size() == 2 (updateMultiPhaseVisibility()).
    QCheckBox* multiPhaseCheck_ = nullptr;
    QLineEdit* phaseALabelEdit_ = nullptr; ///< label for validEntries()[0]'s own structure (e.g. "bcc")
    QLineEdit* phaseBLabelEdit_ = nullptr; ///< label for validEntries()[1]'s own structure (e.g. "hcp")

    /// Built once, in goNext(), the first time Stage 1 is left forward.
    bool structuresBuilt_ = false;
    std::vector<std::string> builtSpecies_;
    std::vector<core::Structure> builtPristine_;
    std::vector<std::vector<core::Structure>> builtImpurity_;

    /// True when goNext() built the multi-phase (8-supercell) case instead
    /// of the ordinary N-component one above — generateScript() dispatches
    /// on this rather than on N, since N==2 is also the ordinary binary
    /// case's own size.
    bool multiPhaseMode_ = false;
    std::string builtSpeciesA_;
    std::string builtSpeciesB_;
    core::DsimPhaseBranchConfig builtPhaseA_;
    core::DsimPhaseBranchConfig builtPhaseB_;

    // -- Stage 3: Geometry Optimization Settings -----------------------------
    QComboBox* optimizerCombo_ = nullptr;
    ForceConvergenceControl fmax_;
    QSpinBox* maxStepsSpin_ = nullptr;
    CellRelaxationControls cell_{this};
};

} // namespace calango::gui
