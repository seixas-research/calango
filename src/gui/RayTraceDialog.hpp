#pragma once

#include <QComboBox>
#include <QDialog>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QProcess>
#include <QPushButton>
#include <QSpinBox>

namespace calango::gui {

class ViewportWidget;

/// External ray-tracing renders of the active viewport scene.
/// Generates POV-Ray (.pov) or Tachyon (.dat) scene files that reproduce
/// the on-screen scene (camera, lights, representation, colors), and can
/// invoke the installed renderer binary to produce a high-quality image.
class RayTraceDialog : public QDialog {
    Q_OBJECT

public:
    RayTraceDialog(ViewportWidget* viewport, QWidget* parent = nullptr);

private Q_SLOTS:
    void saveSceneFile();
    void render();
    void processFinished(int exitCode, QProcess::ExitStatus status);

private:
    bool isPovray() const;
    QString sceneText(int width, int height) const;
    QString defaultBinary() const;
    void appendLog(const QString& text);

    ViewportWidget* viewport_;
    QComboBox* engineCombo_;
    QSpinBox* widthSpin_;
    QSpinBox* heightSpin_;
    QComboBox* backgroundCombo_;
    QLineEdit* binaryEdit_;
    QPushButton* renderButton_;
    QPlainTextEdit* log_;
    QProcess process_;
    QString outputPath_;
};

} // namespace calango::gui
