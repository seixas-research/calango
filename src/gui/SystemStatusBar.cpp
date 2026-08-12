#include "gui/SystemStatusBar.hpp"

#include "jobs/JobRunner.hpp"
#include "ui/IconManager.hpp"

#include <QHBoxLayout>
#include <QLabel>
#include <QLinearGradient>
#include <QPainter>
#include <QSettings>
#include <QThread>
#include <QTimer>

#include <algorithm>
#include <cstdint>

#ifdef __APPLE__
#include <libproc.h>
#include <mach/mach.h>
#include <mach/mach_time.h>
#include <sys/proc_info.h>
#include <sys/sysctl.h>
#endif

#include <initializer_list>
#include <vector>

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
    , jobGroup_(new QWidget(this))
    , jobNameLabel_(new QLabel(this))
    , jobCpuBar_(new MiniLoadBar(this))
    , jobCpuLabel_(new QLabel(this))
    , jobRamBar_(new MiniLoadBar(this))
    , jobRamLabel_(new QLabel(this))
    , jobElapsedLabel_(new QLabel(this))
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

    // -- Background job ----------------------------------------------------
    // Its own group, hidden while nothing is running: an always-present row of
    // dashes would be four more things to read past on an idle window, and the
    // point of the group is that its presence means "something is running".
    auto* jobLayout = new QHBoxLayout(jobGroup_);
    jobLayout->setContentsMargins(0, 0, 0, 0);
    jobLayout->setSpacing(6);
    auto* separator = new QLabel(QStringLiteral("│"), jobGroup_);
    separator->setEnabled(false);
    jobLayout->addWidget(separator);
    auto* jobTag = new QLabel(tr("Job:"), jobGroup_);
    jobTag->setFont(bold);
    jobLayout->addWidget(jobTag);
    jobLayout->addWidget(jobNameLabel_);
    jobLayout->addWidget(iconLabel(QStringLiteral("cpu-line")));
    jobLayout->addWidget(jobCpuBar_);
    jobLayout->addWidget(jobCpuLabel_);
    jobLayout->addWidget(iconLabel(QStringLiteral("ram-line")));
    jobLayout->addWidget(jobRamBar_);
    jobLayout->addWidget(jobRamLabel_);
    jobLayout->addWidget(iconLabel(QStringLiteral("time-line")));
    jobLayout->addWidget(jobElapsedLabel_);
    layout->addWidget(jobGroup_);
    jobGroup_->hide();

    const QString jobTip =
        tr("The background calculation Calango launched, summed over its whole "
           "process tree.\n\n"
           "The tree rather than the process itself: jobs run through a shell "
           "and the compute usually lives further down (mpirun → gpaw), so "
           "sampling only what Calango started would report a few percent for "
           "a machine running flat out.");
    for (QWidget* w : std::initializer_list<QWidget*>{
             jobNameLabel_, jobCpuBar_, jobCpuLabel_, jobRamBar_, jobRamLabel_,
             jobElapsedLabel_})
        w->setToolTip(jobTip);

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
    refreshJob();
}

void SystemStatusBar::setJobRunner(jobs::JobRunner* runner)
{
    runner_ = runner;
    refreshJob();
}

void SystemStatusBar::refreshJob()
{
    const qint64 pid = runner_ ? runner_->processId() : 0;
    if (pid == 0) {
        jobGroup_->hide();
        jobPid_ = 0;
        prevJobCpuSeconds_ = -1.0;
        return;
    }

    if (pid != jobPid_) {
        // A different job: start its accounting fresh, or the first sample
        // would be this job's total minus the previous one's.
        jobPid_ = pid;
        prevJobCpuSeconds_ = -1.0;
        jobWallClock_.start();
        jobElapsed_.start();
        jobNameLabel_->setText(runner_->description());
    }
    jobGroup_->show();

    double cpuSeconds = 0.0;
    double residentMiB = 0.0;
    if (!sampleTree(pid, cpuSeconds, residentMiB)) {
        jobCpuBar_->setValue(-1.0);
        jobCpuLabel_->setText(tr("N/A"));
        jobRamBar_->setValue(-1.0);
        jobRamLabel_->setText(tr("N/A"));
    } else {
        const double wallSeconds = jobWallClock_.restart() / 1000.0;
        double percent = 0.0;
        if (prevJobCpuSeconds_ >= 0.0 && wallSeconds > 0.0)
            percent = 100.0 * (cpuSeconds - prevJobCpuSeconds_) / wallSeconds;
        prevJobCpuSeconds_ = cpuSeconds;
        percent = std::max(0.0, percent);
        // Scaled by the core count, so a bar that is full means "using the
        // whole machine" rather than "using one core". The printed number
        // stays the Activity-Monitor-style sum across cores, which is what a
        // user comparing against the system monitor expects to see.
        const double cores = std::max(1, QThread::idealThreadCount());
        jobCpuBar_->setValue(percent / cores);
        jobCpuLabel_->setText(tr("%1%").arg(percent, 0, 'f', 0));

        const double total = systemMemoryTotalMiB();
        if (total > 0.0) {
            jobRamBar_->setValue(100.0 * residentMiB / total);
            jobRamLabel_->setText(tr("%1 MB").arg(residentMiB, 0, 'f', 0));
        } else {
            jobRamBar_->setValue(-1.0);
            jobRamLabel_->setText(tr("%1 MB").arg(residentMiB, 0, 'f', 0));
        }
    }

    const qint64 seconds = jobElapsed_.elapsed() / 1000;
    jobElapsedLabel_->setText(
        QStringLiteral("%1:%2:%3")
            .arg(seconds / 3600, 2, 10, QLatin1Char('0'))
            .arg((seconds / 60) % 60, 2, 10, QLatin1Char('0'))
            .arg(seconds % 60, 2, 10, QLatin1Char('0')));
}

bool SystemStatusBar::sampleTree(qint64 root, double& cpuSeconds,
                                 double& residentMiB)
{
    cpuSeconds = 0.0;
    residentMiB = 0.0;
#ifdef __APPLE__
    // One sysctl gives every process with its parent, from which the subtree
    // under `root` is assembled. Cheaper than shelling out to ps once a
    // second, and it does not spawn a process to measure processes.
    int name[4] = {CTL_KERN, KERN_PROC, KERN_PROC_ALL, 0};
    std::size_t length = 0;
    if (sysctl(name, 3, nullptr, &length, nullptr, 0) != 0 || length == 0)
        return false;
    // The table can grow between sizing and reading; the slack absorbs that.
    length += 16 * sizeof(kinfo_proc);
    std::vector<char> buffer(length);
    if (sysctl(name, 3, buffer.data(), &length, nullptr, 0) != 0)
        return false;

    const auto count = length / sizeof(kinfo_proc);
    const auto* table = reinterpret_cast<const kinfo_proc*>(buffer.data());

    // Walk down from the root, adding children level by level. Process counts
    // here are in the hundreds, so the repeated sweep is cheaper than building
    // an index.
    std::vector<pid_t> tree{static_cast<pid_t>(root)};
    for (std::size_t scanned = 0; scanned < tree.size();) {
        const std::size_t frontier = tree.size();
        for (std::size_t i = scanned; i < frontier; ++i) {
            for (std::size_t k = 0; k < count; ++k) {
                const pid_t pid = table[k].kp_proc.p_pid;
                if (table[k].kp_eproc.e_ppid != tree[i] || pid == tree[i])
                    continue;
                if (std::find(tree.begin(), tree.end(), pid) == tree.end())
                    tree.push_back(pid);
            }
        }
        scanned = frontier;
    }

    // PROC_PIDTASKINFO, not proc_pid_rusage(): the rusage counters under-report
    // another process's CPU by more than an order of magnitude (measured: a
    // pegged core came back as 2%), while pti_total_user/system are the same
    // totals Activity Monitor shows. They are in MACH TICKS, so the timebase
    // conversion below is required — treating them as nanoseconds is silently
    // wrong by the timebase ratio.
    mach_timebase_info_data_t timebase{};
    if (mach_timebase_info(&timebase) != KERN_SUCCESS || timebase.denom == 0)
        return false;

    bool any = false;
    for (const pid_t pid : tree) {
        proc_taskinfo info{};
        if (proc_pidinfo(pid, PROC_PIDTASKINFO, 0, &info, sizeof(info))
            < static_cast<int>(sizeof(info)))
            continue; // exited between the sweep and now, or not ours to read
        const double nanoseconds =
            static_cast<double>(info.pti_total_user + info.pti_total_system)
            * static_cast<double>(timebase.numer)
            / static_cast<double>(timebase.denom);
        cpuSeconds += nanoseconds / 1.0e9;
        residentMiB +=
            static_cast<double>(info.pti_resident_size) / (1024.0 * 1024.0);
        any = true;
    }
    return any;
#else
    (void)root;
    return false;
#endif
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
