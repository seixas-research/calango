#include "gui/OpticsPlotStyleDialog.hpp"

#include <QApplication>
#include <QCheckBox>
#include <QColorDialog>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFontComboBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QPushButton>
#include <QSpinBox>
#include <QVBoxLayout>

namespace calango::gui {

QFont OpticsPlotStyle::axisFont() const
{
    QFont font = axisFontFamily.isEmpty() ? QApplication::font()
                                          : QFont(axisFontFamily);
    font.setPointSize(axisFontSize);
    return font;
}

QColor OpticsPlotStyle::effectiveGridColor() const
{
    QColor color = gridColor;
    color.setAlphaF(qBound(0.0, gridAlpha, 1.0));
    return color;
}

QColor OpticsPlotStyle::effectiveThresholdBandColor() const
{
    QColor color = thresholdBandColor;
    color.setAlphaF(qBound(0.0, thresholdBandOpacity, 1.0));
    return color;
}

OpticsPlotStyleDialog::OpticsPlotStyleDialog(const OpticsPlotStyle& style,
                                             QWidget* parent)
    : OpticsPlotStyleDialog(style, /*withThresholdBand=*/false, parent)
{
}

OpticsPlotStyleDialog::OpticsPlotStyleDialog(const OpticsPlotStyle& style,
                                             bool withThresholdBand,
                                             QWidget* parent)
    : QDialog(parent)
    , style_(style)
{
    setWindowTitle(tr("Customize Appearance"));

    auto* layout = new QVBoxLayout(this);

    auto* fillGroup = new QGroupBox(tr("Background"), this);
    auto* fillForm = new QFormLayout(fillGroup);
    canvasButton_ = colorButton(&style_.canvasBackground);
    plotButton_ = colorButton(&style_.plotBackground);
    fillForm->addRow(tr("Canvas fill:"), canvasButton_);
    fillForm->addRow(tr("Plot area fill:"), plotButton_);
    layout->addWidget(fillGroup);

    auto* curveGroup = new QGroupBox(tr("Curves"), this);
    auto* curveForm = new QFormLayout(curveGroup);
    overrideCurveCheck_ =
        new QCheckBox(tr("Use one color for every curve"), curveGroup);
    overrideCurveCheck_->setToolTip(
        tr("Off, curves cycle through a palette so paired quantities (ε₁ with "
           "ε₂, n with k) stay distinguishable. On, they share one stroke — "
           "usually what a single-curve figure wants."));
    curveForm->addRow(overrideCurveCheck_);
    curveButton_ = colorButton(&style_.curveColor);
    curveForm->addRow(tr("Curve color:"), curveButton_);
    lineWidthSpin_ = new QDoubleSpinBox(curveGroup);
    lineWidthSpin_->setRange(0.2, 8.0);
    lineWidthSpin_->setSingleStep(0.2);
    lineWidthSpin_->setDecimals(1);
    curveForm->addRow(tr("Line width:"), lineWidthSpin_);
    lineStyleCombo_ = new QComboBox(curveGroup);
    lineStyleCombo_->addItem(tr("Solid"), static_cast<int>(Qt::SolidLine));
    lineStyleCombo_->addItem(tr("Dashed"), static_cast<int>(Qt::DashLine));
    lineStyleCombo_->addItem(tr("Dotted"), static_cast<int>(Qt::DotLine));
    curveForm->addRow(tr("Line style:"), lineStyleCombo_);
    layout->addWidget(curveGroup);

    auto* axisGroup = new QGroupBox(tr("Axes"), this);
    auto* axisForm = new QFormLayout(axisGroup);
    fontCombo_ = new QFontComboBox(axisGroup);
    axisForm->addRow(tr("Font family:"), fontCombo_);
    fontSizeSpin_ = new QSpinBox(axisGroup);
    fontSizeSpin_->setRange(5, 48);
    fontSizeSpin_->setSuffix(tr(" pt"));
    axisForm->addRow(tr("Font size:"), fontSizeSpin_);
    labelButton_ = colorButton(&style_.axisLabelColor);
    axisForm->addRow(tr("Label color:"), labelButton_);
    layout->addWidget(axisGroup);

    auto* gridGroup = new QGroupBox(tr("Grid"), this);
    auto* gridForm = new QFormLayout(gridGroup);
    gridCheck_ = new QCheckBox(tr("Show grid lines"), gridGroup);
    gridForm->addRow(gridCheck_);
    gridButton_ = colorButton(&style_.gridColor);
    gridForm->addRow(tr("Grid color:"), gridButton_);
    gridAlphaSpin_ = new QDoubleSpinBox(gridGroup);
    gridAlphaSpin_->setRange(0.0, 1.0);
    gridAlphaSpin_->setSingleStep(0.05);
    gridAlphaSpin_->setDecimals(2);
    gridAlphaSpin_->setToolTip(
        tr("Grid opacity. A grid at full strength competes with the data; "
           "0.3–0.5 keeps it readable as a reference without doing so."));
    gridForm->addRow(tr("Grid opacity:"), gridAlphaSpin_);
    layout->addWidget(gridGroup);

    if (withThresholdBand) {
        auto* bandGroup = new QGroupBox(tr("Threshold Band"), this);
        auto* bandForm = new QFormLayout(bandGroup);
        bandButton_ = colorButton(&style_.thresholdBandColor);
        bandForm->addRow(tr("Band color:"), bandButton_);
        bandPatternCombo_ = new QComboBox(bandGroup);
        bandPatternCombo_->addItem(tr("Diagonal hatch (⟋)"),
                                   static_cast<int>(Qt::BDiagPattern));
        bandPatternCombo_->addItem(tr("Diagonal hatch (⟍)"),
                                   static_cast<int>(Qt::FDiagPattern));
        bandPatternCombo_->addItem(tr("Cross hatch"),
                                   static_cast<int>(Qt::DiagCrossPattern));
        bandPatternCombo_->addItem(tr("Dotted fill"),
                                   static_cast<int>(Qt::Dense4Pattern));
        bandPatternCombo_->addItem(tr("Solid fill"),
                                   static_cast<int>(Qt::SolidPattern));
        bandForm->addRow(tr("Fill pattern:"), bandPatternCombo_);
        bandOpacitySpin_ = new QDoubleSpinBox(bandGroup);
        bandOpacitySpin_->setRange(0.05, 1.0);
        bandOpacitySpin_->setSingleStep(0.05);
        bandOpacitySpin_->setDecimals(2);
        bandOpacitySpin_->setToolTip(
            tr("Band opacity. The corridor is context, not data — keep it "
               "light enough that the curve stays the loudest thing on the "
               "plot."));
        bandForm->addRow(tr("Opacity:"), bandOpacitySpin_);
        layout->addWidget(bandGroup);

        connect(bandPatternCombo_, &QComboBox::currentIndexChanged, this,
                [this] {
                    style_.thresholdBandPattern = static_cast<Qt::BrushStyle>(
                        bandPatternCombo_->currentData().toInt());
                    emitStyle();
                });
        connect(bandOpacitySpin_, &QDoubleSpinBox::valueChanged, this,
                [this](double v) {
                    style_.thresholdBandOpacity = v;
                    emitStyle();
                });
    }

    auto* buttons = new QDialogButtonBox(
        QDialogButtonBox::Close | QDialogButtonBox::RestoreDefaults, this);
    layout->addWidget(buttons);
    // The Close button dismisses THIS dialog and nothing else — wired to the
    // button's own click rather than the box's accept/reject roles, so no
    // dialog-level done() semantics are involved that a hosting window could
    // be sensitive to.
    connect(buttons->button(QDialogButtonBox::Close), &QPushButton::clicked,
            this, &QWidget::close);
    connect(buttons->button(QDialogButtonBox::RestoreDefaults),
            &QPushButton::clicked, this,
            &OpticsPlotStyleDialog::restoreDefaults);

    // In a QDialog every push button (the color swatches included) is
    // autoDefault, so Return after typing in a spin box "clicks" the first
    // one — which is how adjusting a value could dismiss the dialog, or
    // worse pop a color picker, before the change was even seen. Styling is
    // judged live against the plot; no button here earns Return.
    for (QPushButton* button : findChildren<QPushButton*>()) {
        button->setAutoDefault(false);
        button->setDefault(false);
    }

    syncToControls();

    // Live application: every control writes through to the viewer as it
    // changes, so the result is judged against the real plot.
    connect(overrideCurveCheck_, &QCheckBox::toggled, this, [this](bool on) {
        style_.overrideCurveColor = on;
        curveButton_->setEnabled(on);
        emitStyle();
    });
    connect(lineWidthSpin_, &QDoubleSpinBox::valueChanged, this,
            [this](double v) { style_.lineWidth = v; emitStyle(); });
    connect(lineStyleCombo_, &QComboBox::currentIndexChanged, this, [this] {
        style_.lineStyle =
            static_cast<Qt::PenStyle>(lineStyleCombo_->currentData().toInt());
        emitStyle();
    });
    connect(fontCombo_, &QFontComboBox::currentFontChanged, this,
            [this](const QFont& font) {
                style_.axisFontFamily = font.family();
                emitStyle();
            });
    connect(fontSizeSpin_, &QSpinBox::valueChanged, this,
            [this](int v) { style_.axisFontSize = v; emitStyle(); });
    connect(gridCheck_, &QCheckBox::toggled, this, [this](bool on) {
        style_.showGrid = on;
        gridButton_->setEnabled(on);
        gridAlphaSpin_->setEnabled(on);
        emitStyle();
    });
    connect(gridAlphaSpin_, &QDoubleSpinBox::valueChanged, this,
            [this](double v) { style_.gridAlpha = v; emitStyle(); });
}

QPushButton* OpticsPlotStyleDialog::colorButton(QColor* target)
{
    auto* button = new QPushButton(this);
    button->setFixedWidth(80);
    paintSwatch(button, *target);
    connect(button, &QPushButton::clicked, this, [this, button, target] {
        const QColor chosen = QColorDialog::getColor(
            *target, this, tr("Select colour"),
            QColorDialog::ShowAlphaChannel);
        if (!chosen.isValid())
            return;
        *target = chosen;
        paintSwatch(button, chosen);
        emitStyle();
    });
    return button;
}

void OpticsPlotStyleDialog::paintSwatch(QPushButton* button,
                                        const QColor& color)
{
    button->setStyleSheet(
        QStringLiteral("background-color: %1; border: 1px solid #666;")
            .arg(color.name()));
}

void OpticsPlotStyleDialog::syncToControls()
{
    const QSignalBlocker b1(overrideCurveCheck_);
    const QSignalBlocker b2(lineWidthSpin_);
    const QSignalBlocker b3(lineStyleCombo_);
    const QSignalBlocker b4(fontCombo_);
    const QSignalBlocker b5(fontSizeSpin_);
    const QSignalBlocker b6(gridCheck_);
    const QSignalBlocker b7(gridAlphaSpin_);

    overrideCurveCheck_->setChecked(style_.overrideCurveColor);
    curveButton_->setEnabled(style_.overrideCurveColor);
    lineWidthSpin_->setValue(style_.lineWidth);
    lineStyleCombo_->setCurrentIndex(
        lineStyleCombo_->findData(static_cast<int>(style_.lineStyle)));
    fontCombo_->setCurrentFont(style_.axisFont());
    fontSizeSpin_->setValue(style_.axisFontSize);
    gridCheck_->setChecked(style_.showGrid);
    gridButton_->setEnabled(style_.showGrid);
    gridAlphaSpin_->setEnabled(style_.showGrid);
    gridAlphaSpin_->setValue(style_.gridAlpha);

    paintSwatch(canvasButton_, style_.canvasBackground);
    paintSwatch(plotButton_, style_.plotBackground);
    paintSwatch(curveButton_, style_.curveColor);
    paintSwatch(labelButton_, style_.axisLabelColor);
    paintSwatch(gridButton_, style_.gridColor);

    // Present only when the dialog was built with the band group.
    if (bandButton_) {
        const QSignalBlocker b8(bandPatternCombo_);
        const QSignalBlocker b9(bandOpacitySpin_);
        paintSwatch(bandButton_, style_.thresholdBandColor);
        bandPatternCombo_->setCurrentIndex(bandPatternCombo_->findData(
            static_cast<int>(style_.thresholdBandPattern)));
        bandOpacitySpin_->setValue(style_.thresholdBandOpacity);
    }
}

void OpticsPlotStyleDialog::restoreDefaults()
{
    style_ = OpticsPlotStyle{};
    syncToControls();
    emitStyle();
}

void OpticsPlotStyleDialog::emitStyle()
{
    Q_EMIT styleChanged(style_);
}

} // namespace calango::gui
