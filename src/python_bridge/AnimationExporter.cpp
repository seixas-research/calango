#include "python_bridge/AnimationExporter.hpp"

#include <pybind11/embed.h>

#include <QByteArray>

#include <algorithm>
#include <stdexcept>

namespace py = pybind11;

namespace calango::pybridge {

void AnimationExporter::exportGif(const std::vector<QImage>& frames,
                                  const QString& path,
                                  int fps,
                                  bool transparent)
{
    if (frames.empty())
        throw std::runtime_error("No frames to export");

    py::module_ pil;
    try {
        pil = py::module_::import("PIL.Image");
    } catch (const py::error_already_set&) {
        throw std::runtime_error(
            "GIF export needs Pillow in the embedded Python environment.\n"
            "Install it with:  pip install pillow");
    }

    try {
        py::list pyFrames;
        for (const QImage& frame : frames) {
            const QImage rgba = frame.convertToFormat(QImage::Format_RGBA8888);
            pyFrames.append(pil.attr("frombytes")(
                "RGBA", py::make_tuple(rgba.width(), rgba.height()),
                py::bytes(reinterpret_cast<const char*>(rgba.constBits()),
                          static_cast<std::size_t>(rgba.sizeInBytes()))));
        }

        py::dict locals;
        locals["frames"] = pyFrames;
        locals["path"] = path.toStdString();
        locals["duration_ms"] = std::max(20, 1000 / std::max(1, fps));
        locals["transparent"] = transparent;

        // Palette index 255 is reserved for transparency; disposal=2
        // clears each frame so rotating structures don't leave trails.
        py::exec(R"PY(
from PIL import Image

processed = []
for frame in frames:
    if transparent:
        alpha = frame.getchannel("A")
        mask = alpha.point(lambda a: 255 if a <= 128 else 0)
        pal = frame.convert("RGB").convert("P", palette=Image.ADAPTIVE, colors=255)
        pal.paste(255, mask)
        processed.append(pal)
    else:
        processed.append(frame.convert("RGB").convert("P", palette=Image.ADAPTIVE, colors=256))

kwargs = dict(save_all=True, append_images=processed[1:], duration=duration_ms, loop=0)
if transparent:
    kwargs.update(transparency=255, disposal=2)
processed[0].save(path, **kwargs)
)PY",
                 // locals doubles as globals: script-defined functions must
                 // see the script's own names (see AseBridge::symmetryInfo).
                 locals, locals);
    } catch (const py::error_already_set& e) {
        throw std::runtime_error(std::string("GIF export failed:\n") + e.what());
    }
}

const std::vector<AnimationExporter::VideoFormat>&
AnimationExporter::videoFormats()
{
    // Codec availability comes from the ffmpeg imageio-ffmpeg bundles, which is
    // a full build: libx264, libx265, libvpx-vp9 and mpeg4 are all present. The
    // list is deliberately short — one entry per genuinely different answer to
    // "where is this file going to be played".
    static const std::vector<VideoFormat> kFormats = {
        {QT_TRANSLATE_NOOP("AnimationExporter", "MP4 video (H.264)"),
         "mp4", "libx264", "yuv420p"},
        {QT_TRANSLATE_NOOP("AnimationExporter", "MP4 video (H.265 / HEVC)"),
         "mp4", "libx265", "yuv420p"},
        {QT_TRANSLATE_NOOP("AnimationExporter", "QuickTime movie (H.264)"),
         "mov", "libx264", "yuv420p"},
        {QT_TRANSLATE_NOOP("AnimationExporter", "Matroska video (H.264)"),
         "mkv", "libx264", "yuv420p"},
        {QT_TRANSLATE_NOOP("AnimationExporter", "WebM video (VP9)"),
         "webm", "libvpx-vp9", "yuv420p"},
        {QT_TRANSLATE_NOOP("AnimationExporter", "AVI video (MPEG-4)"),
         "avi", "mpeg4", "yuv420p"},
        {QT_TRANSLATE_NOOP("AnimationExporter", "Animated GIF"),
         "gif", "", ""},
    };
    return kFormats;
}

namespace {

/// Import check shared by every ffmpeg-backed path. The message names the two
/// packages and the install line, because a missing bundled ffmpeg is by far
/// the most common way a video export fails.
void requireImageio()
{
    try {
        py::module_::import("imageio");
        py::module_::import("imageio_ffmpeg");
    } catch (const py::error_already_set&) {
        throw std::runtime_error(
            "Video export needs imageio with its bundled ffmpeg in the "
            "embedded\nPython environment. Install with:  pip install imageio "
            "imageio-ffmpeg");
    }
}

/// One frame repacked as densely-packed RGB888 rows, cropped to width×height.
/// QImage pads scanlines to 4 bytes, which numpy's reshape would misread.
QByteArray packRgb(const QImage& frame, int width, int height)
{
    const QImage rgb =
        frame.copy(0, 0, width, height).convertToFormat(QImage::Format_RGB888);
    QByteArray packed;
    packed.reserve(width * height * 3);
    for (int y = 0; y < height; ++y)
        packed.append(reinterpret_cast<const char*>(rgb.constScanLine(y)),
                      width * 3);
    return packed;
}

} // namespace

void AnimationExporter::exportVideo(const std::vector<QImage>& frames,
                                    const QString& path,
                                    int fps,
                                    const QString& codec,
                                    const QString& pixelFormat)
{
    if (frames.empty())
        throw std::runtime_error("No frames to export");
    requireImageio();

    // yuv420p requires even dimensions — crop a pixel if needed.
    const int width = frames.front().width() & ~1;
    const int height = frames.front().height() & ~1;
    if (width < 2 || height < 2)
        throw std::runtime_error("Frames too small for video export");

    try {
        py::list pyFrames;
        for (const QImage& frame : frames) {
            const QByteArray packed = packRgb(frame, width, height);
            pyFrames.append(py::bytes(packed.constData(),
                                      static_cast<std::size_t>(packed.size())));
        }

        py::dict locals;
        locals["frames"] = pyFrames;
        locals["path"] = path.toStdString();
        locals["fps"] = std::max(1, fps);
        locals["width"] = width;
        locals["height"] = height;
        locals["codec"] = codec.toStdString();
        locals["pixelformat"] = pixelFormat.toStdString();

        py::exec(R"PY(
import numpy as np
import imageio

writer = imageio.get_writer(
    path, fps=fps, codec=codec, quality=8, pixelformat=pixelformat)
try:
    for raw in frames:
        writer.append_data(np.frombuffer(raw, dtype=np.uint8).reshape(height, width, 3))
finally:
    writer.close()
)PY",
                 // locals doubles as globals: script-defined functions must
                 // see the script's own names (see AseBridge::symmetryInfo).
                 locals, locals);
    } catch (const py::error_already_set& e) {
        throw std::runtime_error(std::string("Video export failed:\n") + e.what());
    }
}

namespace {

/// Decode one animation frame from disk, with the frame index in the error
/// message: a ray-traced animation that silently loses frame 137 of 400 is
/// far harder to diagnose than one that refuses to encode.
QImage loadFrameOrThrow(const QStringList& paths, int index)
{
    const QString& path = paths.at(index);
    QImage image(path);
    if (image.isNull()) {
        throw std::runtime_error(
            QStringLiteral("Frame %1 of %2 could not be read back (%3).\n"
                           "The renderer may have failed or been interrupted "
                           "on that frame.")
                .arg(index + 1)
                .arg(paths.size())
                .arg(path)
                .toStdString());
    }
    return image;
}

} // namespace

void AnimationExporter::exportGifFromFiles(const QStringList& framePaths,
                                           const QString& path,
                                           int fps,
                                           bool transparent)
{
    if (framePaths.isEmpty())
        throw std::runtime_error("No frames to export");

    py::module_ pil;
    try {
        pil = py::module_::import("PIL.Image");
    } catch (const py::error_already_set&) {
        throw std::runtime_error(
            "GIF export needs Pillow in the embedded Python environment.\n"
            "Install it with:  pip install pillow");
    }

    try {
        py::dict locals;
        locals["path"] = path.toStdString();
        locals["duration_ms"] = std::max(20, 1000 / std::max(1, fps));
        locals["transparent"] = transparent;

        // Quantize frame by frame so only the (small) palettized frames are
        // retained; the full-resolution RGBA decode is released each round.
        py::exec(R"PY(
from PIL import Image

processed = []

def add_frame(raw, width, height):
    frame = Image.frombytes("RGBA", (width, height), raw)
    if transparent:
        alpha = frame.getchannel("A")
        mask = alpha.point(lambda a: 255 if a <= 128 else 0)
        pal = frame.convert("RGB").convert("P", palette=Image.ADAPTIVE, colors=255)
        pal.paste(255, mask)
        processed.append(pal)
    else:
        processed.append(
            frame.convert("RGB").convert("P", palette=Image.ADAPTIVE, colors=256))
    frame.close()
)PY",
                 // locals doubles as globals: script-defined functions must
                 // see the script's own names (see AseBridge::symmetryInfo).
                 locals, locals);

        auto addFrame = locals["add_frame"];
        for (int i = 0; i < framePaths.size(); ++i) {
            const QImage rgba =
                loadFrameOrThrow(framePaths, i).convertToFormat(QImage::Format_RGBA8888);
            addFrame(py::bytes(reinterpret_cast<const char*>(rgba.constBits()),
                               static_cast<std::size_t>(rgba.sizeInBytes())),
                     rgba.width(), rgba.height());
        }

        py::exec(R"PY(
kwargs = dict(save_all=True, append_images=processed[1:], duration=duration_ms, loop=0)
if transparent:
    kwargs.update(transparency=255, disposal=2)
processed[0].save(path, **kwargs)
for frame in processed:
    frame.close()
processed.clear()
)PY",
                 locals, locals);
    } catch (const py::error_already_set& e) {
        throw std::runtime_error(std::string("GIF export failed:\n") + e.what());
    }
}

void AnimationExporter::exportMp4FromFiles(const QStringList& framePaths,
                                           const QString& path,
                                           int fps)
{
    exportVideoFromFiles(framePaths, path, fps, QStringLiteral("libx264"),
                         QStringLiteral("yuv420p"));
}

void AnimationExporter::exportVideoFromFiles(const QStringList& framePaths,
                                             const QString& path,
                                             int fps,
                                             const QString& codec,
                                             const QString& pixelFormat)
{
    if (framePaths.isEmpty())
        throw std::runtime_error("No frames to export");
    requireImageio();

    // The whole stream is sized from frame 0; yuv420p requires even
    // dimensions, so crop a pixel where needed (as the in-memory path does).
    const QImage first = loadFrameOrThrow(framePaths, 0);
    const int width = first.width() & ~1;
    const int height = first.height() & ~1;
    if (width < 2 || height < 2)
        throw std::runtime_error("Frames too small for video export");

    py::dict locals;
    locals["path"] = path.toStdString();
    locals["fps"] = std::max(1, fps);
    locals["width"] = width;
    locals["height"] = height;
    locals["codec"] = codec.toStdString();
    locals["pixelformat"] = pixelFormat.toStdString();

    try {
        py::exec(R"PY(
import numpy as np
import imageio

writer = imageio.get_writer(
    path, fps=fps, codec=codec, quality=8, pixelformat=pixelformat)

def add_frame(raw):
    writer.append_data(np.frombuffer(raw, dtype=np.uint8).reshape(height, width, 3))

def close_writer():
    writer.close()
)PY",
                 // locals doubles as globals: script-defined functions must
                 // see the script's own names (see AseBridge::symmetryInfo).
                 locals, locals);
    } catch (const py::error_already_set& e) {
        throw std::runtime_error(std::string("Video export failed:\n") + e.what());
    }

    // From here the ffmpeg writer owns a subprocess and a partially written
    // file: every exit path has to close it, or the process leaks and the
    // video is left unplayable.
    const auto closeWriter = [&locals] {
        try {
            locals["close_writer"]();
        } catch (const py::error_already_set&) {
            // Already reporting a more useful failure; don't mask it.
        }
    };

    try {
        auto addFrame = locals["add_frame"];
        for (int i = 0; i < framePaths.size(); ++i) {
            const QImage source = (i == 0) ? first : loadFrameOrThrow(framePaths, i);
            if (source.width() < width || source.height() < height) {
                // catch(...) below closes the writer on the way out.
                throw std::runtime_error(
                    QStringLiteral("Frame %1 is %2×%3 but the video is %4×%5 — "
                                   "every frame must be rendered at the same "
                                   "resolution.")
                        .arg(i + 1)
                        .arg(source.width())
                        .arg(source.height())
                        .arg(width)
                        .arg(height)
                        .toStdString());
            }
            const QByteArray packed = packRgb(source, width, height);
            addFrame(py::bytes(packed.constData(),
                               static_cast<std::size_t>(packed.size())));
        }
    } catch (const py::error_already_set& e) {
        closeWriter();
        throw std::runtime_error(std::string("Video export failed:\n") + e.what());
    } catch (...) {
        closeWriter();
        throw;
    }
    closeWriter();
}

} // namespace calango::pybridge
