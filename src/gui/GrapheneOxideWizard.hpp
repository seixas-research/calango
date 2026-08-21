#pragma once

#include "core/GrapheneOxideBuilder.hpp"

#include <QDialog>

#include <optional>

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QFormLayout;
class QGroupBox;
class QLabel;
class QPushButton;
class QSlider;
class QSpinBox;
class QStackedWidget;

namespace calango::gui {

/// Modules → 2D Materials → "Graphene Oxide…": a two-stage builder for a
/// functionalized graphene substrate.
///
///   Stage 1 — Base Structure: an infinite periodic sheet, or a finite
///     nanoflake C(6m²)H(6m) of index m — the difference that decides whether
///     there is any edge chemistry to do at all.
///   Stage 2 — Functionalization & Oxidation Level: how much oxygen to put on,
///     and what it becomes. Three questions, one ratio slider each: the total
///     oxidation as O/C, the basal chemistry as H/O, and — on a flake only —
///     how much of the oxygen goes to the rim and what it turns into there.
///
/// Stage 2 drives Dosing::DecoupledRegions and nothing else. The builder also
/// supports Dosing::ExplicitCoverage, a per-group coverage table, and this
/// dialog used to offer it as a second mode; it no longer does. A composition
/// is what graphene oxide is characterized by and what a paper quotes, whereas
/// a per-group coverage is an implementation detail of how the sites got
/// filled — and offering both meant a panel of controls where half were inert
/// depending on a combo box most people never touched. The library path stays
/// (the builder's tests drive the placement engine through it); the dialog
/// asks one question, in the units the answer is reported in.
///
/// Graphene oxide is non-stoichiometric and disordered, so what this produces
/// is a representative sample at a requested composition rather than "the"
/// structure. The seed is exposed for exactly that reason: a sample nobody can
/// regenerate is not a result.
class GrapheneOxideWizard : public QDialog {
    Q_OBJECT

public:
    explicit GrapheneOxideWizard(QWidget* parent = nullptr);

    /// The generated structure, valid after exec() returns Accepted.
    const std::optional<core::Structure>& result() const { return result_; }
    /// What the builder actually placed, for the caller's status line.
    const core::GrapheneOxideBuilder::Report& report() const { return report_; }

public:
    /// True when the user asked for the MDMC refinement on stage 3. The host
    /// builds and opens the structure either way, then offers the follow-on
    /// wizard where a calculator is chosen.
    bool mdmcRequested() const;
    /// Whether the structure just built has hydroxyls placed as bonded,
    /// opposite-face pairs (the "Hydroxyls antiposition" option) — read by
    /// the host so the follow-on MDMC wizard can move each pair as one
    /// compound unit instead of two independently sited hydroxyls. Valid
    /// after exec() returns Accepted, same as result()/report().
    bool hydroxylAntiposition() const;

private Q_SLOTS:
    void goNext();
    void goBack();
    /// Re-run the site arithmetic, update which controls apply to the current
    /// base and dosing mode, and refresh the live summary. Cheap: it counts
    /// sites rather than building the structure.
    void refreshSummary();

private:
    core::GrapheneOxideBuilder::Config config() const;
    /// Carbons in the substrate the current settings describe, split into the
    /// two pools the chemistry draws from.
    void substrateCounts(int& total, int& basal, int& edge) const;
    bool flakeSelected() const;
    /// Show stage `index`, with the header and button states that go with it.
    /// One place, so the three stages cannot disagree about which is last.
    void showStage(int index);
    static constexpr int kLastStage = 2;

    static constexpr std::size_t kGroups = core::GrapheneOxideBuilder::kGroupCount;

    QStackedWidget* stack_ = nullptr;
    QPushButton* backButton_ = nullptr;
    QPushButton* nextButton_ = nullptr;
    QLabel* stageLabel_ = nullptr;

    // Stage 1 — base structure
    QComboBox* baseCombo_ = nullptr;
    QGroupBox* sheetBox_ = nullptr;
    QComboBox* latticeCombo_ = nullptr;
    QSpinBox* supercellSpin_[2] = {nullptr, nullptr};
    QGroupBox* flakeBox_ = nullptr;
    QComboBox* generationCombo_ = nullptr;
    QCheckBox* hydrogenCheck_ = nullptr;
    QLabel* baseSummary_ = nullptr;

    /// One ratio: a slider to sweep it, a spin box to type it exactly, and a
    /// read-out that says what the number MEANS.
    ///
    /// The slider and the box are two views of one value, so they have to agree
    /// bit for bit — a slider that cannot reach the number in the box next to
    /// it is worse than either control alone. Both are therefore quantized to
    /// the SAME grid: every slider here counts in thousandths, so its integer
    /// position divided by 1000 IS the physical value, and the box shows three
    /// decimals. No rounding happens in either direction, which is what lets
    /// the two be synchronized without drift.
    struct RatioControl {
        QSlider* slider = nullptr;
        QDoubleSpinBox* box = nullptr;
        QLabel* readout = nullptr;

        /// The physical value. Reads the SLIDER, which is the single source of
        /// truth; the box is kept equal to it.
        double value() const;
    };

    /// Slider units per 1.0 of ratio. Fixed for every control so one
    /// conversion serves them all and no call site invents a scale.
    static constexpr int kRatioScale = 1000;

    /// Build a caption / slider / spin box / read-out row and wire the two
    /// controls to each other.
    void addRatioRow(QFormLayout* form, RatioControl& control,
                     const QString& name, const QString& caption,
                     const QString& tooltip, double initial, double maximum);

    // Stage 2 — functionalization. One slider per question, and no mode
    // selector: there is only one way to state an oxidation level here.
    //
    /// Total oxidation as O/C, 0.00 (pristine) to 0.50 (the stoichiometric
    /// ceiling, C2O). The builder works in C/O, so this is inverted on the way
    /// in; O/C is what the UI shows because it is linear in oxygen content and
    /// has a meaningful zero, neither of which C/O has.
    RatioControl basalOxidation_;
    /// Basal chemistry as H/O: 0 = epoxide only, 1 = hydroxyl only. Defaults
    /// to 2/3 -- epoxide:hydroxyl = 1:2.
    RatioControl basalHydrogen_;
    /// Force every hydroxyl onto a bonded pair of basal carbons, one -OH per
    /// carbon on opposite faces (a trans-diol), instead of each hydroxyl
    /// sitting on its own independently chosen carbon.
    QCheckBox* hydroxylAntipositionCheck_ = nullptr;
    /// Share of the oxygen budget delivered at the EDGES rather than on the
    /// basal plane — the edge oxidation density. Flake only.
    RatioControl edgeOxidation_;
    /// Edge composition: 0 = carbonyl only, 1 = carboxyl only. Flake only.
    RatioControl edgeCarboxyl_;
    /// Holds the two edge controls, so both vanish together on a periodic
    /// sheet — which has no rim for either of them to describe.
    QGroupBox* edgeChemistryBox_ = nullptr;
    QLabel* edgeNote_ = nullptr;
    QCheckBox* bothFacesCheck_ = nullptr;
    /// Stage 3 — opt in to the MDMC refinement.
    QCheckBox* mdmcCheck_ = nullptr;
    QLabel* mdmcCostNote_ = nullptr;
    QSpinBox* seedSpin_ = nullptr;
    QLabel* coverageSummary_ = nullptr;

    /// False until every widget exists. Guards refreshSummary() against the
    /// signals that setChecked()/setValue() emit during construction.
    bool built_ = false;

    std::optional<core::Structure> result_;
    core::GrapheneOxideBuilder::Report report_;
};

} // namespace calango::gui
