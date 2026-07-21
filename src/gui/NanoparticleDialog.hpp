#pragma once

#include "core/Structure.hpp"

#include <QDialog>

#include <optional>

class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QPushButton;
class QSpinBox;
class QTableWidget;

namespace calango::gui {

/// Build → "Metallic Nanoparticle": Wulff-construction equilibrium
/// shapes (per-facet surface-energy ratios, ase.cluster) or spherical
/// clusters carved from the bulk lattice (FCC/BCC/HCP), sized by target
/// atom count or radius. Accepting exposes the cluster via result().
class NanoparticleDialog : public QDialog {
    Q_OBJECT

public:
    explicit NanoparticleDialog(QWidget* parent = nullptr);

    const std::optional<core::Structure>& result() const { return result_; }
    QString resultName() const;

private Q_SLOTS:
    void generate();

private:
    std::optional<core::Structure> result_;
    int elementZ_ = 29; // Cu

    QPushButton* elementButton_;
    QComboBox* modeCombo_;
    QComboBox* latticeCombo_;
    QDoubleSpinBox* latticeConstantSpin_;
    QTableWidget* facetTable_;
    QSpinBox* sizeSpin_;
    QComboBox* roundingCombo_;
    QDoubleSpinBox* radiusSpin_;
    QLabel* statusLabel_;
};

} // namespace calango::gui
