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
    /// One selectable output format of the Export Animation dialog: a
    /// container, the codec ffmpeg encodes with, and the pixel format it
    /// packs into.
    ///
    /// Container and codec are kept as separate strings rather than baked into
    /// one "mp4" flag because they are genuinely independent — .mov and .mkv
    /// carry the same H.264 stream, and .mp4 carries either H.264 or HEVC.
    /// `codec` empty marks the one non-ffmpeg entry, animated GIF, which goes
    /// through Pillow instead.
    struct VideoFormat {
        const char* label;     ///< as shown in the dialog
        const char* extension; ///< without the dot, e.g. "mp4"
        const char* codec;     ///< ffmpeg encoder, empty for GIF
        /// yuv420p is the compatibility choice: it is the only chroma layout
        /// every player, browser and slide deck decodes. yuv420p10le buys
        /// 10-bit depth at the cost of that universality.
        const char* pixelFormat;
    };

    /// The formats offered, in dialog order. MP4/H.264 leads because it is the
    /// one file that plays everywhere without a codec conversation.
    static const std::vector<VideoFormat>& videoFormats();

    /// `transparent` maps alpha <= 128 pixels to GIF transparency
    /// (frames must then be rendered on an alpha-0 background).
    static void exportGif(const std::vector<QImage>& frames,
                          const QString& path,
                          int fps,
                          bool transparent);

    /// Encode `frames` with an arbitrary ffmpeg codec / pixel format. Odd
    /// frame dimensions are cropped by one pixel (yuv420p needs even
    /// dimensions); alpha is ignored — none of the offered containers carries
    /// it, which is why the dialog forces an opaque background for video.
    static void exportVideo(const std::vector<QImage>& frames,
                            const QString& path,
                            int fps,
                            const QString& codec,
                            const QString& pixelFormat);

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

    /// Arbitrary-codec video from files on disk; the streaming counterpart of
    /// exportVideo().
    static void exportVideoFromFiles(const QStringList& framePaths,
                                     const QString& path,
                                     int fps,
                                     const QString& codec,
                                     const QString& pixelFormat);
};

} // namespace calango::pybridge
