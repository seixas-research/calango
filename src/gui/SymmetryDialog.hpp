#pragma once

#include "core/Structure.hpp"

#include <QDialog>

#include <memory>

class QDoubleSpinBox;
class QGroupBox;
class QLabel;
class QPushButton;
class QTableWidget;

namespace calango::gui {

/// Analysis → "Symmetry…": the crystallographic report for the current
/// structure, and everything that follows from the point group it detects.
///
/// Space group (symbol + number), point group, Hall number and crystal system,
/// then three tables over that same detection: the symmetry-inequivalent sites
/// with their Wyckoff letters, the character table of the point group
/// (generated numerically from the group's class-sum algebra, not looked up),
/// and the Γ-point factor-group classification of the vibrational modes —
/// which are Raman-active, IR-active or silent.
///
/// The mode activity used to be a dialog of its own ("Raman Modes"). It was
/// never independent: it is a function of the detected point group and nothing
/// else, it ran the SAME pybridge::RamanAnalysis call this dialog already made
/// for its character table, and — having no tolerance control of its own — it
/// silently answered for a different point group than this dialog showed
/// whenever the tolerance had been moved. One dialog, one detection, one
/// answer.
///
/// Inspection only — the cell-transform actions ("Standardize Cell" / "Reduce
/// to Primitive Cell") live in the Edit Structure dialog.
class SymmetryDialog : public QDialog {
    Q_OBJECT

public:
    explicit SymmetryDialog(std::shared_ptr<const core::Structure> structure,
                            QWidget* parent = nullptr);

private Q_SLOTS:
    void detect();

private:
    /// Rebuild the character table and the Raman/IR activity table from ONE
    /// point-group analysis at the current tolerance (or clear both, saying
    /// why). Both are properties of the same detected group, so analysing
    /// twice would only be two chances to disagree.
    void updateModeAnalysis();

    std::shared_ptr<const core::Structure> structure_;

    QDoubleSpinBox* tolSpin_;
    QLabel* spaceGroupLabel_;
    QLabel* pointGroupLabel_;
    QLabel* crystalLabel_;
    QLabel* hallLabel_;
    QLabel* sitesLabel_;
    QLabel* statusLabel_;
    QTableWidget* table_;
    QGroupBox* characterGroup_;
    QTableWidget* characterTable_;
    /// Γ-point mode activity: the tally, and one row per irrep of the
    /// detected point group.
    QLabel* activitySummary_;
    QTableWidget* activityTable_;
};

} // namespace calango::gui
