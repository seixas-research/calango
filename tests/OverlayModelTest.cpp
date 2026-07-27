// Overlay geometry test.
//
// The "Additional overlays" dock tessellates every entry into ONE pair of
// shared streams, slicing them with per-overlay ranges so each blends at its
// own opacity. That sharing is where the defects live: a range whose first/count
// does not match where the triangles actually landed draws the wrong overlay at
// the wrong transparency, or reads past the buffer — and neither throws, both
// just produce a wrong picture.
//
// So this checks the invariant the renderer relies on: the ranges must exactly
// partition the face stream, in order, with nothing overlapping and nothing
// left over.
//
// GUI-free, GL-free.

#include "gui/OverlayModel.hpp"

#include "core/Structure.hpp"
#include "core/UnitCell.hpp"

#include <QColor>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

using namespace calango;

namespace {

int failures = 0;

void check(bool condition, const std::string& what)
{
    std::printf("  %s %s\n", condition ? "ok  " : "FAIL", what.c_str());
    if (!condition)
        ++failures;
}

core::Structure cubicCell()
{
    core::Structure structure;
    structure.setCell(core::UnitCell({6, 0, 0}, {0, 6, 0}, {0, 0, 6}));
    core::Atom atom;
    atom.atomicNumber = 14;
    atom.position = {1.0, 1.0, 1.0};
    structure.addAtom(atom);
    return structure;
}

gui::Overlay make(gui::Overlay::Kind kind, int id)
{
    gui::Overlay overlay;
    overlay.id = id;
    overlay.kind = kind;
    overlay.center = {0, 0, 0};
    overlay.endPoint = {0, 0, 3};
    overlay.resolution = 10; // small, so the streams stay checkable
    return overlay;
}

/// Every invariant the renderer's per-range draw loop depends on.
void checkStreams(const std::vector<float>& faces,
                  const std::vector<float>& edges,
                  const std::vector<render::StructureRenderer::OverlayRange>& ranges,
                  const std::string& what)
{
    check(faces.size() % 6 == 0, what + ": the face stream is whole vertices");
    check(edges.size() % 6 == 0, what + ": the edge stream is whole vertices");
    const int vertices = static_cast<int>(faces.size() / 6);

    // Ranges must tile the stream head to tail. A gap means geometry that is
    // never drawn; an overlap means geometry drawn twice at two opacities.
    int cursor = 0;
    bool ordered = true;
    for (const auto& range : ranges) {
        if (range.first != cursor || range.count <= 0)
            ordered = false;
        cursor += range.count;
    }
    check(ordered, what + ": ranges tile the face stream without gaps");
    check(cursor == vertices,
          what + ": the ranges account for every face vertex");
    const bool inRange = std::all_of(
        ranges.begin(), ranges.end(),
        [vertices](const render::StructureRenderer::OverlayRange& r) {
            return r.first >= 0 && r.first + r.count <= vertices;
        });
    check(inRange, what + ": no range reads past the face stream");
    const bool alphas = std::all_of(
        ranges.begin(), ranges.end(),
        [](const render::StructureRenderer::OverlayRange& r) {
            return r.alpha > 0.0f && r.alpha <= 1.0f;
        });
    check(alphas, what + ": every range carries a usable alpha");
}

} // namespace

int main()
{
    const core::Structure structure = cubicCell();

    std::printf("Every geometric kind tessellates:\n");
    for (const auto kind :
         {gui::Overlay::Kind::Box, gui::Overlay::Kind::Sphere,
          gui::Overlay::Kind::Ellipsoid, gui::Overlay::Kind::Tube,
          gui::Overlay::Kind::Cone, gui::Overlay::Kind::Plane,
          gui::Overlay::Kind::Disk, gui::Overlay::Kind::LatticePlane}) {
        std::vector<float> faces, edges;
        std::vector<render::StructureRenderer::OverlayRange> ranges;
        gui::appendOverlayGeometry(make(kind, 1), &structure, faces, edges,
                                   ranges);
        const std::string name =
            gui::Overlay::kindName(kind).toStdString();
        check(!faces.empty(), name + " produces geometry");
        check(ranges.size() == 1, name + " produces exactly one range");
        checkStreams(faces, edges, ranges, name);
    }

    std::printf("Text contributes no geometry:\n");
    {
        // Text is painted by the viewport, not tessellated. Emitting an empty
        // range for it would leave a zero-count entry the renderer skips —
        // harmless, but it would also break the tiling invariant above.
        gui::Overlay text = make(gui::Overlay::Kind::Text, 1);
        text.text = QStringLiteral("hello");
        std::vector<float> faces, edges;
        std::vector<render::StructureRenderer::OverlayRange> ranges;
        gui::appendOverlayGeometry(text, &structure, faces, edges, ranges);
        check(faces.empty() && edges.empty() && ranges.empty(),
              "a text overlay appends nothing to the geometry streams");
    }

    std::printf("Several overlays share one stream correctly:\n");
    {
        std::vector<float> faces, edges;
        std::vector<render::StructureRenderer::OverlayRange> ranges;
        auto sphere = make(gui::Overlay::Kind::Sphere, 1);
        sphere.opacity = 0.25;
        auto box = make(gui::Overlay::Kind::Box, 2);
        box.opacity = 0.9;
        auto plane = make(gui::Overlay::Kind::LatticePlane, 3);
        plane.opacity = 0.5;
        auto text = make(gui::Overlay::Kind::Text, 4);
        text.text = QStringLiteral("label");
        for (const gui::Overlay& overlay : {sphere, box, plane, text})
            gui::appendOverlayGeometry(overlay, &structure, faces, edges, ranges);

        check(ranges.size() == 3, "three geometric overlays, three ranges");
        checkStreams(faces, edges, ranges, "mixed scene");
        // Each overlay must keep ITS opacity: the whole point of ranges.
        check(std::abs(ranges[0].alpha - 0.25f) < 1e-6f
                  && std::abs(ranges[1].alpha - 0.9f) < 1e-6f
                  && std::abs(ranges[2].alpha - 0.5f) < 1e-6f,
              "each range keeps its own overlay's opacity, in order");
    }

    std::printf("Visibility and degenerate input:\n");
    {
        std::vector<float> faces, edges;
        std::vector<render::StructureRenderer::OverlayRange> ranges;
        auto hidden = make(gui::Overlay::Kind::Sphere, 1);
        hidden.visible = false;
        gui::appendOverlayGeometry(hidden, &structure, faces, edges, ranges);
        check(faces.empty() && ranges.empty(),
              "a hidden overlay contributes nothing");
    }
    {
        // A lattice plane without a lattice: it must decline rather than
        // inventing an orientation, and must not leave a range behind
        // describing triangles it never wrote.
        std::vector<float> faces, edges;
        std::vector<render::StructureRenderer::OverlayRange> ranges;
        gui::appendOverlayGeometry(make(gui::Overlay::Kind::LatticePlane, 1),
                                   nullptr, faces, edges, ranges);
        check(faces.empty() && ranges.empty(),
              "a lattice plane with no structure emits nothing, not an "
              "empty range");
    }
    {
        // A zero-length tube has no axis, so its orthonormal basis degenerates.
        std::vector<float> faces, edges;
        std::vector<render::StructureRenderer::OverlayRange> ranges;
        auto tube = make(gui::Overlay::Kind::Tube, 1);
        tube.endPoint = tube.center;
        gui::appendOverlayGeometry(tube, &structure, faces, edges, ranges);
        check(faces.empty() && ranges.empty(),
              "a zero-length tube is declined rather than producing NaNs");
    }

    std::printf("Wireframe goes to the edge stream:\n");
    {
        std::vector<float> faces, edges;
        std::vector<render::StructureRenderer::OverlayRange> ranges;
        auto wire = make(gui::Overlay::Kind::Box, 1);
        wire.texture = gui::Overlay::TextureStyle::Wireframe;
        gui::appendOverlayGeometry(wire, &structure, faces, edges, ranges);
        // Wireframe is the absence of a fill: no faces, hence no range, and
        // three edges per triangle.
        check(faces.empty() && ranges.empty(),
              "a wireframe overlay writes no filled faces");
        check(!edges.empty(), "and writes edges instead");
        check(edges.size() % 12 == 0,
              "the edge stream is whole segments (2 vertices each)");
    }

    std::printf("List labels identify the entry:\n");
    {
        auto plane = make(gui::Overlay::Kind::LatticePlane, 1);
        plane.miller[0] = 1;
        plane.miller[1] = 1;
        plane.miller[2] = 0;
        check(plane.displayName().contains(QStringLiteral("1 1 0")),
              "a lattice plane names its Miller indices");
        auto text = make(gui::Overlay::Kind::Text, 2);
        text.text = QStringLiteral("Fermi level");
        check(text.displayName().contains(QStringLiteral("Fermi level")),
              "a text overlay shows its text");
        auto named = make(gui::Overlay::Kind::Sphere, 3);
        named.name = QStringLiteral("active site");
        check(named.displayName().contains(QStringLiteral("active site"))
                  && named.displayName().contains(QStringLiteral("Sphere")),
              "an explicit name is shown beside the type");
    }

    std::printf(failures == 0 ? "\nAll overlay-model checks passed.\n"
                              : "\n%d check(s) FAILED.\n",
                failures);
    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
