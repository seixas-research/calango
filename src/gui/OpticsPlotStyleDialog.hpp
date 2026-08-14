#pragma once

#include <QColor>
#include <QDialog>
#include <QFont>
#include <QString>

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QFontComboBox;
class QPushButton;
class QSpinBox;

namespace calango::gui {

/// Appearance of the optical-spectrum plot.
///
/// Deliberately separate from `BandPdosView::Style`, which serves the band /
/// PDOS viewers: that struct is built around two spin channels, a Fermi
/// reference line and a filled DOS panel, none of which exist here. Sharing it
/// would mean carrying dead fields in both directions and a dialog that hides
/// half its controls depending on caller.
struct OpticsPlotStyle {
    QColor canvasBackground{255, 255, 255};  ///< the whole widget
    QColor plotBackground{255, 255, 255};    ///< inside the axes only

    /// Curves normally cycle through a palette so ε₁ and ε₂ (or n and k) are
    /// distinguishable. `overrideCurveColor` collapses that to one stroke,
    /// which is what a single-curve figure usually wants.
    bool overrideCurveColor = false;
    QColor curveColor{0x1f, 0x77, 0xb4};
    double lineWidth = 1.8;
    Qt::PenStyle lineStyle = Qt::SolidLine;

    QString axisFontFamily;  ///< empty = the application font
    int axisFontSize = 10;
    QColor axisLabelColor{40, 40, 40};

    bool showGrid = true;
    QColor gridColor{228, 228, 228};
    double gridAlpha = 1.0;  ///< [0, 1], folded into gridColor's alpha

    /// Threshold-corridor styling, used by the convergence viewers' band.
    /// Inert for windows that draw no band; the dialog offers these controls
    /// only when asked to (see the second constructor). Default: a quiet
    /// solid gray — the corridor is context, and the earlier green hatch
    /// competed with the curve; the hatch patterns remain available in
    /// Customize Appearance.
    QColor thresholdBandColor{128, 128, 128};
    Qt::BrushStyle thresholdBandPattern = Qt::SolidPattern;
    double thresholdBandOpacity = 0.30;  ///< [0, 1]

    QFont axisFont() const;
    /// `gridColor` with `gridAlpha` applied to its alpha channel.
    QColor effectiveGridColor() const;
    /// `thresholdBandColor` with `thresholdBandOpacity` applied.
    QColor effectiveThresholdBandColor() const;
};

/// "Customize Appearance…" for the optics viewers. Edits apply live through
/// styleChanged() so the result is judged against the real plot, not a preview
/// that might not match it.
class OpticsPlotStyleDialog : public QDialog {
    Q_OBJECT

public:
    explicit OpticsPlotStyleDialog(const OpticsPlotStyle& style,
                                   QWidget* parent = nullptr);
    /// `withThresholdBand` adds the "Threshold Band" group (color, hatch
    /// pattern, opacity) — for the convergence viewers, whose corridor is
    /// part of the figure. The spectrum windows keep the shorter dialog.
    OpticsPlotStyleDialog(const OpticsPlotStyle& style, bool withThresholdBand,
                          QWidget* parent = nullptr);

    OpticsPlotStyle style() const { return style_; }

Q_SIGNALS:
    void styleChanged(const OpticsPlotStyle& style);

private Q_SLOTS:
    void restoreDefaults();

private:
    /// Colour button that opens a picker, writes into `target` and re-emits.
    QPushButton* colorButton(QColor* target);
    void emitStyle();
    void syncToControls();

    OpticsPlotStyle style_;

    QPushButton* canvasButton_ = nullptr;
    QPushButton* plotButton_ = nullptr;
    QPushButton* curveButton_ = nullptr;
    QPushButton* labelButton_ = nullptr;
    QPushButton* gridButton_ = nullptr;
    QCheckBox* overrideCurveCheck_ = nullptr;
    QDoubleSpinBox* lineWidthSpin_ = nullptr;
    QComboBox* lineStyleCombo_ = nullptr;
    QFontComboBox* fontCombo_ = nullptr;
    QSpinBox* fontSizeSpin_ = nullptr;
    QCheckBox* gridCheck_ = nullptr;
    QDoubleSpinBox* gridAlphaSpin_ = nullptr;
    // Threshold-band controls; null unless the dialog was built with them.
    QPushButton* bandButton_ = nullptr;
    QComboBox* bandPatternCombo_ = nullptr;
    QDoubleSpinBox* bandOpacitySpin_ = nullptr;
};

} // namespace calango::gui
