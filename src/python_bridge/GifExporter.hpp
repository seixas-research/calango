#pragma once

#include <QImage>
#include <QString>

#include <vector>

namespace calango::pybridge {

/// Animated-GIF encoder built on the embedded Python interpreter (Pillow).
///
/// Pillow performs proper adaptive palette quantization and GIF
/// transparency handling, which keeps the C++ side free of a bespoke LZW
/// encoder. Requires `pillow` in the embedded environment
/// (`pip install pillow`); throws std::runtime_error with an actionable
/// message when it is missing. GUI-thread only (like all of pybridge).
class GifExporter {
public:
    /// `transparent` maps alpha <= 128 pixels to GIF transparency
    /// (frames must then be rendered on an alpha-0 background).
    static void exportGif(const std::vector<QImage>& frames,
                          const QString& path,
                          int fps,
                          bool transparent);
};

} // namespace calango::pybridge
