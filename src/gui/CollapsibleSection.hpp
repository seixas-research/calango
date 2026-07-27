#pragma once

#include <QWidget>

class QLayout;
class QPropertyAnimation;
class QToolButton;

namespace calango::gui {

/// A single collapsible "accordion" section: a clickable header (disclosure
/// arrow + title) over a content area that animates open/closed. Used to keep
/// dense control panels compact and scannable — group related controls under
/// one header the user can fold away.
///
/// Usage:
///   auto* section = new CollapsibleSection(tr("Vector Overlay"), this);
///   auto* form = new QFormLayout;      // fill with the grouped controls
///   section->setContentLayout(form);
class CollapsibleSection : public QWidget {
    Q_OBJECT

public:
    explicit CollapsibleSection(const QString& title, QWidget* parent = nullptr);

    /// Install the layout shown when the section is expanded (takes ownership
    /// of `contentLayout`). Call once after populating the layout.
    void setContentLayout(QLayout* contentLayout);

    /// Expand or collapse programmatically (animated).
    void setExpanded(bool expanded);

private:
    QToolButton* toggle_ = nullptr;
    QWidget* content_ = nullptr;
    QPropertyAnimation* animation_ = nullptr;
};

} // namespace calango::gui
