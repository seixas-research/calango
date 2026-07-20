#include "gui/TrajectoryPlayerWidget.hpp"

#include <QHBoxLayout>

namespace calango::gui {

TrajectoryPlayerWidget::TrajectoryPlayerWidget(QWidget* parent)
    : QWidget(parent)
    , playButton_(new QPushButton(tr("Play"), this))
    , slider_(new QSlider(Qt::Horizontal, this))
    , frameLabel_(new QLabel(this))
{
    playButton_->setCheckable(true);
    timer_.setInterval(66); // ~15 fps

    auto* layout = new QHBoxLayout(this);
    layout->addWidget(playButton_);
    layout->addWidget(slider_, 1);
    layout->addWidget(frameLabel_);

    connect(playButton_, &QPushButton::toggled, this, [this](bool playing) {
        playButton_->setText(playing ? tr("Pause") : tr("Play"));
        if (playing && frameCount_ > 1)
            timer_.start();
        else
            timer_.stop();
    });

    connect(&timer_, &QTimer::timeout, this, [this] {
        if (frameCount_ > 0)
            slider_->setValue((slider_->value() + 1) % frameCount_);
    });

    connect(slider_, &QSlider::valueChanged, this, [this](int value) {
        updateLabel();
        Q_EMIT frameChanged(value);
    });

    setFrameCount(0);
}

void TrajectoryPlayerWidget::setFrameCount(int count)
{
    frameCount_ = count;
    stop();
    const bool usable = count > 1;
    slider_->setEnabled(usable);
    playButton_->setEnabled(usable);
    slider_->setRange(0, std::max(0, count - 1));
    if (slider_->value() != 0)
        slider_->setValue(0); // emits frameChanged
    else if (count > 0)
        Q_EMIT frameChanged(0);
    updateLabel();
}

void TrajectoryPlayerWidget::stop()
{
    playButton_->setChecked(false);
}

void TrajectoryPlayerWidget::updateLabel()
{
    frameLabel_->setText(QStringLiteral("%1 / %2")
                             .arg(frameCount_ > 0 ? slider_->value() + 1 : 0)
                             .arg(frameCount_));
}

} // namespace calango::gui
