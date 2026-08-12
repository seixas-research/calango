// GPU telemetry: the contract, not the hardware.
//
// This test runs on CI boxes with no GPU, on Apple Silicon, and on Linux with
// or without an NVIDIA driver, so it cannot assert a utilization number. What
// it CAN assert is the invariant the status bar depends on, and the one that
// was actually broken: the backend must either report a device with usable
// values, or say WHY it could not. The previous behaviour — GPU and VRAM
// hardcoded to "N/A" with no source ever consulted — satisfies neither, and a
// silently blank reading is indistinguishable from an idle GPU.

#include "gui/GpuTelemetry.hpp"

#include <QCoreApplication>

#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace {

int failures = 0;

void check(bool ok, const char* what)
{
    std::printf("  %s %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok)
        ++failures;
}

} // namespace

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);

    std::printf("GPU telemetry:\n");
    const calango::gui::GpuSample sample = calango::gui::sampleGpu();

    std::printf("    available=%d name='%s' source='%s' util=%.1f%% "
                "used=%.0f MiB total=%.0f MiB\n",
                sample.available ? 1 : 0, qPrintable(sample.name),
                qPrintable(sample.source), sample.utilizationPercent,
                sample.memoryUsedMiB, sample.memoryTotalMiB);
    if (!sample.available)
        std::printf("    reason: %s\n", qPrintable(sample.unavailableReason));

    // The invariant: never silent. Either it worked, or it explains itself.
    check(sample.available || !sample.unavailableReason.isEmpty(),
          "a failed sample always carries a reason");

    if (sample.available) {
        // A source is what turns "0 %" from a mystery into a fact about a
        // named backend — it is in the status-bar tooltip for that reason.
        check(!sample.source.isEmpty(),
              "a successful sample names the backend it came from");
        check(sample.utilizationPercent >= 0.0
                  && sample.utilizationPercent <= 100.0,
              "utilization is a percentage in range");
        check(std::isfinite(sample.utilizationPercent),
              "and is finite");
        // Memory is optional (a backend may expose utilization only), but if
        // present it must be non-negative and consistent.
        if (sample.memoryUsedMiB >= 0.0) {
            check(std::isfinite(sample.memoryUsedMiB),
                  "used memory is finite when reported");
            if (sample.memoryTotalMiB > 0.0)
                check(sample.memoryUsedMiB <= sample.memoryTotalMiB * 1.05,
                      "used memory does not exceed the total");
            else
                // Unified memory (Apple Silicon): no separate pool, so no
                // total. That is reported as absent rather than as system RAM
                // wearing a VRAM label.
                check(sample.memoryTotalMiB < 0.0,
                      "an absent total is negative, not zero — zero would be "
                      "a division waiting to happen");
        }
        check(sample.unavailableReason.isEmpty(),
              "and carries no failure reason");
    } else {
        std::printf("    (no GPU metric source here — the reason above is the "
                    "product behaviour under test)\n");
    }

    // Sampling repeatedly must stay stable: the status bar calls this once a
    // second for the lifetime of the application, and a backend that leaked a
    // handle or re-dlopen'd NVML per call would degrade over hours rather
    // than fail visibly.
    bool stable = true;
    for (int i = 0; i < 20; ++i) {
        const calango::gui::GpuSample repeat = calango::gui::sampleGpu();
        stable = stable && repeat.available == sample.available;
    }
    check(stable, "20 consecutive samples agree on availability");

    calango::gui::shutdownGpuTelemetry();
    check(true, "shutdown releases the backend without crashing");

    if (failures == 0) {
        std::printf("\nAll GPU telemetry checks passed.\n");
        return EXIT_SUCCESS;
    }
    std::printf("\n%d GPU telemetry check(s) FAILED.\n", failures);
    return EXIT_FAILURE;
}
