#pragma once

#include <QString>

namespace calango::gui {

/// Application appearance controller. Applies a Dark, Light, or System-default
/// color scheme by switching to the Fusion style (which honors QPalette on
/// every platform) and installing the matching palette + a small stylesheet.
/// "System" resolves to the OS dark/light preference (QStyleHints::colorScheme
/// on Qt 6.5+). The resolved dark/light result also drives the Zone-1 logo.
class ThemeManager {
public:
    enum class Theme { System, Dark, Light };

    static Theme fromString(const QString& text);
    static QString toString(Theme theme);

    /// Read the persisted theme (QSettings key "appearance/theme").
    static Theme current();

    /// True when `theme` renders as dark (System resolved against the OS).
    static bool isEffectivelyDark(Theme theme);

    /// Apply the theme to the running QApplication. Returns the effective
    /// dark(true)/light(false) result so callers can pick a matching logo.
    static bool apply(Theme theme);
};

} // namespace calango::gui
