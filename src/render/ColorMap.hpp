#pragma once

#include <QColor>

namespace calango::render {

/// What drives per-atom colors in the viewport.
enum class ColorMode {
    Element,                  ///< CPK palette (+ user overrides) — the default
    CoordinationNumber,       ///< discrete CN mapped onto a gradient
    GeneralizedCoordination,  ///< continuous GCN mapped onto a gradient
    CustomScalar,             ///< any per-atom scalar field (charges, |forces|, ...)
};

/// Gradients for scalar color mapping. Viridis…Cividis are the
/// perceptually-uniform matplotlib family; Hot/Afmhot are the classic
/// black-body ramps. Enum order is the Representation-panel combo order —
/// append only, existing values are persisted in project files.
enum class ColorGradient {
    Viridis,
    Plasma,
    Turbo,
    Inferno,
    Magma,
    Cividis,
    Hot,
    Afmhot,
    Coolwarm, ///< diverging blue-white-red (potentials, signed fields)
    Rainbow,  ///< classic violet-to-red spectral ramp
};

namespace ColorMap {

/// Color at normalized position t ∈ [0, 1] (clamped) along the gradient.
/// `inverted` reverses the mapping (t -> 1 - t), so minima take the high
/// end of the palette and maxima the low end — matplotlib's "_r" variants.
QColor sample(ColorGradient gradient, float t, bool inverted = false);

} // namespace ColorMap
} // namespace calango::render
