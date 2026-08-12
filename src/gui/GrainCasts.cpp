#include "gui/GrainCasts.hpp"

#include <algorithm>
#include <cmath>

namespace calango::gui {

QColor grainCastColor(int index)
{
    const int grain = std::max(0, index);
    // 137.508 deg — the golden angle. See the header for why a rotation beats
    // a fixed palette here.
    const double hue = std::fmod(grain * 137.508, 360.0);
    const int saturation = (grain % 3 == 1) ? 165 : 220;
    const int value = (grain % 3 == 2) ? 195 : 245;
    return QColor::fromHsv(static_cast<int>(hue), saturation, value);
}

GrainCastAssignment grainCastsFor(const std::vector<double>& grainField)
{
    GrainCastAssignment result;
    if (grainField.empty())
        return result;

    int grainCount = 0;
    for (const double value : grainField) {
        // A negative tag means "no grain" and must not stretch the count; the
        // atom lands in the fallback cast below.
        const int grain = static_cast<int>(value);
        if (grain >= 0)
            grainCount = std::max(grainCount, grain + 1);
    }
    if (grainCount < 2)
        return result;

    result.grainCount = grainCount;
    result.colors.reserve(static_cast<std::size_t>(grainCount));
    for (int grain = 0; grain < grainCount; ++grain)
        result.colors.push_back(grainCastColor(grain));

    result.atomCasts.assign(grainField.size(), 0);
    for (std::size_t i = 0; i < grainField.size(); ++i) {
        const int grain = static_cast<int>(grainField[i]);
        if (grain >= 0 && grain < grainCount)
            result.atomCasts[i] = grain + 1; // cast 0 is the fallback
    }
    return result;
}

} // namespace calango::gui
