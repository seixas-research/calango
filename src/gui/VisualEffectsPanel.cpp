#include "gui/VisualEffectsPanel.hpp"

#include "gui/LightingPanel.hpp"
#include "gui/ViewportWidget.hpp"

#include <QColorDialog>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QPushButton>
#include <QSlider>
#include <QSpinBox>
#include <QLabel>
#include <QSignalBlocker>
#include <QTabBar>
#include <QTabWidget>
#include <QVBoxLayout>

#include <algorithm>

namespace calango::gui {

VisualEffectsPanel::VisualEffectsPanel(ViewportWidget* viewport, QWidget* parent)
    : QWidget(parent), viewport_(viewport)
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    auto* tabs = new QTabWidget(this);
    // All four headers must stay fully visible: disable the scroll buttons and
    // label elision so the tab bar always reports the width it truly needs,
    // then feed that back into the panel's minimum width below.
    tabs->setUsesScrollButtons(false);
    tabs->setElideMode(Qt::ElideNone);
    tabs->tabBar()->setExpanding(false);
    // Logical sequence: Lighting → Shadow → Fog → Blur → Occlusion. Blur and
    // Occlusion are separate pages rather than one: they are independent
    // effects with independent controls, and sharing a page made the SSAO
    // parameters read as if they modified the depth-of-field blur.
    tabs->addTab(new LightingPanel(viewport_, tabs), tr("Lighting"));
    tabs->addTab(buildShadowTab(), tr("Shadow"));
    tabs->addTab(buildFogTab(), tr("Fog"));
    tabs->addTab(buildDepthBlurTab(), tr("Blur"));
    tabs->addTab(buildOcclusionTab(), tr("Occlusion"));
    layout->addWidget(tabs);

    // Zone-9 dock width constraint: size the panel so the tab bar shows every
    // header without horizontal scrolling or clipped text. The tab bar's size
    // hint sums the four labels for the current font/style; the margin covers
    // the tab-widget frame and the dock's own borders. Computed dynamically so
    // it tracks font/DPI/translation changes rather than a hard-coded number.
    const int headerWidth = tabs->tabBar()->sizeHint().width();
    setMinimumWidth(headerWidth + 24);
}

QWidget* VisualEffectsPanel::buildShadowTab()
{
    auto* page = new QWidget(this);
    auto* pageLayout = new QVBoxLayout(page);
    auto& style = viewport_->style();

    auto* group = new QGroupBox(tr("Directional shadows"), page);
    group->setCheckable(true);
    group->setChecked(style.shadowsEnabled);
    group->setToolTip(
        tr("Real-time shadow mapping with percentage-closer filtering: atoms "
           "and bonds cast shadows onto neighbouring geometry. Adds a "
           "depth-only pass per frame, so it costs roughly one extra draw of "
           "the scene."));
    auto* form = new QFormLayout(group);

    auto* intensitySpin = new QDoubleSpinBox(group);
    intensitySpin->setRange(0.0, 1.0);
    intensitySpin->setDecimals(2);
    intensitySpin->setSingleStep(0.05);
    intensitySpin->setValue(style.shadowStrength);
    auto* intensitySlider = new QSlider(Qt::Horizontal, group);
    intensitySlider->setRange(0, 100);
    intensitySlider->setValue(static_cast<int>(style.shadowStrength * 100.0f));
    intensitySpin->setToolTip(
        tr("How much direct light an occluded surface loses. Ambient light is "
           "never attenuated, so even at 1.0 shadowed geometry stays readable "
           "rather than going black."));
    auto* intensityRow = new QWidget(group);
    auto* intensityLayout = new QHBoxLayout(intensityRow);
    intensityLayout->setContentsMargins(0, 0, 0, 0);
    intensityLayout->addWidget(intensitySlider, 1);
    intensityLayout->addWidget(intensitySpin);
    form->addRow(tr("Intensity:"), intensityRow);

    auto* softnessSpin = new QSpinBox(group);
    softnessSpin->setRange(0, 6);
    softnessSpin->setValue(style.shadowSoftness);
    softnessSpin->setToolTip(
        tr("PCF blur radius in shadow-map texels.\n"
           "0 gives hard, aliased edges; each step averages a wider "
           "neighbourhood — (2r+1)² samples per fragment — so quality and "
           "cost both rise with it. 2–3 suits most structures."));
    form->addRow(tr("Softness / blur radius:"), softnessSpin);

    auto* bindingNote = new QLabel(
        tr("The shadow projection follows the <b>primary light</b> (the first "
           "entry under the Lighting tab). Re-aiming that light re-aims the "
           "shadows; fill lights stay unshadowed, which is what keeps "
           "occluded regions legible."),
        group);
    bindingNote->setWordWrap(true);
    bindingNote->setTextFormat(Qt::RichText);
    form->addRow(bindingNote);

    pageLayout->addWidget(group);
    pageLayout->addStretch(1);

    // Shadows change only shading uniforms and the depth pass — no instance
    // buffers to rebuild, so every control is a plain repaint.
    connect(group, &QGroupBox::toggled, this, [this](bool on) {
        viewport_->style().shadowsEnabled = on;
        viewport_->styleChanged(false);
    });
    connect(intensitySlider, &QSlider::valueChanged, this, [this, intensitySpin](int v) {
        const QSignalBlocker blocker(intensitySpin);
        intensitySpin->setValue(v / 100.0);
        viewport_->style().shadowStrength = static_cast<float>(v) / 100.0f;
        viewport_->styleChanged(false);
    });
    connect(intensitySpin, &QDoubleSpinBox::valueChanged, this,
            [this, intensitySlider](double value) {
                const QSignalBlocker blocker(intensitySlider);
                intensitySlider->setValue(static_cast<int>(value * 100.0));
                viewport_->style().shadowStrength = static_cast<float>(value);
                viewport_->styleChanged(false);
            });
    connect(softnessSpin, &QSpinBox::valueChanged, this, [this](int radius) {
        viewport_->style().shadowSoftness = radius;
        viewport_->styleChanged(false);
    });
    return page;
}

QWidget* VisualEffectsPanel::buildFogTab()
{
    auto* page = new QWidget(this);
    auto* pageLayout = new QVBoxLayout(page);
    auto& style = viewport_->style();

    auto* group = new QGroupBox(tr("Fog"), page);
    group->setCheckable(true);
    group->setChecked(style.fogMode != 0);
    auto* form = new QFormLayout(group);

    auto* modeCombo = new QComboBox(group);
    modeCombo->addItems({tr("Linear (start → end)"), tr("Exponential (density)")});
    modeCombo->setCurrentIndex(style.fogMode == 2 ? 1 : 0);
    form->addRow(tr("Mode:"), modeCombo);

    // Fog color swatch (fades geometry into this color; changing the viewport
    // background re-syncs it).
    auto* colorButton = new QPushButton(group);
    colorButton->setAutoFillBackground(true);
    const auto paintSwatch = [colorButton](const QColor& c) {
        colorButton->setStyleSheet(
            QStringLiteral("background-color: %1; min-height: 18px;").arg(c.name()));
    };
    paintSwatch(style.fogColor);
    form->addRow(tr("Color:"), colorButton);

    auto* startSpin = new QDoubleSpinBox(group);
    startSpin->setRange(0.0, 500.0);
    startSpin->setSuffix(QStringLiteral(" Å"));
    startSpin->setValue(style.fogStart);
    form->addRow(tr("Start distance:"), startSpin);

    auto* endSpin = new QDoubleSpinBox(group);
    endSpin->setRange(1.0, 1000.0);
    endSpin->setSuffix(QStringLiteral(" Å"));
    endSpin->setValue(style.fogEnd);
    form->addRow(tr("End distance:"), endSpin);

    auto* densitySpin = new QDoubleSpinBox(group);
    densitySpin->setRange(0.001, 0.5);
    densitySpin->setDecimals(3);
    densitySpin->setSingleStep(0.005);
    densitySpin->setValue(style.fogDensity);
    form->addRow(tr("Density:"), densitySpin);

    pageLayout->addWidget(group);
    pageLayout->addStretch(1);

    const auto applyFog = [this, group, modeCombo, startSpin, endSpin,
                           densitySpin] {
        auto& s = viewport_->style();
        s.fogMode = !group->isChecked() ? 0 : modeCombo->currentIndex() == 0 ? 1 : 2;
        s.fogStart = static_cast<float>(startSpin->value());
        s.fogEnd = static_cast<float>(
            std::max(endSpin->value(), startSpin->value() + 1.0));
        s.fogDensity = static_cast<float>(densitySpin->value());
        viewport_->update();
    };
    connect(group, &QGroupBox::toggled, this, applyFog);
    connect(modeCombo, &QComboBox::currentIndexChanged, this, applyFog);
    connect(startSpin, &QDoubleSpinBox::valueChanged, this, applyFog);
    connect(endSpin, &QDoubleSpinBox::valueChanged, this, applyFog);
    connect(densitySpin, &QDoubleSpinBox::valueChanged, this, applyFog);
    connect(colorButton, &QPushButton::clicked, this, [this, paintSwatch] {
        const QColor picked = QColorDialog::getColor(
            viewport_->style().fogColor, this, tr("Fog Color"));
        if (picked.isValid()) {
            viewport_->style().fogColor = picked;
            paintSwatch(picked);
            viewport_->update();
        }
    });
    return page;
}

QWidget* VisualEffectsPanel::buildDepthBlurTab()
{
    auto* page = new QWidget(this);
    auto* pageLayout = new QVBoxLayout(page);

    auto* group = new QGroupBox(tr("Depth of field"), page);
    group->setCheckable(true);
    group->setChecked(viewport_->depthOfField().enabled);
    group->setToolTip(tr("Blurs geometry away from the focal plane.\n"
                         "While enabled the scene renders without MSAA."));
    auto* form = new QFormLayout(group);

    auto* strengthSlider = new QSlider(Qt::Horizontal, group);
    strengthSlider->setRange(1, 20);
    strengthSlider->setValue(static_cast<int>(viewport_->depthOfField().strength));
    form->addRow(tr("Blur strength:"), strengthSlider);

    auto* rangeSpin = new QDoubleSpinBox(group);
    rangeSpin->setRange(1.0, 200.0);
    rangeSpin->setSuffix(QStringLiteral(" Å"));
    rangeSpin->setValue(viewport_->depthOfField().focusRange);
    rangeSpin->setToolTip(tr("Depth band around the focal plane that stays sharp"));
    form->addRow(tr("Focus range:"), rangeSpin);

    auto* offsetSpin = new QDoubleSpinBox(group);
    offsetSpin->setRange(-200.0, 200.0);
    offsetSpin->setSuffix(QStringLiteral(" Å"));
    offsetSpin->setValue(viewport_->depthOfField().focusOffset);
    offsetSpin->setToolTip(tr("Shift of the focal plane relative to the camera target"));
    form->addRow(tr("Focus offset:"), offsetSpin);

    pageLayout->addWidget(group);

    pageLayout->addStretch(1);

    const auto applyDof = [this, group, strengthSlider, rangeSpin, offsetSpin] {
        auto& dof = viewport_->depthOfField();
        dof.enabled = group->isChecked();
        dof.strength = static_cast<float>(strengthSlider->value());
        dof.focusRange = static_cast<float>(rangeSpin->value());
        dof.focusOffset = static_cast<float>(offsetSpin->value());
        viewport_->update();
    };
    connect(group, &QGroupBox::toggled, this, applyDof);
    connect(strengthSlider, &QSlider::valueChanged, this, applyDof);
    connect(rangeSpin, &QDoubleSpinBox::valueChanged, this, applyDof);
    connect(offsetSpin, &QDoubleSpinBox::valueChanged, this, applyDof);
    return page;
}

QWidget* VisualEffectsPanel::buildOcclusionTab()
{
    auto* page = new QWidget(this);
    auto* pageLayout = new QVBoxLayout(page);

    auto* ssaoGroup = new QGroupBox(tr("Screen-space ambient occlusion"), page);
    ssaoGroup->setCheckable(true);
    ssaoGroup->setChecked(viewport_->ambientOcclusion().enabled);
    ssaoGroup->setToolTip(
        tr("Darkens contact regions — the creases between touching atoms and "
           "the gaps under bonds — which direct lighting alone leaves fully "
           "lit.\nWhile enabled the scene renders without MSAA."));
    auto* ssaoForm = new QFormLayout(ssaoGroup);

    auto* radiusSpin = new QDoubleSpinBox(ssaoGroup);
    radiusSpin->setRange(0.1, 10.0);
    radiusSpin->setDecimals(2);
    radiusSpin->setSingleStep(0.1);
    radiusSpin->setSuffix(QStringLiteral(" Å"));
    radiusSpin->setValue(viewport_->ambientOcclusion().radius);
    radiusSpin->setToolTip(
        tr("How far a neighbouring surface can be and still shade this one. "
           "Around one atomic radius reads best: much larger and the whole "
           "structure dims uniformly, much smaller and only the tightest "
           "creases darken."));
    ssaoForm->addRow(tr("SSAO radius:"), radiusSpin);

    auto* intensitySlider = new QSlider(Qt::Horizontal, ssaoGroup);
    intensitySlider->setRange(0, 100);
    intensitySlider->setValue(static_cast<int>(
        viewport_->ambientOcclusion().intensity * 100.0f));
    intensitySlider->setToolTip(
        tr("Strength of the darkening: 0 leaves the image untouched, 100 "
           "applies the full occlusion factor."));
    ssaoForm->addRow(tr("SSAO intensity:"), intensitySlider);



    auto* samplesSpin = new QSpinBox(ssaoGroup);
    samplesSpin->setRange(4, ViewportWidget::kMaxSsaoSamples);
    samplesSpin->setValue(viewport_->ambientOcclusion().samples);
    samplesSpin->setToolTip(
        tr("Hemisphere samples per pixel. More samples cost frame time and buy "
           "less noise; the blur pass cleans up most of it, so raising this "
           "mainly helps at large radii where the samples spread thin."));
    ssaoForm->addRow(tr("Kernel samples:"), samplesSpin);

    auto* noiseSpin = new QDoubleSpinBox(ssaoGroup);
    noiseSpin->setRange(0.25, 4.0);
    noiseSpin->setDecimals(2);
    noiseSpin->setSingleStep(0.25);
    noiseSpin->setValue(viewport_->ambientOcclusion().noiseScale);
    noiseSpin->setToolTip(
        tr("Scale of the tiled rotation-noise lookup. 1.0 tiles the 4x4 "
           "texture pixel-for-pixel, which is what the blur radius is matched "
           "to; larger values rotate over a coarser grid and leave banding the "
           "blur can no longer fully remove."));
    ssaoForm->addRow(tr("Noise texture scale:"), noiseSpin);

    pageLayout->addWidget(ssaoGroup);
    pageLayout->addStretch(1);

    const auto applySsao = [this, ssaoGroup, radiusSpin, intensitySlider,
                            samplesSpin, noiseSpin] {
        auto& ssao = viewport_->ambientOcclusion();
        ssao.enabled = ssaoGroup->isChecked();
        ssao.radius = static_cast<float>(radiusSpin->value());
        ssao.intensity = static_cast<float>(intensitySlider->value()) / 100.0f;
        ssao.samples = samplesSpin->value();
        ssao.noiseScale = static_cast<float>(noiseSpin->value());
        viewport_->update();
    };
    connect(ssaoGroup, &QGroupBox::toggled, this, applySsao);
    connect(radiusSpin, &QDoubleSpinBox::valueChanged, this, applySsao);
    connect(intensitySlider, &QSlider::valueChanged, this, applySsao);
    connect(samplesSpin, QOverload<int>::of(&QSpinBox::valueChanged), this,
            applySsao);
    connect(noiseSpin, &QDoubleSpinBox::valueChanged, this, applySsao);
    return page;
}


} // namespace calango::gui
