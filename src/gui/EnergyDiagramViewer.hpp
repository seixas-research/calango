#pragma once

#include "gui/EnergyDiagramStyleDialog.hpp"

#include <QDialog>
#include <QPainter>
#include <QWidget>

#include <vector>

class QTableWidget;
class QLabel;
class QPushButton;

namespace calango::gui {

/// One discrete level (possibly a degenerate group) drawn as a horizontal
/// bar in EnergyLevelDiagramWidget.
struct EnergyLevelDiagramEntry {
    int spin = 0;
    std::vector<int> bands; ///< the underlying band index(es); size > 1 =
                            ///< a degenerate group
    double energyEv = 0.0;
    double occupation = 0.0; ///< summed over the group
    bool occupied() const { return occupation > 0.5; }
    int degeneracy() const { return static_cast<int>(bands.size()); }
};

/// Vertical energy-level diagram for a molecular (non-periodic) Kohn-Sham
/// spectrum: one horizontal bar per level (or degenerate group), occupied
/// levels solid and unoccupied ones hollow/dashed, HOMO and LUMO labelled
/// with the gap between them annotated, spin channels drawn as two columns
/// when the parent is spin-polarized. Clicking a level emits levelClicked()
/// with its index into the vector last given to setLevels().
///
/// PlotPalette-white by default, like every other data plot in the app: a
/// white canvas with occupied levels in `series` blue and virtual ones in
/// `seriesAlt` red (setStyle() overrides this — see EnergyDiagramStyleDialog).
/// paintEvent() only forwards to the public renderTo(), which is also what
/// EnergyDiagramViewer's Export Image… uses to rasterise the widget off
/// screen — the same drawing code serves both paths, so an export can never
/// drift from what is on screen (SpectrumPlotWidget's renderTo() is the
/// precedent this follows).
class EnergyLevelDiagramWidget : public QWidget {
    Q_OBJECT

public:
    explicit EnergyLevelDiagramWidget(QWidget* parent = nullptr);

    void setLevels(const std::vector<EnergyLevelDiagramEntry>& levels,
                  int nspins);
    void setSelected(int index);
    void setStyle(const EnergyDiagramStyle& style);

    /// Draws the diagram into `painter` at `size` (independent of the
    /// widget's actual on-screen geometry) — used both by paintEvent() and
    /// by Export Image…, which renders at a larger-than-screen size.
    void renderTo(QPainter& painter, const QSize& size) const;

Q_SIGNALS:
    void levelClicked(int index);

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;

private:
    struct BarRect {
        QRectF rect;
        int index = -1;
    };

    double yFromEnergy(double e, double heightPx) const;

    std::vector<EnergyLevelDiagramEntry> levels_;
    int nspins_ = 1;
    double eMin_ = -1.0;
    double eMax_ = 1.0;
    int selected_ = -1;
    EnergyDiagramStyle style_;
    mutable std::vector<BarRect> lastBars_; ///< rebuilt each render, used by
                                            ///< mousePressEvent for hit-testing
};

/// Results window for the Energy Diagrams module: the level diagram beside a
/// transitions table (energy, wavelength, oscillator strength, allowed/
/// forbidden), reading `energy_diagram.json`. Follows the XAS/Optics results-
/// window shape exactly: a QDialog (every other results viewer in this
/// codebase — BandPdosWindow, XasResultsWindow, MlwfViewer,
/// OpticsResultsWindow, GwResultsWindow, and more — is one; a plain QWidget
/// shown via show() with a parent renders as a frameless, unmovable child
/// EMBEDDED in that parent rather than an independent top-level window,
/// which is the whole reason this needs to be a QDialog and not a QWidget),
/// with CSV export matching the XAS/Optics column-header convention.
///
/// Two independent CSV exports, because the diagram and the table show
/// different data: "Export Levels…" writes the level list the diagram
/// itself draws (one row per level/degenerate group), "Export Transitions…"
/// writes the transitions table. "Customize Appearance…" and "Export
/// Image…" follow XasResultsWindow's exact wiring — a non-modal
/// EnergyDiagramStyleDialog with live styleChanged(), and
/// GuiUtils::savePlotImage() driven by EnergyLevelDiagramWidget::renderTo().
class EnergyDiagramViewer : public QDialog {
    Q_OBJECT

public:
    explicit EnergyDiagramViewer(QWidget* parent = nullptr);

    /// Loads `path` (energy_diagram.json). Returns false (and leaves the
    /// viewer visibly empty, with a status message) on a missing or
    /// unparsable file.
    bool loadResults(const QString& path);
    bool hasData() const { return hasData_; }

private Q_SLOTS:
    void onLevelClicked(int index);
    void exportTransitionsCsv();
    void exportLevelsCsv();
    void exportImage();
    void customizeAppearance();

private:
    void rebuildTransitionsTable();

    EnergyLevelDiagramWidget* diagram_ = nullptr;
    QLabel* levelInfoLabel_ = nullptr;
    QLabel* gapLabel_ = nullptr;
    QTableWidget* transitionsTable_ = nullptr;
    QPushButton* exportButton_ = nullptr;
    QPushButton* exportLevelsButton_ = nullptr;
    QPushButton* exportImageButton_ = nullptr;
    QPushButton* customizeButton_ = nullptr;
    EnergyDiagramStyle style_;

    std::vector<EnergyLevelDiagramEntry> levels_;
    struct Transition {
        int spin = 0;
        int fromBand = 0;
        int toBand = 0;
        double energyEv = 0.0;
        double wavelengthNm = 0.0;
        bool hasWavelength = false;
        double oscillatorStrength = 0.0;
        bool allowed = false;
    };
    std::vector<Transition> transitions_;
    bool hasData_ = false;
};

} // namespace calango::gui
