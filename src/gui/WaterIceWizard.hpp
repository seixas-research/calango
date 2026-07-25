#pragma once

#include "core/IceBuilder.hpp"

#include <QDialog>

#include <optional>

class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QSpinBox;
class QPushButton;
class QStackedWidget;

namespace calango::gui {

/// Build → "Water & Ice…": a two-stage generator for liquid water and the ice
/// polymorphs, running entirely on core::IceBuilder (no Python, no GenIce).
///
///   Stage 1  Box & Domain — cell replication for the crystalline phases, or a
///            density / molecule-count target for the liquid.
///   Stage 2  Phase & Geometry — which polymorph, and the rigid-monomer
///            geometry preset its molecules are built with.
///
/// Accepting exposes the generated cell through result().
class WaterIceWizard : public QDialog {
    Q_OBJECT

public:
    explicit WaterIceWizard(QWidget* parent = nullptr);

    const std::optional<core::IceBuilder::Result>& result() const
    {
        return result_;
    }

private Q_SLOTS:
    void goNext();
    void goBack();
    void generate();
    /// Show only the controls the selected phase actually uses.
    void updatePhaseControls();

private:
    void updateStage();
    QWidget* buildBoxPage();
    QWidget* buildPhasePage();

    std::optional<core::IceBuilder::Result> result_;
    int stage_ = 0;

    QStackedWidget* stack_ = nullptr;
    QLabel* headerLabel_ = nullptr;
    QLabel* estimateLabel_ = nullptr;
    QPushButton* backButton_ = nullptr;
    QPushButton* nextButton_ = nullptr;
    QPushButton* generateButton_ = nullptr;

    QSpinBox* replicateSpins_[3] = {nullptr, nullptr, nullptr};
    QDoubleSpinBox* densitySpin_ = nullptr;
    QSpinBox* moleculeSpin_ = nullptr;
    QDoubleSpinBox* minDistanceSpin_ = nullptr;
    QSpinBox* seedSpin_ = nullptr;
    QComboBox* phaseCombo_ = nullptr;
    QComboBox* geometryCombo_ = nullptr;
    QLabel* phaseNoteLabel_ = nullptr;
};

} // namespace calango::gui
