#pragma once

#include "gui/OverlayModel.hpp"

#include <QDialog>

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QFontComboBox;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QPushButton;
class QSlider;
class QSpinBox;
class QStackedWidget;

namespace calango::gui {

/// The add/edit sheet behind the "Additional overlays" dock.
///
/// One dialog for every overlay kind rather than one per kind: the type is the
/// first control, and changing it swaps the property page beneath. That is what
/// makes "add an overlay" a single decision instead of a menu of nine entry
/// points, and it is why the two former dialogs (Lattice Plane Settings, Custom
/// Overlay Manager) could be folded together at all — most of what they showed
/// was the same position/colour/opacity block twice.
///
/// Applied live: every control writes straight through to the working copy and
/// emits changed(), so the viewport updates as the user drags a slider rather
/// than on OK. Cancel restores the overlay the dialog was opened with.
class OverlayEditDialog : public QDialog {
    Q_OBJECT

public:
    OverlayEditDialog(const Overlay& overlay, bool structureHasCell,
                      QWidget* parent = nullptr);

    /// The edited overlay. Valid at any time — the dialog edits live.
    const Overlay& overlay() const { return overlay_; }

Q_SIGNALS:
    /// The working copy changed; the dock re-pushes the scene.
    void changed();

private Q_SLOTS:
    void onKindChanged();
    /// Read every visible control into the working copy and emit changed().
    void apply();

private:
    QWidget* buildTextPage();
    QWidget* buildLatticePlanePage();
    QWidget* buildPrimitivePage();
    /// Show only the primitive rows the selected kind actually uses.
    void showRelevantPrimitiveRows();
    void load();
    void updateColorButtons();

    Overlay overlay_;
    bool structureHasCell_ = false;
    /// Guards control->model writes while load() is populating the widgets.
    bool loading_ = false;

    QComboBox* kindCombo_ = nullptr;
    QLineEdit* nameEdit_ = nullptr;
    QCheckBox* visibleCheck_ = nullptr;
    QStackedWidget* pages_ = nullptr;

    // Text page
    /// Multi-line and resizable: annotations are routinely a label plus a value
    /// plus a unit, and a one-line QLineEdit forced those into one cramped run.
    QPlainTextEdit* textEdit_ = nullptr;
    QFontComboBox* fontCombo_ = nullptr;
    QSpinBox* fontSizeSpin_ = nullptr;
    QCheckBox* boldCheck_ = nullptr;
    QCheckBox* italicCheck_ = nullptr;
    QPushButton* textColorButton_ = nullptr;
    QCheckBox* backgroundCheck_ = nullptr;
    QPushButton* backgroundColorButton_ = nullptr;
    QSlider* backgroundOpacitySlider_ = nullptr;
    QDoubleSpinBox* textPosSpin_[3] = {nullptr, nullptr, nullptr};

    // Lattice-plane page
    QSpinBox* millerSpin_[3] = {nullptr, nullptr, nullptr};
    /// Slider and spin box over the same value: the slider is for sweeping the
    /// plane through the cell and watching it cut, the spin box for typing the
    /// exact height a figure caption will quote.
    QDoubleSpinBox* offsetSpin_ = nullptr;
    QSlider* offsetSlider_ = nullptr;
    QDoubleSpinBox* widthSpin_ = nullptr;
    QDoubleSpinBox* heightSpin_ = nullptr;
    QCheckBox* edgesCheck_ = nullptr;
    QLabel* noCellLabel_ = nullptr;
    /// Its own button, not colorButton_: both pages are constructed, so a
    /// shared pointer would leave whichever page was built first pointing at
    /// the other page's widget.
    QPushButton* planeColorButton_ = nullptr;

    // Primitive page
    QDoubleSpinBox* centerSpin_[3] = {nullptr, nullptr, nullptr};
    QDoubleSpinBox* sizeSpin_[3] = {nullptr, nullptr, nullptr};
    QDoubleSpinBox* endSpin_[3] = {nullptr, nullptr, nullptr};
    QDoubleSpinBox* normalSpin_[3] = {nullptr, nullptr, nullptr};
    QDoubleSpinBox* rotationSpin_[3] = {nullptr, nullptr, nullptr};
    QDoubleSpinBox* radiusSpin_ = nullptr;
    QSpinBox* resolutionSpin_ = nullptr;
    QComboBox* textureCombo_ = nullptr;
    QComboBox* finishCombo_ = nullptr;
    QPushButton* colorButton_ = nullptr;
    QPushButton* color2Button_ = nullptr;
    QWidget* sizeRow_ = nullptr;
    QWidget* endRow_ = nullptr;
    QWidget* normalRow_ = nullptr;
    QWidget* rotationRow_ = nullptr;
    QWidget* radiusRow_ = nullptr;

    // Shared opacity (every kind but Text, which has no fill to see through).
    QSlider* opacitySlider_ = nullptr;
    QWidget* opacityRow_ = nullptr;
};

} // namespace calango::gui
