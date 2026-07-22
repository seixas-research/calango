#pragma once

#include "core/CalculatorConfig.hpp"
#include "core/MonteCarlo.hpp"
#include "core/Structure.hpp"

#include <QDialog>

#include <memory>
#include <optional>

class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QSpinBox;
class QStackedWidget;

namespace calango::gui {

/// Simulation → "Monte Carlo Simulation": two methods behind one dialog.
///   • Swap-atoms — a native C++ Metropolis lattice sampler for alloy
///     ordering / segregation (no calculator, runs in-process). Accepting in
///     this mode runs it and exposes the trajectory + energetics via
///     swapResult().
///   • Basin Hopping — global optimization for clusters/nanoparticles, emitted
///     as an editable ASE script (random displacement + local optimization +
///     Metropolis) that runs out-of-process; exposes script()/pythonExecutable().
class MonteCarloDialog : public QDialog {
    Q_OBJECT

public:
    enum class Method { SwapAtoms, BasinHopping };

    explicit MonteCarloDialog(std::shared_ptr<const core::Structure> structure,
                              QWidget* parent = nullptr);

    Method method() const;

    /// Valid after accept() in SwapAtoms mode.
    const std::optional<core::SwapMonteCarloResult>& swapResult() const
    {
        return swapResult_;
    }

    /// The generated Basin Hopping script and interpreter (BasinHopping mode).
    QString script() const;
    QString pythonExecutable() const;

private Q_SLOTS:
    void onRun();
    void refreshPreview();
    void updateCalculatorEnabled();
    void browseEnvironment();

private:
    core::CalculatorConfig basinCalculatorConfig() const;
    QString buildBasinHoppingScript() const;

    std::shared_ptr<const core::Structure> structure_;
    std::optional<core::SwapMonteCarloResult> swapResult_;

    QComboBox* methodCombo_;
    QStackedWidget* stack_;

    // --- Swap-atoms page ---------------------------------------------------
    QDoubleSpinBox* swapTempSpin_;
    QDoubleSpinBox* interactionSpin_;
    QDoubleSpinBox* swapCutoffSpin_;
    QSpinBox* swapStepsSpin_;
    QSpinBox* snapshotSpin_;
    QSpinBox* swapSeedSpin_;

    // --- Basin Hopping page ------------------------------------------------
    QComboBox* calcCombo_;
    QComboBox* maceSizeCombo_;
    QComboBox* maceDeviceCombo_;
    QDoubleSpinBox* bhCutoffSpin_;
    QSpinBox* kptSpins_[3];
    QComboBox* optimizerCombo_;
    QDoubleSpinBox* bhTempSpin_;
    QDoubleSpinBox* displacementSpin_;
    QSpinBox* bhStepsSpin_;
    QDoubleSpinBox* fmaxSpin_;
    QSpinBox* maxOptStepsSpin_;
    QSpinBox* bhSeedSpin_;
    QLineEdit* envEdit_;
    QLabel* envStatus_;
    QPlainTextEdit* preview_;

    QLabel* statusLabel_;
    bool manuallyEdited_ = false;
    bool updatingPreview_ = false;
};

} // namespace calango::gui
