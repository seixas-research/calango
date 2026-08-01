#pragma once

#include <QDialog>
#include <QJsonObject>
#include <QString>

class QComboBox;
class QLabel;

namespace calango::gui {

class SpectrumPlotWidget;

/// Viewer for a completed Nonlinear Optics run's `nlopt.json`.
///
/// One selector over every spectrum the run produced — each SHG component,
/// each shift-current component, and the linear tensor — because they share a
/// photon-energy axis but not a unit, and overlaying a susceptibility in pm/V
/// on a conductivity in A/V² would put two unrelated scales on one axis.
///
/// Real and imaginary parts are drawn together rather than separately: for a
/// second-order susceptibility the two carry different physics (Im χ⁽²⁾ is the
/// absorptive part that a measurement of the generated intensity is blind to),
/// and |χ⁽²⁾| alone hides where the sign changes.
class NonlinearOpticsResultsWindow : public QDialog {
    Q_OBJECT

public:
    explicit NonlinearOpticsResultsWindow(QWidget* parent = nullptr);

    /// Load `<directory>/nlopt.json`. False when it is missing, unreadable or
    /// carries no spectrum — the caller then reports that rather than showing
    /// an empty window.
    bool loadResults(const QString& directory);

private Q_SLOTS:
    void showSelectedSpectrum();
    void copyToClipboard();
    void exportCsv();

private:
    /// Fill the selector with one entry per available spectrum, each carrying
    /// the JSON path it draws from.
    void populateSelector();
    /// The (x, named series) of the current selection, as the plot wants them.
    QJsonObject currentSpectrum() const;
    /// CSV of the current selection: the energy column plus every series.
    QString currentAsCsv() const;

    QJsonObject data_;
    QString sourcePath_;
    QComboBox* spectrumCombo_ = nullptr;
    QLabel* summaryLabel_ = nullptr;
    SpectrumPlotWidget* plot_ = nullptr;
};

} // namespace calango::gui
