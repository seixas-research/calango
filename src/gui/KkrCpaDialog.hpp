#pragma once

// Required on Linux: libstdc++ (GCC 13+) no longer pulls <cstdint> in
// transitively. Do not remove even if clangd reports it unused on macOS.
#include <cstdint>

#include "core/CpaSolver.hpp"

#include <QDialog>

class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QPushButton;
class QTableWidget;

namespace calango::gui {

class SpectrumPlotWidget;

/// KKR-CPA for substitutionally disordered alloys.
///
/// Drives core::CpaSolver: a component table (species, concentration, on-site
/// level, exchange splitting) and a model band, solved to the CPA condition
/// and reported as the three things the method is used for — the total and
/// component-projected density of states, the per-species magnetic moments,
/// and the Bloch spectral function that replaces a band structure once the
/// lattice is random.
///
/// The band arrives as a model DOS rather than from a multiple-scattering
/// solve; see core::CpaSolver for exactly what that does and does not cover.
/// The dialog says so on its face rather than leaving it to be discovered from
/// a number that looks plausible.
class KkrCpaDialog : public QDialog {
    Q_OBJECT

public:
    explicit KkrCpaDialog(QWidget* parent = nullptr);

private Q_SLOTS:
    void addComponent();
    void removeComponent();
    void solve();

private:
    /// Read the table into solver components, or report why it cannot be.
    bool readComponents(std::vector<core::CpaSolver::Component>& out,
                        QString* error) const;
    void refreshEnabled();

    QTableWidget* table_ = nullptr;
    QComboBox* bandShape_ = nullptr;
    QDoubleSpinBox* bandwidth_ = nullptr;
    QDoubleSpinBox* broadening_ = nullptr;
    QDoubleSpinBox* electrons_ = nullptr;
    QPushButton* solveButton_ = nullptr;
    QLabel* summary_ = nullptr;
    SpectrumPlotWidget* dosPlot_ = nullptr;
    SpectrumPlotWidget* bsfPlot_ = nullptr;
};

} // namespace calango::gui
