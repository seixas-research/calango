#include "gui/DsimPlotStyleDialog.hpp"
#include "gui/GuiUtils.hpp"

#include <QCheckBox>
#include <QColorDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QPushButton>
#include <QVBoxLayout>

namespace calango::gui {

DsimPlotStyleDialog::DsimPlotStyleDialog(const DsimPlotStyle& style, QWidget* parent)
    : QDialog(parent), style_(style)
{
    setWindowTitle(tr("Customize Appearance"));

    auto* layout = new QVBoxLayout(this);

    auto* colorGroup = new QGroupBox(tr("Colors"), this);
    auto* colorForm = new QFormLayout(colorGroup);
    curveButton_ = colorButton(&style_.curveColor);
    colorForm->addRow(tr("DeltaH_mix(x) curve:"), curveButton_);
    tangentButton_ = colorButton(&style_.tangentColor);
    colorForm->addRow(tr("Dilute-limit tangents:"), tangentButton_);
    layout->addWidget(colorGroup);

    auto* displayGroup = new QGroupBox(tr("Display"), this);
    auto* displayForm = new QFormLayout(displayGroup);
    showTangentsCheck_ = new QCheckBox(tr("Show dilute-limit tangent lines (Eq. 8)"), displayGroup);
    displayForm->addRow(showTangentsCheck_);
    kjPerMolCheck_ = new QCheckBox(tr("kJ/mol (unchecked: eV/atom)"), displayGroup);
    displayForm->addRow(tr("Units:"), kjPerMolCheck_);
    showColorbarCheck_ = new QCheckBox(tr("Show colorbar (ternary plots only)"), displayGroup);
    displayForm->addRow(showColorbarCheck_);
    layout->addWidget(displayGroup);

    auto* buttons = new QDialogButtonBox(
        QDialogButtonBox::Close | QDialogButtonBox::RestoreDefaults, this);
    layout->addWidget(buttons);
    connect(buttons->button(QDialogButtonBox::Close), &QPushButton::clicked, this, &QWidget::close);
    connect(buttons->button(QDialogButtonBox::RestoreDefaults), &QPushButton::clicked, this,
            &DsimPlotStyleDialog::restoreDefaults);

    for (QPushButton* button : findChildren<QPushButton*>()) {
        button->setAutoDefault(false);
        button->setDefault(false);
    }

    syncToControls();

    connect(showTangentsCheck_, &QCheckBox::toggled, this, [this](bool on) {
        style_.showTangents = on;
        emitStyle();
    });
    connect(kjPerMolCheck_, &QCheckBox::toggled, this, [this](bool on) {
        style_.useKilojoulesPerMole = on;
        emitStyle();
    });
    connect(showColorbarCheck_, &QCheckBox::toggled, this, [this](bool on) {
        style_.showColorbar = on;
        emitStyle();
    });
}

QPushButton* DsimPlotStyleDialog::colorButton(QColor* target)
{
    auto* button = new QPushButton(this);
    button->setFixedWidth(80);
    setButtonColor(button, *target);
    connect(button, &QPushButton::clicked, this, [this, button, target] {
        const QColor chosen = QColorDialog::getColor(*target, this, tr("Select colour"));
        if (!chosen.isValid())
            return;
        *target = chosen;
        setButtonColor(button, chosen);
        emitStyle();
    });
    return button;
}

void DsimPlotStyleDialog::syncToControls()
{
    setButtonColor(curveButton_, style_.curveColor);
    setButtonColor(tangentButton_, style_.tangentColor);
    const QSignalBlocker blockTangents(showTangentsCheck_);
    showTangentsCheck_->setChecked(style_.showTangents);
    const QSignalBlocker blockUnits(kjPerMolCheck_);
    kjPerMolCheck_->setChecked(style_.useKilojoulesPerMole);
    const QSignalBlocker blockColorbar(showColorbarCheck_);
    showColorbarCheck_->setChecked(style_.showColorbar);
}

void DsimPlotStyleDialog::restoreDefaults()
{
    style_ = DsimPlotStyle{};
    syncToControls();
    emitStyle();
}

void DsimPlotStyleDialog::emitStyle()
{
    Q_EMIT styleChanged(style_);
}

} // namespace calango::gui
