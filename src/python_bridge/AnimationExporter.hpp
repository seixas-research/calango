#pragma once

#include <QImage>
#include <QString>

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
};

} // namespace calango::pybridge
