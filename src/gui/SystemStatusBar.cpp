#include "gui/SystemStatusBar.hpp"

#include "ui/IconManager.hpp"

#include <QHBoxLayout>
#include <QLabel>
#include <QLinearGradient>
#include <QPainter>
#include <QSettings>
#include <QThread>
#include <QTimer>

#include <algorithm>

#ifdef __APPLE__
#include <mach/mach.h>
#include <sys/sysctl.h>
#endif

namespace calango::gui {

// ---------------------------------------------------------------------------
// MiniLoadBar
// ---------------------------------------------------------------------------
MiniLoadBar::MiniLoadBar(QWidget* parent)
    : QWidget(parent)
{
    setFixedSize(56, 12);
}

void MiniLoadBar::setValue(double percent)
{
    const double v = percent < 0.0 ? -1.0 : std::clamp(percent, 0.0, 100.0);
    if (qFuzzyCompare(v + 2.0, value_ + 2.0))
        return;
    value_ = v;
    update();
}

void MiniLoadBar::paintEvent(QPaintEvent*)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    const QRectF track = rect().adjusted(0, 0, -1, -1);

    // Black track background.
    painter.setPen(QColor(70, 70, 70));
    painter.setBrush(Qt::black);
    painter.drawRoundedRect(track, 2, 2);

    if (value_ <= 0.0)
        return; // 0 or "no data" — empty track

    // Green→yellow→red gradient, revealed proportionally to the load.
    QLinearGradient gradient(track.left(), 0, track.right(), 0);
    gradient.setColorAt(0.0, QColor(60, 200, 90));
    gradient.setColorAt(0.6, QColor(230, 200, 60));
    gradient.setColorAt(1.0, QColor(225, 70, 60));

    QRectF fill = track;
    fill.setWidth(track.width() * value_ / 100.0);
    painter.setPen(Qt::NoPen);
    painter.setBrush(gradient);
    painter.setClipRect(fill);
    painter.drawRoundedRect(track, 2, 2);
}

// ---------------------------------------------------------------------------
// SystemStatusBar
// ---------------------------------------------------------------------------
SystemStatusBar::SystemStatusBar(QWidget* parent)
    : QWidget(parent)
    , cpuBar_(new MiniLoadBar(this))
    , cpuLabel_(new QLabel(this))
    , ramBar_(new MiniLoadBar(this))
    , ramLabel_(new QLabel(this))
    , gpuBar_(new MiniLoadBar(this))
    , gpuLabel_(new QLabel(this))
    , vramBar_(new MiniLoadBar(this))
    , vramLabel_(new QLabel(this))
    , threadsLabel_(new QLabel(this))
    , timer_(new QTimer(this))
{
    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(6, 0, 6, 0);
    layout->setSpacing(6);

    auto* tag = new QLabel(tr("Calango:"), this);
    QFont bold = tag->font();
    bold.setBold(true);
    tag->setFont(bold);
    layout->addWidget(tag);

    // Theme-tinted RemixIcon in front of each indicator's name. Small (14 px)
    // to sit comfortably in the status bar; the text name is kept for clarity.
    const auto iconLabel = [this](const QString& remixName) {
        auto* label = new QLabel(this);
        label->setPixmap(ui::IconManager::pixmap(
            remixName, ui::IconManager::color(ui::IconManager::State::Active),
            /*px=*/14));
        return label;
    };
    const auto addMetric = [&](const QString& iconName, const QString& name,
                               MiniLoadBar* bar, QLabel* label,
                               const QString& tip) {
        auto* icon = iconLabel(iconName);
        icon->setToolTip(tip);
        layout->addWidget(icon);
        layout->addWidget(new QLabel(name, this));
        layout->addWidget(bar);
        layout->addWidget(label);
        bar->setToolTip(tip);
        label->setToolTip(tip);
    };
    addMetric(QStringLiteral("cpu-line"), tr("CPU"), cpuBar_, cpuLabel_,
              tr("Calango CPU usage"));
    addMetric(QStringLiteral("ram-line"), tr("RAM"), ramBar_, ramLabel_,
              tr("Calango memory (MB and % of system RAM)"));
    addMetric(QStringLiteral("computer-line"), tr("GPU"), gpuBar_, gpuLabel_,
              tr("Calango GPU utilization (when GPU execution is active)"));
    addMetric(QStringLiteral("hard-drive-2-line"), tr("VRAM"), vramBar_,
              vramLabel_, tr("Calango VRAM usage"));
    auto* threadsIcon = iconLabel(QStringLiteral("stack-line"));
    threadsIcon->setToolTip(tr("Active thread count (OMP_NUM_THREADS)"));
    layout->addWidget(threadsIcon);
    layout->addWidget(threadsLabel_);
    threadsLabel_->setToolTip(tr("Active thread count (OMP_NUM_THREADS)"));

    cpuLabel_->setText(tr("…"));
    ramLabel_->setText(tr("…"));
    // No per-process GPU/VRAM metric source on this platform (Metal exposes
    // none) — shown as N/A with an empty bar rather than faked.
    gpuBar_->setValue(-1.0);
    gpuLabel_->setText(tr("N/A"));
    vramBar_->setValue(-1.0);
    vramLabel_->setText(tr("N/A"));
    refreshThreads();

    procWallClock_.start();
    connect(timer_, &QTimer::timeout, this, &SystemStatusBar::refresh);
    timer_->start(1000); // strict 1.0 s sampling interval
    refresh();
}

void SystemStatusBar::refreshThreads()
{
    const int threads =
        QSettings().value(QStringLiteral("jobs/ompThreads"), 0).toInt();
    threadsLabel_->setText(threads > 0 ? tr("Threads: %1").arg(threads)
                                       : tr("Threads: auto"));
}

void SystemStatusBar::refresh()
{
    const double cpu = sampleProcessCpuPercent();
    cpuBar_->setValue(cpu);
    cpuLabel_->setText(cpu < 0.0 ? tr("N/A")
                                 : tr("%1%").arg(cpu, 0, 'f', 0));

    const double mem = sampleProcessMemoryMiB();
    const double total = systemMemoryTotalMiB();
    if (mem < 0.0) {
        ramBar_->setValue(-1.0);
        ramLabel_->setText(tr("N/A"));
    } else if (total > 0.0) {
        const double pct = 100.0 * mem / total;
        ramBar_->setValue(pct);
        ramLabel_->setText(tr("%1 MB (%2%)").arg(mem, 0, 'f', 0).arg(pct, 0, 'f', 1));
    } else {
        ramBar_->setValue(-1.0);
        ramLabel_->setText(tr("%1 MB").arg(mem, 0, 'f', 0));
    }

    refreshThreads();
}

double SystemStatusBar::sampleProcessCpuPercent()
{
#ifdef __APPLE__
    task_thread_times_info threadTimes;
    mach_msg_type_number_t c1 = TASK_THREAD_TIMES_INFO_COUNT;
    task_basic_info_data_t basic;
    mach_msg_type_number_t c2 = TASK_BASIC_INFO_COUNT;
    if (task_info(mach_task_self(), TASK_THREAD_TIMES_INFO,
                  reinterpret_cast<task_info_t>(&threadTimes), &c1)
            != KERN_SUCCESS
        || task_info(mach_task_self(), TASK_BASIC_INFO,
                     reinterpret_cast<task_info_t>(&basic), &c2)
            != KERN_SUCCESS)
        return -1.0;

    const auto toSec = [](const time_value_t& t) {
        return static_cast<double>(t.seconds)
            + static_cast<double>(t.microseconds) / 1.0e6;
    };
    const double cpuSeconds = toSec(threadTimes.user_time)
        + toSec(threadTimes.system_time) + toSec(basic.user_time)
        + toSec(basic.system_time);

    const double wallSeconds = procWallClock_.restart() / 1000.0;
    double percent = 0.0;
    if (prevProcCpuSeconds_ >= 0.0 && wallSeconds > 0.0)
        percent = 100.0 * (cpuSeconds - prevProcCpuSeconds_) / wallSeconds;
    prevProcCpuSeconds_ = cpuSeconds;
    return percent < 0.0 ? 0.0 : percent;
#else
    return -1.0;
#endif
}

double SystemStatusBar::sampleProcessMemoryMiB()
{
#ifdef __APPLE__
    mach_task_basic_info_data_t info;
    mach_msg_type_number_t count = MACH_TASK_BASIC_INFO_COUNT;
    if (task_info(mach_task_self(), MACH_TASK_BASIC_INFO,
                  reinterpret_cast<task_info_t>(&info), &count)
        != KERN_SUCCESS)
        return -1.0;
    return static_cast<double>(info.resident_size) / (1024.0 * 1024.0);
#else
    return -1.0;
#endif
}

double SystemStatusBar::systemMemoryTotalMiB()
{
#ifdef __APPLE__
    uint64_t memSize = 0;
    size_t len = sizeof(memSize);
    if (sysctlbyname("hw.memsize", &memSize, &len, nullptr, 0) != 0)
        return 0.0;
    return static_cast<double>(memSize) / (1024.0 * 1024.0);
#else
    return 0.0;
#endif
}

} // namespace calango::gui
