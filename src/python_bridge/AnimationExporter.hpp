#pragma once

#include <QImage>
#include <QString>
#include <QStringList>

#include <vector>

namespace calango::pybridge {

/// Animation encoders built on the embedded Python interpreter.
///
///  - GIF  via Pillow (`pip install pillow`): adaptive palette
///    quantization + optional GIF transparency.
///  - MP4  via imageio + bundled ffmpeg (`pip install imageio
///    imageio-ffmpeg`): H.264, yuv420p, no alpha channel — frames must be
///    rendered on an opaque background.
///
/// Both throw std::runtime_error with an actionable message when the
/// Python package is missing. GUI-thread only (like all of pybridge).
class AnimationExporter {
public:
    /// `transparent` maps alpha <= 128 pixels to GIF transparency
    /// (frames must then be rendered on an alpha-0 background).
    static void exportGif(const std::vector<QImage>& frames,
                          const QString& path,
                          int fps,
                          bool transparent);

    /// H.264 MP4. Odd frame dimensions are cropped by one pixel to satisfy
    /// yuv420p; alpha is ignored.
    static void exportMp4(const std::vector<QImage>& frames,
                          const QString& path,
                          int fps);

    // -- Disk-backed variants (ray-traced trajectory animations) -----------
    //
    // The in-memory overloads above need every frame resident at once, which
    // is fine for a 72-frame turntable at 640×480 (~66 MB) but not for a
    // 500-frame Tachyon render at 1920×1440 (~5.5 GB). The external
    // ray-tracer already writes one PNG per frame, so these overloads stream
    // straight off disk: each image is decoded, encoded and released before
    // the next is read, so peak memory stays at a single frame.
    //
    // `framePaths` is used in the given order — the caller owns frame
    // ordering, and every path must exist and decode, otherwise the export
    // throws naming the offending frame rather than silently dropping it.

    /// GIF from PNG (or any Qt-readable) files on disk.
    static void exportGifFromFiles(const QStringList& framePaths,
                                   const QString& path,
                                   int fps,
                                   bool transparent);

    /// H.264 MP4 from PNG (or any Qt-readable) files on disk. All frames
    /// must share the first frame's dimensions.
    static void exportMp4FromFiles(const QStringList& framePaths,
                                   const QString& path,
                                   int fps);
};

} // namespace calango::pybridge
