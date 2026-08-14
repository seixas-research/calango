#pragma once

// Required on Linux: libstdc++ (GCC 13+) no longer pulls <cstdint> in
// transitively. Do not remove even if clangd reports it unused on macOS.
#include <cstdint>

#include "core/CrpaSolver.hpp"

#include <QDialog>
#include <QString>

class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QPlainTextEdit;
class QPushButton;
class QSpinBox;
class QTableWidget;

namespace calango::gui {

/// Constrained RPA setup: define the correlated subspace, screen everything
/// outside it, and read off U and J.
///
/// Reached from Wannier Functions because that is where its input comes from:
/// a completed Wannier run's `_hr.dat` and the centres and spreads from its
/// `.wout`. Nothing else is required, and nothing else is read — the solver
/// underneath is calculator-agnostic by construction, so a Wannier basis from
/// any code drives it identically.
class CrpaDialog : public QDialog {
    Q_OBJECT

public:
    explicit CrpaDialog(QWidget* parent = nullptr);

    /// Load a wannier90 `_hr.dat` and populate the orbital table from it.
    bool loadHamiltonian(const QString& path, QString* error);

private Q_SLOTS:
    void browseHamiltonian();
    void loadDemoModel();
    void compute();

private:
    bool buildModel(core::CrpaSolver::Model& model, QString* error) const;
    void rebuildTable(std::size_t orbitals);

    QLabel* sourceLabel_ = nullptr;
    QTableWidget* orbitalTable_ = nullptr;
    QSpinBox* kmesh_[3] = {nullptr, nullptr, nullptr};
    QDoubleSpinBox* screeningCutoff_ = nullptr;
    QDoubleSpinBox* broadening_ = nullptr;
    QDoubleSpinBox* electrons_ = nullptr;
    QComboBox* shellL_ = nullptr;
    QPushButton* computeButton_ = nullptr;
    QPlainTextEdit* report_ = nullptr;

    /// H(R) as read from disk (or from the built-in demo), kept apart from the
    /// table because the table only carries the per-orbital metadata.
    std::vector<core::CrpaSolver::HoppingBlock> hoppings_;
    std::array<std::array<double, 3>, 3> cell_{
        {{4.0, 0.0, 0.0}, {0.0, 4.0, 0.0}, {0.0, 0.0, 4.0}}};
};

} // namespace calango::gui
