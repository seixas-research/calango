#pragma once

// Required on Linux: libstdc++ (GCC 13+) no longer pulls <cstdint> in
// transitively. Do not remove even if clangd reports it unused on macOS.
#include <cstdint>

#include "gui/SpectralHeatmapWidget.hpp"

#include <QDialog>

class QDoubleSpinBox;
class QPushButton;

namespace calango::gui {

/// "Customize Appearance…" for the effective band structure.
///
/// The counterpart of PlotStyleDialog, which does the same job for the
/// ordinary band/PDOS plot. Kept as a separate class rather than generalising
/// that one: the two styles overlap in their chrome and share nothing in the
/// part that matters — one styles dispersion curves, this one styles a
/// spectral weight field — and a single dialog covering both would be a
/// switch on which half to show.
///
/// Emits on every edit, so the plot follows the control live; there is no
/// Apply button and nothing to forget to press.
class SpectralPlotStyleDialog : public QDialog {
    Q_OBJECT

public:
    SpectralPlotStyleDialog(const SpectralHeatmapWidget::Style& style,
                            QWidget* parent = nullptr);

    /// Seed the energy-window spin boxes from the data's own range.
    void setEnergyBounds(double minimum, double maximum);

Q_SIGNALS:
    void styleChanged(const SpectralHeatmapWidget::Style& style);
    void energyWindowChanged(double minimum, double maximum);

private:
    /// A colour swatch button that edits `target` in place and re-emits.
    QPushButton* colorButton(QColor* target);
    void emitStyle();

    SpectralHeatmapWidget::Style style_;
    QDoubleSpinBox* energyMin_ = nullptr;
    QDoubleSpinBox* energyMax_ = nullptr;
};

} // namespace calango::gui
