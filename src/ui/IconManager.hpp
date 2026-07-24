#pragma once

#include <QColor>
#include <QIcon>
#include <QPixmap>
#include <QString>

namespace calango::ui {

/// Centralized RemixIcon loader and styler for the whole application.
///
/// RemixIcon SVGs are monochrome (`fill="currentColor"`), so a single asset per
/// glyph is tinted at load time to whatever the active theme and widget state
/// require — no per-theme asset variants. Assets live under
/// `assets/.internal/icons/<name>.svg` (bundled at
/// `:/assets/.internal/icons/`), where `<name>` is the RemixIcon file stem,
/// e.g. `file-copy-line`, `cpu-line`, `ruler-2-line`.
///
/// Icons render in high-contrast light neutrals in Dark Mode and dark neutrals
/// in Light Mode, with distinct Active / Disabled / Hovered / Pressed colours.
class IconManager {
public:
    /// Widget interaction state selecting the tint colour.
    enum class State { Active, Disabled, Hovered, Pressed };

    /// Multi-mode themed icon for RemixIcon `name`. Carries Normal (Active
    /// colour), Disabled and Active (hover) pixmaps so Qt widgets pick the
    /// right one automatically as the control's state changes.
    static QIcon icon(const QString& name, int px = 24);

    /// Single-colour themed icon (one mode) — for the rare caller that wants a
    /// fixed tint regardless of widget state.
    static QIcon icon(const QString& name, const QColor& color, int px = 24);

    /// Device-pixel-ratio-aware tinted pixmap of the RemixIcon at `px` logical
    /// pixels. Falls back to a null pixmap when the asset is missing.
    static QPixmap pixmap(const QString& name, const QColor& color, int px = 24);

    /// Themed tint colour for `state`, resolved against the active theme.
    static QColor color(State state);

    /// True when a bundled SVG asset exists for `name`.
    static bool has(const QString& name);
};

} // namespace calango::ui
