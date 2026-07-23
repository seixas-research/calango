#pragma once

#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QLabel>
#include <QPushButton>
#include <QSlider>
#include <QWidget>

#include <array>

namespace calango::gui {

class ViewportWidget;

/// "Representation" dock panel: representation mode, atom color mapping
/// (element / CN / GCN / custom scalar with gradient + legend range),
/// global atom-radius and bond-width scales, gradient bond shading,
/// per-element settings, and the viewport background color.
class RepresentationPanel : public QWidget {
    Q_OBJECT

public:
    explicit RepresentationPanel(ViewportWidget* viewport, QWidget* parent = nullptr);

Q_SIGNALS:
    /// Assign `order` (1-3) to the currently selected atom pair. Emitted
    /// only from the bond-order buttons; MainWindow owns the mutable
    /// structure and applies the change (with an undo snapshot).
    void bondOrderAssignRequested(int order);

private Q_SLOTS:
    void applyColorMode();
    void refreshPropertyList();
    void syncColoringFromViewport();

private:
    ViewportWidget* viewport_;

    QComboBox* modeCombo_;
    QComboBox* colorModeCombo_;
    QComboBox* gradientCombo_;
    QCheckBox* invertGradientCheck_;
    QComboBox* propertyCombo_;
    QLabel* rangeLabel_;
    QSlider* atomScaleSlider_;
    QDoubleSpinBox* atomScaleSpin_;
    QSlider* bondWidthSlider_;
    QDoubleSpinBox* bondWidthSpin_;
    QCheckBox* gradientBondsCheck_;
    std::array<QPushButton*, 3> bondOrderButtons_{};
    QComboBox* vectorOverlayCombo_;
    QComboBox* surfaceFinishCombo_;
    QSlider* vectorScaleSlider_;
    QDoubleSpinBox* vectorScaleSpin_;
};

} // namespace calango::gui
