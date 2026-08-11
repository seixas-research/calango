#pragma once

#include "core/GrapheneOxideBuilder.hpp"

#include <QDialog>

#include <optional>

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
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
///   Stage 2 — Functionalization & Oxidation Level: which oxygen-bearing groups
///     to attach, split into the basal-plane (sp3) and edge (sp2) families, and
///     how much oxygen to put on — either as explicit per-group coverages or by
///     driving the structure to a target C/O ratio.
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

    /// Read a ratio slider as its physical value. Every slider here is an
    /// integer 0..1000 standing for a ratio, so one conversion serves them all
    /// and no call site has to remember the scale.
    static double sliderValue(const QSlider* slider, double maximum);

    // Stage 2 — functionalization
    QComboBox* dosingCombo_ = nullptr;
    /// Total oxidation as O/C, 0.00 (pristine) to 0.50 (the stoichiometric
    /// ceiling, C2O). The builder works in C/O, so this is inverted on the way
    /// in; O/C is what the UI shows because it is linear in oxygen content and
    /// has a meaningful zero, neither of which C/O has.
    QSlider* oxygenToCarbonSlider_ = nullptr;
    QLabel* oxygenToCarbonLabel_ = nullptr;
    /// Basal chemistry as H/O: 0 = epoxide only, 1 = hydroxyl only.
    QSlider* basalHydrogenSlider_ = nullptr;
    QLabel* basalHydrogenLabel_ = nullptr;
    /// Share of the oxygen budget delivered at the EDGES rather than on the
    /// basal plane — the edge oxidation density.
    QSlider* edgeShareSlider_ = nullptr;
    QLabel* edgeShareLabel_ = nullptr;
    /// Edge composition: 0 = carbonyl only, 1 = carboxyl only.
    QSlider* edgeCarboxylSlider_ = nullptr;
    QLabel* edgeCarboxylLabel_ = nullptr;
    QGroupBox* ratioBox_ = nullptr;
    QGroupBox* edgeChemistryBox_ = nullptr;
    QLabel* amountHint_ = nullptr;
    QLabel* edgeNote_ = nullptr;
    QCheckBox* groupCheck_[kGroups] = {};
    QDoubleSpinBox* groupAmount_[kGroups] = {};
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
