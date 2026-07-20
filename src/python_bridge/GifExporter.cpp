#include "python_bridge/GifExporter.hpp"

#include <pybind11/embed.h>

#include <algorithm>
#include <stdexcept>

namespace py = pybind11;

namespace calango::pybridge {

void GifExporter::exportGif(const std::vector<QImage>& frames,
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
                 py::globals(), locals);
    } catch (const py::error_already_set& e) {
        throw std::runtime_error(std::string("GIF export failed:\n") + e.what());
    }
}

} // namespace calango::pybridge
