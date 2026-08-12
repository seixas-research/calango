#pragma once

#include "core/TdbDatabase.hpp"

#include <QDialog>
#include <QStringList>

#include <vector>

class QCheckBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QVBoxLayout;

namespace calango::gui {

/// Modules → "CALPHAD…": load a thermodynamic database and choose the system.
///
/// The two questions a CALPHAD calculation starts from are which ELEMENTS the
/// system contains and which PHASES are allowed to compete in it, and both
/// come out of the database rather than out of a fixed list — so the controls
/// are built from the file after it loads.
///
/// Independent of pycalphad, deliberately. Reading a `.tdb` to find its
/// elements and phases is text parsing (see core::TdbDatabase), and a module
/// that could not populate its own checkboxes without a solver installed
/// would be unusable on most machines — pycalphad is in no Calango
/// environment by default. The solver is needed to COMPUTE, and that runs
/// through a generated script that reports its own absence, like every engine
/// in the project.
///
/// Phase availability follows the element selection live. A phase whose
/// sublattice cannot be filled from the chosen elements is disabled and says
/// why, rather than being silently dropped or offered and then failing inside
/// the solver.
class CalphadDialog : public QDialog {
    Q_OBJECT

public:
    explicit CalphadDialog(QWidget* parent = nullptr);

    /// Load a database from `path`. Returns false and reports in the status
    /// line when the file is not a usable `.tdb`. Public so a test can drive
    /// it without a file dialog.
    bool loadDatabase(const QString& path);
    /// Parse a database from text, for the same reason.
    bool loadDatabaseText(const QString& text, const QString& label);

    /// Elements the user has ticked.
    QStringList selectedElements() const;
    /// Phases the user has ticked — the ones allowed to compete. A phase the
    /// current element selection cannot support is never included, even if it
    /// was ticked before the selection narrowed.
    QStringList selectedPhases() const;

    const core::TdbDatabase& database() const { return database_; }

private Q_SLOTS:
    void browseForDatabase();
    /// Re-derive which phases the element selection can support, and update
    /// the summary.
    void refreshAvailability();
    /// Compute and show the T–x diagram (two elements) or isothermal section
    /// (three) for the current selection. Entirely in C++: equilibrium in a
    /// binary is a convex hull over the phases' Gibbs curves, and a hull needs
    /// no solver.
    void openPhaseDiagram();
    /// Build a database from first-principles energies instead of loading one.
    void openGenerator();

private:
    void rebuildSelectors();
    void setStatus(const QString& text, bool ok);

    core::TdbDatabase database_;
    bool loaded_ = false;

    QLineEdit* pathEdit_ = nullptr;
    QLabel* statusLabel_ = nullptr;
    QLabel* summaryLabel_ = nullptr;
    QPushButton* warningsButton_ = nullptr;
    QPushButton* diagramButton_ = nullptr;
    QPushButton* generateButton_ = nullptr;

    QVBoxLayout* elementLayout_ = nullptr;
    QVBoxLayout* phaseLayout_ = nullptr;
    std::vector<QCheckBox*> elementBoxes_;
    std::vector<QCheckBox*> phaseBoxes_;
};

} // namespace calango::gui
