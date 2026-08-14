#pragma once

// Required on Linux: libstdc++ (GCC 13+) no longer pulls <cstdint> in
// transitively. Do not remove even if clangd reports it unused on macOS.
#include <cstdint>

#include <vector>

namespace calango::core {

/// Frame order for a "ping-pong" (forward-then-reverse) animation: the indices
/// `0 .. count-1` followed by the same sequence played back, so a clip that
/// does not start where it ends still loops seamlessly.
///
/// TWO frames are dropped, and which two is the whole contract:
///
///   * the LAST frame of the forward pass is not repeated as the first of the
///     return pass — it is the same picture, and showing it twice in a row is
///     a visible stall at the turnaround;
///   * the FIRST frame is not repeated at the end of the return pass either,
///     because the player wraps straight back to it — the same stall, at the
///     loop seam this time.
///
/// So `count` frames become `2 * count - 2`:
///
///     count = 5 -> 0 1 2 3 4 3 2 1   (not 0 1 2 3 4 4 3 2 1 0)
///
/// Below three frames there is nothing to reverse — one frame is a still, and
/// two would reduce to themselves — so the forward order is returned unchanged
/// and the caller's "the video is twice as long" arithmetic must come from the
/// SIZE of this vector rather than from doubling the count itself.
///
/// Header-only and free of Qt for the same reason NeighborCellRange's offsets
/// are inline: the rule is pure arithmetic whose off-by-one behaviour at the
/// two seams is the entire feature, and it has to be checkable without linking
/// a video encoder or a dialog.
inline std::vector<int> pingPongOrder(int count)
{
    std::vector<int> order;
    if (count <= 0)
        return order;
    order.reserve(static_cast<std::size_t>(count > 2 ? 2 * count - 2 : count));
    for (int i = 0; i < count; ++i)
        order.push_back(i);
    for (int i = count - 2; i >= 1; --i)
        order.push_back(i);
    return order;
}

/// How many frames a ping-pong pass of `count` rendered frames encodes to.
/// The count the export dialog shows, and the one the duration read-out is
/// derived from.
inline int pingPongFrameCount(int count)
{
    return count > 2 ? 2 * count - 2 : (count > 0 ? count : 0);
}

} // namespace calango::core
