#include "gui/VolumetricStyle.hpp"

namespace calango::gui {

const QVector<render::ColorGradient>& volumetricGradients()
{
    // Each entry is a distinct ramp — no two share a color progression, so the
    // list stays a genuine palette rather than near-duplicates of one another.
    static const QVector<render::ColorGradient> kGradients{
        render::ColorGradient::Viridis,  render::ColorGradient::Plasma,
        render::ColorGradient::Inferno,  render::ColorGradient::Magma,
        render::ColorGradient::Cividis,  render::ColorGradient::Afmhot,
        render::ColorGradient::Hot,      render::ColorGradient::Spectral,
        render::ColorGradient::Greys,    render::ColorGradient::Rainbow,
        render::ColorGradient::Gnuplot};
    return kGradients;
}

QStringList volumetricGradientNames()
{
    return {QStringLiteral("Viridis"),  QStringLiteral("Plasma"),
            QStringLiteral("Inferno"),  QStringLiteral("Magma"),
            QStringLiteral("Cividis"),  QStringLiteral("Afmhot"),
            QStringLiteral("Hot"),      QStringLiteral("Spectral"),
            QStringLiteral("Greys"),    QStringLiteral("Rainbow"),
            QStringLiteral("Gnuplot")};
}

} // namespace calango::gui
