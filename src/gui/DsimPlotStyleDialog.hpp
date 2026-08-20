#pragma once

#include "gui/PlotPalette.hpp"

#include <QColor>
#include <QDialog>

class QCheckBox;
class QPushButton;

namespace calango::gui {

/// Appearance of the DSIM DeltaH_mix(x) plot. Deliberately small — the
/// curve is one series plus two short tangent segments (Eq. 8), not a
/// multi-axis spectrum, so there is no grid/font/threshold-band struct to
/// reuse here any more than EnergyDiagramStyle reuses OpticsPlotStyle.
struct DsimPlotStyle {
    QColor curveColor = PlotPalette::series;
    QColor tangentColor = PlotPalette::reference;
    /// The dilute-limit tangent lines at x=0/x=1 (Eq. 8) — on by default,
    /// same "black dashed line" role as the paper's own Fig. 1(e)/2, but
    /// toggleable since they can clutter a curve that is itself nearly
    /// linear end to end.
    bool showTangents = true;
    /// kJ/mol (every figure in the paper) vs. eV/atom — a direct way to
    /// cross-check the plotted scale against a hand calculation without
    /// leaving the viewer.
    bool useKilojoulesPerMole = true;
    /// The ternary plot's colour-scale legend (N=3 only; no effect on the
    /// binary/pairwise views). On by default, but a full-width triangle
    /// reads the composition triangle's own dashed gridlines and corner
    /// labels more easily than one sharing its width with a colorbar.
    bool showColorbar = true;
};

/// "Customize Appearance…" for the DSIM results viewer, same live-update
/// convention as EnergyDiagramStyleDialog/OpticsPlotStyleDialog: every
/// control writes through immediately via styleChanged().
class DsimPlotStyleDialog : public QDialog {
    Q_OBJECT

public:
    explicit DsimPlotStyleDialog(const DsimPlotStyle& style, QWidget* parent = nullptr);

Q_SIGNALS:
    void styleChanged(const DsimPlotStyle& style);

private:
    QPushButton* colorButton(QColor* target);
    void syncToControls();
    void restoreDefaults();
    void emitStyle();

    DsimPlotStyle style_;
    QPushButton* curveButton_ = nullptr;
    QPushButton* tangentButton_ = nullptr;
    QCheckBox* showTangentsCheck_ = nullptr;
    QCheckBox* kjPerMolCheck_ = nullptr;
    QCheckBox* showColorbarCheck_ = nullptr;
};

} // namespace calango::gui
