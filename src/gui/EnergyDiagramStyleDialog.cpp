#include "gui/EnergyDiagramStyleDialog.hpp"
#include "gui/GuiUtils.hpp"

#include <QColorDialog>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QPushButton>
#include <QVBoxLayout>

namespace calango::gui {

EnergyDiagramStyleDialog::EnergyDiagramStyleDialog(const EnergyDiagramStyle& style,
                                                   QWidget* parent)
    : QDialog(parent)
    , style_(style)
{
    setWindowTitle(tr("Customize Appearance"));

    auto* layout = new QVBoxLayout(this);

    auto* colorGroup = new QGroupBox(tr("Colors"), this);
    auto* colorForm = new QFormLayout(colorGroup);
    canvasButton_ = colorButton(&style_.canvasBackground);
    colorForm->addRow(tr("Background:"), canvasButton_);
    occupiedButton_ = colorButton(&style_.occupiedColor);
    colorForm->addRow(tr("Occupied levels:"), occupiedButton_);
    unoccupiedButton_ = colorButton(&style_.unoccupiedColor);
    colorForm->addRow(tr("Empty levels:"), unoccupiedButton_);
    gapLineButton_ = colorButton(&style_.gapLineColor);
    colorForm->addRow(tr("HOMO–LUMO gap line:"), gapLineButton_);
    layout->addWidget(colorGroup);

    auto* lineGroup = new QGroupBox(tr("Lines"), this);
    auto* lineForm = new QFormLayout(lineGroup);
    lineWidthSpin_ = new QDoubleSpinBox(lineGroup);
    lineWidthSpin_->setRange(0.5, 8.0);
    lineWidthSpin_->setSingleStep(0.5);
    lineWidthSpin_->setDecimals(1);
    lineForm->addRow(tr("Level line width:"), lineWidthSpin_);
    layout->addWidget(lineGroup);

    auto* buttons = new QDialogButtonBox(
        QDialogButtonBox::Close | QDialogButtonBox::RestoreDefaults, this);
    layout->addWidget(buttons);
    // Close dismisses only this dialog, not the diagram window behind it —
    // wired to the button's own click rather than accept/reject.
    connect(buttons->button(QDialogButtonBox::Close), &QPushButton::clicked,
            this, &QWidget::close);
    connect(buttons->button(QDialogButtonBox::RestoreDefaults),
            &QPushButton::clicked, this,
            &EnergyDiagramStyleDialog::restoreDefaults);

    // Every push button in a QDialog is autoDefault by default, so Return
    // after editing the spin box would "click" the first button (a color
    // swatch) instead of just committing the value.
    for (QPushButton* button : findChildren<QPushButton*>()) {
        button->setAutoDefault(false);
        button->setDefault(false);
    }

    syncToControls();

    // Live application: every control writes through immediately, judged
    // against the real diagram rather than a preview swatch.
    connect(lineWidthSpin_, &QDoubleSpinBox::valueChanged, this,
            [this](double v) { style_.lineWidth = v; emitStyle(); });
}

QPushButton* EnergyDiagramStyleDialog::colorButton(QColor* target)
{
    auto* button = new QPushButton(this);
    button->setFixedWidth(80);
    setButtonColor(button, *target);
    connect(button, &QPushButton::clicked, this, [this, button, target] {
        const QColor chosen = QColorDialog::getColor(
            *target, this, tr("Select colour"), QColorDialog::ShowAlphaChannel);
        if (!chosen.isValid())
            return;
        *target = chosen;
        setButtonColor(button, chosen);
        emitStyle();
    });
    return button;
}

void EnergyDiagramStyleDialog::syncToControls()
{
    const QSignalBlocker block(lineWidthSpin_);
    lineWidthSpin_->setValue(style_.lineWidth);

    setButtonColor(canvasButton_, style_.canvasBackground);
    setButtonColor(occupiedButton_, style_.occupiedColor);
    setButtonColor(unoccupiedButton_, style_.unoccupiedColor);
    setButtonColor(gapLineButton_, style_.gapLineColor);
}

void EnergyDiagramStyleDialog::restoreDefaults()
{
    style_ = EnergyDiagramStyle{};
    syncToControls();
    emitStyle();
}

void EnergyDiagramStyleDialog::emitStyle()
{
    Q_EMIT styleChanged(style_);
}

} // namespace calango::gui
