#pragma once

#include <QDialog>

namespace calango::gui {

class ViewportWidget;

/// View → "Visual Effects": distance fog (linear/exponential) and the
/// depth-of-field composite pass, with live sliders. Modeless so the
/// scene can be orbited while tuning.
class VisualEffectsDialog : public QDialog {
    Q_OBJECT

public:
    explicit VisualEffectsDialog(ViewportWidget* viewport,
                                 QWidget* parent = nullptr);

private:
    ViewportWidget* viewport_;
};

} // namespace calango::gui
