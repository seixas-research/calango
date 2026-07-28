#include "gui/EffectiveBandsWindow.hpp"

#include "gui/GuiUtils.hpp"
#include "gui/SpectralHeatmapWidget.hpp"
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
    gradientCombo->addItem(tr("Viridis"),
                           static_cast<int>(render::ColorGradient::Viridis));
    gradientCombo->addItem(tr("Plasma"),
                           static_cast<int>(render::ColorGradient::Plasma));
    gradientCombo->addItem(tr("Coolwarm"),
                           static_cast<int>(render::ColorGradient::Coolwarm));
    gradientCombo->addItem(tr("Inferno"),
                           static_cast<int>(render::ColorGradient::Inferno));
    gradientCombo->addItem(tr("Cividis"),
                           static_cast<int>(render::ColorGradient::Cividis));
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
