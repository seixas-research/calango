#pragma once

#include "core/CalculatorConfig.hpp"
#include "core/Structure.hpp"

#include <QDialog>

#include <memory>
#include <vector>

class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QLineEdit;
class QSpinBox;

namespace calango::gui {

/// Simulation → "Nudged Elastic Band (NEB)": an interactive, non-modal path
/// builder. The user picks reactant/product endpoints (open tabs or files),
/// interpolates an initial band (linear or IDPP) previewed as a scrubbable
/// trajectory in the main viewport, configures the solver (Standard / CI-NEB /
/// AutoNEB, spring constant, optimizer, convergence), and launches the
/// relaxation as a streaming job whose energy-barrier profile lands in the
/// Job panel's Energy plot.
class NebDialog : public QDialog {
    Q_OBJECT

public:
    struct NamedStructure {
        QString name;
        std::shared_ptr<const core::Structure> structure;
    };

    explicit NebDialog(std::vector<NamedStructure> openDocs,
                       QWidget* parent = nullptr);

    /// The most recently interpolated band (endpoints + intermediates).
    const std::vector<std::shared_ptr<core::Structure>>& band() const
    {
        return band_;
    }

    /// The generated NEB relaxation script and its interpreter (valid after a
    /// runRequested() emission).
    QString script() const { return script_; }
    QString pythonExecutable() const;

Q_SIGNALS:
    /// Emitted when the user previews an interpolation; the host loads `band`
    /// as a scrubbable multi-frame document.
    void previewRequested(const std::vector<std::shared_ptr<core::Structure>>& band);
    /// Emitted when the user launches the run; the host stages band() and runs
    /// script().
    void runRequested();

private Q_SLOTS:
    void doPreview();
    void doRun();
    void browseInitial();
    void browseFinal();
    void browseEnvironment();
    void updateCalculatorEnabled();

private:
    std::shared_ptr<const core::Structure> endpoint(QComboBox* combo) const;
    bool computeBand();
    core::CalculatorConfig calculatorConfig() const;
    QString buildNebScript() const;
    void repopulateEndpointCombos(int initialSel, int finalSel);

    std::vector<NamedStructure> openDocs_;
    std::vector<std::shared_ptr<core::Structure>> band_;
    QString script_;

    QComboBox* initialCombo_;
    QComboBox* finalCombo_;
    QSpinBox* imagesSpin_;
    QComboBox* methodCombo_;

    QComboBox* variantCombo_;
    QDoubleSpinBox* springSpin_;
    QDoubleSpinBox* fmaxSpin_;
    QSpinBox* maxStepsSpin_;
    QComboBox* optimizerCombo_;

    QComboBox* calcCombo_;
    QComboBox* maceSizeCombo_;
    QComboBox* maceDeviceCombo_;
    /// Shared ML-potential controls: one model/checkpoint path (enabled only
    /// for the file-backed engines — CHGNet and MatterSim carry their own
    /// weights) and the device they run on.
    QLineEdit* mlipModelEdit_ = nullptr;
    QComboBox* mlipDeviceCombo_ = nullptr;
    QDoubleSpinBox* cutoffSpin_;
    QSpinBox* kptSpins_[3];

    QLineEdit* envEdit_;
    QLabel* envStatus_;
    QLabel* statusLabel_;
};

} // namespace calango::gui
