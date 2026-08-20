#pragma once

#include "core/CalculatorConfig.hpp"
#include "core/DsimScriptGenerator.hpp"
#include "core/Structure.hpp"
#include "gui/SimulationWizardBase.hpp"

#include <QList>
#include <QPair>
#include <QString>

#include <memory>

class QLabel;
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
/// — plus the supercell repeat count. Stages 2-3 are the shared Calculator
/// Settings and ASE Script Review, exactly as in ClusterExpansionWizard.
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
    QString generateScript() const override;
    QString exportFileName() const override { return QStringLiteral("dsim.py"); }
    QStringList calculatorElements() const override;
    /// Any ASE calculator works (energies + forces + a cell filter needs
    /// stress) — not restricted to one engine, same stance as
    /// ElasticConfig.
    bool calculatorAllowed(core::CalculatorKind /*kind*/) const override { return true; }

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

    /// Built once, in goNext(), the first time Stage 1 is left forward.
    bool structuresBuilt_ = false;
    std::vector<std::string> builtSpecies_;
    std::vector<core::Structure> builtPristine_;
    std::vector<std::vector<core::Structure>> builtImpurity_;
};

} // namespace calango::gui
