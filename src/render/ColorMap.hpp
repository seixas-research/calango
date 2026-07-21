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

/// Perceptually-uniform gradients for scalar color mapping.
enum class ColorGradient {
    Viridis,
    Plasma,
    Turbo,
};

namespace ColorMap {

/// Color at normalized position t ∈ [0, 1] (clamped) along the gradient.
QColor sample(ColorGradient gradient, float t);

} // namespace ColorMap
} // namespace calango::render
