#include "gui/EffectiveBandsWindow.hpp"

#include "gui/GuiUtils.hpp"
#include "gui/SpectralHeatmapWidget.hpp"
#include "gui/SpectralPlotStyleDialog.hpp"
#include "render/ColorMap.hpp"

#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

namespace calango::gui {

EffectiveBandsWindow::EffectiveBandsWindow(const QString& directory,
                                           QWidget* parent)
    : QDialog(parent), plot_(new SpectralHeatmapWidget(this))
{
    setWindowTitle(tr("Effective Band Structure (Unfolding)"));
    resize(820, 640);

    hasData_ = plot_->loadFromJson(directory + QStringLiteral("/effective_bands.json"));

    auto* layout = new QVBoxLayout(this);
    layout->addWidget(plot_, 1);

    // The controls are view-side only: σ, threshold and the Fermi shift all
    // re-derive from the stored spectral weights, so none of them needs the
    // job re-run.
    auto* row = new QHBoxLayout;
    row->addWidget(new QLabel(tr("Colormap:"), this));
    auto* gradientCombo = new QComboBox(this);
    // The whole render::ColorGradient set rather than a hand-picked five. The
    // colormaps already exist and are used by the volumetric and 2D-bands
    // viewers; offering a subset here only meant this plot could not be made
    // to match a figure the rest of the app can already produce.
    //
    // Perceptually uniform maps first — they are the right default for a
    // magnitude like a spectral weight — then the rest, with the diverging
    // and rainbow maps last because they misrepresent an all-positive field.
    const struct {
        const char* label;
        render::ColorGradient gradient;
    } kGradients[] = {
        {QT_TR_NOOP("Viridis"), render::ColorGradient::Viridis},
        {QT_TR_NOOP("Plasma"), render::ColorGradient::Plasma},
        {QT_TR_NOOP("Inferno"), render::ColorGradient::Inferno},
        {QT_TR_NOOP("Magma"), render::ColorGradient::Magma},
        {QT_TR_NOOP("Cividis"), render::ColorGradient::Cividis},
        {QT_TR_NOOP("Turbo"), render::ColorGradient::Turbo},
        {QT_TR_NOOP("Greyscale"), render::ColorGradient::Greys},
        {QT_TR_NOOP("Hot"), render::ColorGradient::Hot},
        {QT_TR_NOOP("Afmhot"), render::ColorGradient::Afmhot},
        {QT_TR_NOOP("Gnuplot"), render::ColorGradient::Gnuplot},
        {QT_TR_NOOP("Spectral"), render::ColorGradient::Spectral},
        {QT_TR_NOOP("Coolwarm"), render::ColorGradient::Coolwarm},
        {QT_TR_NOOP("Rainbow"), render::ColorGradient::Rainbow},
    };
    for (const auto& entry : kGradients)
        gradientCombo->addItem(tr(entry.label),
                               static_cast<int>(entry.gradient));
    row->addWidget(gradientCombo);
    connect(gradientCombo, &QComboBox::currentIndexChanged, this,
            [this, gradientCombo](int) {
                plot_->setGradient(static_cast<render::ColorGradient>(
                    gradientCombo->currentData().toInt()));
            });

    row->addWidget(new QLabel(tr("Threshold:"), this));
    auto* thresholdSpin = new QDoubleSpinBox(this);
    thresholdSpin->setRange(0.0, 0.95);
    thresholdSpin->setDecimals(3);
    thresholdSpin->setSingleStep(0.01);
    thresholdSpin->setValue(0.02);
    thresholdSpin->setToolTip(
        tr("Intensity below this fraction of the maximum is not drawn. "
           "Unfolding always produces a low-weight haze; raising this "
           "isolates the host-like bands."));
    row->addWidget(thresholdSpin);
    connect(thresholdSpin, &QDoubleSpinBox::valueChanged, this,
            [this](double v) { plot_->setIntensityThreshold(v); });

    row->addWidget(new QLabel(tr("σ:"), this));
    auto* sigmaSpin = new QDoubleSpinBox(this);
    sigmaSpin->setRange(0.001, 2.0);
    sigmaSpin->setDecimals(3);
    sigmaSpin->setSingleStep(0.01);
    sigmaSpin->setValue(0.05);
    sigmaSpin->setSuffix(tr(" eV"));
    sigmaSpin->setToolTip(
        tr("Gaussian broadening, re-applied to the stored spectral weights — "
           "adjustable without re-running the calculation."));
    row->addWidget(sigmaSpin);
    connect(sigmaSpin, &QDoubleSpinBox::valueChanged, this,
            [this](double v) { plot_->setSigma(v); });

    // richTextCheckBox rather than a plain QCheckBox: this caption is nothing
    // BUT the symbol, so a literal "E_F" would be the whole label wrong.
    QCheckBox* shiftCheck = nullptr;
    QWidget* shiftRow =
        richTextCheckBox(tr("E − E<sub>F</sub>"), shiftCheck, this);
    shiftCheck->setChecked(true);
    shiftRow->setToolTip(tr("Plot energies relative to the Fermi level."));
    row->addWidget(shiftRow);
    connect(shiftCheck, &QCheckBox::toggled, this,
            [this](bool on) { plot_->setShiftFermiToZero(on); });

    // Everything below the frequently-turned knobs lives in the appearance
    // dialog, exactly as the band/PDOS window does it: the same button label,
    // the same live-apply wiring, and a modeless dialog so the plot can be
    // watched while it is edited.
    auto* customizeButton = new QPushButton(tr("Customize Appearance…"), this);
    connect(customizeButton, &QPushButton::clicked, this,
            [this, gradientCombo, thresholdSpin] {
                auto* dialog =
                    new SpectralPlotStyleDialog(plot_->style(), this);
                dialog->setAttribute(Qt::WA_DeleteOnClose);
                dialog->setEnergyBounds(plot_->energyMin(), plot_->energyMax());
                // The dialog holds a SNAPSHOT of the style, but the colormap
                // and threshold live on the toolbar above and can move while
                // it is open. Re-imposing them on the way through keeps the
                // toolbar authoritative for the two fields it owns; without
                // this, nudging the threshold and then touching any dialog
                // control would silently put the threshold back.
                connect(dialog, &SpectralPlotStyleDialog::styleChanged, plot_,
                        [this, gradientCombo, thresholdSpin](
                            SpectralHeatmapWidget::Style style) {
                            style.gradient =
                                static_cast<render::ColorGradient>(
                                    gradientCombo->currentData().toInt());
                            style.intensityThreshold = thresholdSpin->value();
                            plot_->setStyle(style);
                        });
                connect(dialog, &SpectralPlotStyleDialog::energyWindowChanged,
                        plot_, &SpectralHeatmapWidget::setEnergyWindow);
                dialog->show();
            });
    row->addWidget(customizeButton);

    row->addStretch(1);
    auto* exportImage = new QPushButton(tr("Export Image…"), this);
    connect(exportImage, &QPushButton::clicked, this,
            [this] { plot_->exportImage(this); });
    row->addWidget(exportImage);
    auto* exportData = new QPushButton(tr("Export Data…"), this);
    connect(exportData, &QPushButton::clicked, this,
            [this] { plot_->exportData(this); });
    row->addWidget(exportData);
    layout->addLayout(row);
}

} // namespace calango::gui
