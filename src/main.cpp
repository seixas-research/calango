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
#include <QFileOpenEvent>
#include <QIcon>
#include <QMessageBox>
#include <QSettings>
#include <QStringList>
#include <QSurfaceFormat>
#include <QTimer>

#include <cstdio>
#include <cstring>
#include <utility>

namespace {
/// QApplication that understands the OS "open this document" protocol.
///
/// Finder (macOS) never passes a double-clicked file on argv: Launch Services
/// delivers it as an Apple Event that Qt surfaces as a QFileOpenEvent — both
/// when the double-click launches the app and when the app is already
/// running (or a file is dropped on the Dock icon). The event can therefore
/// arrive before the main window exists, so paths are queued until a window
/// is attached. On Linux/Windows the file-manager association passes the
/// path on argv instead, which main() already handles; FileOpen events
/// simply never occur there, making this class harmless cross-platform.
class CalangoApplication : public QApplication {
public:
    using QApplication::QApplication;

    void attachWindow(calango::gui::MainWindow* window)
    {
        window_ = window;
        for (const QString& path : std::as_const(pending_))
            window_->loadFile(path);
        pending_.clear();
    }

    /// True once any document arrived from the OS — used to keep the welcome
    /// screen from opening on top of a launch-by-double-click.
    bool receivedFileOpen() const { return receivedFileOpen_; }

protected:
    bool event(QEvent* event) override
    {
        if (event->type() == QEvent::FileOpen) {
            const QString path = static_cast<QFileOpenEvent*>(event)->file();
            if (!path.isEmpty()) {
                receivedFileOpen_ = true;
                if (window_)
                    window_->loadFile(path);
                else
                    pending_.append(path);
            }
            return true;
        }
        return QApplication::event(event);
    }

private:
    calango::gui::MainWindow* window_ = nullptr;
    QStringList pending_;
    bool receivedFileOpen_ = false;
};
} // namespace

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

    // `calango --version` / `-v`: without this, the flag falls through to the
    // argv-as-file-to-open loop below and QMessageBoxes a FileNotFoundError
    // for a structure literally named "--version" — no window ever shows,
    // so there is nowhere for the intended behavior to come from otherwise.
    if (argc > 1 && (std::strcmp(argv[1], "--version") == 0 ||
                     std::strcmp(argv[1], "-v") == 0)) {
        std::printf("Calango %s\n", CALANGO_VERSION);
        return 0;
    }

    // `calango --help` / `-h`: same reasoning as --version above.
    if (argc > 1 && (std::strcmp(argv[1], "--help") == 0 ||
                     std::strcmp(argv[1], "-h") == 0)) {
        std::printf(
            "Usage: calango [FILE...]\n"
            "       calango --version | -v\n"
            "       calango --help | -h\n"
            "       calango --probe-python\n\n"
            "With no options, opens each FILE (structure, trajectory, or\n"
            "project) in its own tab, or shows the welcome screen if none\n"
            "are given.\n");
        return 0;
    }

    QSurfaceFormat format;
    format.setVersion(3, 3);
    format.setProfile(QSurfaceFormat::CoreProfile);
    format.setDepthBufferSize(24);
    format.setSamples(4);
    QSurfaceFormat::setDefaultFormat(format);

    CalangoApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("Calango"));
    QApplication::setApplicationVersion(QStringLiteral(CALANGO_VERSION));
    QApplication::setOrganizationName(QStringLiteral("Seixas Research"));
#ifdef Q_OS_MACOS
    // Suppress AppKit's auto-injected Edit-menu items before menus are built.
    disableMacEditMenuInjections();
#endif
    // Window / taskbar / dock icon. The two variants differ in more than
    // style: icon_osx.png carries the ~80% margin the Dock expects, while
    // icon_linux.png is full-bleed for taskbars that pad the icon themselves.
    // Using either one everywhere leaves it noticeably mis-sized on the other
    // platform. (On a macOS .app the Finder icon comes from the bundled
    // calango.icns instead; this sets the in-session dock/window icon.)
#ifdef Q_OS_MACOS
    QApplication::setWindowIcon(
        QIcon(QStringLiteral(":/assets/calango/icon_osx.png")));
#else
    QApplication::setWindowIcon(
        QIcon(QStringLiteral(":/assets/calango/icon_linux.png")));
#endif

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
    app.attachWindow(&window); // drains any FileOpen that beat the window
    window.show();

    if (argc > 1) {
        for (int i = 1; i < argc; ++i) // each file opens in its own tab
            window.loadFile(QString::fromLocal8Bit(argv[i]));
    } else if (calango::gui::WelcomeDialog::showAtStartupEnabled()) {
#ifdef Q_OS_MACOS
        // A Finder launch ("Open With", double-click, drop on the Dock)
        // leaves argc at 1 — the document arrives as a QFileOpenEvent once
        // the event loop runs. Deciding "no files, show the welcome screen"
        // right here would race that delivery, so the decision is deferred
        // one event-loop turn; by then a launch-by-document has been seen.
        QTimer::singleShot(0, &app, [&app, &window] {
            if (!app.receivedFileOpen())
                window.showWelcomeScreen();
        });
#else
        // No files on the command line: greet the user with the welcome
        // screen (recent projects + quick actions), unless they turned it off.
        window.showWelcomeScreen();
#endif
    }

    return QApplication::exec();
}
