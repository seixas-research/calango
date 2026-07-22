#include "gui/BrillouinZoneStyleDialog.hpp"

#include <QCheckBox>
#include <QColorDialog>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QIcon>
#include <QPixmap>
#include <QPushButton>
#include <QVBoxLayout>

#include <functional>

namespace calango::gui {

BrillouinZoneStyleDialog::BrillouinZoneStyleDialog(
    const BrillouinZoneView::Style& style, QWidget* parent)
    : QDialog(parent)
    , style_(style)
{
    setWindowTitle(tr("Brillouin Zone & k-Path Styling"));

    auto* layout = new QVBoxLayout(this);
    auto* form = new QFormLayout;
    layout->addLayout(form);

    const auto makeColorButton = [this](const QColor& initial,
                                        std::function<void(const QColor&)> apply) {
        auto* button = new QPushButton(this);
        button->setFixedWidth(120);
        updateSwatch(button, initial);
        connect(button, &QPushButton::clicked, this, [this, button, apply] {
            const QColor chosen = QColorDialog::getColor(
                button->property("swatch").value<QColor>(), this,
                tr("Select Color"), QColorDialog::ShowAlphaChannel);
            if (chosen.isValid()) {
                updateSwatch(button, chosen);
                apply(chosen);
                emitChanged();
            }
        });
        return button;
    };

    // BZ surface color.
    form->addRow(tr("BZ surface color:"),
                 makeColorButton(style_.surfaceColor, [this](const QColor& c) {
                     const float a = style_.surfaceColor.alphaF();
                     style_.surfaceColor = c;
                     style_.surfaceColor.setAlphaF(a);
                 }));

    // BZ transparency (alpha 0..1).
    auto* alphaSpin = new QDoubleSpinBox(this);
    alphaSpin->setRange(0.0, 1.0);
    alphaSpin->setSingleStep(0.05);
    alphaSpin->setDecimals(2);
    alphaSpin->setValue(style_.surfaceAlpha);
    form->addRow(tr("BZ transparency (alpha):"), alphaSpin);
    connect(alphaSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this,
            [this](double v) {
                style_.surfaceAlpha = static_cast<float>(v);
                emitChanged();
            });

    // BZ border wireframe color.
    form->addRow(tr("BZ border wireframe color:"),
                 makeColorButton(style_.edgeColor,
                                 [this](const QColor& c) { style_.edgeColor = c; }));

    // k-path line color.
    form->addRow(tr("k-path line color:"),
                 makeColorButton(style_.pathColor,
                                 [this](const QColor& c) { style_.pathColor = c; }));

    // k-path line thickness.
    auto* thicknessSpin = new QDoubleSpinBox(this);
    thicknessSpin->setRange(0.5, 12.0);
    thicknessSpin->setSingleStep(0.5);
    thicknessSpin->setDecimals(1);
    thicknessSpin->setSuffix(tr(" px"));
    thicknessSpin->setValue(style_.pathThickness);
    form->addRow(tr("k-path line thickness:"), thicknessSpin);
    connect(thicknessSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this,
            [this](double v) {
                style_.pathThickness = static_cast<float>(v);
                emitChanged();
            });

    // High-symmetry k-point labels (default on).
    auto* labelsCheck = new QCheckBox(
        tr("Show high-symmetry k-point labels"), this);
    labelsCheck->setChecked(style_.showLabels);
    layout->addWidget(labelsCheck);
    connect(labelsCheck, &QCheckBox::toggled, this, [this](bool on) {
        style_.showLabels = on;
        emitChanged();
    });

    // Sequential point numbers (default off).
    auto* ordersCheck = new QCheckBox(
        tr("Show sequential point numbers along path"), this);
    ordersCheck->setChecked(style_.showOrderNumbers);
    layout->addWidget(ordersCheck);
    connect(ordersCheck, &QCheckBox::toggled, this, [this](bool on) {
        style_.showOrderNumbers = on;
        emitChanged();
    });

    // Directional arrows along the k-path legs (default on).
    auto* arrowsCheck = new QCheckBox(
        tr("Show direction arrows along k-path"), this);
    arrowsCheck->setChecked(style_.showPathArrows);
    layout->addWidget(arrowsCheck);
    connect(arrowsCheck, &QCheckBox::toggled, this, [this](bool on) {
        style_.showPathArrows = on;
        emitChanged();
    });

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::accept);
    layout->addWidget(buttons);
}

void BrillouinZoneStyleDialog::updateSwatch(QPushButton* button,
                                            const QColor& color)
{
    button->setProperty("swatch", color);
    QPixmap pix(64, 16);
    pix.fill(color);
    button->setIcon(QIcon(pix));
    button->setText(color.name(QColor::HexRgb).toUpper());
}

void BrillouinZoneStyleDialog::emitChanged()
{
    Q_EMIT styleChanged(style_);
}

} // namespace calango::gui
