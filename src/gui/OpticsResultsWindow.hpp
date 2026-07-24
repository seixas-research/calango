#pragma once

#include <QDialog>
#include <QList>
#include <QPair>
#include <QString>

#include <vector>

class QComboBox;

namespace calango::gui {

/// Post-processing viewer for a finished optical-properties job. Reads the
/// `optics.json` written by OpticsScriptGenerator (a photon-energy grid plus,
/// per direction, ε₁/ε₂, absorption, reflectivity, n/k and the loss function)
/// and plots any one quantity against ħω for a chosen direction. A small
/// self-contained multi-series QPainter widget draws the curves; the data can
/// be exported as CSV or the plot as a high-resolution image. hasData()
/// reports whether a usable optics.json was found and parsed.
class OpticsResultsWindow : public QDialog {
    Q_OBJECT

public:
    explicit OpticsResultsWindow(const QString& directory,
                                 QWidget* parent = nullptr);

    bool hasData() const { return hasData_; }

private Q_SLOTS:
    /// Rebuild the plotted series from the Quantity + Direction combos.
    void updatePlot();
    /// Energy + every quantity for the current direction, one row per sample.
    void exportCsv();
    /// Render the current plot to a high-resolution PNG / JPEG.
    void exportImage();

private:
    void loadDirectory(const QString& directory);

    /// The seven spectra stored for one polarization direction.
    struct DirectionData {
        std::vector<double> eps1, eps2, absorption, reflectivity, n, k, loss;
    };
    const DirectionData* currentDirection() const;

    std::vector<double> energy_;                       ///< ħω grid, eV
    QList<QPair<QString, DirectionData>> directions_;  ///< in xx, yy, zz order

    class OpticsPlotWidget* plot_ = nullptr;
    QComboBox* quantityCombo_ = nullptr;
    QComboBox* directionCombo_ = nullptr;
    bool hasData_ = false;
};

} // namespace calango::gui
