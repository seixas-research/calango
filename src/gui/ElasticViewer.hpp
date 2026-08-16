#pragma once

#include <QDialog>
#include <QJsonObject>
#include <QString>

class QComboBox;
class QLabel;
class QTableWidget;

namespace calango::gui {

class ElasticPointPlot;

/// The read-out for a completed Elastic Properties run. Parses
/// `elastic.json` and shows:
///   - the 6x6 elasticity tensor C_ij (Voigt, GPa), switchable between raw
///     and point-group-symmetrized;
///   - Born mechanical stability: the crystal-class-specific closed-form
///     criterion (Mouhat & Coudert) when the point group falls into one of
///     the five recognised Laue classes, and the universal general
///     (all-eigenvalues-positive) criterion always;
///   - Voigt/Reuss/Hill bulk and shear moduli, Young's modulus and Poisson's
///     ratio (bulk), or the 2D layer modulus / in-plane Young's / Poisson's
///     ratio and 2D Born stability for a monolayer;
///   - the raw stress-strain (or energy-strain) points behind each Voigt
///     column, so the linearity (or, for energy-strain, the curvature) the
///     fit assumes can be checked directly — via ElasticPointPlot, a small
///     plot widget adapted from the Piezoelectric Tensor viewer's own
///     PiezoelectricPointPlot but supporting both a linear AND a quadratic
///     overlay, since the energy-strain method's fit genuinely is a
///     parabola, not a line.
///
/// Opened from the Processes panel and automatically when the job finishes.
/// Purely a viewer, like PiezoelectricViewer.
class ElasticViewer : public QDialog {
    Q_OBJECT

public:
    explicit ElasticViewer(QWidget* parent = nullptr);

    bool loadResults(const QString& jsonPath);

private Q_SLOTS:
    void refreshTensorTable();
    void refreshPlot();
    void copyToClipboard();
    void exportCsv();

private:
    void refreshStabilityAndModuli();

    QJsonObject data_;
    QString sourcePath_;

    QLabel* summaryLabel_ = nullptr;

    QComboBox* tensorKindCombo_ = nullptr; // raw / symmetrized (GPa)
    QTableWidget* tensorTable_ = nullptr; // 6x6

    QLabel* stabilityLabel_ = nullptr;
    QTableWidget* stabilityTable_ = nullptr; // Born criteria: expression/value/satisfied
    QLabel* moduliLabel_ = nullptr; // VRH (bulk) or layer-modulus/Young/Poisson (2D)

    QComboBox* componentCombo_ = nullptr; // which Voigt strain column
    QComboBox* rowCombo_ = nullptr; // which stress row (stress method) or "Energy"
    ElasticPointPlot* plot_ = nullptr;
};

} // namespace calango::gui
