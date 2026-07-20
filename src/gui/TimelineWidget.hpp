#pragma once

#include <QComboBox>
#include <QLabel>
#include <QSlider>
#include <QTimer>
#include <QToolButton>
#include <QWidget>

namespace calango::gui {

/// Interactive playback timeline for MD / optimization trajectories,
/// embedded below the 3D viewport (above the job console). Transport
/// controls (first / previous / play-pause / next / last), a tick-marked
/// scrubber and a playback-speed selector. Pure View: it only reports the
/// requested frame index; the controller owns the frames.
class TimelineWidget : public QWidget {
    Q_OBJECT

public:
    explicit TimelineWidget(QWidget* parent = nullptr);

    /// Resets to frame 0 (emits frameChanged(0)) and stops playback.
    void setFrameCount(int count);
    int currentFrame() const { return slider_->value(); }

public Q_SLOTS:
    void stop();

Q_SIGNALS:
    void frameChanged(int index);

private:
    void step(int delta);
    void updateLabel();
    void applySpeed();

    QToolButton* firstButton_;
    QToolButton* prevButton_;
    QToolButton* playButton_;
    QToolButton* nextButton_;
    QToolButton* lastButton_;
    QSlider* slider_;
    QComboBox* speedCombo_;
    QLabel* frameLabel_;
    QTimer timer_;
    int frameCount_ = 0;
};

} // namespace calango::gui
