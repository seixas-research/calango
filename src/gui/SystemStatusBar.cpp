#include "gui/SystemStatusBar.hpp"

#include <QFrame>
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
#include <mach/mach_host.h>
#include <mach/mach_time.h>
#include <mach/processor_info.h>
#include <sys/sysctl.h>
#endif

namespace calango::gui {

// ---------------------------------------------------------------------------
// MiniLoadBar
// ---------------------------------------------------------------------------
MiniLoadBar::MiniLoadBar(QWidget* parent)
    : QWidget(parent)
{
    setFixedSize(64, 12);
    setToolTip(tr("Total system CPU load"));
}

void MiniLoadBar::setValue(double percent)
{
    const double clamped = std::clamp(percent, 0.0, 100.0);
    if (qFuzzyCompare(clamped + 1.0, value_ + 1.0))
        return;
    value_ = clamped;
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
        return;

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
    , appCpuLabel_(new QLabel(this))
    , appMemLabel_(new QLabel(this))
    , threadsLabel_(new QLabel(this))
    , cpuBar_(new MiniLoadBar(this))
    , sysCpuLabel_(new QLabel(this))
    , sysMemLabel_(new QLabel(this))
    , gpuLabel_(new QLabel(this))
    , vramLabel_(new QLabel(this))
    , timer_(new QTimer(this))
{
    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(6, 0, 6, 0);
    layout->setSpacing(8);

    const auto groupTag = [this](const QString& text) {
        auto* tag = new QLabel(text, this);
        QFont f = tag->font();
        f.setBold(true);
        tag->setFont(f);
        return tag;
    };
    const auto separator = [this] {
        auto* line = new QFrame(this);
        line->setFrameShape(QFrame::VLine);
        line->setFrameShadow(QFrame::Sunken);
        return line;
    };

    // Group 1 — Calango application metrics.
    layout->addWidget(groupTag(tr("Calango:")));
    layout->addWidget(appCpuLabel_);
    layout->addWidget(appMemLabel_);
    layout->addWidget(threadsLabel_);
    layout->addWidget(separator());
    // Group 2 — host machine metrics.
    layout->addWidget(groupTag(tr("Host:")));
    layout->addWidget(cpuBar_);
    layout->addWidget(sysCpuLabel_);
    layout->addWidget(sysMemLabel_);
    layout->addWidget(gpuLabel_);
    layout->addWidget(vramLabel_);

    appCpuLabel_->setToolTip(tr("Calango CPU usage"));
    appMemLabel_->setToolTip(tr("Calango resident memory"));
    threadsLabel_->setToolTip(
        tr("Active OMP_NUM_THREADS vs. total CPU cores"));
    sysCpuLabel_->setToolTip(tr("Total system CPU usage"));
    sysMemLabel_->setToolTip(tr("System used / total RAM"));
    gpuLabel_->setToolTip(tr("GPU utilization (if a metric source exists)"));
    vramLabel_->setToolTip(tr("VRAM used / total (if a metric source exists)"));

    appCpuLabel_->setText(tr("CPU: …"));
    appMemLabel_->setText(tr("RAM: …"));
    sysCpuLabel_->setText(tr("CPU: …"));
    sysMemLabel_->setText(tr("RAM: …"));
    gpuLabel_->setText(tr("GPU: N/A"));
    vramLabel_->setText(tr("VRAM: N/A"));
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
    const int cores = std::max(1, QThread::idealThreadCount());
    const QString configured =
        threads > 0 ? QString::number(threads) : tr("auto");
    threadsLabel_->setText(tr("Threads: %1 / %2 Cores").arg(configured).arg(cores));
}

void SystemStatusBar::refresh()
{
    // -- Group 1: Calango application metrics -------------------------------
    const double procCpu = sampleProcessCpuPercent();
    appCpuLabel_->setText(procCpu < 0.0 ? tr("CPU: N/A")
                                        : tr("CPU: %1%").arg(procCpu, 0, 'f', 0));
    const double appMem = sampleProcessMemoryMiB();
    appMemLabel_->setText(appMem < 0.0 ? tr("RAM: N/A")
                                       : tr("RAM: %1 MB").arg(appMem, 0, 'f', 0));
    refreshThreads();

    // -- Group 2: host machine metrics --------------------------------------
    const double sysCpu = sampleSystemCpuPercent();
    cpuBar_->setValue(sysCpu < 0.0 ? 0.0 : sysCpu);
    sysCpuLabel_->setText(sysCpu < 0.0 ? tr("CPU: …")
                                       : tr("CPU: %1%").arg(sysCpu, 0, 'f', 0));

    double sysUsed = 0.0, sysTotal = 0.0;
    if (sampleSystemMemoryMiB(sysUsed, sysTotal))
        sysMemLabel_->setText(tr("RAM: %1 / %2 MB")
                                  .arg(sysUsed, 0, 'f', 0)
                                  .arg(sysTotal, 0, 'f', 0));
    else
        sysMemLabel_->setText(tr("RAM: N/A"));

    // GPU/VRAM have no portable per-process metric source on this platform
    // (Metal exposes no utilization API); shown as N/A rather than faked.
}

double SystemStatusBar::sampleSystemCpuPercent()
{
#ifdef __APPLE__
    natural_t cpuCount = 0;
    processor_info_array_t info = nullptr;
    mach_msg_type_number_t infoCount = 0;
    if (host_processor_info(mach_host_self(), PROCESSOR_CPU_LOAD_INFO, &cpuCount,
                            &info, &infoCount)
        != KERN_SUCCESS)
        return -1.0;

    unsigned long long busy = 0, total = 0;
    auto* ticks = reinterpret_cast<processor_cpu_load_info_t>(info);
    for (natural_t i = 0; i < cpuCount; ++i) {
        const auto& c = ticks[i].cpu_ticks;
        const unsigned long long b = static_cast<unsigned long long>(c[CPU_STATE_USER])
            + c[CPU_STATE_SYSTEM] + c[CPU_STATE_NICE];
        busy += b;
        total += b + c[CPU_STATE_IDLE];
    }
    vm_deallocate(mach_task_self(), reinterpret_cast<vm_address_t>(info),
                  infoCount * sizeof(int));

    double percent = -1.0;
    if (prevCpuTotal_ != 0 && total > prevCpuTotal_) {
        const double dBusy = static_cast<double>(busy - prevCpuBusy_);
        const double dTotal = static_cast<double>(total - prevCpuTotal_);
        percent = dTotal > 0.0 ? 100.0 * dBusy / dTotal : 0.0;
    }
    prevCpuBusy_ = busy;
    prevCpuTotal_ = total;
    return percent;
#else
    return -1.0;
#endif
}

double SystemStatusBar::sampleProcessCpuPercent()
{
#ifdef __APPLE__
    // CPU seconds consumed by this process = live threads (thread-times) plus
    // already-terminated threads (basic info).
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
    double percent = -1.0;
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

bool SystemStatusBar::sampleSystemMemoryMiB(double& used, double& total)
{
#ifdef __APPLE__
    uint64_t memSize = 0;
    size_t len = sizeof(memSize);
    if (sysctlbyname("hw.memsize", &memSize, &len, nullptr, 0) != 0)
        return false;
    total = static_cast<double>(memSize) / (1024.0 * 1024.0);

    vm_size_t pageSize = 0;
    vm_statistics64_data_t vmStats;
    mach_msg_type_number_t count = HOST_VM_INFO64_COUNT;
    if (host_page_size(mach_host_self(), &pageSize) != KERN_SUCCESS
        || host_statistics64(mach_host_self(), HOST_VM_INFO64,
                             reinterpret_cast<host_info64_t>(&vmStats), &count)
            != KERN_SUCCESS)
        return false;

    // "Used" ≈ active + wired + compressed (free/inactive are reclaimable).
    const double usedPages = static_cast<double>(vmStats.active_count)
        + vmStats.wire_count + vmStats.compressor_page_count;
    used = usedPages * static_cast<double>(pageSize) / (1024.0 * 1024.0);
    return true;
#else
    (void)used;
    (void)total;
    return false;
#endif
}

} // namespace calango::gui
