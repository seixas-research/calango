#pragma once

#include <QTimer>
#include <QWidget>

class QLabel;
class QSlider;
class QToolButton;

namespace calango::gui {

/// Playback timeline for Film mode, sitting below the 3D viewport in place of
/// the trajectory timeline.
///
/// Deliberately NOT the trajectory's TimelineWidget. That one counts frames of
/// a simulation, which is the natural unit when the frames ARE the data. A
/// film is authored in SECONDS — the duration is the number that has to fit a
/// slide or a talk — and its frame count is a consequence of the duration and
/// the chosen rate rather than a property of anything. Showing seconds where
/// the user typed seconds is the whole point.
///
/// While film mode is on, this timeline is also what drives the trajectory:
/// the film's priority setting decides how the two lengths are reconciled, so
/// only one scrubber may be in charge at a time.
///
/// Pure View: it reports the requested time and owns no film.
class FilmTimelineWidget : public QWidget {
    Q_OBJECT

public:
    explicit FilmTimelineWidget(QWidget* parent = nullptr);

    /// Re-range the scrubber. Keeps the playhead where it is (as a fraction of
    /// the film) so editing the duration in the dialog does not jump the
    /// preview back to the start on every keystroke.
    void setFilm(double durationSeconds, int fps);

    double currentTime() const;
    bool isPlaying() const { return timer_.isActive(); }

public Q_SLOTS:
    /// Stop playback and rewind to 0.
    void stop();
    /// Start playing from the current position (from 0 if already at the end).
    void play();
    void setCurrentTime(double seconds);

Q_SIGNALS:
    /// The playhead moved — by a scrub, a transport button or a timer tick.
    void timeChanged(double seconds);

private:
    void advance();
    void updateLabel();

    QToolButton* rewindButton_;
    QToolButton* playButton_;
    QToolButton* endButton_;
    QSlider* slider_;   ///< in film frames, so a scrub lands on a real frame
    QLabel* timeLabel_;
    QTimer timer_;

    double duration_ = 10.0;
    int fps_ = 30;
    int frameCount_ = 300;
    /// The playhead, in seconds — the source of truth, with the slider as a
    /// quantized VIEW of it. Storing only the slider's integer frame would
    /// re-quantize the position on every re-range, and the production dialog
    /// re-ranges this on every keystroke: a duration typed one digit at a
    /// time would walk the playhead a frame at a time as a side effect.
    double time_ = 0.0;
    /// Guards the slider handler while setFilm() re-ranges it.
    bool rescaling_ = false;
};

} // namespace calango::gui
