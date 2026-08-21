#pragma once

// Pure, dependency-free helpers for routing a path handed to Calango from
// outside the app — a CLI argument, a QFileOpenEvent (macOS Finder
// double-click / "Open With" / Dock drop), or a Linux/Windows file-manager
// association — to the right handler in MainWindow::loadFile().
//
// Extracted out of MainWindow.cpp (an anonymous-namespace function there
// before this) so the routing decision is unit-testable on its own: this
// header has no Structure/Document/widget dependency, so a test can include
// it without linking any of MainWindow's ~9.5k lines. MainWindow::loadFile()
// is the only caller in production; it still owns everything these
// functions merely decide the shape of (which ASE format hint to pass,
// whether to treat the path as a project instead of a structure).

#include <QFileInfo>
#include <QLatin1String>
#include <QString>

namespace calango::gui {

/// True for a path MainWindow::loadFile() restores as a project workspace
/// (ProjectSerializer) rather than importing as a structure through ASE.
///
/// Deliberately narrower than the welcome screen's recent-files split (which
/// also recognizes the legacy ".calango" extension for display purposes):
/// this mirrors exactly the check loadFile() has always made, so extracting
/// it changes nothing about which files get restored as projects.
inline bool isCalangoProjectFile(const QString& path)
{
    return path.endsWith(QStringLiteral(".calproj"), Qt::CaseInsensitive);
}

/// Explicit ASE format hints for extensions ase.io cannot infer reliably
/// from content alone. An empty string means "let ASE sniff the contents",
/// which is correct for .out and every extension not listed below.
inline QString formatHintFor(const QString& path)
{
    const QString suffix = QFileInfo(path).suffix().toLower();
    if (suffix == QLatin1String("data"))
        return QStringLiteral("lammps-data");
    if (suffix == QLatin1String("dump") || suffix == QLatin1String("lammpstrj"))
        return QStringLiteral("lammps-dump-text");
    if (suffix == QLatin1String("pwi") || suffix == QLatin1String("in"))
        return QStringLiteral("espresso-in");
    if (suffix == QLatin1String("pwo"))
        return QStringLiteral("espresso-out");
    if (suffix == QLatin1String("gjf") || suffix == QLatin1String("com"))
        return QStringLiteral("gaussian-in");
    if (suffix == QLatin1String("cell"))
        return QStringLiteral("castep-cell");
    if (suffix == QLatin1String("res"))
        return QStringLiteral("res");
    return {}; // .out and others: let ASE sniff the contents
}

} // namespace calango::gui
