#include "gui/PlotStyleDialog.hpp"

#include "gui/GuiUtils.hpp"

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
#include <QSpinBox>
#include <QVBoxLayout>

namespace calango::gui {

namespace {

/// Stroke styles offered for the dispersion and reference lines, in combo
/// order. Kept to the four that read clearly at plot line widths.
constexpr Qt::PenStyle kPenStyles[] = {Qt::SolidLine, Qt::DashLine,
                                       Qt::DotLine, Qt::DashDotLine};

int penStyleIndex(Qt::PenStyle style)
{
    for (int i = 0; i < 4; ++i)
        if (kPenStyles[i] == style)
            return i;
    return 0;
}

QComboBox* makePenCombo(QWidget* parent, Qt::PenStyle current)
{
    auto* combo = new QComboBox(parent);
    combo->addItems({QObject::tr("Solid"), QObject::tr("Dashed"),
                     QObject::tr("Dotted"), QObject::tr("Dash-dot")});
    combo->setCurrentIndex(penStyleIndex(current));
    return combo;
}

QDoubleSpinBox* makeSizeSpin(QWidget* parent, double value, double minimum,
                             double maximum, const QString& suffix)
{
    auto* spin = new QDoubleSpinBox(parent);
    spin->setRange(minimum, maximum);
    spin->setDecimals(1);
    spin->setSingleStep(0.5);
    spin->setValue(value);
    spin->setSuffix(suffix);
    return spin;
}

} // namespace

QPushButton* PlotStyleDialog::colorButton(QColor* target)
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

PlotStyleDialog::PlotStyleDialog(const BandPdosView::Style& style, bool phonon,
                                 QWidget* parent)
    : QDialog(parent)
    , style_(style)
    , phonon_(phonon)
{
    setWindowTitle(phonon_ ? tr("Phonon Plot Appearance")
                           : tr("Electronic Structure Viewer Appearance"));

    auto* layout = new QVBoxLayout(this);

    // --- Typography ---------------------------------------------------------
    auto* typeGroup = new QGroupBox(tr("Typography"), this);
    auto* typeForm = new QFormLayout(typeGroup);
    tickSizeSpin_ = makeSizeSpin(typeGroup, style_.tickPointSize, 5.0, 40.0,
                                 tr(" pt"));
    typeForm->addRow(tr("Tick labels:"), tickSizeSpin_);
    titleSizeSpin_ = makeSizeSpin(typeGroup, style_.axisTitlePointSize, 5.0,
                                  40.0, tr(" pt"));
    titleSizeSpin_->setToolTip(
        tr("Axis titles default to the tick-label size: a title larger than "
           "the numbers it labels reads as a mismatch."));
    typeForm->addRow(tr("Axis titles:"), titleSizeSpin_);
    annotationSizeSpin_ = makeSizeSpin(typeGroup, style_.annotationPointSize,
                                       5.0, 40.0, tr(" pt"));
    typeForm->addRow(phonon_ ? tr("Branch annotations:") : tr("Gap annotations:"),
                     annotationSizeSpin_);
    layout->addWidget(typeGroup);

    // --- Dispersion curves --------------------------------------------------
    auto* bandGroup = new QGroupBox(
        phonon_ ? tr("Dispersion branches") : tr("Band structure curves"), this);
    auto* bandForm = new QFormLayout(bandGroup);
    auto* colorRow = new QHBoxLayout;
    colorRow->addWidget(colorButton(&style_.bandColors[0]));
    colorRow->addWidget(colorButton(&style_.bandColors[1]));
    colorRow->addStretch(1);
    bandForm->addRow(phonon_ ? tr("Branch colors:") : tr("Spin ↑ / ↓ colors:"),
                     colorRow);
    bandPenCombo_ = makePenCombo(bandGroup, style_.bandPenStyle);
    bandForm->addRow(tr("Stroke style:"), bandPenCombo_);
    bandWidthSpin_ = makeSizeSpin(bandGroup, style_.bandLineWidth, 0.2, 10.0,
                                  tr(" px"));
    bandForm->addRow(tr("Thickness k:"), bandWidthSpin_);
    layout->addWidget(bandGroup);

    // --- Reference line -----------------------------------------------------
    auto* fermiGroup = new QGroupBox(
        phonon_ ? tr("ω = 0 reference line") : tr("Fermi level line"), this);
    auto* fermiForm = new QFormLayout(fermiGroup);
    showFermiCheck_ = new QCheckBox(tr("Show reference line"), fermiGroup);
    showFermiCheck_->setChecked(style_.showFermi);
    fermiForm->addRow(QString(), showFermiCheck_);
    fermiForm->addRow(tr("Color:"), colorButton(&style_.fermiColor));
    fermiPenCombo_ = makePenCombo(fermiGroup, style_.fermiPenStyle);
    fermiForm->addRow(tr("Stroke style:"), fermiPenCombo_);
    fermiWidthSpin_ = makeSizeSpin(fermiGroup, style_.fermiLineWidth, 0.2, 10.0,
                                   tr(" px"));
    fermiForm->addRow(tr("Thickness:"), fermiWidthSpin_);
    layout->addWidget(fermiGroup);

    // --- Plot chrome --------------------------------------------------------
    auto* chromeGroup = new QGroupBox(tr("Plot area"), this);
    auto* chromeForm = new QFormLayout(chromeGroup);
    chromeForm->addRow(tr("Background:"), colorButton(&style_.background));
    chromeForm->addRow(tr("Border / spines:"), colorButton(&style_.spineColor));
    spineWidthSpin_ = makeSizeSpin(chromeGroup, style_.spineWidth, 0.2, 8.0,
                                   tr(" px"));
    chromeForm->addRow(tr("Border thickness:"), spineWidthSpin_);
    chromeForm->addRow(tr("Grid / ticks:"), colorButton(&style_.gridColor));
    tickWidthSpin_ = makeSizeSpin(chromeGroup, style_.tickWidth, 0.2, 8.0,
                                  tr(" px"));
    chromeForm->addRow(tr("Tick thickness:"), tickWidthSpin_);
    chromeForm->addRow(tr("Text:"), colorButton(&style_.textColor));
    layout->addWidget(chromeGroup);

    // --- DOS ----------------------------------------------------------------
    auto* dosGroup = new QGroupBox(phonon_ ? tr("PhDOS") : tr("PDOS"), this);
    auto* dosForm = new QFormLayout(dosGroup);
    fillDosCheck_ = new QCheckBox(tr("Fill area under the curves"), dosGroup);
    fillDosCheck_->setChecked(style_.fillDos);
    dosForm->addRow(QString(), fillDosCheck_);
    fillAlphaSpin_ = new QSpinBox(dosGroup);
    fillAlphaSpin_->setRange(0, 255);
    fillAlphaSpin_->setValue(style_.dosFillAlpha);
    fillAlphaSpin_->setToolTip(tr("Opacity of the fill (0 = invisible, "
                                  "255 = opaque)."));
    dosForm->addRow(tr("Fill opacity:"), fillAlphaSpin_);
    layout->addWidget(dosGroup);

    // --- Frequency-axis bounds (phonon only) --------------------------------
    if (phonon_) {
        auto* boundsGroup = new QGroupBox(tr("Frequency axis"), this);
        auto* boundsForm = new QFormLayout(boundsGroup);
        minBoundSpin_ = new QDoubleSpinBox(boundsGroup);
        minBoundSpin_->setRange(-10000.0, 10000.0);
        minBoundSpin_->setSuffix(tr(" cm⁻¹"));
        // Imaginary (unstable) modes are reported as negative frequencies, so
        // the lower bound must be allowed below zero.
        minBoundSpin_->setToolTip(
            tr("Lower bound. Negative values show imaginary (unstable) "
               "modes, which are plotted below ω = 0."));
        maxBoundSpin_ = new QDoubleSpinBox(boundsGroup);
        maxBoundSpin_->setRange(-10000.0, 10000.0);
        maxBoundSpin_->setSuffix(tr(" cm⁻¹"));
        boundsForm->addRow(tr("Minimum:"), minBoundSpin_);
        boundsForm->addRow(tr("Maximum:"), maxBoundSpin_);
        layout->addWidget(boundsGroup);
        const auto emitBounds = [this] {
            Q_EMIT boundsChanged(minBoundSpin_->value(), maxBoundSpin_->value());
        };
        connect(minBoundSpin_, &QDoubleSpinBox::valueChanged, this, emitBounds);
        connect(maxBoundSpin_, &QDoubleSpinBox::valueChanged, this, emitBounds);
    }

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);

    // Everything applies live — the point of a styling dialog is to judge the
    // result against the real plot.
    for (QDoubleSpinBox* spin :
         {tickSizeSpin_, titleSizeSpin_, annotationSizeSpin_, bandWidthSpin_,
          fermiWidthSpin_, spineWidthSpin_, tickWidthSpin_}) {
        connect(spin, &QDoubleSpinBox::valueChanged, this,
                [this] { emitStyle(); });
    }
    for (QComboBox* combo : {bandPenCombo_, fermiPenCombo_})
        connect(combo, &QComboBox::currentIndexChanged, this,
                [this] { emitStyle(); });
    for (QCheckBox* check : {showFermiCheck_, fillDosCheck_})
        connect(check, &QCheckBox::toggled, this, [this] { emitStyle(); });
    connect(fillAlphaSpin_, &QSpinBox::valueChanged, this,
            [this] { emitStyle(); });
}

void PlotStyleDialog::setBounds(double minimum, double maximum)
{
    if (!minBoundSpin_)
        return;
    const QSignalBlocker blockMin(minBoundSpin_);
    const QSignalBlocker blockMax(maxBoundSpin_);
    minBoundSpin_->setValue(minimum);
    maxBoundSpin_->setValue(maximum);
}

void PlotStyleDialog::emitStyle()
{
    style_.tickPointSize = tickSizeSpin_->value();
    style_.axisTitlePointSize = titleSizeSpin_->value();
    style_.annotationPointSize = annotationSizeSpin_->value();
    style_.bandPenStyle = kPenStyles[bandPenCombo_->currentIndex()];
    style_.bandLineWidth = bandWidthSpin_->value();
    style_.showFermi = showFermiCheck_->isChecked();
    style_.fermiPenStyle = kPenStyles[fermiPenCombo_->currentIndex()];
    style_.fermiLineWidth = fermiWidthSpin_->value();
    style_.spineWidth = spineWidthSpin_->value();
    style_.tickWidth = tickWidthSpin_->value();
    style_.fillDos = fillDosCheck_->isChecked();
    style_.dosFillAlpha = fillAlphaSpin_->value();
    // Colors are written straight into style_ by colorButton().
    Q_EMIT styleChanged(style_);
}

} // namespace calango::gui
