#pragma once

#include "core/SolidInterfaceBuilder.hpp"

#include <QString>
#include <QWizard>

#include <memory>
#include <optional>
#include <utility>
#include <vector>

class QComboBox;
class QDoubleSpinBox;
class QFormLayout;
class QLabel;
class QSpinBox;
class QTableWidget;

namespace calango::gui {

class SolidInterfaceWizard;

/// One candidate parent lattice: a name for the UI and the crystal itself.
/// Multi-phase polycrystals need more than one, and the only place a second
/// crystal can come from is another open workspace tab.
using PhaseSource = std::pair<QString, std::shared_ptr<const core::Structure>>;

/// Stage 1: which interface, and the plane it lives on.
class SolidInterfaceKindPage : public QWizardPage {
    Q_OBJECT

public:
    explicit SolidInterfaceKindPage(SolidInterfaceWizard* wizard);

    void initializePage() override;
    bool isComplete() const override;

private Q_SLOTS:
    void refresh();

private:
    void applyKindVisibility();

    SolidInterfaceWizard* wizard_;
    QFormLayout* form_;
    QComboBox* kindCombo_;
    QComboBox* axisCombo_;
    QDoubleSpinBox* positionSpin_;
    QDoubleSpinBox* faultSpins_[2];
    QDoubleSpinBox* gapSpin_;
    QDoubleSpinBox* mergeSpin_;
    QLabel* summaryLabel_;
    bool valid_ = false;
};

/// Stage 2: the grains. Box size for every space-filling kind, misorientation
/// for a bicrystal, grain count and seed for a polycrystal, and the phase
/// table for a multi-phase one.
class SolidInterfaceGrainPage : public QWizardPage {
    Q_OBJECT

public:
    explicit SolidInterfaceGrainPage(SolidInterfaceWizard* wizard);

    void initializePage() override;
    bool isComplete() const override;
    bool validatePage() override;

private Q_SLOTS:
    void refresh();

private:
    void applyKindVisibility();

    SolidInterfaceWizard* wizard_;
    QFormLayout* form_;
    QSpinBox* repeatSpins_[3];
    QDoubleSpinBox* rotationASpin_;
    QDoubleSpinBox* rotationBSpin_;
    QSpinBox* grainSpin_;
    QSpinBox* seedSpin_;
    QTableWidget* phaseTable_;
    QLabel* estimateLabel_;
};

/// "Build → Solid Interface…": stacking faults, twin boundaries, bicrystals
/// and (multi-phase) polycrystals, built from the open structures as parent
/// lattices.
///
///   Stage 1  Interface & plane — kind, boundary normal and position, fault
///            vector, gap, merge tolerance
///   Stage 2  Grains — box repeats, misorientation, grain count, phases
///
/// The geometry is core::SolidInterfaceBuilder; this class owns the parameters
/// and the finished cell.
class SolidInterfaceWizard : public QWizard {
    Q_OBJECT

public:
    /// `phases` lists the crystals available as parent lattices — normally the
    /// open workspace tabs, with the active one first. The first entry is the
    /// parent for every kind but the multi-phase polycrystal, which draws from
    /// whichever entries the user weights above zero.
    explicit SolidInterfaceWizard(std::vector<PhaseSource> phases,
                                  QWidget* parent = nullptr);

    const std::optional<core::SolidInterfaceBuilder::Result>& result() const
    {
        return result_;
    }

    std::vector<PhaseSource> phases;
    core::SolidInterfaceBuilder::Params params;
    /// Per-entry weight of `phases`, edited by the grain page. Zero excludes.
    std::vector<double> weights;

    bool build(QString* error);

private:
    std::optional<core::SolidInterfaceBuilder::Result> result_;
};

} // namespace calango::gui
