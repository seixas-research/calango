#pragma once

#include <QDialog>
#include <QJsonObject>
#include <QString>
#include <QWidget>

#include <vector>

class QComboBox;
class QLabel;
class QTableWidget;

namespace calango::gui {

/// One component's P(eps) points and fitted line — the "inspect the
/// linearity behind one Voigt column" plot the piezoelectric task asks for.
/// A single series at a time (PlotPalette's convention is two curves at
/// most; six overlaid components would defeat the point of the plot), picked
/// by PiezoelectricViewer's combo boxes.
class PiezoelectricPointPlot : public QWidget {
    Q_OBJECT

public:
    explicit PiezoelectricPointPlot(QWidget* parent = nullptr);

    /// `eps` and `pAxis` (the polarization component being differentiated,
    /// C/m^2) must be the same length. `slope`/`intercept` draw the fitted
    /// line — the same numbers the JSON's tensor entry came from, not a
    /// second independent fit, so the plot cannot disagree with the table.
    void setSeries(std::vector<double> eps, std::vector<double> pAxis,
                   double slope, double intercept, const QString& axisLabel);

    QSize sizeHint() const override { return {480, 300}; }

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    std::vector<double> eps_;
    std::vector<double> pAxis_;
    double slope_ = 0.0;
    double intercept_ = 0.0;
    QString axisLabel_;
};

/// The read-out for a completed Piezoelectric Tensor run. Parses
/// `piezoelectric.json` and shows:
///   - the e_ij tensor (Voigt, C/m^2), switchable between proper/improper
///     and symmetrized/raw;
///   - the point group spglib detected, and whether symmetrization ran;
///   - the per-component P(eps) points behind the tensor, so the linearity
///     the finite-difference method assumes can be checked directly rather
///     than trusted;
///   - the d_ij tensor (pm/V), when an elastic stiffness was supplied.
///
/// Opened from the Processes panel and automatically when the job finishes.
/// Purely a viewer, like BornChargesViewer.
class PiezoelectricViewer : public QDialog {
    Q_OBJECT

public:
    explicit PiezoelectricViewer(QWidget* parent = nullptr);

    bool loadResults(const QString& jsonPath);

private Q_SLOTS:
    void refreshTensorTable();
    void refreshPlot();
    void copyToClipboard();
    void exportCsv();

private:
    QJsonObject data_;
    QString sourcePath_;

    QLabel* summaryLabel_ = nullptr;
    QComboBox* tensorKindCombo_ = nullptr; // proper/improper x raw/symmetrized
    QTableWidget* tensorTable_ = nullptr;
    QLabel* dTensorLabel_ = nullptr;
    QTableWidget* dTensorTable_ = nullptr;

    QComboBox* componentCombo_ = nullptr;
    QComboBox* axisCombo_ = nullptr;
    PiezoelectricPointPlot* plot_ = nullptr;
};

} // namespace calango::gui
