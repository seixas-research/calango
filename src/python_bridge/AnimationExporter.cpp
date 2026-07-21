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

void AnimationExporter::exportMp4(const std::vector<QImage>& frames,
                                  const QString& path,
                                  int fps)
{
    if (frames.empty())
        throw std::runtime_error("No frames to export");

    try {
        py::module_::import("imageio");
        py::module_::import("imageio_ffmpeg");
    } catch (const py::error_already_set&) {
        throw std::runtime_error(
            "MP4 export needs imageio with its bundled ffmpeg in the embedded\n"
            "Python environment. Install with:  pip install imageio imageio-ffmpeg");
    }

    // yuv420p requires even dimensions — crop a pixel if needed.
    const int width = frames.front().width() & ~1;
    const int height = frames.front().height() & ~1;
    if (width < 2 || height < 2)
        throw std::runtime_error("Frames too small for video export");

    try {
        py::list pyFrames;
        for (const QImage& frame : frames) {
            const QImage rgb =
                frame.copy(0, 0, width, height).convertToFormat(QImage::Format_RGB888);
            // QImage pads scanlines to 4 bytes; repack rows densely.
            QByteArray packed;
            packed.reserve(width * height * 3);
            for (int y = 0; y < height; ++y)
                packed.append(reinterpret_cast<const char*>(rgb.constScanLine(y)),
                              width * 3);
            pyFrames.append(py::bytes(packed.constData(),
                                      static_cast<std::size_t>(packed.size())));
        }

        py::dict locals;
        locals["frames"] = pyFrames;
        locals["path"] = path.toStdString();
        locals["fps"] = std::max(1, fps);
        locals["width"] = width;
        locals["height"] = height;

        py::exec(R"PY(
import numpy as np
import imageio

writer = imageio.get_writer(
    path, fps=fps, codec="libx264", quality=8, pixelformat="yuv420p")
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
        throw std::runtime_error(std::string("MP4 export failed:\n") + e.what());
    }
}

} // namespace calango::pybridge
