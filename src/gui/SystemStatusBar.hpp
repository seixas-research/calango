#pragma once

#include <QElapsedTimer>
#include <QWidget>

class QLabel;
class QTimer;

namespace calango::gui {

/// Small load meter: a black track with a green→yellow→red gradient fill
/// proportional to a 0–100 value. Used for the total-system-CPU indicator.
class MiniLoadBar : public QWidget {
    Q_OBJECT

public:
    explicit MiniLoadBar(QWidget* parent = nullptr);
    void setValue(double percent); ///< clamped to [0, 100]

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    double value_ = 0.0;
};

/// Permanent status-bar widget with two clearly separated logical groups:
///   Group 1 (Calango) — this application's CPU %, RAM (MB), and the active
///     OMP_NUM_THREADS.
///   Group 2 (Host) — total system CPU % (mini gradient bar + readout),
///     used/total system RAM (MB), GPU %, and VRAM used/total (MB).
/// A vertical separator divides the groups. Sampled on a strict 1.0 s timer
/// via mach APIs on macOS; unavailable metrics (GPU/VRAM) show N/A.
class SystemStatusBar : public QWidget {
    Q_OBJECT

public:
    explicit SystemStatusBar(QWidget* parent = nullptr);

public Q_SLOTS:
    /// Refresh the thread label immediately (e.g. after Preferences changes).
    void refreshThreads();

private:
    void refresh();
    /// Total host CPU utilization since the previous call (-1 on first sample
    /// or if unavailable).
    double sampleSystemCpuPercent();
    /// This process's CPU utilization since the previous call, summed across
    /// cores (like Activity Monitor; can exceed 100). -1 if unavailable.
    double sampleProcessCpuPercent();
    /// Resident memory of this process in MiB (-1 if unavailable).
    double sampleProcessMemoryMiB();
    /// System used / total physical memory in MiB (false if unavailable).
    bool sampleSystemMemoryMiB(double& used, double& total);

    // Group 1 — Calango application metrics.
    QLabel* appCpuLabel_;
    QLabel* appMemLabel_;
    QLabel* threadsLabel_;
    // Group 2 — host machine metrics.
    MiniLoadBar* cpuBar_;
    QLabel* sysCpuLabel_;
    QLabel* sysMemLabel_;
    QLabel* gpuLabel_;
    QLabel* vramLabel_;
    QTimer* timer_;

    unsigned long long prevCpuBusy_ = 0;
    unsigned long long prevCpuTotal_ = 0;
    double prevProcCpuSeconds_ = -1.0;
    QElapsedTimer procWallClock_;
};

} // namespace calango::gui
