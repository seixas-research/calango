#pragma once

#include <QDialog>
#include <QJsonObject>

class QComboBox;
class QLabel;
class QPushButton;
class QTableWidget;

namespace calango::gui {

/// "Define calculator settings…": a direct editor for
/// ~/.calango/calculator_parameters.json — the per-engine, per-element
/// suggested defaults (plane-wave cutoff, k-point mesh) the simulation
/// wizards open with.
///
/// Offered from the convergence result windows because that is where the
/// numbers come from: the user has just read a converged cutoff off the
/// curve, and this is the shortest path from that reading to every future
/// wizard opening on it.
///
/// One row per element plus a pinned "(default)" row for the engine-wide
/// fallback. Empty cells mean "unset — fall through": to the default row for
/// elements, to the built-in values for the default row. The file's other
/// engines and its _comment keys are preserved verbatim on save.
class CalculatorParametersDialog : public QDialog {
    Q_OBJECT

public:
    explicit CalculatorParametersDialog(QWidget* parent = nullptr);

private Q_SLOTS:
    void addElementRow();
    void removeSelectedRows();
    void save();

private:
    void loadFile();
    /// Rebuild the table from root_'s entry for the engine now selected.
    void populateTable();
    /// Fold the table back into root_'s entry for `engineKey` (the engine
    /// the table was SHOWING, which on an engine switch is not the combo's
    /// current one).
    void commitTable(const QString& engineKey);
    QString selectedEngineKey() const;

    QComboBox* engineCombo_ = nullptr;
    QTableWidget* table_ = nullptr;
    QLabel* statusLabel_ = nullptr;
    QPushButton* removeButton_ = nullptr;

    /// The whole file, edits included; written back on Save.
    QJsonObject root_;
    /// The engine the table currently displays (combo text lags during a
    /// switch, see commitTable()).
    QString shownEngineKey_;
};

} // namespace calango::gui
