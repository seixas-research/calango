#include "gui/WelcomeDialog.hpp"

#include "gui/SettingsManager.hpp"
#include "gui/ThemeManager.hpp"

#include <QCheckBox>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QPixmap>
#include <QPushButton>
#include <QSettings>
#include <QVBoxLayout>

namespace calango::gui {

namespace {
const auto kShowAtStartupKey = QStringLiteral("welcome/showAtStartup");
} // namespace

bool WelcomeDialog::showAtStartupEnabled()
{
    return QSettings().value(kShowAtStartupKey, true).toBool();
}

WelcomeDialog::WelcomeDialog(const QStringList& recentProjects,
                             const QStringList& recentStructures,
                             QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle(tr("Welcome to Calango"));
    // Wider than before: three columns rather than two.
    setMinimumWidth(760);

    auto* layout = new QVBoxLayout(this);
    layout->setSpacing(14);

    // Banner header — the Zone-1 brand logo matching the active theme, so the
    // Welcome Screen and the branding panel always show the same asset.
    const bool dark = ThemeManager::isEffectivelyDark(ThemeManager::current());
    const QPixmap banner(dark
                             ? QStringLiteral(":/assets/.internal/logo_dark.png")
                             : QStringLiteral(":/assets/.internal/logo_light.png"));
    if (!banner.isNull()) {
        auto* bannerLabel = new QLabel(this);
        bannerLabel->setAlignment(Qt::AlignCenter);
        bannerLabel->setPixmap(banner.scaledToWidth(
            520, Qt::SmoothTransformation));
        layout->addWidget(bannerLabel);
    }

    auto* body = new QHBoxLayout;
    layout->addLayout(body, 1);

    // Recent projects and recent structures, side by side. Reopening a saved
    // workspace and opening a bare geometry are different intents, so they get
    // their own columns instead of one merged list where whichever kind was
    // touched less recently would be pushed out of view.
    const auto addRecentColumn = [this, body](const QString& title,
                                              const QStringList& paths,
                                              const QString& emptyText) {
        auto* column = new QVBoxLayout;
        column->addWidget(new QLabel(title, this));
        auto* list = new QListWidget(this);
        if (paths.isEmpty()) {
            auto* item = new QListWidgetItem(emptyText, list);
            item->setFlags(Qt::NoItemFlags);
        } else {
            for (const QString& path : paths) {
                auto* item =
                    new QListWidgetItem(QFileInfo(path).fileName(), list);
                // The full path in the tooltip: file names alone are routinely
                // ambiguous ("POSCAR", "structure.cif") across directories.
                item->setToolTip(path);
                item->setData(Qt::UserRole, path);
            }
        }
        column->addWidget(list, 1);
        body->addLayout(column, 1);
        connect(list, &QListWidget::itemActivated, this,
                [this](QListWidgetItem* item) {
                    const QString path = item->data(Qt::UserRole).toString();
                    if (!path.isEmpty())
                        chooseAndAccept(Choice::OpenRecent, path);
                });
    };
    addRecentColumn(tr("Recent Projects"), recentProjects,
                    tr("(no recent projects)"));
    addRecentColumn(tr("Recent Structures"), recentStructures,
                    tr("(no recent structures)"));

    // Quick actions.
    auto* actions = new QVBoxLayout;
    actions->addWidget(new QLabel(tr("Quick Start"), this));
    auto* newButton = new QPushButton(tr("New Project"), this);
    auto* openButton = new QPushButton(tr("Open Project…"), this);
    auto* geometryButton = new QPushButton(tr("Open Geometry…"), this);
    for (auto* button : {newButton, openButton, geometryButton})
        button->setMinimumHeight(36);
    actions->addWidget(newButton);
    actions->addWidget(openButton);
    actions->addWidget(geometryButton);
    actions->addStretch(1);
    body->addLayout(actions, 1);
    connect(newButton, &QPushButton::clicked, this,
            [this] { chooseAndAccept(Choice::NewProject); });
    connect(openButton, &QPushButton::clicked, this,
            [this] { chooseAndAccept(Choice::OpenProject); });
    connect(geometryButton, &QPushButton::clicked, this,
            [this] { chooseAndAccept(Choice::OpenGeometry); });

    // Persisted "show at startup" toggle — bound to the `show_welcome_screen`
    // property in ~/.calango/settings.json, saved immediately on every change.
    auto* showCheck = new QCheckBox(tr("Show Welcome Screen on startup"), this);
    showCheck->setChecked(showAtStartupEnabled());
    layout->addWidget(showCheck);
    connect(showCheck, &QCheckBox::toggled, this, [](bool on) {
        QSettings().setValue(kShowAtStartupKey, on);
        SettingsManager::save(); // flush to settings.json right away
    });
}

void WelcomeDialog::chooseAndAccept(Choice choice, const QString& path)
{
    choice_ = choice;
    selectedPath_ = path;
    accept();
}

} // namespace calango::gui
