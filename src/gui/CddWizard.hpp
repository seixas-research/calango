#pragma once

#include "core/CddScriptGenerator.hpp"
#include "core/Structure.hpp"
#include "gui/SimulationWizardBase.hpp"

#include <QList>
#include <QString>

#include <memory>
#include <optional>

class QComboBox;
class QLabel;
class QListWidget;
class QPushButton;
class QRadioButton;

namespace calango::gui {

/// Analysis → "Charge Density Difference (CDD)…".
///
///     Δρ = ρ(A+B) − ρ(A) − ρ(B)
///
/// Where the charge actually went when two fragments were brought together.
/// Each density on its own is dominated by the atomic cores and shows nothing
/// interpretable; only the difference does, and only if all three come from
/// the same calculator on the same grid.
///
/// Two stages, then the script:
///   1. "Density Source" — which completed Single-point supplies ρ(A+B), and
///      whether to difference the all-electron density or the pseudodensity.
///   2. "Subsystem Partition" — split the atoms into A and B by moving them
///      between two columns.
///   3. "ASE Script Review".
///
/// There is no Calculator Settings stage (showsCalculatorStage() == false).
/// Asking the user to re-specify the engine would be asking them to introduce
/// exactly the inconsistency that makes a CDD meaningless: the fragments are
/// rebuilt from the parent's own `.gpw` at run time, so cutoff, XC, grid,
/// k-points and convergence cannot drift between the three terms. The
/// interpreter is inherited from the baseline for the same reason.
class CddWizard : public SimulationWizardBase {
    Q_OBJECT

public:
    /// One selectable source of rho(A+B).
    ///
    /// The geometry travels WITH the baseline rather than being read from the
    /// current document: the document may have been edited, or be a different
    /// system entirely, since the run — and the atom indices the generated
    /// script emits have to be the ones inside that run's .gpw.
    struct Baseline {
        QString label;
        QString directory;
        std::shared_ptr<const core::Structure> structure;
    };

    explicit CddWizard(QWidget* parent = nullptr);

    /// Completed single-points that saved wavefunctions (`.gpw`). Call after
    /// construction, before exec().
    void setDensityBaselines(QList<Baseline> baselines);

    /// Interpreter the run binds to: the baseline's own environment when its
    /// calculator.json records one, else the standard per-engine resolution.
    QString pythonExecutable() const override;

    /// Directory of the baseline the run reads ρ(A+B) from. Empty when none is
    /// selected.
    QString baselineDirectory() const;

protected:
    QString wizardTitle() const override;
    QString settingsHeader() const override { return tr("Density Source"); }
    QWidget* buildSettingsPage() override;
    QString secondSettingsHeader() const override
    {
        return tr("Subsystem Partition");
    }
    QWidget* buildSecondSettingsPage() override;
    bool showsCalculatorStage() const override { return false; }
    bool calculatorAllowed(core::CalculatorKind kind) const override
    {
        // GPAW only: the fragment densities are produced by restarting the
        // parent's .gpw, which no other backend here writes.
        return kind == core::CalculatorKind::Gpaw;
    }
    QString generateScript() const override;
    QString exportFileName() const override { return QStringLiteral("cdd.py"); }
    /// Fill the partition columns on leaving stage 1 — the atom list is not
    /// known until a baseline has been chosen.
    void goNext() override;

private Q_SLOTS:
    /// Re-read the selected baseline's structure and calculator provenance.
    void onBaselineChanged();
    /// Move the selected atoms between the two columns.
    void moveSelection(QListWidget* from, QListWidget* to);

private:
    core::CddRunConfig runConfig() const;
    /// Rebuild both columns from `structure_`, putting everything in A. Called
    /// whenever the baseline changes, because indices from the previous
    /// structure mean nothing in the new one.
    void resetPartition();
    /// Enable the move buttons that have something to move, and say what the
    /// split currently is.
    void updatePartitionState();
    /// One list row for atom `index`.
    QString atomLabel(int index) const;

    QList<Baseline> baselines_;
    /// The selected baseline's geometry; null when nothing is selected.
    std::shared_ptr<const core::Structure> structure_;
    std::optional<InheritedCalculator> inherited_;

    // Stage 1
    QComboBox* baselineCombo_ = nullptr;
    QLabel* inheritedLabel_ = nullptr;
    QRadioButton* pseudoRadio_ = nullptr;
    QRadioButton* allElectronRadio_ = nullptr;

    // Stage 2
    QListWidget* listA_ = nullptr;
    QListWidget* listB_ = nullptr;
    QPushButton* toBButton_ = nullptr;
    QPushButton* toAButton_ = nullptr;
    QLabel* partitionStatus_ = nullptr;
};

} // namespace calango::gui
