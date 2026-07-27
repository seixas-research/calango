#include "ui/IconManager.hpp"

#include "gui/ThemeManager.hpp"

#include <QAbstractButton>
#include <QAction>
#include <QEvent>
#include <QFile>
#include <QGuiApplication>
#include <QLabel>
#include <QPainter>
#include <QPointer>
#include <QSvgRenderer>

#include <algorithm>
#include <vector>

namespace calango::ui {
namespace {

QString assetPath(const QString& name)
{
    return QStringLiteral(":/assets/icons/%1.svg").arg(name);
}

// Supersample factor: render the SVG a few times larger than the logical size
// and stamp a matching device-pixel-ratio, so a single cached pixmap stays
// crisp on 1x through 3x displays.
constexpr int kSupersample = 3;

} // namespace

QColor IconManager::color(State state)
{
    const bool dark =
        gui::ThemeManager::isEffectivelyDark(gui::ThemeManager::current());
    switch (state) {
    case State::Active:
        return dark ? QColor(0xE8, 0xEA, 0xED) : QColor(0x3C, 0x40, 0x43);
    case State::Disabled:
        return dark ? QColor(0x5F, 0x63, 0x68) : QColor(0xBD, 0xC1, 0xC6);
    case State::Hovered:
        return dark ? QColor(0xFF, 0xFF, 0xFF) : QColor(0x20, 0x21, 0x24);
    case State::Pressed:
        return dark ? QColor(0x8A, 0xB4, 0xF8) : QColor(0x1A, 0x73, 0xE8);
    }
    return dark ? QColor(0xE8, 0xEA, 0xED) : QColor(0x3C, 0x40, 0x43);
}

bool IconManager::has(const QString& name)
{
    return QFile::exists(assetPath(name));
}

QPixmap IconManager::pixmap(const QString& name, const QColor& color, int px)
{
    const QString path = assetPath(name);
    QSvgRenderer renderer(path);
    if (!renderer.isValid())
        return {};

    const int side = std::max(1, px) * kSupersample;
    QPixmap pm(side, side);
    pm.fill(Qt::transparent);
    {
        QPainter p(&pm);
        p.setRenderHint(QPainter::Antialiasing);
        p.setRenderHint(QPainter::SmoothPixmapTransform);
        renderer.render(&p, QRectF(0, 0, side, side));
        // RemixIcon glyphs paint as currentColor (black by default); keep only
        // their alpha shape and flood it with the requested tint.
        p.setCompositionMode(QPainter::CompositionMode_SourceIn);
        p.fillRect(pm.rect(), color);
    }
    pm.setDevicePixelRatio(kSupersample);
    return pm;
}

QIcon IconManager::icon(const QString& name, int px)
{
    QIcon result;
    const QPixmap active = pixmap(name, color(State::Active), px);
    if (active.isNull())
        return result; // missing asset — empty icon rather than a wrong glyph
    result.addPixmap(active, QIcon::Normal);
    result.addPixmap(pixmap(name, color(State::Disabled), px), QIcon::Disabled);
    // Qt uses the Active mode when a control is hovered/focused.
    result.addPixmap(pixmap(name, color(State::Hovered), px), QIcon::Active);
    // Selected (e.g. pressed/checked toolbuttons in some styles).
    result.addPixmap(pixmap(name, color(State::Pressed), px), QIcon::Selected);
    return result;
}

QIcon IconManager::icon(const QString& name, const QColor& color, int px)
{
    const QPixmap pm = pixmap(name, color, px);
    return pm.isNull() ? QIcon() : QIcon(pm);
}

// ---------------------------------------------------------------------------
// Theme-adaptive bindings
// ---------------------------------------------------------------------------

namespace {

/// One widget whose icon tracks the theme. QPointer so a destroyed widget
/// becomes null rather than dangling — the registry outlives individual
/// dialogs, and nothing un-registers on destruction.
struct Binding {
    QPointer<QObject> target;
    QString name;
    int px = 24;
};

std::vector<Binding>& bindings()
{
    static std::vector<Binding> registry;
    return registry;
}

/// Apply one binding. Returns false when its target is gone.
bool applyBinding(const Binding& binding)
{
    QObject* target = binding.target.data();
    if (!target)
        return false;
    if (auto* button = qobject_cast<QAbstractButton*>(target))
        button->setIcon(IconManager::icon(binding.name, binding.px));
    else if (auto* action = qobject_cast<QAction*>(target))
        action->setIcon(IconManager::icon(binding.name, binding.px));
    else if (auto* label = qobject_cast<QLabel*>(target))
        label->setPixmap(IconManager::pixmap(
            binding.name, IconManager::color(IconManager::State::Active),
            binding.px));
    return true;
}

void addBinding(QObject* target, const QString& name, int px)
{
    if (!target)
        return;
    // Re-binding the same widget replaces its entry rather than stacking a
    // second one, so a widget whose icon is swapped at runtime does not end up
    // with two bindings fighting on the next theme change.
    for (Binding& existing : bindings()) {
        if (existing.target.data() == target) {
            existing.name = name;
            existing.px = px;
            applyBinding(existing);
            return;
        }
    }
    bindings().push_back({target, name, px});
    applyBinding(bindings().back());
}

/// Watches the application for a genuine theme change and re-tints every bound
/// icon.
///
/// It deliberately does NOT listen for QEvent::PaletteChange or
/// QEvent::StyleChange. Those are per-widget events that Qt sends as a
/// CONSEQUENCE of restyling a widget — including the restyle that setting an
/// icon itself provokes. Handling them here closes a feedback loop:
///
///     refreshAll() -> setIcon() -> StyleChange -> refreshAll() -> ...
///
/// Measured on an idle startup that ran ~2960 refreshes in 12 seconds, each
/// re-rasterizing every bound SVG at 3x for four widget states. The result is
/// an application that is pegged at full CPU and whose event loop never gets
/// far enough to finish showing the menu bar.
///
/// ApplicationPaletteChange and ThemeChange are application-level and fire once
/// per actual theme switch, which is the signal that was wanted.
class ThemeWatcher : public QObject {
public:
    using QObject::QObject;

protected:
    bool eventFilter(QObject* watched, QEvent* event) override
    {
        switch (event->type()) {
        case QEvent::ApplicationPaletteChange:
        case QEvent::ThemeChange:
            IconManager::refreshAll();
            break;
        default:
            break;
        }
        return QObject::eventFilter(watched, event);
    }
};

} // namespace

void IconManager::bind(QAbstractButton* button, const QString& name, int px)
{
    addBinding(button, name, px);
}

void IconManager::bind(QAction* action, const QString& name, int px)
{
    addBinding(action, name, px);
}

void IconManager::bind(QLabel* label, const QString& name, int px)
{
    addBinding(label, name, px);
}

void IconManager::refreshAll()
{
    // Setting an icon restyles the widget, which Qt reports as a PaletteChange
    // / StyleChange event. Any caller reached from an event handler can
    // therefore re-enter here mid-sweep. The guard makes that harmless
    // regardless of how refreshAll() comes to be called — the loop above is
    // closed by not listening to those events, and this makes sure a future
    // caller cannot reopen it.
    static bool running = false;
    if (running)
        return;
    running = true;

    auto& registry = bindings();
    // Re-apply, compacting away the entries whose widget has been destroyed.
    registry.erase(
        std::remove_if(registry.begin(), registry.end(),
                       [](const Binding& b) { return !applyBinding(b); }),
        registry.end());

    running = false;
}

void IconManager::installThemeWatcher(QObject* app)
{
    static ThemeWatcher* watcher = nullptr;
    if (watcher || !app)
        return;
    watcher = new ThemeWatcher(app);
    app->installEventFilter(watcher);
}

} // namespace calango::ui
