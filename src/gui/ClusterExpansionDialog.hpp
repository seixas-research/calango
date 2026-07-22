#pragma once

#include "core/ClusterExpansion.hpp"
#include "core/Structure.hpp"

#include <QDialog>

#include <memory>
#include <optional>

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QLineEdit;
class QSpinBox;

namespace calango::gui {

/// Build → "Cluster Expansion": generate a symmetry-inequivalent ensemble of
/// alloy decorations of the current structure's active sublattice, natively
/// (no ICET dependency). The active Wyckoff sites are every atom of a chosen
/// parent element; they are decorated with a multi-component substitution set
/// and reduced to distinct cluster-correlation fingerprints up to the pair /
/// triplet / quadruplet cutoffs. Accepting exposes the ensemble via result().
class ClusterExpansionDialog : public QDialog {
    Q_OBJECT

public:
    explicit ClusterExpansionDialog(std::shared_ptr<const core::Structure> structure,
                                    QWidget* parent = nullptr);

    const std::optional<core::ClusterExpansionResult>& result() const
    {
        return result_;
    }

private Q_SLOTS:
    void generate();

private:
    std::shared_ptr<const core::Structure> structure_;
    std::optional<core::ClusterExpansionResult> result_;

    QComboBox* activeCombo_;
    QLineEdit* speciesEdit_;
    QSpinBox* supercellSpins_[3];
    QDoubleSpinBox* pairCutoffSpin_;
    QDoubleSpinBox* tripletCutoffSpin_;
    QDoubleSpinBox* quadCutoffSpin_;
    QSpinBox* maxConfigsSpin_;
    QSpinBox* seedSpin_;
    QCheckBox* fixedCompCheck_;
    QLineEdit* compositionEdit_;
    QLabel* statusLabel_;
};

} // namespace calango::gui
