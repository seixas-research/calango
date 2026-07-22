#pragma once

#include "core/Structure.hpp"

#include <QDialog>

#include <optional>
#include <string>

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

    // Faceted ase.cluster shapes: `shellSpin_` is shells (icosahedron),
    // edge length (octahedron / cuboctahedron) or {110} layers (rhombic
    // dodecahedron); the decahedron uses its own p/q/r triple.
    QSpinBox* shellSpin_;
    QSpinBox* decaPSpin_;
    QSpinBox* decaQSpin_;
    QSpinBox* decaRSpin_;

    QLabel* statusLabel_;

    /// The ase.cluster shape keyword for a faceted mode index, or "" for the
    /// non-faceted Wulff/spherical modes.
    std::string shapeForMode(int mode) const;
};

} // namespace calango::gui
