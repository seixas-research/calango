#pragma once

#include "core/DislocationBuilder.hpp"

#include <QWizard>

#include <memory>
#include <optional>

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QFormLayout;
class QLabel;

namespace calango::gui {

class DislocationWizard;

/// Stage 1: which defect, and where. The type decides what the second page
/// even asks about, so it is settled first and on its own.
class DislocationTypePage : public QWizardPage {
    Q_OBJECT

public:
    explicit DislocationTypePage(DislocationWizard* wizard);

    void initializePage() override;
    bool isComplete() const override;

private Q_SLOTS:
    void refresh();

private:
    DislocationWizard* wizard_;
    QComboBox* typeCombo_;
    QComboBox* lineCombo_;
    QDoubleSpinBox* burgersSpin_;
    QComboBox* signCombo_;
    QDoubleSpinBox* centerSpins_[2];
    QLabel* summaryLabel_;
    bool valid_ = false;
};

/// Stage 2: the elasticity the field is computed from, plus whatever the
/// chosen type needs on top of it — a dipole separation, or a full elastic
/// tensor and a Burgers direction for the anisotropic solution.
class DislocationElasticityPage : public QWizardPage {
    Q_OBJECT

public:
    explicit DislocationElasticityPage(DislocationWizard* wizard);

    void initializePage() override;
    bool isComplete() const override;
    bool validatePage() override;

private Q_SLOTS:
    void refresh();

private:
    /// Show only the rows the current type reads. An elastic tensor offered
    /// for an isotropic construction is a set of numbers that go nowhere.
    void applyTypeVisibility();

    DislocationWizard* wizard_;
    QFormLayout* form_;
    QDoubleSpinBox* poissonSpin_;
    QDoubleSpinBox* separationSpin_;
    QComboBox* symmetryCombo_;
    QDoubleSpinBox* c11Spin_;
    QDoubleSpinBox* c12Spin_;
    QDoubleSpinBox* c44Spin_;
    QDoubleSpinBox* c13Spin_;
    QDoubleSpinBox* c33Spin_;
    QDoubleSpinBox* burgersDirectionSpins_[3];
    QCheckBox* wrapCheck_;
    QLabel* estimateLabel_;
};

/// "Build → Dislocation…": insert a Volterra dislocation into the current
/// structure by displacing its atoms with a closed-form elastic field.
///
///   Stage 1  Type & geometry — edge/screw/glide/climb/anisotropic, line
///            direction, |b|, sign, where the line sits
///   Stage 2  Elasticity — Poisson ratio, dipole separation, or the full
///            elastic tensor for the anisotropic case
///
/// The physics is core::DislocationBuilder; this class owns only the
/// parameters and the finished cell.
class DislocationWizard : public QWizard {
    Q_OBJECT

public:
    explicit DislocationWizard(std::shared_ptr<const core::Structure> source,
                               QWidget* parent = nullptr);

    const std::optional<core::DislocationBuilder::Result>& result() const
    {
        return result_;
    }

    /// Shared state, owned here and edited by the two pages.
    std::shared_ptr<const core::Structure> source;
    core::DislocationBuilder::Params params;

    /// Run the builder with the current parameters. Returns false and fills
    /// `error` when the request is refused.
    bool build(QString* error);

private:
    std::optional<core::DislocationBuilder::Result> result_;
};

} // namespace calango::gui
