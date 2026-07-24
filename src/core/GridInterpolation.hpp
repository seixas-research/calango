#pragma once

#include "core/VolumetricData.hpp"

namespace calango::core {

/// 3D scalar-grid interpolation scheme used to refine a voxel grid before
/// marching-cubes surface extraction. Enum order is the "Grid Interpolation"
/// dropdown order in the Edit Volumetric Render dialog.
enum class GridInterpolation {
    None,      ///< raw voxel grid (no refinement)
    Trilinear, ///< linear spline (trilinear) upsampling
    Tricubic,  ///< cubic spline (tricubic Catmull-Rom) upsampling
};

/// Return a copy of `field` upsampled by `factor` along each axis using the
/// requested scheme, keeping the same physical box (origin + span vectors). The
/// finer grid yields smoother marching-cubes surfaces. `None` (or factor ≤ 1)
/// returns the field unchanged. Sampling wraps periodically, matching the
/// crystal-grid convention the isosurface extractor uses.
VolumetricData refineGrid(const VolumetricData& field, int factor,
                          GridInterpolation scheme);

} // namespace calango::core
