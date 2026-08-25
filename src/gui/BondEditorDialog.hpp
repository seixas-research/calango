#pragma once

#include "core/BondRules.hpp"
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
#include <vector>

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
///
/// EDITS ARE TRAJECTORY-WIDE. Every rule entered here is applied to every
/// frame of the active trajectory, not only to the frame on screen. Bonding is
/// a statement about the chemistry of a system, and a system does not change
/// its chemistry between two samples of the same run — an override that existed
/// on frame 0 and vanished on frame 1 was never a physical statement, only an
/// artifact of which frame happened to be displayed when the button was
/// pressed. It also made every trajectory-wide export (the .extxyz writer, the
/// ray-traced film, the animated GIF) disagree with the viewport.
///
/// The two rule kinds propagate DIFFERENTLY, and the difference is the point:
///   - Index rules (pair 12–37, order 2) name atoms, and an atom keeps its
///     index for the whole run, so the same pair is marked on every frame.
///   - Element rules (every Si–O between 1.4 and 1.9 Å) name a GEOMETRIC
///     CONDITION, so they are re-evaluated against each frame's own
///     coordinates. Copying frame 0's match list forward would freeze a bond
///     onto a pair that has since dissociated — which is precisely the thing a
///     reactive trajectory is being watched for.
/// Frames whose atom count differs from the displayed one are skipped: an
/// index there refers to a different atom.
class BondEditorDialog : public QDialog {
    Q_OBJECT

public:
    /// `frames` is the active trajectory (empty, or a single frame, for a
    /// static structure). `structure` must be the frame currently displayed;
    /// it is edited whether or not it also appears in `frames`.
    BondEditorDialog(std::shared_ptr<core::Structure> structure,
                     std::vector<std::shared_ptr<core::Structure>> frames,
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
    /// The element/distance rule the "By Chemical Elements" controls describe.
    core::ElementBondRule currentElementRule() const;

    /// Every frame an edit is applied to: the displayed structure plus each
    /// trajectory frame of the same size, each listed once. Built in the
    /// constructor and never empty.
    void collectTargets(std::vector<std::shared_ptr<core::Structure>> frames);
    /// `targets_` as raw pointers, which is what core::BondRules takes.
    std::vector<core::Structure*> targetFrames() const;
    /// "Applied to all 250 trajectory frames." — shown next to the manual-bond
    /// controls so the scope is stated where the rules are entered, and left
    /// empty for a static structure where there is nothing to say.
    QString scopeSummary() const;

    std::shared_ptr<core::Structure> structure_;
    std::vector<std::shared_ptr<core::Structure>> targets_;
    /// Trajectory frames that were left alone because their atom count does
    /// not match the displayed frame's. Reported, never silently dropped.
    int skippedFrames_ = 0;
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
    /// Stroke: solid/dashed/dotted, and the width in the same units the
    /// cell wireframe's own width uses. Both are cut into the overlay's
    /// geometry rather than set on the GL state — see
    /// render::StructureRenderer::buildHydrogenBondDashes.
    QComboBox* hbondStyleCombo_ = nullptr;
    QDoubleSpinBox* hbondWidthSpin_ = nullptr;
    QLabel* hbondCountLabel_ = nullptr;

    QListWidget* overrideList_;
};

} // namespace calango::gui
