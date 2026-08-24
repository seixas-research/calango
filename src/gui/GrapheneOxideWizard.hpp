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

/// Stage 1's substrate preview. Defined in GrapheneOxideWizard.cpp.
class GrapheneOxidePreviewWidget;

/// Modules → Graphene Oxide → "Graphene Oxide Builder…": a two-stage builder
/// for a functionalized graphene substrate. GENERATION ONLY — the module that
/// used to also offer an MCMD refinement on a third stage no longer does; that
/// refinement is now its own module, "GO/MCMD" (GrapheneOxideMcmdWizard),
/// which takes this dialog's output as input rather than being chained from
/// it. Several independent MCMD runs from one build (different temperatures,
/// seeds, step counts) is the whole reason for the split — see
/// GrapheneOxideMcmdWizard's own doc comment.
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
///
/// The result is a "Graphene Oxide Build": the structure PLUS the persisted
/// per-atom classification core::GrapheneOxideBuilder::build() writes onto it
/// ("go_group" / "go_group_id" / "go_pair_id", alongside the existing "edge"
/// field) — the contract every downstream GO module (GO/MCMD, GO/MC-Opt,
/// GO Functional
/// Group Analysis, GO Pair Correlation) reads instead of re-deriving its own
/// notion of "which carbon is which". Nothing in this dialog has to do
/// anything extra to produce that contract; core::GrapheneOxideBuilder::build()
/// already writes it.
class GrapheneOxideWizard : public QDialog {
    Q_OBJECT

public:
    explicit GrapheneOxideWizard(QWidget* parent = nullptr);

    /// The generated structure, valid after exec() returns Accepted.
    const std::optional<core::Structure>& result() const { return result_; }
    /// What the builder actually placed, for the caller's status line.
    const core::GrapheneOxideBuilder::Report& report() const { return report_; }
    /// What was ASKED for — the config() actually passed to
    /// core::GrapheneOxideBuilder::build(), read by the host to record the
    /// build's provenance (Document::goBuildProvenance) alongside the
    /// persisted per-atom contract build() itself writes onto the structure.
    /// Cheap to call again after exec(): it only reads widget state, the same
    /// widgets refreshSummary() has been reading throughout.
    core::GrapheneOxideBuilder::Config config() const;

    /// Seed every control from `config` — the inverse of config().
    ///
    /// Exists for the Orchestration canvas, where a Graphene Oxide Builder
    /// node stores its configuration and re-opens this wizard to edit it. A
    /// wizard that always opened on its defaults would silently discard the
    /// node's settings the moment anyone looked at them.
    ///
    /// Only the fields config() WRITES are read back. The rest of
    /// Builder::Config is not reachable from this dialog at all (it offers one
    /// dosing mode), so restoring them here would claim an edit the user
    /// cannot make; the canvas node keeps the whole Config and hands back what
    /// this returns, so nothing is lost either way.
    void setConfig(const core::GrapheneOxideBuilder::Config& config);

public:
    /// Whether the structure just built has hydroxyls placed as bonded,
    /// opposite-face pairs (the "Hydroxyls antiposition" option) — read by
    /// the host so the follow-on MCMD wizard can move each pair as one
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
    /// Carbons in the substrate the current settings describe, split into the
    /// two pools the chemistry draws from.
    void substrateCounts(int& total, int& basal, int& edge) const;
    bool flakeSelected() const;
    /// Show stage `index`, with the header and button states that go with it.
    /// One place, so the three stages cannot disagree about which is last.
    void showStage(int index);
    static constexpr int kLastStage = 1;

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
    /// Vacuum clearance, in angstrom. Applies to BOTH bases — the sheet's
    /// gap above and below, and the flake box's padding on every face.
    QDoubleSpinBox* vacuumSpin_ = nullptr;
    QGroupBox* flakeBox_ = nullptr;
    QComboBox* generationCombo_ = nullptr;
    QCheckBox* hydrogenCheck_ = nullptr;
    QLabel* baseSummary_ = nullptr;
    /// Stage 1's live drawing of the substrate the current settings produce.
    /// Defined in the .cpp — it is a paintEvent and nothing else, so it needs
    /// no moc and no header of its own.
    GrapheneOxidePreviewWidget* preview_ = nullptr;

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
        /// Drive both halves to `value`. The slider is written last so its
        /// own signal is the one that lands, keeping it the source of truth.
        void setValue(double value);
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
    QSpinBox* seedSpin_ = nullptr;
    QLabel* coverageSummary_ = nullptr;

    /// False until every widget exists. Guards refreshSummary() against the
    /// signals that setChecked()/setValue() emit during construction.
    bool built_ = false;

    std::optional<core::Structure> result_;
    core::GrapheneOxideBuilder::Report report_;
};

} // namespace calango::gui
