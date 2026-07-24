#pragma once

#include "gui/SimulationWizardBase.hpp"

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QSpinBox;

namespace calango::gui {

/// Simulation → "Geometry Optimization…": a 3-stage wizard. Stage 1 is the
/// shared Calculator Settings (engine, XC, cutoff, k-grid, precision — matching
/// the Single-Point wizard); Stage 2 is the relaxation settings (optimizer,
/// force convergence, step cap, and optional variable-cell relaxation with a
/// hydrostatic/anisotropic stress mask); Stage 3 is the ASE script review.
class GeometryOptimizationWizard : public SimulationWizardBase {
    Q_OBJECT

public:
    explicit GeometryOptimizationWizard(QWidget* parent = nullptr);

protected:
    QString wizardTitle() const override;
    QString settingsHeader() const override;
    QWidget* buildSettingsPage() override;
    /// Calculator Settings first, then the relaxation settings page.
    bool settingsStageFirst() const override { return false; }
    QString generateScript() const override;
    QString exportFileName() const override { return QStringLiteral("optimize.py"); }

private Q_SLOTS:
    void updateCellEnabled();

private:
    core::CalculatorConfig config() const;

    QComboBox* optimizerCombo_;
    QDoubleSpinBox* fmaxSpin_;
    QSpinBox* maxStepsSpin_;
    QCheckBox* relaxCellCheck_;
    QComboBox* cellFilterCombo_;
    QComboBox* stressMaskCombo_;   // anisotropic / hydrostatic / custom
    QWidget* voigtRow_;            // custom Voigt mask checkbox row
    QCheckBox* voigtChecks_[6];    // [xx, yy, zz, yz, xz, xy]
};

} // namespace calango::gui
