#pragma once

#include "core/PolymerBuilder.hpp"

#include <QDialog>

#include <optional>

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QPushButton;
class QSpinBox;
class QStackedWidget;

namespace calango::gui {

/// Build → "Macromolecules…": a two-stage polymer construction wizard running
/// on core::PolymerBuilder.
///
///   Stage 1  Simulation Box Configuration — explicit box edges, or a density
///            target the box is sized from, plus the chain count.
///   Stage 2  Polymer Architecture & Tactics — monomer chemistry, degree of
///            polymerization, tacticity, end capping and backbone conformation.
///
/// Accepting exposes the packed cell through result().
class MacromoleculeWizard : public QDialog {
    Q_OBJECT

public:
    explicit MacromoleculeWizard(QWidget* parent = nullptr);

    const std::optional<core::PolymerBuilder::Result>& result() const
    {
        return result_;
    }

private Q_SLOTS:
    void goNext();
    void goBack();
    void generate();
    /// Enable only the controls the current monomer / box mode actually uses.
    void updateControls();

private:
    void updateStage();
    QWidget* buildBoxPage();
    QWidget* buildArchitecturePage();

    std::optional<core::PolymerBuilder::Result> result_;
    int stage_ = 0;

    QStackedWidget* stack_ = nullptr;
    QLabel* headerLabel_ = nullptr;
    QLabel* estimateLabel_ = nullptr;
    QPushButton* backButton_ = nullptr;
    QPushButton* nextButton_ = nullptr;
    QPushButton* generateButton_ = nullptr;

    QCheckBox* densityTargetCheck_ = nullptr;
    QDoubleSpinBox* densitySpin_ = nullptr;
    QDoubleSpinBox* boxSpins_[3] = {nullptr, nullptr, nullptr};
    QSpinBox* chainCountSpin_ = nullptr;
    QDoubleSpinBox* minDistanceSpin_ = nullptr;
    QSpinBox* seedSpin_ = nullptr;

    QComboBox* monomerCombo_ = nullptr;
    QSpinBox* degreeSpin_ = nullptr;
    QComboBox* tacticityCombo_ = nullptr;
    QComboBox* endCapCombo_ = nullptr;
    QComboBox* conformationCombo_ = nullptr;
    QDoubleSpinBox* helixTorsionSpin_ = nullptr;
    QLabel* tacticityNoteLabel_ = nullptr;
};

} // namespace calango::gui
