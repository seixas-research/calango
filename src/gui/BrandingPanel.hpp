#pragma once

#include <QFont>
#include <QPixmap>
#include <QString>
#include <QWidget>

namespace calango::gui {

/// Zone-1 branding card: the Calango logo painted centered — scaled to fit
/// with its aspect ratio preserved (no stretch, no cropping; letterboxed when
/// the panel's aspect differs) and re-rendered at device resolution whenever
/// the zone is resized — with the application version ("Version: x.y.z")
/// centered directly below it. The logo asset follows the active theme
/// (logo_dark.png / logo_light.png) via setDarkVariant().
class BrandingPanel : public QWidget {
    Q_OBJECT

public:
    explicit BrandingPanel(QWidget* parent = nullptr);

    /// Switch between the dark- and light-theme logo assets. No-op (beyond a
    /// repaint) when the variant is unchanged.
    void setDarkVariant(bool dark);

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    bool dark_ = false;
    QPixmap source_;    ///< the current logo asset at native resolution
    QPixmap scaled_;    ///< fit-scaled cache (aspect-preserved, letterboxed)
    QSize scaledFor_;   ///< device-pixel target the cache was built for
    QString versionText_; ///< "Version: x.y.z", drawn centered under the logo
    QFont versionFont_;   ///< slightly smaller than the widget default
};

} // namespace calango::gui
