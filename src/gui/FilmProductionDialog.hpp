#pragma once

#include "render/Film.hpp"

#include <QDialog>

class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QListWidget;
class QPushButton;
class QSpinBox;
class QTableWidget;

namespace calango::gui {

class ViewportWidget;

/// View toolbar → "Film production…": turn the saved points-of-view into a
/// film, and preview it in the 3D viewport.
///
/// The unit of authoring here is the SHOT — a saved camera position plus what
/// the casts look like while the camera is there — and the film is the list of
/// them plus a duration. Everything else (frame count, per-shot timing, how
/// far the camera has travelled at 3.2 s) is derived, because those are the
/// numbers nobody wants to compute by hand and everybody wants to be exact.
///
/// Modeless and live, like the point-of-view dialog it feeds from: every edit
/// republishes the script, so the viewport and the film timeline update as the
/// film is built. Watching the transition is the only way to judge it.
class FilmProductionDialog : public QDialog {
    Q_OBJECT

public:
    FilmProductionDialog(ViewportWidget* viewport, render::FilmScript script,
                         QWidget* parent = nullptr);

    /// The script as currently edited.
    const render::FilmScript& script() const { return script_; }

public Q_SLOTS:
    /// The host's trajectory changed (tab switch, loaded result): re-couple
    /// the priority controls to it. Zero frames disables them entirely.
    void setTrajectory(int frameCount, double fps);
    /// Show a different film — the incoming tab's, on a workspace switch.
    /// Films are per workspace, so without this the dialog would keep editing
    /// (and republishing) the film of the tab it was opened on, overwriting
    /// whatever the tab now in front had.
    void setScript(const render::FilmScript& script);

Q_SIGNALS:
    /// The film was edited. The host stores it on the document and re-ranges
    /// the film timeline.
    void scriptChanged(const render::FilmScript& script);
    /// "Preview" — start playback from the beginning in the main viewport.
    void previewRequested();

private Q_SLOTS:
    void addShotFromSaved();
    void addShotFromCurrentView();
    void removeShot();
    void moveShot(int delta);
    void shotSelectionChanged();
    void applyShotEdits();
    void applyTiming();

private:
    void refreshSavedList();
    void refreshShotList();
    void refreshCastTable();
    void updateSummary();
    /// Push the current script out and refresh the derived read-outs.
    void publish();
    int selectedShot() const;

    ViewportWidget* viewport_;
    render::FilmScript script_;
    /// Guards the control handlers while the dialog fills them in.
    bool loading_ = false;

    QListWidget* savedList_;   ///< saved points-of-view to draw shots from
    QListWidget* shotList_;    ///< the film, in order
    QPushButton* removeButton_;
    QPushButton* upButton_;
    QPushButton* downButton_;
    QComboBox* transitionCombo_;
    QTableWidget* castTable_;  ///< per-shot cast opacity keyframes
    QLabel* castNote_;

    QDoubleSpinBox* durationSpin_;
    QSpinBox* fpsSpin_;
    QComboBox* priorityCombo_;
    QLabel* priorityNote_;
    QLabel* summaryLabel_;
};

} // namespace calango::gui
