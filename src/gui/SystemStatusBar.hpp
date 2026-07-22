#pragma once

#include <QElapsedTimer>
#include <QWidget>

class QLabel;
class QTimer;

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

/// Permanent status-bar widget showing EXCLUSIVELY Calango's own resource
/// usage (no host-machine totals): CPU %, RAM (MB + % of system RAM), GPU %,
/// and VRAM (MB) — each in a mini gradient progress bar — plus the active
/// thread count. GPU/VRAM show N/A where no per-process metric source exists
/// (e.g. Metal on macOS). Sampled on a strict 1.0 s timer.
class SystemStatusBar : public QWidget {
    Q_OBJECT

public:
    explicit SystemStatusBar(QWidget* parent = nullptr);

public Q_SLOTS:
    /// Refresh the thread label immediately (e.g. after Preferences changes).
    void refreshThreads();

private:
    void refresh();
    /// This process's CPU utilization since the previous call, summed across
    /// cores (like Activity Monitor; can exceed 100). -1 if unavailable.
    double sampleProcessCpuPercent();
    /// Resident memory of this process in MiB (-1 if unavailable).
    double sampleProcessMemoryMiB();
    /// Total physical system memory in MiB (0 if unavailable) — the RAM-bar
    /// denominator.
    double systemMemoryTotalMiB();

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

    double prevProcCpuSeconds_ = -1.0;
    QElapsedTimer procWallClock_;
};

} // namespace calango::gui
