#pragma once

#include <QColor>
#include <QIcon>
#include <QPixmap>
#include <QString>

class QAbstractButton;
class QAction;
class QLabel;
class QObject;

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

    // -- Theme-adaptive binding ---------------------------------------------
    //
    // icon() resolves the tint from the theme that is active AT THE MOMENT OF
    // THE CALL and bakes it into pixmaps. A widget whose icon was built under
    // Dark keeps its near-white glyphs after a switch to Light, where they are
    // invisible against a light background — the white-on-white bug.
    //
    // bind() sets the icon AND records the (widget, name) pair, so refreshAll()
    // can re-tint every bound icon when the palette changes. Prefer it over
    // icon() for any long-lived widget; icon() remains correct for transient
    // dialogs built after the theme is already settled.

    /// Set `button`'s icon from `name` and keep it in sync with the theme.
    static void bind(QAbstractButton* button, const QString& name, int px = 24);
    /// Set `action`'s icon from `name` and keep it in sync with the theme.
    static void bind(QAction* action, const QString& name, int px = 24);
    /// Set `label`'s pixmap from `name` and keep it in sync with the theme.
    static void bind(QLabel* label, const QString& name, int px = 24);

    /// Re-tint every live binding against the current theme. Bindings whose
    /// widget has been destroyed are dropped as they are encountered.
    static void refreshAll();

    /// Install the application-wide theme watcher. It listens for the
    /// APPLICATION-level QEvent::ApplicationPaletteChange / QEvent::ThemeChange
    /// and calls refreshAll().
    ///
    /// It must not listen for the per-widget QEvent::PaletteChange /
    /// QEvent::StyleChange: Qt sends those as a consequence of restyling a
    /// widget, which setting an icon does, so handling them closes a
    /// refresh -> setIcon -> StyleChange -> refresh loop that pegs the CPU and
    /// starves the event loop. See the note in ThemeWatcher.
    ///
    /// Safe to call more than once — only the first installs a filter. Call
    /// once at startup, after the QApplication exists.
    static void installThemeWatcher(QObject* app);
};

} // namespace calango::ui
