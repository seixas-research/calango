#pragma once

#include "core/GrapheneOxideBuilder.hpp"

#include <QDialog>

#include <memory>
#include <optional>

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QPushButton;
class QSpinBox;
class QStackedWidget;

namespace calango::gui {

/// Modules → 2D Materials → "Graphene Oxide…": a two-stage builder for a
/// functionalized graphene sheet.
///
///   Stage 1 — Base Lattice & Supercell: which graphene cell to tile and how
///     many times.
///   Stage 2 — Functionalization & Coverages: which oxygen-bearing groups to
///     attach and at what coverage, with a live account of what the lattice can
///     actually accommodate.
///
/// Graphene oxide is non-stoichiometric and disordered, so what this produces
/// is a representative sample at a requested composition rather than "the"
/// structure. The seed is exposed for exactly that reason: a sample nobody can
/// regenerate is not a result.
class GrapheneOxideWizard : public QDialog {
    Q_OBJECT

public:
    explicit GrapheneOxideWizard(QWidget* parent = nullptr);

    /// The generated sheet, valid after exec() returns Accepted.
    const std::optional<core::Structure>& result() const { return result_; }
    /// What the builder actually placed, for the caller's status line.
    const core::GrapheneOxideBuilder::Report& report() const { return report_; }

private Q_SLOTS:
    void goNext();
    void goBack();
    /// Re-run the coverage arithmetic and update the live summary. Cheap: it
    /// counts sites rather than building the structure.
    void refreshSummary();

private:
    core::GrapheneOxideBuilder::Config config() const;

    QStackedWidget* stack_ = nullptr;
    QPushButton* backButton_ = nullptr;
    QPushButton* nextButton_ = nullptr;
    QLabel* stageLabel_ = nullptr;

    // Stage 1
    QComboBox* latticeCombo_ = nullptr;
    QSpinBox* supercellSpin_[2] = {nullptr, nullptr};
    QLabel* latticeSummary_ = nullptr;

    // Stage 2
    QCheckBox* groupCheck_[4] = {nullptr, nullptr, nullptr, nullptr};
    QDoubleSpinBox* groupCoverage_[4] = {nullptr, nullptr, nullptr, nullptr};
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
