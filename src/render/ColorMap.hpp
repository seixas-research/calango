#pragma once

#include <QColor>

namespace calango::render {

/// What drives per-atom colors in the viewport. Saved by numeric value in
/// .calproj project files — append only, like ColorGradient below.
enum class ColorMode {
    Element,                  ///< CPK palette (+ user overrides) — the default
    CoordinationNumber,       ///< discrete CN mapped onto a gradient
    GeneralizedCoordination,  ///< continuous GCN mapped onto a gradient
    CustomScalar,             ///< any per-atom scalar field (charges, |forces|, ...)
    /// One flat colour per CAST: every atom takes the colour of the cast it
    /// belongs to (explicit pick, or a default qualitative cycle — see
    /// StructureRenderer::castColor()). Not a scalar mapping: casts are
    /// nominal groups, so there is no gradient and no legend range.
    Cast,
    /// One flat colour per identified LOCAL STRUCTURE — fcc, hcp, bcc,
    /// icosahedral, cubic/hexagonal diamond, or none of them (see
    /// core::identifyStructuralPhases).
    ///
    /// Nominal like Cast rather than scalar like CN: "hcp" is not a larger
    /// number than "fcc", and putting the seven labels on a gradient would
    /// invent an ordering that means nothing. Colours come from the phase
    /// palette in the Style, edited through "Phase colors…".
    Phase,
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
    Greys,    ///< perceptual light-to-dark grayscale (matplotlib "Greys")
    Spectral, ///< diverging red-yellow-blue (matplotlib "Spectral")
    Gnuplot,  ///< gnuplot's pm3d ramp: √t, t³, sin(2πt) — black-purple-yellow
};

namespace ColorMap {

/// Color at normalized position t ∈ [0, 1] (clamped) along the gradient.
/// `inverted` reverses the mapping (t -> 1 - t), so minima take the high
/// end of the palette and maxima the low end — matplotlib's "_r" variants.
QColor sample(ColorGradient gradient, float t, bool inverted = false);

} // namespace ColorMap
} // namespace calango::render
