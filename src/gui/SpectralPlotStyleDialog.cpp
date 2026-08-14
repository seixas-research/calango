#include "gui/SpectralPlotStyleDialog.hpp"

#include <QCheckBox>
#include <QColorDialog>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QPushButton>
#include <QSignalBlocker>
#include <QVBoxLayout>

#include <algorithm>

namespace calango::gui {

namespace {

// Same four the band/PDOS dialog offers, in the same order, so the two
// "Customize Appearance…" dialogs present identical choices for the same idea.
constexpr Qt::PenStyle kPenStyles[] = {Qt::SolidLine, Qt::DashLine,
                                       Qt::DotLine, Qt::DashDotLine};

int penStyleIndex(Qt::PenStyle style)
{
    for (int i = 0; i < 4; ++i)
        if (kPenStyles[i] == style)
            return i;
    return 0;
}

void setButtonColor(QPushButton* button, const QColor& color)
{
    button->setStyleSheet(
        QStringLiteral("background-color: %1; border: 1px solid #888;")
            .arg(color.name()));
}

} // namespace

QPushButton* SpectralPlotStyleDialog::colorButton(QColor* target)
{
    auto* button = new QPushButton(this);
    button->setFixedWidth(64);
    setButtonColor(button, *target);
    connect(button, &QPushButton::clicked, this, [this, button, target] {
        const QColor chosen =
            QColorDialog::getColor(*target, this, tr("Select Color"),
                                   QColorDialog::ShowAlphaChannel);
        if (!chosen.isValid())
            return;
        *target = chosen;
        setButtonColor(button, chosen);
        emitStyle();
    });
    return button;
}

void SpectralPlotStyleDialog::emitStyle()
{
    Q_EMIT styleChanged(style_);
}

SpectralPlotStyleDialog::SpectralPlotStyleDialog(
    const SpectralHeatmapWidget::Style& style, QWidget* parent)
    : QDialog(parent), style_(style)
{
    setWindowTitle(tr("Effective Band Structure Appearance"));
    auto* outer = new QVBoxLayout(this);

    // -- Spectral weight ----------------------------------------------------
    auto* weightGroup = new QGroupBox(tr("Spectral Weight"), this);
    auto* weightForm = new QFormLayout(weightGroup);

    auto* modeCombo = new QComboBox(weightGroup);
    modeCombo->addItem(tr("Heatmap (broadened field)"),
                       static_cast<int>(SpectralHeatmapWidget::RenderMode::Heatmap));
    modeCombo->addItem(tr("Scatter (one marker per state)"),
                       static_cast<int>(SpectralHeatmapWidget::RenderMode::Scatter));
    modeCombo->setCurrentIndex(
        style_.mode == SpectralHeatmapWidget::RenderMode::Scatter ? 1 : 0);
    modeCombo->setToolTip(
        tr("Heatmap broadens every state by σ and paints a continuous field — "
           "the right picture when bands overlap.\n\n"
           "Scatter draws the eigenvalues themselves with no broadening at "
           "all, which is sharper where the bands are clean and is how most "
           "of the unfolding literature presents them."));
    weightForm->addRow(tr("Rendering:"), modeCombo);
    connect(modeCombo, &QComboBox::currentIndexChanged, this,
            [this, modeCombo](int) {
                style_.mode = static_cast<SpectralHeatmapWidget::RenderMode>(
                    modeCombo->currentData().toInt());
                emitStyle();
            });

    auto* opacitySpin = new QDoubleSpinBox(weightGroup);
    opacitySpin->setRange(0.05, 1.0);
    opacitySpin->setSingleStep(0.05);
    opacitySpin->setDecimals(2);
    opacitySpin->setValue(style_.opacity);
    opacitySpin->setToolTip(
        tr("Opacity of the weight field. Below 1 the spectrum is blended "
           "toward the background, which is what lets an overlay stay legible "
           "on top of it."));
    weightForm->addRow(tr("Opacity:"), opacitySpin);
    connect(opacitySpin, &QDoubleSpinBox::valueChanged, this, [this](double v) {
        style_.opacity = v;
        emitStyle();
    });

    auto* markerSpin = new QDoubleSpinBox(weightGroup);
    markerSpin->setRange(0.5, 40.0);
    markerSpin->setSingleStep(0.5);
    markerSpin->setDecimals(1);
    markerSpin->setValue(style_.markerSize);
    markerSpin->setSuffix(tr(" px"));
    markerSpin->setToolTip(
        tr("Marker diameter at the maximum spectral weight. Scatter mode "
           "only."));
    weightForm->addRow(tr("Marker size:"), markerSpin);
    connect(markerSpin, &QDoubleSpinBox::valueChanged, this, [this](double v) {
        style_.markerSize = v;
        emitStyle();
    });

    auto* scaleCheck = new QCheckBox(tr("Marker size scales with weight"),
                                     weightGroup);
    scaleCheck->setChecked(style_.markerScalesWithWeight);
    scaleCheck->setToolTip(
        tr("On: a marker's AREA tracks its weight, so a weak state is a small "
           "dot rather than a pale one.\n\n"
           "Off: every marker is the same size and only the colour carries the "
           "weight — easier to see where states exist at all."));
    weightForm->addRow(QString(), scaleCheck);
    connect(scaleCheck, &QCheckBox::toggled, this, [this](bool on) {
        style_.markerScalesWithWeight = on;
        emitStyle();
    });

    auto* colorbarCheck = new QCheckBox(tr("Show colour scale"), weightGroup);
    colorbarCheck->setChecked(style_.showColorbar);
    colorbarCheck->setToolTip(
        tr("The scale is labelled by FRACTION of the maximum rather than by an "
           "absolute intensity: A(k,E) depends on σ and the bin width, so an "
           "absolute number would move when neither the physics nor the "
           "colours did."));
    weightForm->addRow(QString(), colorbarCheck);
    connect(colorbarCheck, &QCheckBox::toggled, this, [this](bool on) {
        style_.showColorbar = on;
        emitStyle();
    });
    outer->addWidget(weightGroup);

    // -- Energy window ------------------------------------------------------
    auto* windowGroup = new QGroupBox(tr("Energy Window"), this);
    auto* windowForm = new QFormLayout(windowGroup);
    energyMin_ = new QDoubleSpinBox(windowGroup);
    energyMax_ = new QDoubleSpinBox(windowGroup);
    for (auto* spin : {energyMin_, energyMax_}) {
        spin->setRange(-500.0, 500.0);
        spin->setDecimals(2);
        spin->setSingleStep(0.5);
        spin->setSuffix(tr(" eV"));
    }
    energyMin_->setValue(-10.0);
    energyMax_->setValue(10.0);
    auto* windowRow = new QHBoxLayout;
    windowRow->addWidget(energyMin_);
    windowRow->addWidget(energyMax_);
    windowForm->addRow(tr("Range:"), windowRow);
    // Re-bins rather than merely re-scaling, so it is emitted separately from
    // the style: the window is the one control here that costs a recompute.
    const auto emitWindow = [this] {
        Q_EMIT energyWindowChanged(energyMin_->value(), energyMax_->value());
    };
    connect(energyMin_, &QDoubleSpinBox::valueChanged, this, emitWindow);
    connect(energyMax_, &QDoubleSpinBox::valueChanged, this, emitWindow);
    outer->addWidget(windowGroup);

    // -- Fermi reference ----------------------------------------------------
    auto* fermiGroup = new QGroupBox(tr("Fermi Level"), this);
    auto* fermiForm = new QFormLayout(fermiGroup);
    auto* showFermi = new QCheckBox(tr("Draw the reference line"), fermiGroup);
    showFermi->setChecked(style_.showFermi);
    fermiForm->addRow(QString(), showFermi);
    connect(showFermi, &QCheckBox::toggled, this, [this](bool on) {
        style_.showFermi = on;
        emitStyle();
    });

    auto* fermiRow = new QHBoxLayout;
    fermiRow->addWidget(colorButton(&style_.fermiColor));
    auto* fermiPen = new QComboBox(fermiGroup);
    fermiPen->addItems({tr("Solid"), tr("Dashed"), tr("Dotted"),
                        tr("Dash-dot")});
    fermiPen->setCurrentIndex(penStyleIndex(style_.fermiPenStyle));
    fermiRow->addWidget(fermiPen, 1);
    fermiForm->addRow(tr("Colour / style:"), fermiRow);
    connect(fermiPen, &QComboBox::currentIndexChanged, this, [this](int index) {
        style_.fermiPenStyle = kPenStyles[std::max(0, std::min(index, 3))];
        emitStyle();
    });

    auto* fermiWidth = new QDoubleSpinBox(fermiGroup);
    fermiWidth->setRange(0.2, 8.0);
    fermiWidth->setSingleStep(0.2);
    fermiWidth->setDecimals(1);
    fermiWidth->setValue(style_.fermiLineWidth);
    fermiForm->addRow(tr("Line width:"), fermiWidth);
    connect(fermiWidth, &QDoubleSpinBox::valueChanged, this, [this](double v) {
        style_.fermiLineWidth = v;
        emitStyle();
    });
    outer->addWidget(fermiGroup);

    // -- Chrome -------------------------------------------------------------
    auto* chromeGroup = new QGroupBox(tr("Axes and Background"), this);
    auto* chromeForm = new QFormLayout(chromeGroup);
    chromeForm->addRow(tr("Background:"), colorButton(&style_.background));
    chromeForm->addRow(tr("Axis colour:"), colorButton(&style_.spineColor));
    chromeForm->addRow(tr("Text colour:"), colorButton(&style_.textColor));

    const auto addFontSpin = [&](const QString& label, double* target,
                                 const QString& tip) {
        auto* spin = new QDoubleSpinBox(chromeGroup);
        spin->setRange(5.0, 40.0);
        spin->setSingleStep(0.5);
        spin->setDecimals(1);
        spin->setValue(*target);
        spin->setSuffix(tr(" pt"));
        if (!tip.isEmpty())
            spin->setToolTip(tip);
        chromeForm->addRow(label, spin);
        connect(spin, &QDoubleSpinBox::valueChanged, this,
                [this, target](double v) {
                    *target = v;
                    emitStyle();
                });
    };
    addFontSpin(tr("Tick labels:"), &style_.tickPointSize, QString());
    addFontSpin(tr("Axis titles:"), &style_.axisTitlePointSize, QString());
    addFontSpin(tr("k-path labels:"), &style_.annotationPointSize,
                tr("Size of the high-symmetry point labels under the plot (Γ, "
                   "X, M …) and of the colour-scale numbers."));

    auto* spineWidth = new QDoubleSpinBox(chromeGroup);
    spineWidth->setRange(0.2, 8.0);
    spineWidth->setSingleStep(0.2);
    spineWidth->setDecimals(1);
    spineWidth->setValue(style_.spineWidth);
    chromeForm->addRow(tr("Axis width:"), spineWidth);
    connect(spineWidth, &QDoubleSpinBox::valueChanged, this, [this](double v) {
        style_.spineWidth = v;
        emitStyle();
    });
    outer->addWidget(chromeGroup);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    outer->addWidget(buttons);
}

void SpectralPlotStyleDialog::setEnergyBounds(double minimum, double maximum)
{
    // Set without emitting: this seeds the controls from the data that is
    // already loaded, and firing energyWindowChanged here would re-bin the
    // spectral function to the window it already has.
    QSignalBlocker blockMin(energyMin_);
    QSignalBlocker blockMax(energyMax_);
    energyMin_->setValue(minimum);
    energyMax_->setValue(maximum);
}

} // namespace calango::gui
