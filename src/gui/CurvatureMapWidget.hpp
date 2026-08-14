#pragma once

// Required on Linux: libstdc++ (GCC 13+) no longer pulls <cstdint> in
// transitively. Do not remove even if clangd reports it unused on macOS.
#include <cstdint>

#include "core/BerryPhase.hpp"
#include "render/ColorMap.hpp"

#include <QImage>
#include <QWidget>

namespace calango::gui {

/// Berry curvature Ω(k) over a k-plane, as a colour map.
///
/// Reuses render::ColorMap, the same gradient set the volumetric, 2D-bands and
/// effective-band viewers draw from, so a curvature map can be made to match a
/// figure the rest of the application already produces.
///
/// The default gradient is DIVERGING (Coolwarm) rather than perceptually
/// uniform, and that is deliberate: curvature is a signed quantity whose
/// interesting structure is where it changes sign, and a sequential map hides
/// exactly that. The scale is symmetric about zero for the same reason.
class CurvatureMapWidget : public QWidget {
    Q_OBJECT

public:
    explicit CurvatureMapWidget(QWidget* parent = nullptr);

    void setMap(const core::BerryPhase::CurvatureMap& map);
    void setGradient(render::ColorGradient gradient);
    void clear();

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    void rebuild();

    core::BerryPhase::CurvatureMap map_;
    QImage image_;
    render::ColorGradient gradient_ = render::ColorGradient::Coolwarm;
    bool hasData_ = false;
};

} // namespace calango::gui
