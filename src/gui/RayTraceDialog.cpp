#include "gui/RayTraceDialog.hpp"

#include "gui/ViewportWidget.hpp"
#include "render/RayTraceExporter.hpp"

#include <QDialogButtonBox>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFontDatabase>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QMessageBox>
#include <QSettings>
#include <QTextStream>
#include <QVBoxLayout>

namespace calango::gui {

namespace {
QString settingsKeyFor(bool povray)
{
    return povray ? QStringLiteral("render/povrayBinary")
                  : QStringLiteral("render/tachyonBinary");
}
} // namespace

RayTraceDialog::RayTraceDialog(ViewportWidget* viewport, QWidget* parent)
    : QDialog(parent)
    , viewport_(viewport)
{
    setWindowTitle(tr("Ray-Traced Render"));
    resize(620, 460);

    engineCombo_ = new QComboBox(this);
    engineCombo_->addItems({QStringLiteral("POV-Ray"), QStringLiteral("Tachyon")});

    widthSpin_ = new QSpinBox(this);
    widthSpin_->setRange(64, 16384);
    widthSpin_->setValue(1920);
    heightSpin_ = new QSpinBox(this);
    heightSpin_->setRange(64, 16384);
    heightSpin_->setValue(1440);

    backgroundCombo_ = new QComboBox(this);
    backgroundCombo_->addItems({tr("Solid white"), tr("Viewport color")});

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
    form->addRow(tr("Renderer binary:"), binaryRow);

    log_ = new QPlainTextEdit(this);
    log_->setReadOnly(true);
    log_->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));

    auto* buttons = new QDialogButtonBox(this);
    auto* saveSceneButton =
        buttons->addButton(tr("Save Scene File…"), QDialogButtonBox::ActionRole);
    renderButton_ = buttons->addButton(tr("Render…"), QDialogButtonBox::ActionRole);
    buttons->addButton(QDialogButtonBox::Close);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(saveSceneButton, &QPushButton::clicked, this, &RayTraceDialog::saveSceneFile);
    connect(renderButton_, &QPushButton::clicked, this, &RayTraceDialog::render);

    auto* layout = new QVBoxLayout(this);
    layout->addLayout(form);
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
    return isPovray() ? render::RayTraceExporter::povray(inputs)
                      : render::RayTraceExporter::tachyon(inputs);
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
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QMessageBox::critical(this, windowTitle(), tr("Could not write %1").arg(path));
        return;
    }
    QTextStream(&file) << sceneText(widthSpin_->value(), heightSpin_->value());
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
    QFile sceneFile(scenePath);
    if (!sceneFile.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QMessageBox::critical(this, windowTitle(),
                              tr("Could not write %1").arg(scenePath));
        return;
    }
    QTextStream(&sceneFile) << sceneText(width, height);
    sceneFile.close();

    QStringList arguments;
    if (isPovray()) {
        arguments = {QStringLiteral("+I") + scenePath,
                     QStringLiteral("+O") + outputPath_,
                     QStringLiteral("+W%1").arg(width),
                     QStringLiteral("+H%1").arg(height),
                     QStringLiteral("+A0.3"), QStringLiteral("-D")};
    } else {
        arguments = {scenePath,
                     QStringLiteral("-o"), outputPath_,
                     QStringLiteral("-format"), QStringLiteral("PNG"),
                     QStringLiteral("-res"), QString::number(width),
                     QString::number(height),
                     QStringLiteral("-aasamples"), QStringLiteral("4")};
    }

    appendLog(tr("Running: %1 %2\n").arg(binaryEdit_->text(), arguments.join(' ')));
    renderButton_->setEnabled(false);
    process_.start(binaryEdit_->text(), arguments);
    if (!process_.waitForStarted(3000)) {
        appendLog(tr("Could not start '%1' — is it installed and on PATH?\n")
                      .arg(binaryEdit_->text()));
        renderButton_->setEnabled(true);
    }
}

void RayTraceDialog::processFinished(int exitCode, QProcess::ExitStatus status)
{
    renderButton_->setEnabled(true);
    if (status == QProcess::NormalExit && exitCode == 0)
        appendLog(tr("\nDone: %1\n").arg(outputPath_));
    else
        appendLog(tr("\nRenderer exited with code %1.\n").arg(exitCode));
}

void RayTraceDialog::appendLog(const QString& text)
{
    log_->moveCursor(QTextCursor::End);
    log_->insertPlainText(text);
    log_->moveCursor(QTextCursor::End);
}

} // namespace calango::gui
