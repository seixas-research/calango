#pragma once

#include <QLabel>
#include <QPushButton>
#include <QSlider>
#include <QTimer>
#include <QWidget>

namespace calango::gui {

/// Play / pause / scrub controls for trajectory playback (MD or
/// optimization paths). Pure View: it only reports the requested frame
/// index; the controller owns the frames and swaps the model.
class TrajectoryPlayerWidget : public QWidget {
    Q_OBJECT

public:
    explicit TrajectoryPlayerWidget(QWidget* parent = nullptr);

    /// Resets to frame 0 (emits frameChanged(0)) and stops playback.
    void setFrameCount(int count);
    int currentFrame() const { return slider_->value(); }

public Q_SLOTS:
    void stop();

Q_SIGNALS:
    void frameChanged(int index);

private:
    void updateLabel();

    QPushButton* playButton_;
    QSlider* slider_;
    QLabel* frameLabel_;
    QTimer timer_;
    int frameCount_ = 0;
};

} // namespace calango::gui
