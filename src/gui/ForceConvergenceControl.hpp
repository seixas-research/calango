#pragma once

#include <QString>

class QDoubleSpinBox;
class QFormLayout;
class QWidget;

namespace calango::gui {

/// The force-convergence criterion (fmax, eV/Å) every relaxation-capable
/// wizard/dialog exposes: Geometry Optimization, Cluster Expansion's batch
/// relax, Basin Hopping's inner local optimizer, and NEB's path solver.
///
/// One implementation rather than four hand-maintained QDoubleSpinBoxes: the
/// four had already drifted — different ranges, different decimal counts,
/// and two of them never called setSingleStep() at all, which left Qt's
/// default step of 1.0 slamming a field meant to be nudged in thousandths
/// straight to its range maximum on the first click. Geometry Optimization's
/// own UI default (0.020) did not even match core::CalculatorConfig::fmax's
/// default (0.05) — evidence of exactly the drift a fourth hand-copy invites.
/// See CellRelaxationControls for the same argument made about the
/// cell-relaxation rows this class sits beside in two of its four hosts.
///
/// Not a QWidget of its own: the row belongs in the host's existing form
/// layout, beside the optimizer it qualifies.
class ForceConvergenceControl {
public:
    /// Append the row to `form` (whose parent widget owns the new spin box).
    /// `label` lets a host phrase it in its own words ("Force convergence
    /// (fmax):", "Energy convergence (fmax):", ...) while every host shares
    /// exactly how the NUMBER behaves.
    void build(QFormLayout* form, QWidget* parent, const QString& label);

    /// core::CalculatorConfig::fmax's own default (0.05 eV/Å) before build();
    /// the spin box's current value afterward.
    double value() const;
    void setValue(double fmax);

    /// The underlying widget, for a host that needs to enable/disable it
    /// (MonteCarloWizard, toggled with the optimizer choice), fold it into a
    /// setFormRowVisible() call (ClusterExpansionWizard), or connect its
    /// valueChanged() straight to a preview refresh. Null before build().
    QDoubleSpinBox* spinBox() const { return spin_; }

private:
    QDoubleSpinBox* spin_ = nullptr;
};

} // namespace calango::gui
