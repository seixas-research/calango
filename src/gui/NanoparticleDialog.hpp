#pragma once

#include "core/Structure.hpp"

#include <QDialog>

#include <optional>
#include <string>

class QComboBox;
class QDoubleSpinBox;
class QGroupBox;
class QLabel;
class QPushButton;
class QRadioButton;
class QSpinBox;
class QStackedWidget;
class QTableWidget;

namespace calango::gui {

/// Build → "Nanoparticle & Cluster Builder": a two-stage wizard.
///   Stage 1 — pick the generation method: Wulff construction (thermodynamic
///             equilibrium shape from surface-energy ratios) or a symmetric
///             crystal cluster (icosahedron, dodecahedron, cuboctahedron,
///             octahedron, decahedron, or a spherical FCC/BCC/HCP cluster).
///   Stage 2 — enter the parameters for the chosen method and generate.
/// Accepting exposes the cluster via result(); the caller opens it in a tab.
class NanoparticleDialog : public QDialog {
    Q_OBJECT

public:
    explicit NanoparticleDialog(QWidget* parent = nullptr);

    const std::optional<core::Structure>& result() const { return result_; }
    QString resultName() const;

private Q_SLOTS:
    void generate();
    void showParameters(); ///< Stage 1 → Stage 2
    void showMethod();     ///< Stage 2 → Stage 1

private:
    bool wulffChosen() const;
    /// Cluster shape keyword for the backend, or "" for a spherical cluster.
    std::string clusterShape() const;
    void syncClusterControls();

    std::optional<core::Structure> result_;
    int elementZ_ = 29; // Cu

    QStackedWidget* stack_;

    // --- Stage 1: method ---------------------------------------------------
    QRadioButton* wulffRadio_;
    QRadioButton* clusterRadio_;

    // --- Stage 2: shared ---------------------------------------------------
    QLabel* stageTitle_;
    QPushButton* elementButton_;
    QDoubleSpinBox* latticeConstantSpin_;

    // Wulff parameters
    QGroupBox* wulffSection_;
    QTableWidget* facetTable_;
    QComboBox* wulffLatticeCombo_; // fcc / bcc / sc
    QSpinBox* sizeSpin_;
    QComboBox* roundingCombo_;

    // Symmetric-cluster parameters
    QGroupBox* clusterSection_;
    QComboBox* clusterShapeCombo_;
    QSpinBox* shellSpin_;
    QSpinBox* decaPSpin_;
    QSpinBox* decaQSpin_;
    QSpinBox* decaRSpin_;
    QComboBox* sphericalLatticeCombo_; // fcc / bcc / hcp
    QDoubleSpinBox* radiusSpin_;

    QLabel* statusLabel_;
    QPushButton* backButton_;
    QPushButton* nextButton_;
    QPushButton* generateButton_;
};

} // namespace calango::gui
