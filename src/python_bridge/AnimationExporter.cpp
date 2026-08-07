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

/// Resolves an ffmpeg and defines the `add_frame(raw)` / `close_writer()` pair
/// every video path drives, over whichever of two routes is actually available.
///
/// The second route exists because of a Linux-only failure that was ours, not
/// the user's. The code used to require `import imageio_ffmpeg` — the PyPI
/// package that bundles a private ffmpeg binary — and Debian and Ubuntu do not
/// package it under that name (see the CPack block in CMakeLists.txt, which
/// recommends python3-imageio and suggests the system `ffmpeg` on exactly that
/// reasoning). So a machine installed from the .deb had python3-imageio and
/// /usr/bin/ffmpeg, everything the export needs, and refused every video format
/// with "install imageio-ffmpeg". macOS and Windows ship the bundled wheel and
/// never saw it.
///
/// So: use imageio when its ffmpeg companion is there, and otherwise drive the
/// system binary directly. Details of the second path that are not optional on
/// Linux —
///
///   • argv as a LIST, never a shell string: a distro ffmpeg is at a plain
///     path, but IMAGEIO_FFMPEG_EXE and a Conda prefix routinely are not, and
///     `shell=True` would split "…/Application Support/…" in half.
///   • `-y`: without it ffmpeg asks before overwriting an existing file, on the
///     same stdin the frames are being written to, and the export deadlocks
///     instead of failing.
///   • stderr to a TEMPORARY FILE, not a pipe: ffmpeg writes progress to
///     stderr, a pipe holds 64 KB on Linux, and a full pipe blocks ffmpeg while
///     we are still writing frames into its stdin — a deadlock that scales in
///     precisely with the length of the animation. A file never blocks, and it
///     is what the error message is read back from.
constexpr const char* kVideoWriterSource = R"PY(
import os
import shutil
import subprocess
import tempfile


def _bundled_ffmpeg():
    """imageio's own ffmpeg, when the wheel that carries it is installed."""
    try:
        import imageio_ffmpeg
    except Exception:
        return None
    try:
        exe = imageio_ffmpeg.get_ffmpeg_exe()
    except Exception:
        # Present but unusable: the wheel installed without its binary, or the
        # binary lost its exec bit on a noexec mount.
        return None
    return exe if exe and os.path.isfile(exe) and os.access(exe, os.X_OK) else None


def _system_ffmpeg():
    """A distribution's ffmpeg. PATH first, so a Conda env or a module load
    wins over whatever is in /usr/bin."""
    candidates = [os.environ.get("IMAGEIO_FFMPEG_EXE"), shutil.which("ffmpeg")]
    candidates += ["/usr/bin/ffmpeg", "/usr/local/bin/ffmpeg",
                   "/snap/bin/ffmpeg", "/opt/homebrew/bin/ffmpeg"]
    for exe in candidates:
        if exe and os.path.isfile(exe) and os.access(exe, os.X_OK):
            return exe
    return None


ffmpeg_exe = _bundled_ffmpeg()
via_imageio = ffmpeg_exe is not None
if ffmpeg_exe is None:
    ffmpeg_exe = _system_ffmpeg()
if ffmpeg_exe is None:
    raise RuntimeError(
        "No ffmpeg was found, so no video format can be written.\n"
        "Install either one:\n"
        "    pip install imageio-ffmpeg        (bundles its own ffmpeg)\n"
        "    apt install ffmpeg                (Debian/Ubuntu)\n"
        "    conda install -c conda-forge ffmpeg\n"
        "Or point IMAGEIO_FFMPEG_EXE at an ffmpeg binary.\n"
        "Animated GIF needs none of this and is written by Pillow.")

# Let the rest of the process (and imageio itself) reuse what we resolved.
os.environ.setdefault("IMAGEIO_FFMPEG_EXE", ffmpeg_exe)

_writer = None
_proc = None
_errfile = None

if via_imageio:
    import numpy as np
    import imageio

    _writer = imageio.get_writer(
        path, fps=fps, codec=codec, quality=8, pixelformat=pixelformat)

    def add_frame(raw):
        _writer.append_data(
            np.frombuffer(raw, dtype=np.uint8).reshape(height, width, 3))

    def close_writer():
        _writer.close()
else:
    _errfile = tempfile.TemporaryFile()
    _proc = subprocess.Popen(
        [ffmpeg_exe, "-hide_banner", "-loglevel", "error", "-y",
         "-f", "rawvideo", "-vcodec", "rawvideo",
         "-s", "%dx%d" % (width, height), "-pix_fmt", "rgb24",
         "-r", "%d" % fps, "-i", "-", "-an",
         "-vcodec", codec, "-pix_fmt", pixelformat, path],
        stdin=subprocess.PIPE, stdout=subprocess.DEVNULL, stderr=_errfile)

    def add_frame(raw):
        try:
            _proc.stdin.write(raw)
        except BrokenPipeError:
            # ffmpeg died mid-stream — an unsupported encoder is the usual
            # reason. Its own message is the useful one, so surface that
            # instead of the pipe error.
            close_writer()
            raise

    def close_writer():
        if _proc is None:
            return
        try:
            if _proc.stdin and not _proc.stdin.closed:
                _proc.stdin.close()
        except Exception:
            pass
        code = _proc.wait()
        _errfile.seek(0)
        message = _errfile.read().decode("utf-8", "replace").strip()
        _errfile.close()
        globals()["_proc"] = None
        if code != 0:
            raise RuntimeError(
                "ffmpeg failed (exit %d) writing %s with codec '%s'.\n%s\n\n"
                "An 'Unknown encoder' here means this ffmpeg was built without "
                "that codec — libx265 and libvpx-vp9 in particular are absent "
                "from some minimal builds. H.264 (libx264) is the most widely "
                "available choice."
                % (code, os.path.basename(path), codec,
                   message or "(no diagnostic output)"))
)PY";

/// Build the writer described above. Returns the scope holding `add_frame` and
/// `close_writer`; the caller drives them and MUST call close_writer() on every
/// exit path, because by then a subprocess and a half-written file exist.
py::dict makeVideoWriter(const QString& path, int fps, int width, int height,
                         const QString& codec, const QString& pixelFormat)
{
    py::dict scope;
    scope["path"] = path.toStdString();
    scope["fps"] = std::max(1, fps);
    scope["width"] = width;
    scope["height"] = height;
    scope["codec"] = codec.toStdString();
    scope["pixelformat"] = pixelFormat.toStdString();
    try {
        // locals doubles as globals: script-defined functions must see the
        // script's own names (see AseBridge::symmetryInfo).
        py::exec(kVideoWriterSource, scope, scope);
    } catch (const py::error_already_set& e) {
        throw std::runtime_error(std::string("Video export failed:\n") + e.what());
    }
    return scope;
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

    // yuv420p requires even dimensions — crop a pixel if needed.
    const int width = frames.front().width() & ~1;
    const int height = frames.front().height() & ~1;
    if (width < 2 || height < 2)
        throw std::runtime_error("Frames too small for video export");

    py::dict scope =
        makeVideoWriter(path, fps, width, height, codec, pixelFormat);

    // Fed one frame at a time rather than as a list of every frame converted up
    // front: the list held a second full copy of the whole animation in memory,
    // which for a 4K film is gigabytes for no reason.
    const auto closeWriter = [&scope] {
        try {
            scope["close_writer"]();
        } catch (const py::error_already_set&) {
            // Already reporting a more useful failure; don't mask it.
        }
    };

    try {
        auto addFrame = scope["add_frame"];
        for (const QImage& frame : frames) {
            const QByteArray packed = packRgb(frame, width, height);
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

    // NOT in the guard above: closing is where ffmpeg's exit status is
    // collected, so a failure here is the encoder's own diagnostic and has to
    // reach the caller rather than be swallowed.
    try {
        scope["close_writer"]();
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

    // The whole stream is sized from frame 0; yuv420p requires even
    // dimensions, so crop a pixel where needed (as the in-memory path does).
    const QImage first = loadFrameOrThrow(framePaths, 0);
    const int width = first.width() & ~1;
    const int height = first.height() & ~1;
    if (width < 2 || height < 2)
        throw std::runtime_error("Frames too small for video export");

    py::dict locals =
        makeVideoWriter(path, fps, width, height, codec, pixelFormat);

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

    // Deliberately NOT closeWriter(): closing is where ffmpeg's exit status is
    // collected, and an encoder that refused the codec reports it only here.
    // Swallowing it would leave an unplayable file and a success message.
    try {
        locals["close_writer"]();
    } catch (const py::error_already_set& e) {
        throw std::runtime_error(std::string("Video export failed:\n") + e.what());
    }
}

} // namespace calango::pybridge
