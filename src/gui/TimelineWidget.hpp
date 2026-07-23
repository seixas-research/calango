#pragma once

#include <QCheckBox>
#include <QDoubleSpinBox>
#include <QLabel>
#include <QSlider>
#include <QTimer>
#include <QToolButton>
#include <QWidget>

namespace calango::gui {

/// Interactive playback timeline for MD / optimization trajectories,
/// embedded below the 3D viewport (above the job console). Transport
/// controls (first / previous / play-pause / next / last), a tick-marked
/// scrubber, a loop toggle and a playback-speed selector. Pure View: it only
/// reports the requested frame index; the controller owns the frames.
class TimelineWidget : public QWidget {
    Q_OBJECT

public:
    explicit TimelineWidget(QWidget* parent = nullptr);

    /// Resets to frame 0 (emits frameChanged(0)) and stops playback.
    void setFrameCount(int count);
    /// Live-streaming growth: extends the range without resetting the
    /// playhead or stopping playback (new frames keep arriving).
    void extendFrameCount(int count);
    int currentFrame() const { return slider_->value(); }
    /// Programmatic jump (emits frameChanged like a user scrub).
    void setCurrentFrame(int index) { slider_->setValue(index); }

public Q_SLOTS:
    void stop();

Q_SIGNALS:
    void frameChanged(int index);

private:
    /// Manual transport step (next / previous buttons): always wraps, so the
    /// user can walk off either end of the trajectory and come back.
    void step(int delta);
    /// One playback timer tick. Unlike step() this honors the loop toggle:
    /// with looping off it stops playback on the last frame instead of
    /// wrapping to frame 0.
    void advancePlayback();
    void updateLabel();
    void applySpeed();

    QToolButton* firstButton_;
    QToolButton* prevButton_;
    QToolButton* playButton_;
    QToolButton* nextButton_;
    QToolButton* lastButton_;
    QSlider* slider_;
    QCheckBox* loopCheck_;    ///< restart at frame 0 after the last frame
    QDoubleSpinBox* fpsSpin_; ///< exact numeric playback rate
    QLabel* frameLabel_;
    QTimer timer_;
    int frameCount_ = 0;
};

} // namespace calango::gui
