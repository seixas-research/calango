#include "gui/EditVolumetricRenderDialog.hpp"

#include <QColorDialog>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QCheckBox>
#include <QComboBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QSlider>
#include <QTabWidget>
#include <QVBoxLayout>

#include <algorithm>

namespace calango::gui {

namespace {
constexpr int kSliderSteps = 1000;

QComboBox* gradientCombo(QWidget* parent, render::ColorGradient current)
{
    auto* combo = new QComboBox(parent);
    combo->addItems(volumetricGradientNames());
    const int i = volumetricGradients().indexOf(current);
    combo->setCurrentIndex(i >= 0 ? i : 0);
    return combo;
}
} // namespace

EditVolumetricRenderDialog::EditVolumetricRenderDialog(
    const VolumetricStyle& style, VolumetricRenderMode mode, double fieldMin,
    double fieldMax, QWidget* parent)
    : QDialog(parent)
    , style_(style)
    , fieldMin_(fieldMin)
    , fieldMax_(std::max(fieldMax, fieldMin + 1e-30))
{
    setWindowTitle(tr("Edit Volumetric Render"));
    resize(420, 460);

    auto* layout = new QVBoxLayout(this);
    tabs_ = new QTabWidget(this);
    tabs_->addTab(buildIsosurfaceTab(), tr("Isosurfaces"));
    tabs_->addTab(buildColorSliceTab(), tr("Color Slice"));
    tabs_->addTab(buildPotentialTab(), tr("Potential Map"));
    tabs_->setCurrentIndex(static_cast<int>(mode));
    layout->addWidget(tabs_);
    // Switching tab switches the active render mode.
    connect(tabs_, &QTabWidget::currentChanged, this,
            [this] { emitChange(); });

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);
}

VolumetricRenderMode EditVolumetricRenderDialog::mode() const
{
    return static_cast<VolumetricRenderMode>(tabs_->currentIndex());
}

void EditVolumetricRenderDialog::emitChange()
{
    if (!updating_)
        Q_EMIT styleChanged(style_, mode());
}

QWidget* EditVolumetricRenderDialog::buildIsosurfaceTab()
{
    auto* page = new QWidget(this);
    auto* form = new QFormLayout(page);

    isoGradientCombo_ = gradientCombo(page, style_.gradient);
    form->addRow(tr("Colormap:"), isoGradientCombo_);
    connect(isoGradientCombo_, &QComboBox::currentIndexChanged, this,
            [this](int i) {
                const auto& g = volumetricGradients();
                if (i >= 0 && i < g.size())
                    style_.gradient = g.at(i);
                emitChange();
            });

    auto* isoRow = new QWidget(page);
    auto* isoRowLayout = new QHBoxLayout(isoRow);
    isoRowLayout->setContentsMargins(0, 0, 0, 0);
    isoSlider_ = new QSlider(Qt::Horizontal, isoRow);
    isoSlider_->setRange(0, kSliderSteps);
    isoSpin_ = new QDoubleSpinBox(isoRow);
    isoSpin_->setDecimals(5);
    isoSpin_->setRange(fieldMin_, fieldMax_);
    isoSpin_->setValue(std::clamp(style_.isovalue, fieldMin_, fieldMax_));
    isoRowLayout->addWidget(isoSlider_, 1);
    isoRowLayout->addWidget(isoSpin_);
    form->addRow(tr("Isovalue:"), isoRow);
    syncIsoSlider();
    connect(isoSlider_, &QSlider::valueChanged, this, [this] {
        if (updating_)
            return;
        style_.isovalue = isovalueFromSlider();
        const QSignalBlocker b(isoSpin_);
        isoSpin_->setValue(style_.isovalue);
        emitChange();
    });
    connect(isoSpin_, &QDoubleSpinBox::valueChanged, this, [this](double v) {
        if (updating_)
            return;
        style_.isovalue = v;
        syncIsoSlider();
        emitChange();
    });

    isoOpacitySpin_ = new QDoubleSpinBox(page);
    isoOpacitySpin_->setRange(0.0, 1.0);
    isoOpacitySpin_->setSingleStep(0.05);
    isoOpacitySpin_->setValue(style_.isoOpacity);
    isoOpacitySpin_->setToolTip(tr("Surface opacity / alpha blending."));
    form->addRow(tr("Opacity:"), isoOpacitySpin_);
    connect(isoOpacitySpin_, &QDoubleSpinBox::valueChanged, this,
            [this](double v) {
                style_.isoOpacity = v;
                emitChange();
            });

    specularSpin_ = new QDoubleSpinBox(page);
    specularSpin_->setRange(0.0, 1.0);
    specularSpin_->setSingleStep(0.05);
    specularSpin_->setValue(style_.specular);
    specularSpin_->setToolTip(
        tr("Specular material finish (lit volume viewers only)."));
    form->addRow(tr("Specular finish:"), specularSpin_);
    connect(specularSpin_, &QDoubleSpinBox::valueChanged, this,
            [this](double v) {
                style_.specular = v;
                emitChange();
            });

    posColorButton_ = new QPushButton(page);
    updateColorButton(posColorButton_, style_.positiveColor);
    form->addRow(tr("Positive phase color:"), posColorButton_);
    connect(posColorButton_, &QPushButton::clicked, this, [this] {
        const QColor c = QColorDialog::getColor(style_.positiveColor, this,
                                                tr("Positive Phase Color"));
        if (c.isValid()) {
            style_.positiveColor = c;
            updateColorButton(posColorButton_, c);
            emitChange();
        }
    });

    negColorButton_ = new QPushButton(page);
    updateColorButton(negColorButton_, style_.negativeColor);
    negColorButton_->setToolTip(
        tr("Color of the negative-phase lobe for signed fields (e.g. Wannier "
           "orbitals ψ<0)."));
    form->addRow(tr("Negative phase color:"), negColorButton_);
    connect(negColorButton_, &QPushButton::clicked, this, [this] {
        const QColor c = QColorDialog::getColor(style_.negativeColor, this,
                                                tr("Negative Phase Color"));
        if (c.isValid()) {
            style_.negativeColor = c;
            updateColorButton(negColorButton_, c);
            emitChange();
        }
    });

    return page;
}

QWidget* EditVolumetricRenderDialog::buildColorSliceTab()
{
    auto* page = new QWidget(this);
    auto* form = new QFormLayout(page);

    planeCombo_ = new QComboBox(page);
    planeCombo_->addItems({QStringLiteral("XY (hkl 001)"),
                           QStringLiteral("XZ (hkl 010)"),
                           QStringLiteral("YZ (hkl 100)")});
    planeCombo_->setCurrentIndex(std::clamp(style_.slicePlane, 0, 2));
    form->addRow(tr("Plane orientation:"), planeCombo_);
    connect(planeCombo_, &QComboBox::currentIndexChanged, this, [this](int i) {
        style_.slicePlane = i;
        emitChange();
    });

    sliceOffsetSlider_ = new QSlider(Qt::Horizontal, page);
    sliceOffsetSlider_->setRange(0, kSliderSteps);
    sliceOffsetSlider_->setValue(
        static_cast<int>(std::clamp(style_.sliceOffset, 0.0, 1.0) * kSliderSteps));
    form->addRow(tr("Offset:"), sliceOffsetSlider_);
    connect(sliceOffsetSlider_, &QSlider::valueChanged, this, [this](int v) {
        style_.sliceOffset = static_cast<double>(v) / kSliderSteps;
        emitChange();
    });

    sliceGradientCombo_ = gradientCombo(page, style_.gradient);
    form->addRow(tr("Colormap:"), sliceGradientCombo_);
    connect(sliceGradientCombo_, &QComboBox::currentIndexChanged, this,
            [this](int i) {
                const auto& g = volumetricGradients();
                if (i >= 0 && i < g.size())
                    style_.gradient = g.at(i);
                emitChange();
            });

    sliceOpacitySpin_ = new QDoubleSpinBox(page);
    sliceOpacitySpin_->setRange(0.0, 1.0);
    sliceOpacitySpin_->setSingleStep(0.05);
    sliceOpacitySpin_->setValue(style_.sliceOpacity);
    form->addRow(tr("Transparency:"), sliceOpacitySpin_);
    connect(sliceOpacitySpin_, &QDoubleSpinBox::valueChanged, this,
            [this](double v) {
                style_.sliceOpacity = v;
                emitChange();
            });

    return page;
}

QWidget* EditVolumetricRenderDialog::buildPotentialTab()
{
    auto* page = new QWidget(this);
    auto* form = new QFormLayout(page);

    auto* intro = new QLabel(
        tr("Project an electrostatic / work-function potential as a "
           "color-mapped plane, with an explicit color-ramp window and a 1D "
           "planar-average profile V̄ along the chosen axis."),
        page);
    intro->setWordWrap(true);
    form->addRow(intro);

    potentialGradientCombo_ = gradientCombo(page, style_.gradient);
    form->addRow(tr("Color ramp:"), potentialGradientCombo_);
    connect(potentialGradientCombo_, &QComboBox::currentIndexChanged, this,
            [this](int i) {
                const auto& g = volumetricGradients();
                if (i >= 0 && i < g.size())
                    style_.gradient = g.at(i);
                emitChange();
            });

    potentialBoundsCheck_ =
        new QCheckBox(tr("Use explicit ramp bounds"), page);
    potentialBoundsCheck_->setChecked(style_.potentialUseBounds);
    form->addRow(tr("Ramp bounds:"), potentialBoundsCheck_);

    potentialMinSpin_ = new QDoubleSpinBox(page);
    potentialMinSpin_->setDecimals(4);
    potentialMinSpin_->setRange(-1e6, 1e6);
    potentialMinSpin_->setValue(style_.potentialMin);
    potentialMinSpin_->setSuffix(tr(" eV"));
    form->addRow(tr("Ramp min:"), potentialMinSpin_);
    potentialMaxSpin_ = new QDoubleSpinBox(page);
    potentialMaxSpin_->setDecimals(4);
    potentialMaxSpin_->setRange(-1e6, 1e6);
    potentialMaxSpin_->setValue(style_.potentialMax);
    potentialMaxSpin_->setSuffix(tr(" eV"));
    form->addRow(tr("Ramp max:"), potentialMaxSpin_);

    potentialAxisCombo_ = new QComboBox(page);
    potentialAxisCombo_->addItems(
        {QStringLiteral("x"), QStringLiteral("y"), QStringLiteral("z")});
    potentialAxisCombo_->setCurrentIndex(std::clamp(style_.potentialAxis, 0, 2));
    potentialAxisCombo_->setToolTip(
        tr("Axis the 1D planar-average profile V̄ is taken along."));
    form->addRow(tr("Planar-average axis:"), potentialAxisCombo_);

    const auto syncEnabled = [this] {
        const bool on = potentialBoundsCheck_->isChecked();
        potentialMinSpin_->setEnabled(on);
        potentialMaxSpin_->setEnabled(on);
    };
    syncEnabled();

    connect(potentialBoundsCheck_, &QCheckBox::toggled, this,
            [this, syncEnabled](bool on) {
                style_.potentialUseBounds = on;
                syncEnabled();
                emitChange();
            });
    connect(potentialMinSpin_, &QDoubleSpinBox::valueChanged, this,
            [this](double v) {
                style_.potentialMin = v;
                emitChange();
            });
    connect(potentialMaxSpin_, &QDoubleSpinBox::valueChanged, this,
            [this](double v) {
                style_.potentialMax = v;
                emitChange();
            });
    connect(potentialAxisCombo_, &QComboBox::currentIndexChanged, this,
            [this](int i) {
                style_.potentialAxis = i;
                emitChange();
            });

    return page;
}

void EditVolumetricRenderDialog::setFieldRange(double fieldMin, double fieldMax)
{
    fieldMin_ = fieldMin;
    fieldMax_ = std::max(fieldMax, fieldMin + 1e-30);
    updating_ = true;
    style_.isovalue = std::clamp(style_.isovalue, fieldMin_, fieldMax_);
    isoSpin_->setRange(fieldMin_, fieldMax_);
    isoSpin_->setValue(style_.isovalue);
    syncIsoSlider();
    if (!style_.potentialUseBounds) {
        style_.potentialMin = fieldMin_;
        style_.potentialMax = fieldMax_;
        potentialMinSpin_->setValue(fieldMin_);
        potentialMaxSpin_->setValue(fieldMax_);
    }
    updating_ = false;
}

double EditVolumetricRenderDialog::isovalueFromSlider() const
{
    const double t = static_cast<double>(isoSlider_->value()) / kSliderSteps;
    return fieldMin_ + t * (fieldMax_ - fieldMin_);
}

void EditVolumetricRenderDialog::syncIsoSlider()
{
    const double span = fieldMax_ - fieldMin_;
    const double t = span > 0.0 ? (style_.isovalue - fieldMin_) / span : 0.0;
    const QSignalBlocker b(isoSlider_);
    isoSlider_->setValue(static_cast<int>(std::clamp(t, 0.0, 1.0) * kSliderSteps));
}

void EditVolumetricRenderDialog::updateColorButton(QPushButton* button,
                                                   const QColor& color)
{
    button->setText(color.name(QColor::HexRgb).toUpper());
    button->setStyleSheet(
        QStringLiteral("QPushButton { background-color: %1; color: %2; }")
            .arg(color.name(),
                 color.lightnessF() > 0.5 ? QStringLiteral("#000")
                                          : QStringLiteral("#fff")));
}

} // namespace calango::gui
