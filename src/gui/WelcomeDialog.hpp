#pragma once

#include <QDialog>
#include <QString>
#include <QStringList>

namespace calango::gui {

/// Startup welcome screen: the brand banner, lists of recent projects and
/// recent structures, and quick actions (New Project / Open Project / Open
/// Geometry). A persisted "Show Welcome Screen on startup" checkbox (QSettings
/// key "welcome/showAtStartup", default on) governs whether it appears next
/// launch. The dialog only records the user's choice; the host (MainWindow)
/// performs it.
///
/// Projects and structures are listed SEPARATELY rather than merged into one
/// "recent files" column: reopening a saved workspace and opening a bare
/// geometry are different intents, and a single list would bury whichever kind
/// the user happened to touch less recently.
class WelcomeDialog : public QDialog {
    Q_OBJECT

public:
    enum class Choice { None, NewProject, OpenProject, OpenGeometry, OpenRecent };

    /// `recentProjects` are workspace files (.calproj/.calango);
    /// `recentStructures` are geometry files. Both are absolute paths the host
    /// has already filtered to ones that still exist.
    WelcomeDialog(const QStringList& recentProjects,
                  const QStringList& recentStructures,
                  QWidget* parent = nullptr);

    Choice choice() const { return choice_; }
    /// The chosen path when choice() == OpenRecent — a project or a structure;
    /// the host loads either through the same loadFile() entry point.
    QString selectedPath() const { return selectedPath_; }

    /// Whether the welcome screen should be shown at startup (persisted).
    static bool showAtStartupEnabled();

private:
    void chooseAndAccept(Choice choice, const QString& path = {});

    Choice choice_ = Choice::None;
    QString selectedPath_;
};

} // namespace calango::gui
