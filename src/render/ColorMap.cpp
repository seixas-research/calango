#include "render/ColorMap.hpp"

#include <algorithm>
#include <array>
#include <cstddef>

namespace calango::render {

namespace {

struct Rgb {
    float r, g, b;
};

// 9 anchors at t = 0, 0.125, ..., 1.0, linearly interpolated. Anchors are
// taken from the reference matplotlib (viridis, plasma) and Google AI
// (turbo) colormaps — visually indistinguishable from the full 256-entry
// tables at sphere-shading resolution.
constexpr std::array<Rgb, 9> kViridis{{
    {0.267f, 0.005f, 0.329f},
    {0.283f, 0.141f, 0.458f},
    {0.254f, 0.265f, 0.530f},
    {0.207f, 0.372f, 0.553f},
    {0.164f, 0.471f, 0.558f},
    {0.128f, 0.567f, 0.551f},
    {0.135f, 0.659f, 0.518f},
    {0.267f, 0.749f, 0.441f},
    {0.993f, 0.906f, 0.144f},
}};

constexpr std::array<Rgb, 9> kPlasma{{
    {0.050f, 0.030f, 0.528f},
    {0.294f, 0.012f, 0.631f},
    {0.492f, 0.012f, 0.658f},
    {0.658f, 0.134f, 0.588f},
    {0.798f, 0.280f, 0.470f},
    {0.899f, 0.422f, 0.361f},
    {0.973f, 0.585f, 0.252f},
    {0.993f, 0.771f, 0.155f},
    {0.940f, 0.975f, 0.131f},
}};

constexpr std::array<Rgb, 9> kTurbo{{
    {0.190f, 0.072f, 0.232f},
    {0.276f, 0.408f, 0.926f},
    {0.100f, 0.706f, 0.902f},
    {0.169f, 0.916f, 0.594f},
    {0.634f, 0.999f, 0.223f},
    {0.933f, 0.812f, 0.227f},
    {0.984f, 0.552f, 0.154f},
    {0.848f, 0.248f, 0.053f},
    {0.480f, 0.016f, 0.011f},
}};

const std::array<Rgb, 9>& anchors(ColorGradient gradient)
{
    switch (gradient) {
    case ColorGradient::Plasma:
        return kPlasma;
    case ColorGradient::Turbo:
        return kTurbo;
    case ColorGradient::Viridis:
        break;
    }
    return kViridis;
}

} // namespace

QColor ColorMap::sample(ColorGradient gradient, float t)
{
    const auto& table = anchors(gradient);
    t = std::clamp(t, 0.0f, 1.0f);
    const float scaled = t * static_cast<float>(table.size() - 1);
    const auto low = static_cast<std::size_t>(scaled);
    const auto high = std::min(low + 1, table.size() - 1);
    const float frac = scaled - static_cast<float>(low);

    const Rgb& a = table[low];
    const Rgb& b = table[high];
    return QColor::fromRgbF(a.r + (b.r - a.r) * frac,
                            a.g + (b.g - a.g) * frac,
                            a.b + (b.b - a.b) * frac);
}

} // namespace calango::render
