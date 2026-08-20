#pragma once

#include "gui/DsimPlotStyleDialog.hpp"

#include <QDialog>
#include <QJsonObject>

class QCheckBox;
class QLabel;
class QStackedWidget;
class QTableWidget;

namespace calango::gui {

class EgqcaPlotWidget;
class DsimTernaryPlotWidget;

/// Displays `dsim.json` (schema `calango.dsim/2`, N components, N >= 2):
/// species[0..N) build a table of the N pristine + N(N-1) impurity
/// supercells, and — depending on N — a binary DeltaH_mix(x) curve with the
/// two dilute-limit tangent slopes (N=2, Eq. 8, the same "black dashed
/// lines" convention as the working paper's Fig. 1(e)/2), a ternary
/// composition-triangle mixing-enthalpy map (N=3), or the N(N-1)/2
/// pairwise binary sub-curves together on one plot (N>=4, no direct
/// N-dimensional visualization — see docs/simulations/dsim.md's
/// "Extensibility" section).
///
/// Opened the same way as ElasticViewer/PiezoelectricViewer (loadResults(),
/// three-place MainWindow dispatch on `dsim.json`) rather than EGQCA's
/// separate-loader shape, since DSIM's result is a single self-contained
/// job output, not a re-analysis of an existing batch.
class DsimResultsWindow : public QDialog {
    Q_OBJECT

public:
    explicit DsimResultsWindow(QWidget* parent = nullptr);

    /// Loads `<directory>/dsim.json`. Returns false (and leaves the window
    /// showing an explanatory message) when the file is missing or has no
    /// usable analysis — e.g. one of the supercells failed to relax.
    bool loadResults(const QString& path);

private Q_SLOTS:
    void exportImage();
    void exportData();
    void customizeAppearance();

private:
    void populateTable(const QJsonObject& records);
    /// (Re)builds the plot(s) from `root_`'s "analysis" object and the
    /// current `style_` — called from loadResults() and again whenever the
    /// appearance dialog (or the always-visible tangent-lines checkbox)
    /// changes something, so a colour/unit/tangent-visibility change is
    /// judged against the real curve, not a preview.
    void rebuildPlot();

    QStackedWidget* plotStack_ = nullptr;
    EgqcaPlotWidget* curvePlot_ = nullptr; ///< N=2 binary curve, or N>=4 pairwise curves
    DsimTernaryPlotWidget* ternaryPlot_ = nullptr; ///< N=3 only
    QTableWidget* table_ = nullptr;
    QLabel* summaryLabel_ = nullptr;
    /// A quick, always-visible twin of DsimPlotStyle::showTangents — the
    /// Customize Appearance… dialog has the same toggle (colours belong
    /// there), but hiding the tangent lines is common enough to want one
    /// click, not a dialog round trip. Only meaningful for N=2 (the
    /// ternary/pairwise views have no tangent-line concept), disabled
    /// otherwise. The two stay in sync either way.
    QCheckBox* tangentsCheck_ = nullptr;
    QJsonObject root_;
    QStringList species_;
    DsimPlotStyle style_;
};

} // namespace calango::gui
