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
};

namespace ColorMap {

/// Color at normalized position t ∈ [0, 1] (clamped) along the gradient.
QColor sample(ColorGradient gradient, float t);

} // namespace ColorMap
} // namespace calango::render
