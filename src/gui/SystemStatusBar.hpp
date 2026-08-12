#pragma once

#include <QElapsedTimer>
#include <QWidget>

class QLabel;
class QTimer;

namespace calango::jobs {
class JobRunner;
}

namespace calango::gui {

/// Small load meter: a black track with a green→yellow→red gradient fill
/// proportional to a 0–100 value. A negative value renders an empty track
/// ("no data", e.g. GPU/VRAM where no metric source exists).
class MiniLoadBar : public QWidget {
    Q_OBJECT

public:
    explicit MiniLoadBar(QWidget* parent = nullptr);
    /// Fill fraction 0–100; a negative value shows an empty ("no data") track.
    void setValue(double percent);

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    double value_ = -1.0;
};

/// Permanent status-bar widget showing the resource usage of Calango AND of
/// the background job it has spawned — never host-machine totals.
///
/// Two groups, because they answer different questions. The first is the
/// application itself: CPU %, RAM (MB + % of system RAM), GPU %, VRAM (MB) and
/// the active thread count. The second appears only while a job is running and
/// reports that job: its name, its state, its elapsed time, and the CPU and
/// memory of its WHOLE PROCESS TREE.
///
/// The tree, not the direct child, is the number that matters. Calango
/// launches jobs through a shell, and the compute lives further down
/// (`mpirun -n 4 gpaw …`): sampling only the process it started would report a
/// few percent for a machine running flat out, which is worse than reporting
/// nothing. GPU/VRAM show N/A where no per-process metric source exists (e.g.
/// Metal on macOS). Sampled on a strict 1.0 s timer.
class SystemStatusBar : public QWidget {
    Q_OBJECT

public:
    explicit SystemStatusBar(QWidget* parent = nullptr);

    /// Bind the runner whose subprocess is tracked. Without one the job group
    /// simply never appears, so the widget still works standalone.
    void setJobRunner(jobs::JobRunner* runner);

public Q_SLOTS:
    /// Refresh the thread label immediately (e.g. after Preferences changes).
    void refreshThreads();

private:
    void refresh();
    /// Refresh the GPU/VRAM pair from gui/GpuTelemetry. Reports the DEVICE,
    /// with the source and (when it fails) the reason in the tooltip.
    void refreshGpu();
    /// This process's CPU utilization since the previous call, summed across
    /// cores (like Activity Monitor; can exceed 100). -1 if unavailable.
    double sampleProcessCpuPercent();
    /// Resident memory of this process in MiB (-1 if unavailable).
    double sampleProcessMemoryMiB();
    /// Total physical system memory in MiB (0 if unavailable) — the RAM-bar
    /// denominator.
    double systemMemoryTotalMiB();

    /// Refresh the job group from the bound runner.
    void refreshJob();
    /// Cumulative CPU seconds and resident MiB summed over `root` and every
    /// descendant of it. Returns false when the tree could not be walked.
    static bool sampleTree(qint64 root, double& cpuSeconds, double& residentMiB);

    jobs::JobRunner* runner_ = nullptr;

    MiniLoadBar* cpuBar_;
    QLabel* cpuLabel_;
    MiniLoadBar* ramBar_;
    QLabel* ramLabel_;
    MiniLoadBar* gpuBar_;
    QLabel* gpuLabel_;
    MiniLoadBar* vramBar_;
    QLabel* vramLabel_;
    QLabel* threadsLabel_;
    QTimer* timer_;

    // -- Background job ----------------------------------------------------
    QWidget* jobGroup_;
    QLabel* jobNameLabel_;
    MiniLoadBar* jobCpuBar_;
    QLabel* jobCpuLabel_;
    MiniLoadBar* jobRamBar_;
    QLabel* jobRamLabel_;
    QLabel* jobElapsedLabel_;

    double prevProcCpuSeconds_ = -1.0;
    QElapsedTimer procWallClock_;
    /// Per-job CPU accounting, reset when a new pid appears so the first
    /// sample of a job is not the previous job's delta.
    qint64 jobPid_ = 0;
    double prevJobCpuSeconds_ = -1.0;
    QElapsedTimer jobWallClock_;
    QElapsedTimer jobElapsed_;
};

} // namespace calango::gui
