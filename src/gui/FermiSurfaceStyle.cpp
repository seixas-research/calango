#include "gui/FermiSurfaceStyle.hpp"

#include <QJsonArray>

namespace calango::gui {

namespace {

/// The "clamp to the last enumerator, fall back to the current value if the
/// key is absent" idiom MainWindow.cpp's own project-JSON reader uses for
/// every enum it restores — repeated here rather than shared, since the two
/// live in different translation units with no common small-utility header
/// for it yet.
template <typename Enum>
Enum clampedEnum(const QJsonObject& json, const QString& key, Enum current,
                 Enum maxValue)
{
    const int raw = json.value(key).toInt(static_cast<int>(current));
    if (raw < 0 || raw > static_cast<int>(maxValue))
        return current;
    return static_cast<Enum>(raw);
}

} // namespace

FermiSurfaceStyle readFermiSurfaceStyle(const QJsonObject& json)
{
    FermiSurfaceStyle style; // every field starts at its default

    style.meshMode = clampedEnum(json, QStringLiteral("meshMode"), style.meshMode,
                                 FermiSurfaceMeshMode::Combined);
    style.colorMode =
        clampedEnum(json, QStringLiteral("colorMode"), style.colorMode,
                   FermiSurfaceColorMode::ByVelocity);
    style.shading = clampedEnum(json, QStringLiteral("shading"), style.shading,
                                IsoShading::Glossy);
    style.ambient = json.value(QStringLiteral("ambient")).toDouble(style.ambient);
    style.specular =
        json.value(QStringLiteral("specular")).toDouble(style.specular);
    style.opacity = json.value(QStringLiteral("opacity")).toDouble(style.opacity);
    style.wireframeOverlay =
        json.value(QStringLiteral("wireframeOverlay")).toBool(style.wireframeOverlay);

    style.perBandColors.clear();
    const QJsonArray bandColors = json.value(QStringLiteral("perBandColors")).toArray();
    for (const QJsonValue& v : bandColors) {
        // An explicitly empty string marks "no override for this band" (its
        // index still has to line up with the band list), rather than
        // shortening the array and shifting every later band's override.
        const QColor c(v.toString());
        style.perBandColors.push_back(c);
    }
    style.bandGradient =
        clampedEnum(json, QStringLiteral("bandGradient"), style.bandGradient,
                   render::ColorGradient::Gnuplot);
    if (const QColor combined(json.value(QStringLiteral("combinedColor")).toString());
        combined.isValid())
        style.combinedColor = combined;
    style.invertGradient =
        json.value(QStringLiteral("invertGradient")).toBool(style.invertGradient);
    style.velocityUseBounds = json.value(QStringLiteral("velocityUseBounds"))
                                  .toBool(style.velocityUseBounds);
    style.velocityMin =
        json.value(QStringLiteral("velocityMin")).toDouble(style.velocityMin);
    style.velocityMax =
        json.value(QStringLiteral("velocityMax")).toDouble(style.velocityMax);

    style.showZoneEdges =
        json.value(QStringLiteral("showZoneEdges")).toBool(style.showZoneEdges);
    if (const QColor zoneColor(json.value(QStringLiteral("zoneEdgeColor")).toString());
        zoneColor.isValid())
        style.zoneEdgeColor = zoneColor;
    style.zoneEdgeWidth =
        json.value(QStringLiteral("zoneEdgeWidth")).toDouble(style.zoneEdgeWidth);
    style.showAxes = json.value(QStringLiteral("showAxes")).toBool(style.showAxes);
    style.clipToFirstZone =
        json.value(QStringLiteral("clipToFirstZone")).toBool(style.clipToFirstZone);

    style.interpolation =
        clampedEnum(json, QStringLiteral("interpolation"), style.interpolation,
                   core::GridInterpolation::Tricubic);
    style.refine = json.value(QStringLiteral("refine")).toInt(style.refine);
    style.meshSmoothing =
        json.value(QStringLiteral("meshSmoothing")).toInt(style.meshSmoothing);

    style.energyOffset =
        json.value(QStringLiteral("energyOffset")).toDouble(style.energyOffset);

    return style;
}

QJsonObject writeFermiSurfaceStyle(const FermiSurfaceStyle& style)
{
    QJsonObject json;
    json[QStringLiteral("meshMode")] = static_cast<int>(style.meshMode);
    json[QStringLiteral("colorMode")] = static_cast<int>(style.colorMode);
    json[QStringLiteral("shading")] = static_cast<int>(style.shading);
    json[QStringLiteral("ambient")] = style.ambient;
    json[QStringLiteral("specular")] = style.specular;
    json[QStringLiteral("opacity")] = style.opacity;
    json[QStringLiteral("wireframeOverlay")] = style.wireframeOverlay;

    QJsonArray bandColors;
    for (const QColor& c : style.perBandColors)
        bandColors.push_back(c.isValid() ? c.name() : QString());
    json[QStringLiteral("perBandColors")] = bandColors;
    json[QStringLiteral("bandGradient")] = static_cast<int>(style.bandGradient);
    json[QStringLiteral("combinedColor")] = style.combinedColor.name();
    json[QStringLiteral("invertGradient")] = style.invertGradient;
    json[QStringLiteral("velocityUseBounds")] = style.velocityUseBounds;
    json[QStringLiteral("velocityMin")] = style.velocityMin;
    json[QStringLiteral("velocityMax")] = style.velocityMax;

    json[QStringLiteral("showZoneEdges")] = style.showZoneEdges;
    json[QStringLiteral("zoneEdgeColor")] = style.zoneEdgeColor.name();
    json[QStringLiteral("zoneEdgeWidth")] = style.zoneEdgeWidth;
    json[QStringLiteral("showAxes")] = style.showAxes;
    json[QStringLiteral("clipToFirstZone")] = style.clipToFirstZone;

    json[QStringLiteral("interpolation")] = static_cast<int>(style.interpolation);
    json[QStringLiteral("refine")] = style.refine;
    json[QStringLiteral("meshSmoothing")] = style.meshSmoothing;

    json[QStringLiteral("energyOffset")] = style.energyOffset;
    return json;
}

} // namespace calango::gui
