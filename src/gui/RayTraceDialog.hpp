#pragma once

#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QProcess>
#include <QProgressBar>
#include <QPushButton>
#include <QSpinBox>
#include <QStringList>

#include <memory>
#include <vector>

namespace calango::core {
class Structure;
}

namespace calango::gui {

class ViewportWidget;

/// External ray-tracing renders of the active viewport scene.
/// Generates POV-Ray (.pov) or Tachyon (.dat) scene files that reproduce
/// the on-screen scene (camera, lights, representation, colors), and can
/// invoke the installed renderer binary to produce a high-quality image.
///
/// With a trajectory attached (setTrajectory) it also renders every frame
/// through the same pipeline and stitches the results into a .mp4/.gif.
/// The renderer is driven strictly one frame at a time — the next scene
/// file is only written once the previous process has exited successfully —
/// so frames can never be dropped or interleaved, and the intermediate
/// PNGs stay on disk (in a scratch directory removed with the dialog)
/// instead of accumulating in memory.
class RayTraceDialog : public QDialog {
    Q_OBJECT

public:
    RayTraceDialog(ViewportWidget* viewport, QWidget* parent = nullptr);
    ~RayTraceDialog() override;

    /// Attach the current document's trajectory, enabling the animation
    /// controls. Frames are borrowed, not copied; the dialog is modal for
    /// the lifetime of the render so they stay alive.
    void setTrajectory(
        const std::vector<std::shared_ptr<core::Structure>>& frames);

protected:
    /// Aborts an in-flight render (and its child process) rather than
    /// leaving an orphaned renderer writing into a deleted scratch dir.
    void reject() override;

private Q_SLOTS:
    void saveSceneFile();
    void render();
    void renderAnimation();
    void processFinished(int exitCode, QProcess::ExitStatus status);

private:
    bool isPovray() const;
    /// Scene text for the structure currently bound to the viewport.
    QString sceneText(int width, int height) const;
    QString defaultBinary() const;
    void appendLog(const QString& text);

    /// Write `text` to `path`; logs and returns false on failure.
    bool writeSceneFile(const QString& path, const QString& text);
    /// Renderer command line for one scene -> one image.
    QStringList rendererArguments(const QString& scenePath,
                                  const QString& imagePath,
                                  int width, int height) const;

    // -- Animation state machine -------------------------------------------
    //
    // renderNextFrame() binds frame N to the viewport, writes its scene file
    // and starts the renderer; processFinished() validates frame N's output
    // and calls renderNextFrame() for N+1. finishAnimation() encodes.

    void renderNextFrame();
    void finishAnimation(const QString& error);
    void setAnimationControlsEnabled(bool enabled);
    bool animating() const { return frameTotal_ > 0; }

    ViewportWidget* viewport_;
    QComboBox* engineCombo_;
    QSpinBox* widthSpin_;
    QSpinBox* heightSpin_;
    QComboBox* backgroundCombo_;
    QLineEdit* binaryEdit_;
    QPushButton* renderButton_;
    QPushButton* animateButton_;
    QSpinBox* fpsSpin_;
    /// "Ping-pong": encode the traced frames forward then in reverse.
    QCheckBox* pingPongCheck_;
    QProgressBar* progress_;
    QPlainTextEdit* log_;
    QProcess process_;
    QString outputPath_;

    std::vector<std::shared_ptr<core::Structure>> frames_;
    /// Restored onto the viewport when an animation run ends, so scrubbing
    /// the trajectory through the renderer doesn't move the live view.
    std::shared_ptr<const core::Structure> viewportStructureBeforeRun_;

    QString animationOutputPath_;
    QString scratchDir_;      ///< per-run scene/frame scratch (removed after)
    QStringList framePaths_;  ///< rendered PNGs, in frame order
    QString pendingFramePath_;
    int frameIndex_ = 0;
    int frameTotal_ = 0;      ///< 0 = not animating
    int frameWidth_ = 0;
    int frameHeight_ = 0;
    bool animationCancelled_ = false;
};

} // namespace calango::gui
