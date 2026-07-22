#include "gui/WelcomeDialog.hpp"

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

WelcomeDialog::WelcomeDialog(const QStringList& recentProjects, QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle(tr("Welcome to Calango"));
    setMinimumWidth(560);

    auto* layout = new QVBoxLayout(this);
    layout->setSpacing(14);

    // Banner header — the brand panel, centered, aspect preserved.
    const QPixmap banner(QStringLiteral(":/assets/.internal/panel.png"));
    if (!banner.isNull()) {
        auto* bannerLabel = new QLabel(this);
        bannerLabel->setAlignment(Qt::AlignCenter);
        bannerLabel->setPixmap(banner.scaledToWidth(
            520, Qt::SmoothTransformation));
        layout->addWidget(bannerLabel);
    }

    auto* body = new QHBoxLayout;
    layout->addLayout(body, 1);

    // Recent projects.
    auto* recentColumn = new QVBoxLayout;
    recentColumn->addWidget(new QLabel(tr("Recent Projects"), this));
    auto* recentList = new QListWidget(this);
    if (recentProjects.isEmpty()) {
        auto* item = new QListWidgetItem(tr("(no recent projects)"), recentList);
        item->setFlags(Qt::NoItemFlags);
    } else {
        for (const QString& path : recentProjects) {
            auto* item = new QListWidgetItem(QFileInfo(path).fileName(), recentList);
            item->setToolTip(path);
            item->setData(Qt::UserRole, path);
        }
    }
    recentColumn->addWidget(recentList, 1);
    body->addLayout(recentColumn, 1);
    connect(recentList, &QListWidget::itemActivated, this,
            [this](QListWidgetItem* item) {
                const QString path = item->data(Qt::UserRole).toString();
                if (!path.isEmpty())
                    chooseAndAccept(Choice::OpenRecent, path);
            });

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

    // Persisted "show at startup" toggle.
    auto* showCheck = new QCheckBox(tr("Show Welcome Screen on startup"), this);
    showCheck->setChecked(showAtStartupEnabled());
    layout->addWidget(showCheck);
    connect(showCheck, &QCheckBox::toggled, this, [](bool on) {
        QSettings().setValue(kShowAtStartupKey, on);
    });
}

void WelcomeDialog::chooseAndAccept(Choice choice, const QString& path)
{
    choice_ = choice;
    selectedPath_ = path;
    accept();
}

} // namespace calango::gui
