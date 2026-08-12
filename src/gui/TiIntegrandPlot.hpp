#pragma once

// <cstdint> must stay even when clangd calls it unused: libstdc++ from GCC 13
// no longer pulls the fixed-width integer types in transitively, so removing
// it breaks the Linux .deb build while the macOS build stays green.
#include <cstdint>

#include "core/ThermodynamicIntegration.hpp"

#include <QString>
#include <QWidget>
#include <vector>

namespace calango::gui {

/// ⟨∂U/∂λ⟩ against λ — the integrand a thermodynamic integration actually
/// integrates.
///
/// This exists because the free energy is a NUMBER and the thing that goes
/// wrong is a SHAPE. Every characteristic TI failure is visible here and
/// invisible in ΔF:
///
///   * the endpoint singularity — the curve turning over and running away as
///     λ → 0, with error bars opening out with it;
///   * an under-resolved λ grid — a few nodes strung across a steep region,
///     which the quadrature happily interpolates through;
///   * a window that never equilibrated — one point off the curve its
///     neighbours describe;
///   * hysteresis — the forward and backward sweeps not lying on top of each
///     other.
///
/// A module that reports ΔF ± σ and nothing else asks the user to trust that
/// none of those happened. The error bars are plotted at 1σ from the
/// autocorrelation-corrected estimate, not from the raw variance, because the
/// raw variance understates the uncertainty of a correlated series by exactly
/// the factor this module exists to compute.
class TiIntegrandPlot : public QWidget {
    Q_OBJECT

public:
    explicit TiIntegrandPlot(QWidget* parent = nullptr);

    /// `forward` is required; `backward` may be empty (no hysteresis sweep).
    void setWindows(std::vector<core::TiWindowSample> forward,
                    std::vector<core::TiWindowSample> backward);
    /// Marks the endpoint diagnosis on the plot when one was found.
    void setEndpointWarning(bool suspected, const QString& message);

    /// Render at an arbitrary size, so an exported figure is the figure on
    /// screen rather than a second drawing path that can drift from it.
    void render(QPainter& painter, const QRectF& bounds) const;
    bool exportImage(const QString& path, double scale) const;

    QSize sizeHint() const override { return {640, 380}; }

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    std::vector<core::TiWindowSample> forward_;
    std::vector<core::TiWindowSample> backward_;
    bool endpointSuspected_ = false;
    QString endpointMessage_;
};

} // namespace calango::gui
