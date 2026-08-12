#pragma once

#include "core/Structure.hpp"
#include "core/VibrationalModes.hpp"

#include <QDialog>
#include <QList>
#include <QPair>
#include <QString>

#include <cstdint>
#include <memory>
#include <vector>

class QComboBox;
class QLabel;
class QPushButton;
class QSlider;
class QTimer;

namespace calango::gui {

/// "Analysis → Vibrational Mode Analysis…": pick a phonon mode and watch it.
///
/// A dispersion plot says a branch exists at 480 cm⁻¹; it does not say whether
/// that is a bond stretch, a librational mode or a soft mode about to drive a
/// transition. Animating the eigenvector is what answers that, so this pairs a
/// (q-point, branch) selector with a real-time animation on the MAIN 3D
/// viewport — the structure is inspected with the full representation and
/// measurement tooling rather than in a thumbnail.
///
/// A MODULE, not a panel of the phonon viewer. It used to be reachable only
/// from a button inside PhononPlotWindow, which made "watch the mode" require
/// "keep the dispersion plot open", and routed the resulting trajectory back
/// out through that window purely because it happened to be in the middle. It
/// now takes a completed phonon run the same way every other inheriting module
/// takes its baseline: a combo of the runs this session tracked, plus Browse…
/// for a job from an earlier session or copied back from a cluster. The phonon
/// viewer's button still exists and asks the host to open THIS dialog, so there
/// is one implementation rather than two that drift.
///
/// The physics lives in core::VibrationalModes and is pinned by the
/// `vibrational_modes` test; everything here is decoding JSON, filling combo
/// boxes and pushing frames at the viewport.
///
/// Eigenvectors come from `phonon_modes.json` when the run wrote one. Without
/// it only the frequencies are known (phonon_band.json carries no
/// eigenvectors), and the dialog says so rather than animating a fabricated
/// displacement pattern — a plausible-looking wrong animation is worse than
/// none, because nothing about it looks wrong.
class VibrationalAnalysisDialog : public QDialog {
    Q_OBJECT

public:
    /// `candidates` are (display label, absolute job directory) pairs — the
    /// completed phonon runs the host knows about; may be empty, in which case
    /// only Browse… is offered. `preselected` is the run to open on, added to
    /// the list if it is not already there (this is how the phonon viewer hands
    /// over the job it is showing).
    ///
    /// `fallbackStructure` is used only when the selected run kept no
    /// structure.extxyz of its own. Preferring the run's own geometry is not
    /// fussiness: the eigenvectors are indexed by ITS atom order, and the
    /// active document may since have been edited — or be a different system
    /// entirely, in which case the animation would be nonsense that looks fine.
    VibrationalAnalysisDialog(const QList<QPair<QString, QString>>& candidates,
                              const QString& preselected,
                              std::shared_ptr<const core::Structure> fallbackStructure,
                              QWidget* parent = nullptr);
    ~VibrationalAnalysisDialog() override;

Q_SIGNALS:
    /// Show this geometry on the main 3D viewport, without re-framing the
    /// camera. Emitted every animation tick, and once more with the
    /// UNDISPLACED structure when the dialog closes or its source changes.
    ///
    /// A signal rather than a ViewportWidget* the dialog holds: the viewport is
    /// a QOpenGLWidget, and a dialog that links against one cannot be
    /// constructed by the headless dialog_construction test — which is the only
    /// thing that would catch the signal-during-construction hazards this
    /// dialog is full of (it loads a run, and therefore repopulates two combo
    /// boxes, from inside its own constructor).
    void previewStructureRequested(
        const std::shared_ptr<const core::Structure>& structure);

    /// "Create Mode Trajectory Tab": one full vibrational period of the
    /// selected mode, as frames carrying the harmonic restoring forces. The
    /// host opens it as a new workspace tab (this dialog owns no documents).
    void modeTrajectoryRequested(
        const std::vector<std::shared_ptr<core::Structure>>& frames,
        const QString& label);

private Q_SLOTS:
    void createModeTrajectory();
    void browseForSource();
    /// Re-read everything from the selected run directory.
    void loadSource();
    void onQPointChanged(int index);
    void onModeChanged(int index);
    void togglePlay();
    /// One animation tick: advance the phase and push the displaced geometry.
    void advance();

private:
    /// Parse phonon_modes.json (q-points, frequencies, eigenvectors) and, as a
    /// fallback, the frequencies alone from phonon_band.json. Returns the
    /// reason the directory is unusable, or an empty string when it is.
    QString readModes(const QString& directory);
    /// The run's own structure.extxyz, or the fallback when it has none.
    void resolveStructure(const QString& directory);
    /// Displace the reference structure by the current mode at the current
    /// phase and show it.
    void applyDisplacement();
    /// Put the undisplaced structure back on the viewport.
    void restoreStructure();
    void updateModeLabel();
    /// Enable/disable the animation controls in one place, so "no
    /// eigenvectors" and "no run selected" cannot leave half of them live.
    void setAnimationEnabled(bool enabled);
    /// Current (q-point, branch) selection, or false when either is unset.
    bool currentSelection(std::size_t& qIndex, std::size_t& branch) const;

    std::shared_ptr<const core::Structure> reference_;
    std::shared_ptr<const core::Structure> fallback_;
    core::VibrationalModeSet modes_;
    /// diagnostics_[q][branch] — orthonormality, the acoustic sum rule and the
    /// degeneracy of each branch, computed once per load.
    std::vector<std::vector<core::ModeDiagnostics>> diagnostics_;

    QComboBox* sourceCombo_ = nullptr;
    QLabel* sourceStatus_ = nullptr;
    QComboBox* qpointCombo_ = nullptr;
    QComboBox* modeCombo_ = nullptr;
    QLabel* modeLabel_ = nullptr;
    QLabel* noticeLabel_ = nullptr;
    QSlider* amplitudeSlider_ = nullptr;
    QSlider* speedSlider_ = nullptr;
    QPushButton* playButton_ = nullptr;
    QPushButton* trajectoryButton_ = nullptr;
    QTimer* timer_ = nullptr;
    double phase_ = 0.0;
};

} // namespace calango::gui
