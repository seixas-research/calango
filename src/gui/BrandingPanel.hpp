#pragma once

#include <QPixmap>
#include <QWidget>

namespace calango::gui {

/// Zone-1 branding card: the panel banner (assets/calango/panel.png)
/// painted edge-to-edge — scaled to cover the whole panel (aspect
/// preserved, center-cropped) and re-rendered at device resolution
/// whenever the zone is resized.
class BrandingPanel : public QWidget {
    Q_OBJECT

public:
    explicit BrandingPanel(QWidget* parent = nullptr);

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    QPixmap scaled_; ///< cover-scaled cache for the current size/DPR
};

} // namespace calango::gui
