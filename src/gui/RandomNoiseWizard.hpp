#pragma once

#include "core/Noise.hpp"
#include "core/Structure.hpp"

#include <QDialog>

#include <memory>
#include <vector>

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QPushButton;
class QSpinBox;

namespace calango::gui {

/// Simulation → "Random Noise Setup…": generate a randomly-perturbed
/// trajectory from a reference structure.
///
/// A pure generator, native throughout — like BerryPhaseDialog, there is no
/// script and no job, because there is nothing to run: the perturbation is
/// evaluated in process (core::applyRandomNoise) and the result is a
/// trajectory of frame 0 (the untouched reference) followed by `count`
/// displaced copies, previewed live as a scrubbable tab the moment
/// "Generate structures" is pressed.
///
/// Evaluating the ensemble's energies (or forces, or anything else a
/// calculator produces) is deliberately NOT this dialog's job any more: save
/// the generated trajectory (File → Save Trajectory As…) and load it into an
/// Orchestration "Structure Container" node, which fans a Single-point
/// Calculation node out once per structure — the batch machinery already
/// built for exactly this shape of workflow, rather than a second,
/// wizard-embedded copy of it. A prior version of this dialog DID embed a
/// single-point run per member (RandomNoiseScriptGenerator, since removed);
/// nothing about that configuration was ever persisted to a project file or
/// QSettings, so there is no old data to migrate here — only, potentially, a
/// completed job directory's `random_noise.json` from before this change,
/// which RandomNoiseViewer still reads unmodified.
class RandomNoiseWizard : public QDialog {
    Q_OBJECT

public:
    /// `reference` is the structure to perturb; the ensemble is built from it
    /// and never modifies it.
    explicit RandomNoiseWizard(std::shared_ptr<const core::Structure> reference,
                               QWidget* parent = nullptr);

    /// The generated ensemble — frame 0 is the unperturbed reference, so the
    /// energy distribution (once evaluated downstream) has something to be a
    /// distribution AROUND. Empty until "Generate structures" has been
    /// pressed.
    const std::vector<std::shared_ptr<core::Structure>>& frames() const
    {
        return frames_;
    }

Q_SIGNALS:
    /// A fresh ensemble was generated. The host previews it as a scrubbable
    /// trajectory.
    void structuresGenerated(
        const std::vector<std::shared_ptr<core::Structure>>& frames);

private Q_SLOTS:
    /// Build the ensemble from the current settings and publish it.
    void generateStructures();

private:
    core::NoiseOptions noiseOptions() const;
    /// Enable "Generate structures" only once there is a structure to
    /// perturb, and keep the status line honest about what has been
    /// generated.
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
    /// Constant amplitude (today's behaviour, index 0) vs. a linear ramp from
    /// zero at frame 0 to the full set amplitude at the last frame (index 1).
    QComboBox* rampCombo_ = nullptr;
    QPushButton* generateButton_ = nullptr;
    QLabel* generationStatus_ = nullptr;
};

} // namespace calango::gui
