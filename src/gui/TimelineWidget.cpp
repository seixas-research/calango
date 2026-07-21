#include "gui/TimelineWidget.hpp"

#include <QHBoxLayout>
#include <QStyle>

#include <algorithm>
#include <cmath>

namespace calango::gui {

TimelineWidget::TimelineWidget(QWidget* parent)
    : QWidget(parent)
    , firstButton_(new QToolButton(this))
    , prevButton_(new QToolButton(this))
    , playButton_(new QToolButton(this))
    , nextButton_(new QToolButton(this))
    , lastButton_(new QToolButton(this))
    , slider_(new QSlider(Qt::Horizontal, this))
    , fpsSpin_(new QDoubleSpinBox(this))
    , frameLabel_(new QLabel(this))
{
    firstButton_->setIcon(style()->standardIcon(QStyle::SP_MediaSkipBackward));
    prevButton_->setIcon(style()->standardIcon(QStyle::SP_MediaSeekBackward));
    playButton_->setIcon(style()->standardIcon(QStyle::SP_MediaPlay));
    playButton_->setCheckable(true);
    nextButton_->setIcon(style()->standardIcon(QStyle::SP_MediaSeekForward));
    lastButton_->setIcon(style()->standardIcon(QStyle::SP_MediaSkipForward));

    slider_->setTickPosition(QSlider::TicksBelow);

    // Exact numeric playback rate (typed or stepped), instead of a fixed
    // set of speed multipliers.
    fpsSpin_->setRange(0.1, 120.0);
    fpsSpin_->setDecimals(1);
    fpsSpin_->setValue(15.0);
    fpsSpin_->setSuffix(tr(" fps"));
    fpsSpin_->setToolTip(tr("Playback rate in frames per second"));

    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(6, 2, 6, 2);
    for (QToolButton* button :
         {firstButton_, prevButton_, playButton_, nextButton_, lastButton_})
        layout->addWidget(button);
    layout->addWidget(slider_, 1);
    layout->addWidget(frameLabel_);
    layout->addWidget(fpsSpin_);

    connect(firstButton_, &QToolButton::clicked, this, [this] { slider_->setValue(0); });
    connect(prevButton_, &QToolButton::clicked, this, [this] { step(-1); });
    connect(nextButton_, &QToolButton::clicked, this, [this] { step(+1); });
    connect(lastButton_, &QToolButton::clicked, this,
            [this] { slider_->setValue(slider_->maximum()); });

    connect(playButton_, &QToolButton::toggled, this, [this](bool playing) {
        playButton_->setIcon(style()->standardIcon(
            playing ? QStyle::SP_MediaPause : QStyle::SP_MediaPlay));
        if (playing && frameCount_ > 1)
            timer_.start();
        else
            timer_.stop();
    });

    connect(&timer_, &QTimer::timeout, this, [this] { step(+1); }); // wraps around

    connect(slider_, &QSlider::valueChanged, this, [this](int value) {
        updateLabel();
        Q_EMIT frameChanged(value);
    });

    connect(fpsSpin_, &QDoubleSpinBox::valueChanged, this,
            [this](double) { applySpeed(); });

    applySpeed();
    setFrameCount(0);
}

void TimelineWidget::extendFrameCount(int count)
{
    frameCount_ = count;
    const bool usable = count > 1;
    for (QWidget* widget : std::initializer_list<QWidget*>{
             firstButton_, prevButton_, playButton_, nextButton_, lastButton_,
             slider_, fpsSpin_})
        widget->setEnabled(usable);
    slider_->setRange(0, std::max(0, count - 1));
    slider_->setTickInterval(std::max(1, count / 20));
    updateLabel();
}

void TimelineWidget::setFrameCount(int count)
{
    frameCount_ = count;
    stop();
    const bool usable = count > 1;
    for (QWidget* widget : std::initializer_list<QWidget*>{
             firstButton_, prevButton_, playButton_, nextButton_, lastButton_,
             slider_, fpsSpin_})
        widget->setEnabled(usable);

    slider_->setRange(0, std::max(0, count - 1));
    slider_->setTickInterval(std::max(1, count / 20));
    if (slider_->value() != 0)
        slider_->setValue(0); // emits frameChanged
    else if (count > 0)
        Q_EMIT frameChanged(0);
    updateLabel();
}

void TimelineWidget::stop()
{
    playButton_->setChecked(false);
}

void TimelineWidget::step(int delta)
{
    if (frameCount_ < 1)
        return;
    slider_->setValue((slider_->value() + delta + frameCount_) % frameCount_);
}

void TimelineWidget::updateLabel()
{
    frameLabel_->setText(QStringLiteral("%1 / %2")
                             .arg(frameCount_ > 0 ? slider_->value() + 1 : 0)
                             .arg(frameCount_));
}

void TimelineWidget::applySpeed()
{
    timer_.setInterval(
        std::max(8, static_cast<int>(std::lround(1000.0 / fpsSpin_->value()))));
}

} // namespace calango::gui
