#pragma once

#include "core/CalculatorConfig.hpp"

#include <QObject>

#include <functional>

class QCheckBox;
class QComboBox;
class QFormLayout;
class QWidget;

namespace calango::gui {

/// The variable-cell relaxation controls: "Relax the unit cell", the ASE cell
/// filter, the stress mask (anisotropic / hydrostatic / 2Dxy / custom) and the
/// six Voigt component ticks.
///
/// One implementation, two hosts. Geometry Optimization has always had these;
/// the Cluster Expansion batch now relaxes cells too, and a cluster-expansion
/// hull built from fixed-cell energies is not comparable to one built from
/// relaxed-cell energies — so the batch needs the SAME choices, not a
/// simplified version of them. Two copies of a four-control group whose rules
/// interact (the 2Dxy preset writes the ticks; the ticks are a read-out under
/// the preset and editable only under Custom) is exactly the kind of
/// duplication that drifts: one host gains a mask option and the other quietly
/// keeps generating the old script.
///
/// Not a QWidget of its own: the rows belong in the host's existing form layout,
/// beside the optimizer and the force tolerance they qualify, and wrapping them
/// in a container would put a box around half a decision.
class CellRelaxationControls : public QObject {
    Q_OBJECT

public:
    explicit CellRelaxationControls(QObject* parent = nullptr);

    /// Append the rows to `form` (whose parent widget owns the new widgets).
    /// `onChanged` is invoked whenever a control that alters the generated
    /// script moves — the hosts pass their refreshPreview().
    void build(QWidget* parent, QFormLayout* form,
               std::function<void()> onChanged);

    /// Write `relaxCell`, `cellFilter`, `cellHydrostatic`, `cellCustomMask`
    /// and `cellMask` into `config`. A no-op before build().
    void applyTo(core::CalculatorConfig& config) const;

    bool relaxesCell() const;

    /// Re-apply the enable/visibility rules. Public because a host that offers
    /// a "single-point only" switch (Cluster Expansion) has to fold the whole
    /// group out when relaxation is off — a cell filter for a run that takes no
    /// steps is a control the generated script ignores.
    void updateEnabled();
    /// Force the whole group off, for a host whose task has no relaxation at
    /// all. `available` false disables every control regardless of the
    /// checkbox; true restores the normal rules.
    void setAvailable(bool available);

private:
    std::function<void()> onChanged_;
    bool available_ = true;

    QFormLayout* form_ = nullptr;
    QCheckBox* relaxCellCheck_ = nullptr;
    QComboBox* cellFilterCombo_ = nullptr;
    QComboBox* stressMaskCombo_ = nullptr;
    QWidget* voigtRow_ = nullptr;
    QCheckBox* voigtChecks_[6] = {};

    /// Indices into stressMaskCombo_. Named because several call sites compare
    /// against them and a bare 2/3 is where an inserted entry breaks the mask.
    static constexpr int kStressMaskHydrostatic = 1;
    static constexpr int kStressMask2Dxy = 2;
    static constexpr int kStressMaskCustom = 3;
};

} // namespace calango::gui
