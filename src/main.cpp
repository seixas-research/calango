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
#include "ui/IconManager.hpp"
#include "gui/SettingsManager.hpp"
#include "gui/ThemeManager.hpp"
#include "gui/WelcomeDialog.hpp"
#include "python_bridge/PythonEngine.hpp"

#include <QApplication>
#include <QIcon>
#include <QMessageBox>
#include <QSettings>
#include <QSurfaceFormat>

#include <cstdio>
#include <cstring>

#ifdef Q_OS_MACOS
namespace {
/// Keep the top-level "Edit" menu developer-defined on macOS. AppKit injects
/// its own text-editing items into any menu titled "Edit": "Start Dictation…",
/// "Emoji & Symbols", and (on macOS 15 Sequoia) "Writing Tools"/"AutoFill".
/// These are suppressed by user-defaults keys that AppKit reads while building
/// the menu bar, so they must be set BEFORE the QApplication builds any menus.
/// Writing directly into the application's own defaults domain (QSettings
/// NativeFormat) reaches [NSUserDefaults standardUserDefaults] for the bundled
/// .app; the Qt-side QAction menu-role overrides in MainWindow complete the
/// cleanup.
void disableMacEditMenuInjections()
{
    QSettings defaults;
    // Documented AppKit keys.
    defaults.setValue(QStringLiteral("NSDisabledDictationMenuItem"), true);
    defaults.setValue(QStringLiteral("NSDisabledCharacterPaletteMenuItem"), true);
    // Best-effort for the Sequoia additions (no long-documented public keys).
    defaults.setValue(QStringLiteral("NSDisabledWritingToolsMenuItem"), true);
    defaults.setValue(QStringLiteral("NSDisabledAutoFillMenuItem"), true);
    defaults.sync();
}
} // namespace
#endif

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
#ifdef Q_OS_MACOS
    // Suppress AppKit's auto-injected Edit-menu items before menus are built.
    disableMacEditMenuInjections();
#endif
    // Window / taskbar / dock icon (embedded high-resolution brand icon,
    // transparent-background variant).
    QApplication::setWindowIcon(
        QIcon(QStringLiteral(":/assets/.internal/icon.png")));

    // Centralized settings (~/.calango/settings.json): create-with-defaults on
    // first run, otherwise load and apply into QSettings (JSON authoritative).
    calango::gui::SettingsManager::loadOrInitialize();
    // Apply the persisted appearance theme before any widget is constructed.
    calango::gui::ThemeManager::apply(calango::gui::ThemeManager::current());
    // Watch for later theme changes (Preferences, or the OS switching under
    // "System") and re-tint every bound icon. Installed before any widget so
    // no binding can be created outside its reach.
    calango::ui::IconManager::installThemeWatcher(&app);

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

    if (argc > 1) {
        for (int i = 1; i < argc; ++i) // each file opens in its own tab
            window.loadFile(QString::fromLocal8Bit(argv[i]));
    } else if (calango::gui::WelcomeDialog::showAtStartupEnabled()) {
        // No files on the command line: greet the user with the welcome
        // screen (recent projects + quick actions), unless they turned it off.
        window.showWelcomeScreen();
    }

    return QApplication::exec();
}
