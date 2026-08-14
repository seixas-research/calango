#include "gui/RayTraceDialog.hpp"

#include "core/PingPongOrder.hpp"
#include "core/Structure.hpp"
#include "gui/ViewportWidget.hpp"
#include "python_bridge/AnimationExporter.hpp"
#include "render/RayTraceExporter.hpp"

#include <QCheckBox>
#include <QCoreApplication>
#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFontDatabase>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QSettings>
#include <QStandardPaths>
#include <QTextStream>
#include <QVBoxLayout>

namespace calango::gui {

namespace {

/// Shared verbatim with MainWindow's Export Animation dialog — see the
/// checkbox's comment.
const auto kPingPongKey = QStringLiteral("animation/pingPong");

QString settingsKeyFor(bool povray)
{
    return povray ? QStringLiteral("render/povrayBinary")
                  : QStringLiteral("render/tachyonBinary");
}

/// Zero-padded so the frame files sort in render order in a file manager
/// (and so any external ffmpeg/-pattern use of the scratch dir works).
QString frameStem(int index)
{
    return QStringLiteral("frame_%1").arg(index, 5, 10, QLatin1Char('0'));
}

} // namespace

RayTraceDialog::RayTraceDialog(ViewportWidget* viewport, QWidget* parent)
    : QDialog(parent)
    , viewport_(viewport)
{
    setWindowTitle(tr("Ray-Traced Render"));
    resize(620, 520);

    engineCombo_ = new QComboBox(this);
    engineCombo_->addItems({QStringLiteral("POV-Ray"), QStringLiteral("Tachyon")});

    widthSpin_ = new QSpinBox(this);
    widthSpin_->setRange(64, 16384);
    // H.264 (yuv420p) needs even dimensions; stepping by 2 keeps the
    // animation path from silently cropping the still-render resolution.
    widthSpin_->setSingleStep(2);
    widthSpin_->setValue(1920);
    heightSpin_ = new QSpinBox(this);
    heightSpin_->setRange(64, 16384);
    heightSpin_->setSingleStep(2);
    heightSpin_->setValue(1440);

    backgroundCombo_ = new QComboBox(this);
    backgroundCombo_->addItems({tr("Solid white"), tr("Viewport color")});

    fpsSpin_ = new QSpinBox(this);
    fpsSpin_->setRange(1, 60);
    fpsSpin_->setValue(24);

    // Same option, same setting key and same wording as the Export Animation
    // dialog: a trajectory rendered here is the same clip, and the two dialogs
    // disagreeing about whether it loops would be arbitrary.
    pingPongCheck_ = new QCheckBox(tr("Ping-pong (play forward, then back)"),
                                   this);
    pingPongCheck_->setChecked(
        QSettings().value(kPingPongKey, false).toBool());
    pingPongCheck_->setToolTip(
        tr("Append the trajectory played in reverse, so the clip returns to "
           "its first frame and loops seamlessly.\n\n"
           "Every frame is still traced exactly ONCE — the return half re-uses "
           "the images already on disk, which matters here more than anywhere: "
           "a ray-traced frame costs seconds to minutes. The two frames that "
           "would otherwise appear twice in a row, at the turnaround and at "
           "the loop seam, are dropped."));
    connect(pingPongCheck_, &QCheckBox::toggled, this, [](bool on) {
        QSettings().setValue(kPingPongKey, on);
    });

    binaryEdit_ = new QLineEdit(this);
    auto* browseButton = new QPushButton(tr("Browse…"), this);
    auto* binaryRow = new QHBoxLayout;
    binaryRow->addWidget(binaryEdit_, 1);
    binaryRow->addWidget(browseButton);

    auto* form = new QFormLayout;
    form->addRow(tr("Engine:"), engineCombo_);
    form->addRow(tr("Width (px):"), widthSpin_);
    form->addRow(tr("Height (px):"), heightSpin_);
    form->addRow(tr("Background:"), backgroundCombo_);
    form->addRow(tr("Animation FPS:"), fpsSpin_);
    form->addRow(QString(), pingPongCheck_);
    form->addRow(tr("Renderer binary:"), binaryRow);

    progress_ = new QProgressBar(this);
    progress_->setVisible(false);

    log_ = new QPlainTextEdit(this);
    log_->setReadOnly(true);
    log_->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));

    auto* buttons = new QDialogButtonBox(this);
    auto* saveSceneButton =
        buttons->addButton(tr("Save Scene File…"), QDialogButtonBox::ActionRole);
    renderButton_ = buttons->addButton(tr("Render…"), QDialogButtonBox::ActionRole);
    animateButton_ =
        buttons->addButton(tr("Render Trajectory…"), QDialogButtonBox::ActionRole);
    animateButton_->setEnabled(false);
    animateButton_->setToolTip(
        tr("Ray-trace every trajectory frame and stitch them into a video.\n"
           "Open a trajectory to enable this."));
    buttons->addButton(QDialogButtonBox::Close);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(saveSceneButton, &QPushButton::clicked, this, &RayTraceDialog::saveSceneFile);
    connect(renderButton_, &QPushButton::clicked, this, &RayTraceDialog::render);
    connect(animateButton_, &QPushButton::clicked, this,
            &RayTraceDialog::renderAnimation);

    auto* layout = new QVBoxLayout(this);
    layout->addLayout(form);
    layout->addWidget(progress_);
    layout->addWidget(log_, 1);
    layout->addWidget(buttons);

    const auto loadBinary = [this] {
        const QString stored = QSettings().value(settingsKeyFor(isPovray())).toString();
        binaryEdit_->setText(stored.isEmpty() ? defaultBinary() : stored);
    };
    connect(engineCombo_, &QComboBox::currentIndexChanged, this, loadBinary);
    connect(binaryEdit_, &QLineEdit::textChanged, this, [this] {
        QSettings().setValue(settingsKeyFor(isPovray()), binaryEdit_->text());
    });
    connect(browseButton, &QPushButton::clicked, this, [this] {
        const QString path =
            QFileDialog::getOpenFileName(this, tr("Select Renderer Binary"));
        if (!path.isEmpty())
            binaryEdit_->setText(path);
    });
    loadBinary();

    process_.setProcessChannelMode(QProcess::MergedChannels);
    connect(&process_, &QProcess::readyReadStandardOutput, this, [this] {
        appendLog(QString::fromLocal8Bit(process_.readAllStandardOutput()));
    });
    connect(&process_, &QProcess::finished, this, &RayTraceDialog::processFinished);
}

RayTraceDialog::~RayTraceDialog()
{
    // A renderer still running here would keep writing into a directory we
    // are about to delete — and QProcess would block in its own destructor.
    if (process_.state() != QProcess::NotRunning) {
        process_.disconnect(this);
        process_.kill();
        process_.waitForFinished(2000);
    }
    if (!scratchDir_.isEmpty())
        QDir(scratchDir_).removeRecursively();
}

void RayTraceDialog::setTrajectory(
    const std::vector<std::shared_ptr<core::Structure>>& frames)
{
    frames_ = frames;
    const bool usable = frames_.size() > 1;
    animateButton_->setEnabled(usable);
    if (usable) {
        animateButton_->setToolTip(
            tr("Ray-trace all %1 trajectory frames and stitch them into a "
               "video.").arg(frames_.size()));
    }
}

bool RayTraceDialog::isPovray() const
{
    return engineCombo_->currentIndex() == 0;
}

QString RayTraceDialog::defaultBinary() const
{
    return isPovray() ? QStringLiteral("povray") : QStringLiteral("tachyon");
}

QString RayTraceDialog::sceneText(int width, int height) const
{
    render::RayTraceExporter::SceneInputs inputs;
    const auto structure = viewport_->structure();
    inputs.structure = structure.get();
    inputs.style = viewport_->style();
    inputs.lights = viewport_->lights();
    inputs.camera = viewport_->camera();
    inputs.width = width;
    inputs.height = height;
    inputs.aspect = static_cast<float>(width) / static_cast<float>(height);
    inputs.background = backgroundCombo_->currentIndex() == 0
        ? QColor(Qt::white)
        : viewport_->backgroundColor();
    // Antialiasing is per pixel *per frame*: keep it high for a one-off
    // still, moderate for an animation where it multiplies by frame count.
    inputs.antialiasing = animating() ? 4 : 8;
    return isPovray() ? render::RayTraceExporter::povray(inputs)
                      : render::RayTraceExporter::tachyon(inputs);
}

bool RayTraceDialog::writeSceneFile(const QString& path, const QString& text)
{
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        appendLog(tr("Could not write %1: %2\n").arg(path, file.errorString()));
        return false;
    }
    QTextStream stream(&file);
    stream << text;
    stream.flush();
    file.close();
    // The renderer is a separate process: an unflushed or truncated scene
    // file shows up as a bewildering parse error, so fail loudly here.
    if (file.error() != QFile::NoError) {
        appendLog(tr("Could not write %1: %2\n").arg(path, file.errorString()));
        return false;
    }
    return true;
}

QStringList RayTraceDialog::rendererArguments(const QString& scenePath,
                                              const QString& imagePath,
                                              int width, int height) const
{
    if (isPovray()) {
        return {QStringLiteral("+I") + scenePath,
                QStringLiteral("+O") + imagePath,
                QStringLiteral("+W%1").arg(width),
                QStringLiteral("+H%1").arg(height),
                QStringLiteral("+A0.3"),
                QStringLiteral("-D")}; // no preview window (batch/animation)
    }
    return {scenePath,
            QStringLiteral("-o"), imagePath,
            QStringLiteral("-format"), QStringLiteral("PNG"),
            QStringLiteral("-res"), QString::number(width), QString::number(height),
            QStringLiteral("-aasamples"), animating() ? QStringLiteral("4")
                                                      : QStringLiteral("8")};
}

void RayTraceDialog::saveSceneFile()
{
    const QString filter = isPovray() ? tr("POV-Ray scene (*.pov)")
                                      : tr("Tachyon scene (*.dat)");
    const QString path = QFileDialog::getSaveFileName(
        this, tr("Save Scene File"),
        isPovray() ? QStringLiteral("calango.pov") : QStringLiteral("calango.dat"),
        filter);
    if (path.isEmpty())
        return;
    if (writeSceneFile(path, sceneText(widthSpin_->value(), heightSpin_->value())))
        appendLog(tr("Scene saved: %1\n").arg(path));
}

void RayTraceDialog::render()
{
    if (process_.state() != QProcess::NotRunning) {
        appendLog(tr("A render is already running.\n"));
        return;
    }
    outputPath_ = QFileDialog::getSaveFileName(
        this, tr("Render To"), QStringLiteral("calango_render.png"),
        tr("PNG image (*.png)"));
    if (outputPath_.isEmpty())
        return;

    const int width = widthSpin_->value();
    const int height = heightSpin_->value();
    const QString scenePath = outputPath_
        + (isPovray() ? QStringLiteral(".pov") : QStringLiteral(".dat"));
    if (!writeSceneFile(scenePath, sceneText(width, height)))
        return;

    const QStringList arguments =
        rendererArguments(scenePath, outputPath_, width, height);
    appendLog(tr("Running: %1 %2\n").arg(binaryEdit_->text(), arguments.join(' ')));
    renderButton_->setEnabled(false);
    animateButton_->setEnabled(false);
    process_.start(binaryEdit_->text(), arguments);
    if (!process_.waitForStarted(3000)) {
        appendLog(tr("Could not start '%1' — is it installed and on PATH?\n")
                      .arg(binaryEdit_->text()));
        renderButton_->setEnabled(true);
        animateButton_->setEnabled(frames_.size() > 1);
    }
}

// ---------------------------------------------------------------------------
// Trajectory animation
// ---------------------------------------------------------------------------

void RayTraceDialog::renderAnimation()
{
    if (process_.state() != QProcess::NotRunning) {
        appendLog(tr("A render is already running.\n"));
        return;
    }
    if (frames_.size() < 2) {
        appendLog(tr("No trajectory frames to render.\n"));
        return;
    }

    animationOutputPath_ = QFileDialog::getSaveFileName(
        this, tr("Render Trajectory To"), QStringLiteral("calango_render.mp4"),
        tr("MP4 video (*.mp4);;GIF animation (*.gif)"));
    if (animationOutputPath_.isEmpty())
        return;

    // Frame files live in their own scratch directory next to the output so
    // a partial run leaves nothing behind in the user's working folder; it
    // is removed when the run ends (and by the destructor as a backstop).
    const QString base = QFileInfo(animationOutputPath_).absolutePath();
    QDir parent(base.isEmpty()
                    ? QStandardPaths::writableLocation(QStandardPaths::TempLocation)
                    : base);
    const QString scratchName =
        QStringLiteral(".calango_render_%1").arg(QCoreApplication::applicationPid());
    if (!parent.mkpath(scratchName)) {
        QMessageBox::critical(this, windowTitle(),
                              tr("Could not create a scratch directory for the "
                                 "rendered frames in %1.").arg(parent.path()));
        return;
    }
    scratchDir_ = parent.filePath(scratchName);

    // yuv420p rejects odd dimensions; fix the resolution once, up front, so
    // every frame is encoded at exactly the size the video is opened with.
    frameWidth_ = widthSpin_->value() & ~1;
    frameHeight_ = heightSpin_->value() & ~1;

    framePaths_.clear();
    framePaths_.reserve(static_cast<int>(frames_.size()));
    frameIndex_ = 0;
    frameTotal_ = static_cast<int>(frames_.size());
    animationCancelled_ = false;
    viewportStructureBeforeRun_ = viewport_->structure();

    progress_->setRange(0, frameTotal_);
    progress_->setValue(0);
    progress_->setFormat(tr("Frame %v of %m"));
    progress_->setVisible(true);
    setAnimationControlsEnabled(false);

    appendLog(tr("\nRendering %1 trajectory frames at %2×%3 into %4\n")
                  .arg(frameTotal_)
                  .arg(frameWidth_)
                  .arg(frameHeight_)
                  .arg(animationOutputPath_));
    renderNextFrame();
}

void RayTraceDialog::renderNextFrame()
{
    if (animationCancelled_) {
        finishAnimation(tr("Canceled."));
        return;
    }
    if (frameIndex_ >= frameTotal_) {
        finishAnimation(QString());
        return;
    }

    // Bind the frame to the viewport without reframing the camera: every
    // frame must be rendered from the identical camera pose, otherwise the
    // animation jitters as the structure drifts.
    viewport_->setStructure(frames_[static_cast<std::size_t>(frameIndex_)],
                            /*frameCamera=*/false);

    const QDir scratch(scratchDir_);
    const QString stem = frameStem(frameIndex_);
    const QString scenePath = scratch.filePath(
        stem + (isPovray() ? QStringLiteral(".pov") : QStringLiteral(".dat")));
    pendingFramePath_ = scratch.filePath(stem + QStringLiteral(".png"));

    if (!writeSceneFile(scenePath, sceneText(frameWidth_, frameHeight_))) {
        finishAnimation(tr("Could not write the scene file for frame %1.")
                            .arg(frameIndex_ + 1));
        return;
    }

    const QStringList arguments =
        rendererArguments(scenePath, pendingFramePath_, frameWidth_, frameHeight_);
    process_.start(binaryEdit_->text(), arguments);
    if (!process_.waitForStarted(3000)) {
        finishAnimation(tr("Could not start '%1' — is it installed and on PATH?")
                            .arg(binaryEdit_->text()));
    }
}

void RayTraceDialog::finishAnimation(const QString& error)
{
    const int rendered = framePaths_.size();
    const int expected = frameTotal_;
    frameTotal_ = 0; // leaves the animating() state before any encoding

    if (viewportStructureBeforeRun_) {
        viewport_->setStructure(viewportStructureBeforeRun_, /*frameCamera=*/false);
        viewportStructureBeforeRun_.reset();
    }

    progress_->setVisible(false);
    setAnimationControlsEnabled(true);

    if (!error.isEmpty()) {
        appendLog(tr("\nTrajectory render stopped: %1\n").arg(error));
    } else if (rendered != expected) {
        // Defensive: the per-frame check below should already have caught
        // this, but a short animation must never be presented as complete.
        appendLog(tr("\nOnly %1 of %2 frames were rendered — not encoding.\n")
                      .arg(rendered)
                      .arg(expected));
    } else {
        // Ping-pong, applied to the FILE LIST rather than by rendering the
        // return pass. It matters more here than anywhere: a Tachyon or
        // POV-Ray frame costs seconds to minutes, so tracing the reverse half
        // again would double the wall clock of the whole job to produce
        // pictures that are already on disk. Re-listing the paths costs
        // nothing, and the encoder reads each one as many times as it appears.
        QStringList encodeOrder = framePaths_;
        if (pingPongCheck_->isChecked() && framePaths_.size() > 2) {
            encodeOrder.clear();
            for (const int index :
                 core::pingPongOrder(static_cast<int>(framePaths_.size())))
                encodeOrder.append(framePaths_.at(index));
        }
        appendLog(tr("\nEncoding %1 frames…\n").arg(encodeOrder.size()));
        try {
            const bool isMp4 = animationOutputPath_.endsWith(
                QStringLiteral(".mp4"), Qt::CaseInsensitive);
            if (isMp4) {
                pybridge::AnimationExporter::exportMp4FromFiles(
                    encodeOrder, animationOutputPath_, fpsSpin_->value());
            } else {
                pybridge::AnimationExporter::exportGifFromFiles(
                    encodeOrder, animationOutputPath_, fpsSpin_->value(),
                    /*transparent=*/false);
            }
            appendLog(tr("Done: %1 (%2 frames)\n")
                          .arg(animationOutputPath_)
                          .arg(encodeOrder.size()));
        } catch (const std::exception& e) {
            appendLog(tr("Encoding failed: %1\n").arg(QString::fromUtf8(e.what())));
            QMessageBox::critical(this, windowTitle(), QString::fromUtf8(e.what()));
        }
    }

    framePaths_.clear();
    pendingFramePath_.clear();
    if (!scratchDir_.isEmpty()) {
        QDir(scratchDir_).removeRecursively();
        scratchDir_.clear();
    }
}

void RayTraceDialog::setAnimationControlsEnabled(bool enabled)
{
    renderButton_->setEnabled(enabled);
    animateButton_->setEnabled(enabled && frames_.size() > 1);
    engineCombo_->setEnabled(enabled);
    widthSpin_->setEnabled(enabled);
    heightSpin_->setEnabled(enabled);
    backgroundCombo_->setEnabled(enabled);
}

void RayTraceDialog::processFinished(int exitCode, QProcess::ExitStatus status)
{
    const bool ok = status == QProcess::NormalExit && exitCode == 0;

    if (!animating()) { // single still image
        renderButton_->setEnabled(true);
        animateButton_->setEnabled(frames_.size() > 1);
        if (ok)
            appendLog(tr("\nDone: %1\n").arg(outputPath_));
        else
            appendLog(tr("\nRenderer exited with code %1.\n").arg(exitCode));
        return;
    }

    if (animationCancelled_) { // Close was pressed; the process was killed
        finishAnimation(tr("canceled after %1 of %2 frames")
                            .arg(frameIndex_)
                            .arg(frameTotal_));
        return;
    }

    if (!ok) {
        finishAnimation(tr("the renderer exited with code %1 on frame %2 of %3")
                            .arg(exitCode)
                            .arg(frameIndex_ + 1)
                            .arg(frameTotal_));
        return;
    }

    // A zero exit status is not proof the image landed: POV-Ray and Tachyon
    // both exit 0 after writing nothing when the output format is
    // unsupported by the build. Verify before counting the frame.
    const QFileInfo info(pendingFramePath_);
    if (!info.exists() || info.size() == 0) {
        finishAnimation(
            tr("frame %1 of %2 produced no image (%3).\nIf this is Tachyon, "
               "check that the binary was built with PNG support.")
                .arg(frameIndex_ + 1)
                .arg(frameTotal_)
                .arg(QDir::toNativeSeparators(pendingFramePath_)));
        return;
    }

    framePaths_.append(pendingFramePath_);
    ++frameIndex_;
    progress_->setValue(frameIndex_);
    renderNextFrame();
}

void RayTraceDialog::reject()
{
    if (animating()) {
        // First Close cancels the run (and lets it clean up); a second one
        // closes the dialog.
        animationCancelled_ = true;
        appendLog(tr("Cancelling after the current frame…\n"));
        if (process_.state() != QProcess::NotRunning)
            process_.kill();
        return;
    }
    QDialog::reject();
}

void RayTraceDialog::appendLog(const QString& text)
{
    log_->moveCursor(QTextCursor::End);
    log_->insertPlainText(text);
    log_->moveCursor(QTextCursor::End);
}

} // namespace calango::gui
