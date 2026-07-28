#pragma once

#include "render/Camera.hpp"

#include <QDialog>
#include <QString>

class QDoubleSpinBox;
class QLabel;
class QListWidget;
class QPushButton;

namespace calango::gui {

class ViewportWidget;

/// View toolbar → "Set point-of-view…": read out and edit the camera state
/// numerically, and keep a named library of views.
///
/// Two things a mouse cannot do. First, EXACT values: a figure that has to be
/// reproduced — the same orientation across a series of structures, or a
/// revision of a plot six months later — needs the angles typed, not dragged.
/// Second, RECALL: saved points-of-view persist in QSettings, so the same
/// framing can be applied to a different structure or after a restart.
///
/// Modeless and live: every control writes straight through to the viewport, so
/// the 3D view moves as the numbers change. That is the only way to tell
/// whether a typed angle is the one you meant.
class PointOfViewDialog : public QDialog {
    Q_OBJECT

public:
    explicit PointOfViewDialog(ViewportWidget* viewport,
                               QWidget* parent = nullptr);

public Q_SLOTS:
    /// Re-read the camera into the controls — after an orbit/pan/zoom done
    /// with the mouse while this dialog is open, or a workspace tab switch.
    void syncFromViewport();

public:
    /// Saved views live in QSettings under "pointOfView/<name>", encoded as a
    /// comma-separated tuple so the list survives a restart and can be moved
    /// between machines with the rest of the settings file.
    ///
    /// Public because the Film production dialog reads the same library to
    /// build its shots: one format, written in one place, so a film and a
    /// saved view can never disagree about what a camera state is.
    static QString encode(const render::PointOfView& pov);
    static render::PointOfView decode(const QString& text);
    /// QSettings group the saved views live under.
    static QString settingsGroup();

    /// The camera state "Reset camera" restores, read from
    /// SettingsManager::kDefaultPointOfView. Returns an INVALID point-of-view
    /// when no default has been set, which is the signal to auto-frame the
    /// structure instead — the behaviour the button has always had.
    static render::PointOfView defaultPointOfView();
    /// Persist `pov` as that default (and flush ~/.calango/settings.json).
    /// An invalid `pov` clears it.
    static void setDefaultPointOfView(const render::PointOfView& pov);

private Q_SLOTS:
    void applyToViewport();
    void saveCurrent();
    void loadSelected();
    void deleteSelected();
    /// "Set point-of-view as default" — store the camera on screen as the one
    /// the toolbar's Reset camera button restores, in ~/.calango/settings.json.
    void saveAsDefault();
    /// Drop that default, so Reset camera goes back to auto-framing.
    void clearDefault();

private:
    void refreshSavedList();
    /// Enable/disable the clear button and describe the stored default.
    void refreshDefaultState();

    ViewportWidget* viewport_;
    /// Guards applyToViewport() while syncFromViewport() fills the controls.
    bool syncing_ = false;

    QDoubleSpinBox* zoomSpin_ = nullptr;   ///< camera distance (Å)
    QDoubleSpinBox* yawSpin_ = nullptr;
    QDoubleSpinBox* pitchSpin_ = nullptr;
    QDoubleSpinBox* rollSpin_ = nullptr;   ///< camera tilt about the view axis
    QDoubleSpinBox* panSpin_[3] = {nullptr, nullptr, nullptr};
    QListWidget* savedList_ = nullptr;
    QPushButton* deleteButton_ = nullptr;
    QPushButton* clearDefaultButton_ = nullptr;
    QLabel* defaultLabel_ = nullptr; ///< what Reset camera currently restores
    QLabel* summaryLabel_ = nullptr;
};

} // namespace calango::gui
