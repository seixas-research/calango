#pragma once

#include "core/KpointsConvergenceScriptGenerator.hpp"
#include "gui/GpawElectronicWizard.hpp"

#include <vector>

class QCheckBox;
class QFormLayout;
class QLabel;
class QSpinBox;

namespace calango::gui {

/// Modules → Parameters Convergence → "K-points Convergence…": run the same
/// single-point calculation over an ascending sequence of Monkhorst-Pack
/// meshes and measure how fast the total energy per atom, the forces and the
/// band energies settle. The sibling of the Plane-wave Cutoff Convergence
/// wizard.
///
/// Three stages:
///   1. k-Point Mesh Sweep — isotropic (one subdivision count swept n×n×n)
///      or anisotropic (independent start and stride per axis, for layered
///      and chain-like materials whose reciprocal cell is far from cubic),
///      plus Γ-centering
///   2. Calculator & Convergence Settings — the standard GPAW page (the
///      single k-grid row and the BZ toggles are hidden; the sweep owns the
///      mesh)
///   3. ASE Script Review
///
/// The run at the DENSEST mesh is the convergence reference — the sweep is
/// judged against the best member of the set.
class KpointsConvergenceWizard : public GpawElectronicWizard {
    Q_OBJECT

public:
    explicit KpointsConvergenceWizard(QWidget* parent = nullptr);

protected:
    QString wizardTitle() const override;
    QString settingsHeader() const override
    {
        return tr("k-Point Mesh Sweep");
    }
    QWidget* buildSettingsPage() override;
    /// GPAW (kpts= per point) and VASP (KPOINTS per point via ASE's kpts +
    /// gamma keywords, one directory each).
    bool calculatorAllowed(core::CalculatorKind kind) const override
    {
        return kind == core::CalculatorKind::Gpaw
            || kind == core::CalculatorKind::Vasp;
    }
    QString calculatorSettingsHeader() const override
    {
        return tr("Calculator & Convergence Settings");
    }
    /// The sweep stage defines the meshes (and their Γ-centering); the
    /// calculator page's k-grid row and BZ toggles would be controls the
    /// generated script ignores.
    bool showsKpointGridRow() const override { return false; }

    QString generateScript() const override;
    QString exportFileName() const override
    {
        return QStringLiteral("kpoints_convergence.py");
    }

private:
    /// The sweep as an ascending list of meshes. Isotropic: the subdivision
    /// count runs min, min+stride, …, with the maximum always included (it
    /// is the reference), all swept axes equal. Anisotropic: each axis has
    /// its own start and stride, advanced together over a fixed number of
    /// steps — a stride of 0 pins that axis (a slab's vacuum direction).
    std::vector<core::KpointsConvergenceRunConfig::Mesh> meshes() const;
    core::KpointsConvergenceRunConfig runConfig() const;
    /// Show the rows for the active mode (isotropic or per-axis).
    void updateModeRows();
    /// Keep the "N calculations" line honest about what the controls define.
    void updateSweepSummary();

    // Isotropic controls.
    QSpinBox* minKSpin_ = nullptr;
    QSpinBox* maxKSpin_ = nullptr;
    QSpinBox* strideSpin_ = nullptr;
    // Anisotropic controls: per-axis start and stride, common step count.
    QCheckBox* anisotropicCheck_ = nullptr;
    QSpinBox* axisStartSpins_[3] = {nullptr, nullptr, nullptr};
    QSpinBox* axisStrideSpins_[3] = {nullptr, nullptr, nullptr};
    QWidget* axisRows_[3] = {nullptr, nullptr, nullptr};
    QSpinBox* stepsSpin_ = nullptr;

    QCheckBox* gammaCheck_ = nullptr;
    QLabel* sweepSummary_ = nullptr;
    QFormLayout* sweepForm_ = nullptr;

};

} // namespace calango::gui
