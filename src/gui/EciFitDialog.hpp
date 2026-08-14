#pragma once

// Required on Linux: libstdc++ (GCC 13+) no longer pulls <cstdint> in
// transitively. Do not remove even if clangd reports it unused on macOS.
#include <cstdint>

#include "core/ClusterExpansionFit.hpp"

#include <QDialog>
#include <QString>
#include <functional>
#include <vector>

class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QPlainTextEdit;
class QPushButton;
class QSpinBox;
class QTableWidget;

namespace calango::gui {

/// Fit Effective Cluster Interactions to a completed cluster-expansion run.
///
/// This is the missing middle of the alloy pipeline. The builder enumerates
/// configurations, the batch run computes their energies, and the CVM solver
/// wants interactions — but until this dialog existed the fit lived only in
/// `core` with no way to reach it, so a user could produce a design matrix and
/// a column of energies and still had no way to turn them into a J.
///
/// Reads `cluster_expansion.json`: the `correlation` row of each configuration
/// is the design matrix, `energy_per_atom` the right-hand side. A file written
/// before correlations were emitted has no matrix, and this REFUSES rather
/// than fitting against whatever else it can find — a cluster expansion fitted
/// to the wrong regressors still reproduces its training energies.
class EciFitDialog : public QDialog {
    Q_OBJECT

public:
    explicit EciFitDialog(QWidget* parent = nullptr);

    /// Load a run directory (containing cluster_expansion.json).
    bool loadDirectory(const QString& directory, QString* error);

    /// A finished run this dialog can load without a file dialog.
    struct ProcessSource {
        QString label;
        QString directory;
    };
    /// Supplies the finished runs offered in the "From process" row.
    ///
    /// A callback rather than a list, and rather than a ProcessManagerPanel
    /// pointer: the dialog stays ignorant of where runs come from (so it is
    /// constructible in a test with no panel at all), and the list is pulled
    /// fresh every time the dialog is shown, so a cluster expansion that
    /// finishes while this is open appears on the next open rather than being
    /// frozen at construction.
    void setProcessSourceProvider(
        std::function<std::vector<ProcessSource>()> provider);

    /// The fitted result, valid once `fit()` has succeeded.
    const core::EciFitResult& result() const { return result_; }
    /// Nearest-neighbour pair ECI, the number the CVM solver consumes.
    /// Zero when the fit kept no pair term.
    double nearestNeighbourPairEci() const;

Q_SIGNALS:
    /// Emitted by "Send to CVM…" with the nearest-neighbour pair ECI in eV.
    void sendToCvmRequested(double pairEci);

protected:
    /// Repopulates the process list, so it reflects what has finished by the
    /// time the dialog is actually put on screen.
    void showEvent(QShowEvent* event) override;

private Q_SLOTS:
    void browse();
    void fit();
    void sendToCvm();
    /// Load the run selected in the process combo, skipping the file dialog.
    void loadSelectedProcess();

private:
    void refreshTable();
    void refreshProcessSources();

    QComboBox* processCombo_ = nullptr;
    QPushButton* processLoadButton_ = nullptr;
    std::function<std::vector<ProcessSource>()> processProvider_;

    QComboBox* methodCombo_ = nullptr;
    QSpinBox* foldsSpin_ = nullptr;
    QLabel* sourceLabel_ = nullptr;
    QLabel* diagnosticLabel_ = nullptr;
    QTableWidget* table_ = nullptr;
    QPlainTextEdit* report_ = nullptr;
    QPushButton* fitButton_ = nullptr;
    QPushButton* cvmButton_ = nullptr;

    QString directory_;
    std::vector<std::vector<double>> correlations_;
    std::vector<double> energies_;
    std::vector<core::EciColumn> columns_;
    core::EciFitResult result_;
};

} // namespace calango::gui
