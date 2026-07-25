#pragma once

#include "core/Structure.hpp"

#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDoubleSpinBox>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QSpinBox>

#include <memory>

namespace calango::gui {

class ViewportWidget;

/// "Edit → Bond Editor": interactive control over bond perception.
///   - toggle automatic distance-based bond detection
///   - tune the covalent cutoff multiplier (live)
///   - manually add or suppress bonds through two operational modes:
///       * By Chemical Elements — pick an element pair (Si–O, C–C) and a
///         min/max distance window; every matching pair in range is bonded
///         (or unbonded) in one action.
///       * By Atomic Indices — pick a specific atom pair by 1-based index (or
///         from the viewport selection) and create it as a single/double/
///         triple/aromatic bond, or suppress it. Bond order lives here rather
///         than in the Representation panel: it is a property of the chemistry,
///         not of how the structure is displayed.
///       * Hydrogen Bonds — geometric D-H...A perception (distance + angle
///         criteria), rendered as dashed lines. Not an override list: these are
///         re-derived from the geometry, so they follow a trajectory rather
///         than freezing at the frame they were detected on.
/// Manual overrides live on the Structure (so they survive re-rendering
/// and are captured by undo); edits apply immediately via bondsEdited().
class BondEditorDialog : public QDialog {
    Q_OBJECT

public:
    BondEditorDialog(std::shared_ptr<core::Structure> structure,
                     ViewportWidget* viewport, QWidget* parent = nullptr);

Q_SIGNALS:
    /// Emitted after every structure-level bond edit (add/suppress/clear).
    void bondsEdited();

private Q_SLOTS:
    // -- "By Atomic Indices" mode ------------------------------------------
    void addBond();
    void suppressBond();
    void useSelection();
    // -- "By Chemical Elements" mode ---------------------------------------
    void addBondsByElements();
    void removeBondsByElements();
    // -- Shared override list ----------------------------------------------
    void clearSelectedOverride();
    void clearAllOverrides();

private:
    void refreshOverrideList();
    std::pair<int, int> currentPair() const; ///< 0-based indices
    /// Populate an element combo with the distinct elements present in the
    /// structure (Z stored as item data), returning the created combo.
    QComboBox* makeElementCombo();
    /// Enumerate atom-pair indices matching the two selected elements within
    /// the current [min, max] distance window.
    std::vector<std::pair<int, int>> matchingElementPairs() const;

    std::shared_ptr<core::Structure> structure_;
    ViewportWidget* viewport_;

    QCheckBox* autoBondsCheck_;
    QDoubleSpinBox* toleranceSpin_;

    // "By Chemical Elements" controls.
    QComboBox* elementACombo_ = nullptr;
    QComboBox* elementBCombo_ = nullptr;
    QDoubleSpinBox* minCutoffSpin_ = nullptr;
    QDoubleSpinBox* maxCutoffSpin_ = nullptr;
    QLabel* elementMatchLabel_ = nullptr;

    // "By Atomic Indices" controls.
    QSpinBox* atomISpin_;
    QSpinBox* atomJSpin_;
    QComboBox* bondOrderCombo_ = nullptr;
    QPushButton* useSelectionButton_;
    QLabel* pairInfoLabel_;

    // "Hydrogen Bonds" tab — geometric perception, not stored overrides.
    QCheckBox* hbondEnableCheck_ = nullptr;
    QDoubleSpinBox* hbondDistanceSpin_ = nullptr;
    QDoubleSpinBox* hbondAngleSpin_ = nullptr;
    QPushButton* hbondColorButton_ = nullptr;
    QLabel* hbondCountLabel_ = nullptr;

    QListWidget* overrideList_;
};

} // namespace calango::gui
