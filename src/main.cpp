// Calango — atomistic modeling and simulation front-end.
//
// Startup order matters:
//   1. Default QSurfaceFormat (3.3 core) BEFORE QApplication, so every
//      QOpenGLWidget context inherits it (macOS then provides 4.1 core).
//   2. QApplication before PythonEngine, so import failures can be
//      reported in a message box.
//   3. PythonEngine is created in main() and outlives MainWindow —
//      declaration order below guarantees destruction order.

#include "gui/MainWindow.hpp"
#include "python_bridge/PythonEngine.hpp"

#include <QApplication>
#include <QIcon>
#include <QMessageBox>
#include <QSurfaceFormat>

#include <cstdio>
#include <cstring>

int main(int argc, char* argv[])
{
    // Headless environment check: `calango --probe-python`
    if (argc > 1 && std::strcmp(argv[1], "--probe-python") == 0) {
        calango::pybridge::PythonEngine python;
        std::printf("interpreter: %s\n", python.executable().c_str());
        std::printf("python:      %s\n", python.pythonVersion().c_str());
        std::printf("ase:         %s\n",
                    python.aseAvailable() ? python.aseVersion().c_str() : "NOT AVAILABLE");
        if (!python.aseAvailable())
            std::printf("\n%s\n", python.lastError().c_str());
        return python.aseAvailable() ? 0 : 1;
    }

    QSurfaceFormat format;
    format.setVersion(3, 3);
    format.setProfile(QSurfaceFormat::CoreProfile);
    format.setDepthBufferSize(24);
    format.setSamples(4);
    QSurfaceFormat::setDefaultFormat(format);

    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("Calango"));
    QApplication::setApplicationVersion(QStringLiteral(CALANGO_VERSION));
    QApplication::setOrganizationName(QStringLiteral("Seixas Research"));
    // Window / taskbar / dock icon (embedded high-resolution brand icon).
    QApplication::setWindowIcon(
        QIcon(QStringLiteral(":/assets/calango/icon_white.png")));

    calango::pybridge::PythonEngine python;
    if (!python.aseAvailable()) {
        QMessageBox::warning(
            nullptr, QStringLiteral("Calango"),
            QStringLiteral("Python started, but ASE could not be imported — "
                           "structure I/O and job features will be disabled.\n\n"
                           "Point Calango at an interpreter that has ASE, e.g.:\n"
                           "    export CALANGO_PYTHON=/path/to/.venv/bin/python\n"
                           "or activate that virtualenv before launching.\n"
                           "Diagnose with:  calango --probe-python\n\n%1")
                .arg(QString::fromStdString(python.lastError())));
    }

    calango::gui::MainWindow window;
    window.show();

    for (int i = 1; i < argc; ++i) // each file opens in its own tab
        window.loadFile(QString::fromLocal8Bit(argv[i]));

    return QApplication::exec();
}
