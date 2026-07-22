#pragma once

#include <QDialog>
#include <QString>
#include <QStringList>

namespace calango::gui {

/// Startup welcome screen: the brand banner, a list of recent projects, and
/// quick actions (New Project / Open Project / Open Geometry). A persisted
/// "Show Welcome Screen on startup" checkbox (QSettings key
/// "welcome/showAtStartup", default on) governs whether it appears next launch.
/// The dialog only records the user's choice; the host (MainWindow) performs it.
class WelcomeDialog : public QDialog {
    Q_OBJECT

public:
    enum class Choice { None, NewProject, OpenProject, OpenGeometry, OpenRecent };

    explicit WelcomeDialog(const QStringList& recentProjects,
                           QWidget* parent = nullptr);

    Choice choice() const { return choice_; }
    /// The recent-project path when choice() == OpenRecent.
    QString selectedPath() const { return selectedPath_; }

    /// Whether the welcome screen should be shown at startup (persisted).
    static bool showAtStartupEnabled();

private:
    void chooseAndAccept(Choice choice, const QString& path = {});

    Choice choice_ = Choice::None;
    QString selectedPath_;
};

} // namespace calango::gui
