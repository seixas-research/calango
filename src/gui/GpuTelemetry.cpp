#include "gui/GpuTelemetry.hpp"

#include <QByteArray>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QStringList>

#ifdef __APPLE__
#include <CoreFoundation/CoreFoundation.h>
#include <IOKit/IOKitLib.h>
#endif

#ifdef __linux__
#include <dlfcn.h>
#endif

namespace calango::gui {

namespace {

#ifdef __APPLE__

// --------------------------------------------------------------------------
// macOS: IOKit
// --------------------------------------------------------------------------
// There is no Metal API for "how busy is the GPU" — Metal describes devices
// and schedules work, it does not meter them. What macOS does publish is the
// accelerator driver's own counters, in the IORegistry: every GPU appears as
// an IOAccelerator service carrying a "PerformanceStatistics" dictionary with
// "Device Utilization %" and the in-use memory. That is the same source
// Activity Monitor's GPU history and `powermetrics` read, so the numbers agree
// with what a user can check against.
//
// Read through IOKit directly rather than by parsing `ioreg`: a subprocess per
// second to print the whole registry as text, to recover two integers, is a
// remarkable amount of work for a status bar.

/// A CFNumber/CFBoolean out of a CFDictionary as a double; NaN if absent.
double cfNumber(CFDictionaryRef dict, CFStringRef key)
{
    if (!dict)
        return std::nan("");
    const void* value = CFDictionaryGetValue(dict, key);
    if (!value || CFGetTypeID(value) != CFNumberGetTypeID())
        return std::nan("");
    double out = 0.0;
    if (!CFNumberGetValue(static_cast<CFNumberRef>(value), kCFNumberDoubleType,
                          &out))
        return std::nan("");
    return out;
}

QString cfString(CFTypeRef value)
{
    if (!value)
        return {};
    if (CFGetTypeID(value) == CFStringGetTypeID()) {
        const auto ref = static_cast<CFStringRef>(value);
        char buffer[256] = {};
        if (CFStringGetCString(ref, buffer, sizeof(buffer), kCFStringEncodingUTF8))
            return QString::fromUtf8(buffer);
        return {};
    }
    if (CFGetTypeID(value) == CFDataGetTypeID()) {
        // IOKit stores several registry strings as NUL-terminated CFData
        // ("model" is one), which is why this is not just a CFString read.
        const auto data = static_cast<CFDataRef>(value);
        const auto* bytes = reinterpret_cast<const char*>(CFDataGetBytePtr(data));
        return QString::fromUtf8(
            QByteArray(bytes, static_cast<int>(CFDataGetLength(data))));
    }
    return {};
}

/// Name the adapter from the accelerator service or its PCI/AGX parent.
QString appleGpuName(io_service_t service)
{
    // "model" is what both discrete PCI cards and Apple's own AGX devices
    // carry; IOClass ("AGXAcceleratorG15G") is a driver name, not a product,
    // so it is only the last resort.
    for (CFStringRef key : {CFSTR("model"), CFSTR("IOName")}) {
        if (CFTypeRef value = IORegistryEntrySearchCFProperty(
                service, kIOServicePlane, key, kCFAllocatorDefault,
                kIORegistryIterateRecursively | kIORegistryIterateParents)) {
            const QString name = cfString(value);
            CFRelease(value);
            if (!name.isEmpty())
                return name;
        }
    }
    return {};
}

GpuSample sampleApple()
{
    GpuSample sample;
    // Matching dictionary is consumed by IOServiceGetMatchingServices.
    CFMutableDictionaryRef match = IOServiceMatching("IOAccelerator");
    if (!match) {
        sample.unavailableReason =
            QStringLiteral("IOServiceMatching(\"IOAccelerator\") failed");
        return sample;
    }
    io_iterator_t iterator = IO_OBJECT_NULL;
    if (IOServiceGetMatchingServices(kIOMainPortDefault, match, &iterator)
        != KERN_SUCCESS) {
        sample.unavailableReason =
            QStringLiteral("no IOAccelerator service in the IORegistry");
        return sample;
    }

    // First accelerator that publishes a utilization number wins. A Mac can
    // list several (an eGPU, or the software renderer); the built-in device is
    // enumerated first and is the one doing the work.
    for (io_service_t service = IOIteratorNext(iterator); service;
         service = IOIteratorNext(iterator)) {
        CFMutableDictionaryRef properties = nullptr;
        if (IORegistryEntryCreateCFProperties(service, &properties,
                                              kCFAllocatorDefault, 0)
                != KERN_SUCCESS
            || !properties) {
            IOObjectRelease(service);
            continue;
        }
        const auto stats = static_cast<CFDictionaryRef>(
            CFDictionaryGetValue(properties, CFSTR("PerformanceStatistics")));
        const double utilization =
            cfNumber(stats, CFSTR("Device Utilization %"));
        if (!std::isnan(utilization)) {
            sample.available = true;
            sample.utilizationPercent = utilization;
            sample.source = QStringLiteral("IOKit / IOAccelerator");
            sample.name = appleGpuName(service);
            const double usedBytes =
                cfNumber(stats, CFSTR("In use system memory"));
            if (!std::isnan(usedBytes))
                sample.memoryUsedMiB = usedBytes / (1024.0 * 1024.0);
            // Apple Silicon is unified memory: there is no separate VRAM pool
            // for this to be a fraction of, so no total is reported and the
            // UI shows MiB without a bar.
            CFRelease(properties);
            IOObjectRelease(service);
            break;
        }
        CFRelease(properties);
        IOObjectRelease(service);
    }
    IOObjectRelease(iterator);
    if (!sample.available && sample.unavailableReason.isEmpty())
        sample.unavailableReason = QStringLiteral(
            "IOAccelerator published no \"Device Utilization %\" counter");
    return sample;
}

#endif // __APPLE__

#ifdef __linux__

// --------------------------------------------------------------------------
// Linux: NVML through dlopen, then nvidia-smi, then the DRM sysfs counters
// --------------------------------------------------------------------------
// NVML is loaded at RUNTIME rather than linked. Calango has to build and run
// on machines with no CUDA toolkit and no NVIDIA driver at all, so a link-time
// dependency on libnvidia-ml would trade a working status bar on one class of
// machine for a binary that does not start on another.
//
// The symbol names carry their ABI suffixes (`_v2`) because that is what the
// library exports; the unsuffixed names exist only as macros in nvml.h, which
// is precisely the header this code avoids needing.

struct Nvml {
    void* handle = nullptr;
    int (*init)() = nullptr;
    int (*shutdown)() = nullptr;
    int (*deviceGetCount)(unsigned int*) = nullptr;
    int (*deviceGetHandleByIndex)(unsigned int, void**) = nullptr;
    int (*deviceGetName)(void*, char*, unsigned int) = nullptr;
    int (*deviceGetUtilizationRates)(void*, void*) = nullptr;
    int (*deviceGetMemoryInfo)(void*, void*) = nullptr;
    bool ready = false;
    bool attempted = false;
    QString failure;
};

Nvml& nvml()
{
    static Nvml instance;
    return instance;
}

/// nvmlUtilization_t — two unsigned ints (gpu, memory), in that order.
struct NvmlUtilization {
    unsigned int gpu;
    unsigned int memory;
};
/// nvmlMemory_t — three unsigned long longs (total, free, used).
struct NvmlMemory {
    unsigned long long total;
    unsigned long long free;
    unsigned long long used;
};

bool loadNvml()
{
    Nvml& n = nvml();
    if (n.attempted)
        return n.ready;
    n.attempted = true;
    // The versioned SONAME first: it is what the driver package installs and
    // what is present without the -dev package.
    for (const char* candidate :
         {"libnvidia-ml.so.1", "libnvidia-ml.so"}) {
        n.handle = dlopen(candidate, RTLD_LAZY | RTLD_LOCAL);
        if (n.handle)
            break;
    }
    if (!n.handle) {
        n.failure = QStringLiteral("libnvidia-ml.so.1 not loadable (%1)")
                        .arg(QString::fromUtf8(dlerror() ? dlerror() : "?"));
        return false;
    }
    const auto bind = [&n](const char* symbol) {
        return dlsym(n.handle, symbol);
    };
    n.init = reinterpret_cast<int (*)()>(bind("nvmlInit_v2"));
    if (!n.init)
        n.init = reinterpret_cast<int (*)()>(bind("nvmlInit"));
    n.shutdown = reinterpret_cast<int (*)()>(bind("nvmlShutdown"));
    n.deviceGetCount = reinterpret_cast<int (*)(unsigned int*)>(
        bind("nvmlDeviceGetCount_v2"));
    if (!n.deviceGetCount)
        n.deviceGetCount = reinterpret_cast<int (*)(unsigned int*)>(
            bind("nvmlDeviceGetCount"));
    n.deviceGetHandleByIndex = reinterpret_cast<int (*)(unsigned int, void**)>(
        bind("nvmlDeviceGetHandleByIndex_v2"));
    if (!n.deviceGetHandleByIndex)
        n.deviceGetHandleByIndex =
            reinterpret_cast<int (*)(unsigned int, void**)>(
                bind("nvmlDeviceGetHandleByIndex"));
    n.deviceGetName = reinterpret_cast<int (*)(void*, char*, unsigned int)>(
        bind("nvmlDeviceGetName"));
    n.deviceGetUtilizationRates = reinterpret_cast<int (*)(void*, void*)>(
        bind("nvmlDeviceGetUtilizationRates"));
    n.deviceGetMemoryInfo = reinterpret_cast<int (*)(void*, void*)>(
        bind("nvmlDeviceGetMemoryInfo"));

    if (!n.init || !n.deviceGetCount || !n.deviceGetHandleByIndex
        || !n.deviceGetUtilizationRates) {
        n.failure = QStringLiteral("libnvidia-ml is missing expected symbols");
        return false;
    }
    // NVML_SUCCESS is 0. A non-zero here is the "driver/library version
    // mismatch" case: the library is present but the kernel module it talks to
    // is a different version, which is what a partly-completed driver upgrade
    // leaves behind and is worth naming rather than reporting as "no GPU".
    const int rc = n.init();
    if (rc != 0) {
        n.failure = QStringLiteral("nvmlInit failed (code %1) — usually a "
                                   "driver/library version mismatch")
                        .arg(rc);
        return false;
    }
    n.ready = true;
    return true;
}

bool sampleNvml(GpuSample& sample)
{
    if (!loadNvml()) {
        sample.unavailableReason = nvml().failure;
        return false;
    }
    Nvml& n = nvml();
    unsigned int count = 0;
    if (n.deviceGetCount(&count) != 0 || count == 0) {
        sample.unavailableReason =
            QStringLiteral("NVML initialized but reports no devices");
        return false;
    }
    void* device = nullptr;
    if (n.deviceGetHandleByIndex(0, &device) != 0 || !device) {
        sample.unavailableReason =
            QStringLiteral("NVML could not open device 0");
        return false;
    }
    NvmlUtilization utilization{};
    if (n.deviceGetUtilizationRates(device, &utilization) != 0) {
        sample.unavailableReason =
            QStringLiteral("NVML device 0 reports no utilization counters");
        return false;
    }
    sample.available = true;
    sample.source = QStringLiteral("NVML");
    sample.utilizationPercent = utilization.gpu;
    if (n.deviceGetName) {
        char buffer[128] = {};
        if (n.deviceGetName(device, buffer, sizeof(buffer)) == 0)
            sample.name = QString::fromUtf8(buffer);
    }
    if (n.deviceGetMemoryInfo) {
        NvmlMemory memory{};
        if (n.deviceGetMemoryInfo(device, &memory) == 0 && memory.total > 0) {
            sample.memoryUsedMiB =
                static_cast<double>(memory.used) / (1024.0 * 1024.0);
            sample.memoryTotalMiB =
                static_cast<double>(memory.total) / (1024.0 * 1024.0);
        }
    }
    return true;
}

/// `nvidia-smi`, for the case where the driver is installed but NVML cannot be
/// dlopen'd (a container that mounts the binary but not the library is the
/// common one). A subprocess per second would be absurd, so this is throttled
/// and its last answer reused between refreshes.
bool sampleNvidiaSmi(GpuSample& sample)
{
    static GpuSample cached;
    static QElapsedTimer since;
    if (since.isValid() && since.elapsed() < 2000) {
        if (!cached.available)
            return false;
        sample = cached;
        return true;
    }

    QProcess smi;
    smi.start(QStringLiteral("nvidia-smi"),
              {QStringLiteral("--query-gpu=name,utilization.gpu,memory.used,"
                              "memory.total"),
               QStringLiteral("--format=csv,noheader,nounits")});
    // Bounded: a wedged driver makes nvidia-smi hang, and a status bar must
    // not take the UI thread down with it.
    if (!smi.waitForFinished(1500) || smi.exitCode() != 0) {
        smi.kill();
        smi.waitForFinished(200);
        cached = GpuSample{};
        since.restart();
        sample.unavailableReason =
            QStringLiteral("nvidia-smi is not available or did not respond");
        return false;
    }
    const QString line =
        QString::fromUtf8(smi.readAllStandardOutput()).split(QLatin1Char('\n'),
                                                             Qt::SkipEmptyParts)
            .value(0);
    const QStringList fields = line.split(QLatin1Char(','), Qt::SkipEmptyParts);
    if (fields.size() < 4) {
        cached = GpuSample{};
        since.restart();
        sample.unavailableReason =
            QStringLiteral("nvidia-smi output could not be parsed");
        return false;
    }
    GpuSample parsed;
    parsed.available = true;
    parsed.source = QStringLiteral("nvidia-smi");
    parsed.name = fields[0].trimmed();
    parsed.utilizationPercent = fields[1].trimmed().toDouble();
    parsed.memoryUsedMiB = fields[2].trimmed().toDouble();
    parsed.memoryTotalMiB = fields[3].trimmed().toDouble();
    cached = parsed;
    since.restart();
    sample = parsed;
    return true;
}

/// AMD and Intel, through the kernel's own DRM counters. No vendor library and
/// no subprocess — `gpu_busy_percent` is a file the driver keeps current.
bool sampleDrmSysfs(GpuSample& sample)
{
    for (int card = 0; card < 8; ++card) {
        const QString base =
            QStringLiteral("/sys/class/drm/card%1/device").arg(card);
        QFile busy(base + QStringLiteral("/gpu_busy_percent"));
        if (!busy.exists() || !busy.open(QIODevice::ReadOnly))
            continue;
        bool ok = false;
        const double percent =
            QString::fromUtf8(busy.readAll()).trimmed().toDouble(&ok);
        if (!ok)
            continue;
        sample.available = true;
        sample.source = QStringLiteral("DRM sysfs");
        sample.utilizationPercent = percent;
        QFile used(base + QStringLiteral("/mem_info_vram_used"));
        QFile total(base + QStringLiteral("/mem_info_vram_total"));
        if (used.open(QIODevice::ReadOnly) && total.open(QIODevice::ReadOnly)) {
            sample.memoryUsedMiB =
                QString::fromUtf8(used.readAll()).trimmed().toDouble()
                / (1024.0 * 1024.0);
            sample.memoryTotalMiB =
                QString::fromUtf8(total.readAll()).trimmed().toDouble()
                / (1024.0 * 1024.0);
        }
        QFile vendor(base + QStringLiteral("/uevent"));
        if (vendor.open(QIODevice::ReadOnly)) {
            for (const QString& row :
                 QString::fromUtf8(vendor.readAll()).split(QLatin1Char('\n')))
                if (row.startsWith(QStringLiteral("DRIVER=")))
                    sample.name = row.section(QLatin1Char('='), 1).trimmed();
        }
        return true;
    }
    return false;
}

#endif // __linux__

} // namespace

GpuSample sampleGpu()
{
    GpuSample sample;
#ifdef __APPLE__
    sample = sampleApple();
    return sample;
#elif defined(__linux__)
    // NVML first: it is the supported interface and the only one that answers
    // without spawning anything.
    if (sampleNvml(sample))
        return sample;
    const QString nvmlReason = sample.unavailableReason;
    GpuSample viaSmi;
    if (sampleNvidiaSmi(viaSmi))
        return viaSmi;
    GpuSample viaDrm;
    if (sampleDrmSysfs(viaDrm))
        return viaDrm;
    // Report the FIRST failure, not the last: "NVML says driver/library
    // version mismatch" is actionable, "no DRM counters" is what any machine
    // without an AMD card says and tells the user nothing.
    sample = GpuSample{};
    sample.unavailableReason = nvmlReason.isEmpty()
        ? QStringLiteral("no GPU metric source found")
        : nvmlReason;
    return sample;
#else
    sample.unavailableReason =
        QStringLiteral("no GPU telemetry backend on this platform");
    return sample;
#endif
}

void shutdownGpuTelemetry()
{
#ifdef __linux__
    Nvml& n = nvml();
    if (n.ready && n.shutdown)
        n.shutdown();
    n.ready = false;
    if (n.handle) {
        dlclose(n.handle);
        n.handle = nullptr;
    }
#endif
}

} // namespace calango::gui
