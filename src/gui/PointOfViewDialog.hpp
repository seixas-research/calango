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

private Q_SLOTS:
    void applyToViewport();
    void saveCurrent();
    void loadSelected();
    void deleteSelected();

private:
    void refreshSavedList();
    /// Saved views live in QSettings under "pointOfView/<name>", encoded as a
    /// comma-separated tuple so the list survives a restart and can be moved
    /// between machines with the rest of the settings file.
    static QString encode(const render::PointOfView& pov);
    static render::PointOfView decode(const QString& text);

    ViewportWidget* viewport_;
    /// Guards applyToViewport() while syncFromViewport() fills the controls.
    bool syncing_ = false;

    QDoubleSpinBox* zoomSpin_ = nullptr;   ///< camera distance (Å)
    QDoubleSpinBox* yawSpin_ = nullptr;
    QDoubleSpinBox* pitchSpin_ = nullptr;
    QDoubleSpinBox* rollSpin_ = nullptr;   ///< scene rotation about the view axis
    QDoubleSpinBox* panSpin_[3] = {nullptr, nullptr, nullptr};
    QListWidget* savedList_ = nullptr;
    QPushButton* deleteButton_ = nullptr;
    QLabel* summaryLabel_ = nullptr;
};

} // namespace calango::gui
