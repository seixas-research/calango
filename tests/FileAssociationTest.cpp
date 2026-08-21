// File-association routing: the pure decision logic (which ASE format hint,
// project vs. structure) and the QFileOpenEvent plumbing (CalangoApplication,
// FileOpenQueue) that route a file handed to Calango by the OS -- a CLI
// argument, Finder's double-click / "Open With" / Dock drop, or a
// Linux/Windows file-manager association -- to MainWindow::loadFile().
//
// MainWindow itself (~9.5k lines, zero test coverage -- see CLAUDE.md) is
// deliberately not linked here: FileOpenRouting.hpp, FileOpenQueue.hpp and
// CalangoApplication.hpp exist specifically so this routing/queueing logic
// is testable without it. A QStringList of recorded calls stands in for
// MainWindow::loadFile as the attached handler.

#include "CalangoApplication.hpp"
#include "FileOpenQueue.hpp"
#include "gui/FileOpenRouting.hpp"

#include <QApplication>
#include <QCoreApplication>
#include <QFileOpenEvent>
#include <QString>
#include <QStringList>

#include <cstdio>
#include <cstdlib>

using calango::CalangoApplication;
using calango::FileOpenQueue;
using calango::gui::formatHintFor;
using calango::gui::isCalangoProjectFile;

namespace {

int failures = 0;

void check(bool ok, const char* what)
{
    std::printf("  %s %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok)
        ++failures;
}

void testFormatHintFor()
{
    check(formatHintFor(QStringLiteral("run.data")) == QLatin1String("lammps-data"),
          "LAMMPS .data -> lammps-data");
    check(formatHintFor(QStringLiteral("run.dump")) == QLatin1String("lammps-dump-text"),
          "LAMMPS .dump -> lammps-dump-text");
    check(formatHintFor(QStringLiteral("run.lammpstrj")) == QLatin1String("lammps-dump-text"),
          "LAMMPS .lammpstrj -> lammps-dump-text");
    check(formatHintFor(QStringLiteral("pwscf.pwi")) == QLatin1String("espresso-in"),
          "Quantum ESPRESSO .pwi -> espresso-in");
    check(formatHintFor(QStringLiteral("pwscf.in")) == QLatin1String("espresso-in"),
          "Quantum ESPRESSO .in -> espresso-in");
    check(formatHintFor(QStringLiteral("pwscf.pwo")) == QLatin1String("espresso-out"),
          "Quantum ESPRESSO .pwo -> espresso-out");
    check(formatHintFor(QStringLiteral("mol.gjf")) == QLatin1String("gaussian-in"),
          "Gaussian .gjf -> gaussian-in");
    check(formatHintFor(QStringLiteral("mol.com")) == QLatin1String("gaussian-in"),
          "Gaussian .com -> gaussian-in");
    check(formatHintFor(QStringLiteral("cell.cell")) == QLatin1String("castep-cell"),
          "CASTEP .cell -> castep-cell");
    check(formatHintFor(QStringLiteral("struct.res")) == QLatin1String("res"),
          "SHELX .res -> res");
    check(formatHintFor(QStringLiteral("frame.xyz")).isEmpty(),
          ".xyz has no hint (ASE sniffs the contents)");
    check(formatHintFor(QStringLiteral("frame.extxyz")).isEmpty(),
          ".extxyz has no hint (ASE sniffs the contents)");
    check(formatHintFor(QStringLiteral("structure.cif")).isEmpty(),
          ".cif has no hint (ASE sniffs the contents; PdbxFile disambiguates)");
    check(formatHintFor(QStringLiteral("POSCAR")).isEmpty(),
          "extension-less POSCAR has no hint (ASE sniffs the contents)");
    check(formatHintFor(QStringLiteral("run.out")).isEmpty(),
          ".out has no hint (ASE sniffs the contents)");
    check(formatHintFor(QStringLiteral("density.h5")).isEmpty(),
          ".h5 has no hint (not an ASE structure format at all)");
    // Finder and Explorer both preserve arbitrary case in filenames.
    check(formatHintFor(QStringLiteral("RUN.DATA")) == QLatin1String("lammps-data"),
          "suffix matching is case-insensitive");
}

void testIsCalangoProjectFile()
{
    check(isCalangoProjectFile(QStringLiteral("workspace.calproj")),
          "*.calproj is a project file");
    check(isCalangoProjectFile(QStringLiteral("WORKSPACE.CALPROJ")),
          "matching is case-insensitive");
    check(!isCalangoProjectFile(QStringLiteral("structure.xyz")),
          "*.xyz is not a project file");
    // Preserves loadFile()'s exact pre-extraction behavior: the legacy
    // ".calango" extension is NOT treated as a project by this check --
    // only the welcome screen's separate recent-files classifier recognizes
    // it, for display purposes, alongside .calproj.
    check(!isCalangoProjectFile(QStringLiteral("legacy.calango")),
          "legacy *.calango is not routed as a project by loadFile()");
}

void testFileOpenQueue()
{
    {
        FileOpenQueue queue;
        QStringList opened;
        queue.route(QStringLiteral("first.xyz"));
        queue.route(QStringLiteral("second.cif"));
        check(queue.receivedAny(), "receivedAny() is true once a path is routed");
        check(opened.isEmpty(), "nothing dispatches before a handler is attached");
        queue.attachHandler([&opened](const QString& path) { opened.append(path); });
        check(opened == QStringList({QStringLiteral("first.xyz"), QStringLiteral("second.cif")}),
              "attachHandler() drains the queue in arrival order");
    }
    {
        FileOpenQueue queue;
        QStringList opened;
        queue.attachHandler([&opened](const QString& path) { opened.append(path); });
        queue.route(QStringLiteral("third.vasp"));
        check(opened == QStringList({QStringLiteral("third.vasp")}),
              "route() dispatches immediately once a handler is already attached");
    }
    {
        FileOpenQueue queue;
        check(!queue.receivedAny(), "receivedAny() is false with nothing routed yet");
        queue.route(QString());
        check(!queue.receivedAny(), "an empty path is ignored, not queued or dispatched");
    }
}

/// Drives the REAL CalangoApplication -- the class main.cpp constructs as
/// the app object -- with synthesized QFileOpenEvents, the same delivery
/// mechanism Finder uses (QEvent::FileOpen, via an Apple Event Qt surfaces
/// as this event type). `app` is this test binary's own QApplication
/// instance (see main() below); only one QApplication/QCoreApplication may
/// exist per process, which is why this is a function taking it by
/// reference rather than a self-contained test that constructs its own.
void testCalangoApplicationFileOpenEvent(CalangoApplication& app)
{
    QStringList opened;

    // Before a handler is attached: a FileOpen event queues instead of
    // being dropped -- the cold-Finder-launch race the class exists for
    // (the event can arrive before MainWindow is constructed).
    QFileOpenEvent early(QStringLiteral("/tmp/early.extxyz"));
    QCoreApplication::sendEvent(&app, &early);
    check(opened.isEmpty(), "nothing dispatches before attachHandler() is called");

    app.attachHandler([&opened](const QString& path) { opened.append(path); });
    check(opened == QStringList({QStringLiteral("/tmp/early.extxyz")}),
          "attachHandler() drains a FileOpen event that arrived first");

    // Once attached, further FileOpen events dispatch immediately -- the
    // already-running-instance case. Finder delivers one QFileOpenEvent per
    // selected file when several are opened together, so sending two here
    // also covers "each opens its own tab" (loadFile() is called once per
    // path, independently).
    QFileOpenEvent second(QStringLiteral("/tmp/second.cif"));
    QFileOpenEvent third(QStringLiteral("/tmp/third.vasp"));
    QCoreApplication::sendEvent(&app, &second);
    QCoreApplication::sendEvent(&app, &third);
    check(opened == QStringList({QStringLiteral("/tmp/early.extxyz"),
                                 QStringLiteral("/tmp/second.cif"),
                                 QStringLiteral("/tmp/third.vasp")}),
          "each FileOpen event after attachHandler() dispatches on its own, in order");
    check(app.receivedFileOpen(),
          "receivedFileOpen() is true once any FileOpen event has arrived");
}

} // namespace

int main(int argc, char** argv)
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    CalangoApplication app(argc, argv);

    testFormatHintFor();
    testIsCalangoProjectFile();
    testFileOpenQueue();
    testCalangoApplicationFileOpenEvent(app);

    std::printf(failures == 0 ? "\nAll file-association routing checks passed.\n"
                              : "\n%d check(s) FAILED.\n", failures);
    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
