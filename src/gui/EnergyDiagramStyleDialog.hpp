#pragma once

#include "gui/PlotPalette.hpp"

#include <QColor>
#include <QDialog>

class QPushButton;
class QDoubleSpinBox;

namespace calango::gui {

/// Appearance of the Energy Diagrams level diagram. Deliberately its own
/// small struct rather than reusing OpticsPlotStyle: that one carries grid/
/// font/threshold-band fields this diagram has no use for (no axes, no
/// grid — it is a level diagram, not a spectrum), and a dialog that hides
/// half its controls per caller is worse than two focused ones.
///
/// The canvas is white and the two level colours are blue/red — PlotPalette's
/// own `series`/`seriesAlt` (matplotlib's tab10 blue and red), matching every
/// other plot in the app rather than inventing a third palette — because a
/// level diagram is exactly the kind of figure PlotPalette's own doc comment
/// describes: "a band structure dropped into a paper... is expected on
/// white." The level widget used to follow the THEMED palette instead
/// (dark canvas in Dark theme), which is what made two dozen closely-packed
/// levels unreadable rather than merely dense.
struct EnergyDiagramStyle {
    QColor canvasBackground = PlotPalette::canvas;
    QColor occupiedColor = PlotPalette::series;    ///< blue
    QColor unoccupiedColor = PlotPalette::seriesAlt; ///< red
    QColor gapLineColor = PlotPalette::reference;
    QColor textColor = PlotPalette::text;
    QColor placeholderColor = PlotPalette::placeholder;
    double lineWidth = 2.0;
};

/// "Customize Appearance…" for the Energy Diagram viewer, following the
/// same live-update convention OpticsPlotStyleDialog/XasResultsWindow use:
/// edits apply immediately through styleChanged(), judged against the real
/// diagram rather than a preview swatch.
class EnergyDiagramStyleDialog : public QDialog {
    Q_OBJECT

public:
    explicit EnergyDiagramStyleDialog(const EnergyDiagramStyle& style,
                                      QWidget* parent = nullptr);

Q_SIGNALS:
    void styleChanged(const EnergyDiagramStyle& style);

private:
    QPushButton* colorButton(QColor* target);
    void syncToControls();
    void restoreDefaults();
    void emitStyle();

    EnergyDiagramStyle style_;
    QPushButton* canvasButton_ = nullptr;
    QPushButton* occupiedButton_ = nullptr;
    QPushButton* unoccupiedButton_ = nullptr;
    QPushButton* gapLineButton_ = nullptr;
    QDoubleSpinBox* lineWidthSpin_ = nullptr;
};

} // namespace calango::gui
