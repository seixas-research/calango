#include "gui/CollapsibleSection.hpp"

#include <QLayout>
#include <QPropertyAnimation>
#include <QToolButton>
#include <QVBoxLayout>

namespace calango::gui {

namespace {
constexpr int kAnimationMs = 150;
} // namespace

CollapsibleSection::CollapsibleSection(const QString& title, QWidget* parent)
    : QWidget(parent)
{
    toggle_ = new QToolButton(this);
    toggle_->setStyleSheet(QStringLiteral("QToolButton { border: none; }"));
    toggle_->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    toggle_->setArrowType(Qt::RightArrow); // collapsed by default → compact
    toggle_->setText(title);
    toggle_->setCheckable(true);
    toggle_->setChecked(false);

    content_ = new QWidget(this);
    content_->setMaximumHeight(0); // start collapsed
    content_->setMinimumHeight(0);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addWidget(toggle_);
    layout->addWidget(content_);

    animation_ = new QPropertyAnimation(content_, "maximumHeight", this);
    animation_->setDuration(kAnimationMs);

    connect(toggle_, &QToolButton::toggled, this, [this](bool checked) {
        setExpanded(checked);
    });
    // Once fully expanded, lift the height cap so the content can never clip if
    // it later needs more room; re-cap to 0 while collapsed.
    connect(animation_, &QPropertyAnimation::finished, this, [this] {
        if (toggle_->isChecked())
            content_->setMaximumHeight(QWIDGETSIZE_MAX);
    });
}

void CollapsibleSection::setContentLayout(QLayout* contentLayout)
{
    delete content_->layout();
    content_->setLayout(contentLayout);
}

void CollapsibleSection::setExpanded(bool expanded)
{
    if (toggle_->isChecked() != expanded) {
        const QSignalBlocker block(toggle_);
        toggle_->setChecked(expanded);
    }
    toggle_->setArrowType(expanded ? Qt::DownArrow : Qt::RightArrow);
    const int full =
        content_->layout() ? content_->layout()->sizeHint().height() : 0;
    animation_->stop();
    animation_->setStartValue(content_->maximumHeight());
    animation_->setEndValue(expanded ? full : 0);
    animation_->start();
}

bool CollapsibleSection::isExpanded() const
{
    return toggle_->isChecked();
}

} // namespace calango::gui
