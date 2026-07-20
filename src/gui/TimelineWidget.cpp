#include "gui/TimelineWidget.hpp"

#include <QHBoxLayout>
#include <QStyle>

#include <algorithm>

namespace calango::gui {

namespace {
constexpr int kBaseIntervalMs = 66; // ~15 fps at 1x speed
} // namespace

TimelineWidget::TimelineWidget(QWidget* parent)
    : QWidget(parent)
    , firstButton_(new QToolButton(this))
    , prevButton_(new QToolButton(this))
    , playButton_(new QToolButton(this))
    , nextButton_(new QToolButton(this))
    , lastButton_(new QToolButton(this))
    , slider_(new QSlider(Qt::Horizontal, this))
    , speedCombo_(new QComboBox(this))
    , frameLabel_(new QLabel(this))
{
    firstButton_->setIcon(style()->standardIcon(QStyle::SP_MediaSkipBackward));
    prevButton_->setIcon(style()->standardIcon(QStyle::SP_MediaSeekBackward));
    playButton_->setIcon(style()->standardIcon(QStyle::SP_MediaPlay));
    playButton_->setCheckable(true);
    nextButton_->setIcon(style()->standardIcon(QStyle::SP_MediaSeekForward));
    lastButton_->setIcon(style()->standardIcon(QStyle::SP_MediaSkipForward));

    slider_->setTickPosition(QSlider::TicksBelow);

    speedCombo_->addItem(QStringLiteral("0.25×"), 0.25);
    speedCombo_->addItem(QStringLiteral("0.5×"), 0.5);
    speedCombo_->addItem(QStringLiteral("1×"), 1.0);
    speedCombo_->addItem(QStringLiteral("2×"), 2.0);
    speedCombo_->addItem(QStringLiteral("4×"), 4.0);
    speedCombo_->setCurrentIndex(2);

    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(6, 2, 6, 2);
    for (QToolButton* button :
         {firstButton_, prevButton_, playButton_, nextButton_, lastButton_})
        layout->addWidget(button);
    layout->addWidget(slider_, 1);
    layout->addWidget(frameLabel_);
    layout->addWidget(speedCombo_);

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

    connect(speedCombo_, &QComboBox::currentIndexChanged, this,
            [this](int) { applySpeed(); });

    applySpeed();
    setFrameCount(0);
}

void TimelineWidget::setFrameCount(int count)
{
    frameCount_ = count;
    stop();
    const bool usable = count > 1;
    for (QWidget* widget : std::initializer_list<QWidget*>{
             firstButton_, prevButton_, playButton_, nextButton_, lastButton_,
             slider_, speedCombo_})
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
    const double speed = speedCombo_->currentData().toDouble();
    timer_.setInterval(std::max(10, static_cast<int>(kBaseIntervalMs / speed)));
}

} // namespace calango::gui
