#include "gui/FilmTimelineWidget.hpp"

#include <QHBoxLayout>
#include <QLabel>
#include <QSlider>
#include <QStyle>
#include <QToolButton>

#include <algorithm>
#include <cmath>

namespace calango::gui {

FilmTimelineWidget::FilmTimelineWidget(QWidget* parent)
    : QWidget(parent)
    , rewindButton_(new QToolButton(this))
    , playButton_(new QToolButton(this))
    , endButton_(new QToolButton(this))
    , slider_(new QSlider(Qt::Horizontal, this))
    , timeLabel_(new QLabel(this))
{
    rewindButton_->setIcon(style()->standardIcon(QStyle::SP_MediaSkipBackward));
    rewindButton_->setToolTip(tr("Rewind to the start of the film"));
    playButton_->setIcon(style()->standardIcon(QStyle::SP_MediaPlay));
    playButton_->setCheckable(true);
    playButton_->setToolTip(tr("Play the film in the 3D viewport"));
    endButton_->setIcon(style()->standardIcon(QStyle::SP_MediaSkipForward));
    endButton_->setToolTip(tr("Jump to the last frame"));

    slider_->setToolTip(
        tr("Scrub the film. The camera, the cast opacities and — for a "
           "workspace with a trajectory — the displayed frame all follow the "
           "playhead."));

    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(6, 2, 6, 2);
    layout->addWidget(rewindButton_);
    layout->addWidget(playButton_);
    layout->addWidget(endButton_);
    layout->addWidget(slider_, 1);
    layout->addWidget(timeLabel_);

    connect(rewindButton_, &QToolButton::clicked, this,
            [this] { slider_->setValue(0); });
    connect(endButton_, &QToolButton::clicked, this,
            [this] { slider_->setValue(slider_->maximum()); });
    connect(playButton_, &QToolButton::toggled, this, [this](bool on) {
        playButton_->setIcon(style()->standardIcon(
            on ? QStyle::SP_MediaPause : QStyle::SP_MediaPlay));
        if (!on) {
            timer_.stop();
            return;
        }
        // Pressing play on the last frame means "again", not "do nothing".
        if (slider_->value() >= slider_->maximum())
            slider_->setValue(0);
        timer_.start();
    });
    connect(slider_, &QSlider::valueChanged, this, [this] {
        if (rescaling_)
            return;
        // A scrub or a timer tick moves the slider; the slider then defines
        // the time. (setFilm/setCurrentTime go the other way and set time_
        // first, which is why they suppress this handler.)
        time_ = frameCount_ > 1
            ? duration_ * static_cast<double>(slider_->value())
                / static_cast<double>(frameCount_ - 1)
            : 0.0;
        updateLabel();
        Q_EMIT timeChanged(time_);
    });
    connect(&timer_, &QTimer::timeout, this, &FilmTimelineWidget::advance);

    setFilm(duration_, fps_);
}

void FilmTimelineWidget::setFilm(double durationSeconds, int fps)
{
    const double previousDuration = duration_;
    duration_ = std::max(0.001, durationSeconds);
    fps_ = std::max(1, fps);
    const int frames =
        std::max(1, static_cast<int>(std::lround(duration_ * fps_)));

    // Hold the playhead's POSITION IN THE FILM, not its frame number: editing
    // the duration or the rate in the dialog re-ranges this slider on every
    // keystroke, and snapping back to the start each time would make the live
    // preview useless. The fraction comes from the stored TIME rather than
    // from the slider, so repeated re-ranges do not accumulate rounding.
    const double fraction =
        previousDuration > 0.0 ? std::clamp(time_ / previousDuration, 0.0, 1.0)
                               : 0.0;
    time_ = fraction * duration_;

    rescaling_ = true;
    frameCount_ = frames;
    slider_->setRange(0, frames - 1);
    slider_->setValue(std::clamp(
        static_cast<int>(std::lround(fraction * (frames - 1))), 0, frames - 1));
    rescaling_ = false;

    timer_.setInterval(std::max(1, static_cast<int>(std::lround(1000.0 / fps_))));
    updateLabel();
    Q_EMIT timeChanged(time_);
}

double FilmTimelineWidget::currentTime() const
{
    return time_;
}

void FilmTimelineWidget::setCurrentTime(double seconds)
{
    if (frameCount_ <= 1)
        return;
    const double fraction = std::clamp(seconds / duration_, 0.0, 1.0);
    time_ = fraction * duration_;
    // The slider is the quantized view; set it without letting its handler
    // round `time_` back off the exact value just requested.
    rescaling_ = true;
    slider_->setValue(
        static_cast<int>(std::lround(fraction * (frameCount_ - 1))));
    rescaling_ = false;
    updateLabel();
    Q_EMIT timeChanged(time_);
}

void FilmTimelineWidget::play()
{
    playButton_->setChecked(true);
}

void FilmTimelineWidget::stop()
{
    playButton_->setChecked(false);
    timer_.stop();
    setCurrentTime(0.0);
}

void FilmTimelineWidget::advance()
{
    // A film has an end, unlike a trajectory loop: it stops on the last frame
    // so the closing shot stays on screen, which is what anyone recording the
    // preview or presenting from it expects.
    if (slider_->value() >= slider_->maximum()) {
        playButton_->setChecked(false);
        return;
    }
    slider_->setValue(slider_->value() + 1);
}

void FilmTimelineWidget::updateLabel()
{
    timeLabel_->setText(tr("%1 / %2 s   (frame %3 / %4)")
                            .arg(time_, 0, 'f', 2)
                            .arg(duration_, 0, 'f', 2)
                            .arg(slider_->value() + 1)
                            .arg(frameCount_));
}

} // namespace calango::gui
