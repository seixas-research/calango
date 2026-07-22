#pragma once

#include "gui/BrillouinZoneView.hpp"

#include <QDialog>

namespace calango::gui {

/// "Brillouin Zone & k-Path Styling": edits a BrillouinZoneView::Style —
/// surface color + transparency, border wireframe color, k-path line color +
/// thickness, and toggles for high-symmetry labels and sequential path
/// numbers. Changes are emitted live via styleChanged() so the 3D view
/// updates as the user drags/picks.
class BrillouinZoneStyleDialog : public QDialog {
    Q_OBJECT

public:
    explicit BrillouinZoneStyleDialog(const BrillouinZoneView::Style& style,
                                      QWidget* parent = nullptr);

    BrillouinZoneView::Style style() const { return style_; }

Q_SIGNALS:
    void styleChanged(const BrillouinZoneView::Style& style);

private:
    /// Paint a color swatch onto a picker button.
    void updateSwatch(class QPushButton* button, const QColor& color);
    void emitChanged();

    BrillouinZoneView::Style style_;
};

} // namespace calango::gui
