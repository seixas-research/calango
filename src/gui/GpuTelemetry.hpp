#pragma once

#include <QString>

namespace calango::gui {

/// One reading of the machine's GPU.
///
/// DEVICE-level, not per-process, and that distinction is deliberate rather
/// than a shortcut. Neither platform offers a supported per-process GPU
/// utilization number: macOS publishes accelerator statistics per DEVICE
/// through IOKit, and NVML's per-process accounting reports memory only (and
/// only when the driver is in a non-default accounting mode). Reporting the
/// device is a true statement about the machine; scaling it by anything would
/// be a guess presented as a measurement.
struct GpuSample {
    /// A GPU was found AND a metric could be read from it. False means the UI
    /// should say so rather than draw an empty bar that looks like idle.
    bool available = false;
    /// Human-readable adapter name ("Apple M3 Pro", "NVIDIA GeForce RTX 5060
    /// Ti"). Empty when the backend found a device but could not name it.
    QString name;
    /// Where the numbers came from, for the tooltip: knowing whether a zero
    /// is NVML's or a parsed `nvidia-smi` line is most of the diagnosis when
    /// it is wrong.
    QString source;
    /// Device utilization, 0-100. Negative when unavailable.
    double utilizationPercent = -1.0;
    /// GPU memory in use, MiB. Negative when unavailable.
    double memoryUsedMiB = -1.0;
    /// Total GPU memory, MiB. Negative on unified-memory parts, where there is
    /// no separate pool to be a fraction OF — a percentage there would be a
    /// fraction of system RAM wearing a VRAM label.
    double memoryTotalMiB = -1.0;
    /// Why nothing was reported, when `available` is false. Shown in the
    /// tooltip: "no GPU" and "a GPU whose driver would not initialize" are
    /// different problems and the user is the one who can act on either.
    QString unavailableReason;
};

/// Sample the GPU, or report why not.
///
/// Cheap enough for the status bar's 1 s tick: the macOS path holds its IOKit
/// service open across calls, and the Linux path binds NVML once through
/// `dlopen` and reuses the handle. The `nvidia-smi` fallback is the expensive
/// one (a subprocess), so it runs only when NVML is absent and is rate-limited
/// internally.
///
/// Never throws and never blocks indefinitely; a backend that cannot answer
/// returns `available == false` with a reason.
GpuSample sampleGpu();

/// Drop any cached handles (NVML, IOKit service). Called at shutdown so the
/// driver is released deterministically rather than at static-destruction
/// time, which on NVML is late enough to matter.
void shutdownGpuTelemetry();

} // namespace calango::gui
