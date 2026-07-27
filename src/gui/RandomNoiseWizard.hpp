#pragma once

#include "core/Noise.hpp"
#include "core/RandomNoiseScriptGenerator.hpp"
#include "core/Structure.hpp"
#include "gui/GpawElectronicRows.hpp"
#include "gui/SimulationWizardBase.hpp"

#include <memory>
#include <vector>

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QFormLayout;
class QLabel;
class QPushButton;
class QSpinBox;

namespace calango::gui {

/// Simulation → "Random Noise Setup…": generate a randomly-perturbed ensemble
/// and run a single point on every member of it.
///
/// This replaces the old Build → "Structure Perturbation / Noise…" dialog,
/// which stopped at generation. That was the wrong place to stop: nobody
/// displaces a structure for its own sake. The perturbation exists to sample
/// the potential-energy surface around a geometry — to check that a relaxed
/// structure is really in a well, to measure how steep that well is, or to
/// build training data for a machine-learned potential — and every one of
/// those needs energies, which meant leaving the dialog, finding the new tab
/// and starting a Single-point wizard by hand. Making it a wizard closes that
/// loop, and moves it to the Simulation menu where the thing it produces (a
/// job) actually lives.
///
/// Four stages:
///   1. Random Noise            — the displacement, with Generate structures
///                                and Run simulation
///   2. Calculator & Convergence Settings — the standard engine page
///   3. Single-point Adjustments — what each evaluation records
///   4. ASE Script Review
class RandomNoiseWizard : public SimulationWizardBase {
    Q_OBJECT

public:
    /// `reference` is the structure to perturb; the ensemble is built from it
    /// and never modifies it.
    explicit RandomNoiseWizard(std::shared_ptr<const core::Structure> reference,
                               QWidget* parent = nullptr);

    /// The generated ensemble — frame 0 is the unperturbed reference, so the
    /// energy distribution has something to be a distribution AROUND. Empty
    /// until "Generate structures" has been pressed.
    const std::vector<std::shared_ptr<core::Structure>>& frames() const
    {
        return frames_;
    }

Q_SIGNALS:
    /// A fresh ensemble was generated. The host previews it as a scrubbable
    /// trajectory and stages it for the job.
    void structuresGenerated(
        const std::vector<std::shared_ptr<core::Structure>>& frames);

protected:
    QString wizardTitle() const override;
    QString settingsHeader() const override { return tr("Random Noise"); }
    QWidget* buildSettingsPage() override;
    QString calculatorSettingsHeader() const override
    {
        return tr("Calculator & Convergence Settings");
    }
    /// The optional third stage. Non-empty enables it, giving the four-stage
    /// flow described above.
    QString secondSettingsHeader() const override
    {
        return tr("Single-point Adjustments");
    }
    QWidget* buildSecondSettingsPage() override;

    // The same GPAW form every other simulation wizard presents — an SCF is an
    // SCF whether it runs once or a hundred times.
    void buildConvergenceRows(QFormLayout* form) override
    {
        electronic_.buildConvergenceRows(form, this);
    }
    void buildSpinRows(QFormLayout* form) override
    {
        electronic_.buildSpinRows(form, this);
    }
    QWidget* gpawEnergyToleranceWidget() override
    {
        return electronic_.energyToleranceWidget();
    }
    QWidget* gpawScfStepsWidget() override
    {
        return electronic_.scfStepsWidget();
    }
    bool hasConvergenceExtras() const override { return true; }
    bool hasSpinExtras() const override { return true; }
    QStringList calculatorElements() const override;

    QString generateScript() const override;
    QString exportFileName() const override
    {
        return QStringLiteral("random_noise.py");
    }

    /// Generate the ensemble first if the user advanced with Next instead of
    /// pressing the button. Without this the job would stage no configs.extxyz
    /// and the script would die on its first read() — a failure that belongs
    /// to the wizard, not to the run.
    void goNext() override;

private Q_SLOTS:
    /// Build the ensemble from the current settings and publish it.
    void generateStructures();

private:
    core::NoiseOptions noiseOptions() const;
    core::RandomNoiseRunConfig runConfig() const;
    /// Enable "Run simulation" only once there is something to run on, and
    /// keep the status line honest about what has been generated.
    void updateGenerationState();

    std::shared_ptr<const core::Structure> reference_;
    std::vector<std::shared_ptr<core::Structure>> frames_;

    QComboBox* distributionCombo_ = nullptr;
    QDoubleSpinBox* amplitudeSpin_ = nullptr;
    QSpinBox* seedSpin_ = nullptr;
    QCheckBox* positionsCheck_ = nullptr;
    QCheckBox* cellCheck_ = nullptr;
    QSpinBox* countSpin_ = nullptr;
    QComboBox* accumulationCombo_ = nullptr;
    QPushButton* generateButton_ = nullptr;
    QPushButton* runButton_ = nullptr;
    QLabel* generationStatus_ = nullptr;

    QCheckBox* forcesCheck_ = nullptr;
    QCheckBox* stressCheck_ = nullptr;
    QCheckBox* continueCheck_ = nullptr;

    GpawElectronicRows electronic_;
};

} // namespace calango::gui
