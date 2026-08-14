#include "gui/VisualEffectsPanel.hpp"

#include "gui/GuiUtils.hpp"
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
#include <utility>

namespace calango::gui {

VisualEffectsPanel::VisualEffectsPanel(ViewportWidget* viewport, QWidget* parent)
    : QWidget(parent), viewport_(viewport)
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    auto* tabs = new QTabWidget(this);
    // Every header must stay fully visible: disable the scroll buttons and
    // label elision so the tab bar always reports the width it truly needs,
    // then feed that back into the panel's minimum width below.
    tabs->setUsesScrollButtons(false);
    tabs->setElideMode(Qt::ElideNone);
    tabs->tabBar()->setExpanding(false);
    // Logical sequence: Light → Shadow → Floor → Fog → Blur → SSAO. It runs
    // from what lights the scene, through the scene itself, to the passes that
    // post-process the finished image.
    //
    // Floor sits directly after Shadow because that is the relationship it has
    // with it: the plane exists to RECEIVE the shadow the structure already
    // casts, and the two are almost always reached together. It stays in the
    // first half for the same reason — a ground plane is scene furniture, not
    // an image filter, and grouping it with Blur and SSAO would say otherwise.
    //
    // Blur and SSAO are separate pages rather than one: they are independent
    // effects with independent controls, and sharing a page made the SSAO
    // parameters read as if they modified the depth-of-field blur.
    tabs->addTab(new LightingPanel(viewport_, tabs), tr("Light"));
    tabs->addTab(buildShadowTab(), tr("Shadow"));
    tabs->addTab(buildFloorTab(), tr("Floor"));
    tabs->addTab(buildFogTab(), tr("Fog"));
    tabs->addTab(buildDepthBlurTab(), tr("Blur"));
    tabs->addTab(buildOcclusionTab(), tr("SSAO"));
    layout->addWidget(tabs);

    // Zone-9 dock width constraint: size the panel so the tab bar shows every
    // header without horizontal scrolling or clipped text. The tab bar's size
    // hint sums the labels for the current font/style; the margin covers
    // the tab-widget frame and the dock's own borders. Computed dynamically so
    // it tracks font/DPI/translation changes rather than a hard-coded number —
    // which is what lets a header be added or renamed without re-measuring
    // anything by hand.
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
           "and bonds cast shadows onto neighboring geometry. Adds a "
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
           "neighborhood — (2r+1)² samples per fragment — so quality and "
           "cost both rise with it. 2–3 suits most structures."));
    form->addRow(tr("Softness / blur radius:"), softnessSpin);

    auto* bindingNote = new QLabel(
        tr("The shadow projection follows the <b>primary light</b> (the first "
           "entry under the Light tab). Re-aiming that light re-aims the "
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

QWidget* VisualEffectsPanel::buildFloorTab()
{
    auto* page = new QWidget(this);
    auto* pageLayout = new QVBoxLayout(page);
    auto& style = viewport_->style();

    auto* floorGroup = new QGroupBox(tr("Ground plane (floor)"), page);
    floorGroup->setCheckable(true);
    floorGroup->setChecked(style.floorEnabled);
    floorGroup->setToolTip(
        tr("A large plane just under the structure, so an isolated molecule "
           "reads as an object resting in a space rather than one floating in "
           "a void. It RECEIVES the shadows from the Shadow tab — the atoms "
           "and bonds cast onto it — and casts none of its own.\n\n"
           "Display only: it is never part of the structure, is not picked by "
           "clicks, and never appears in an exported POSCAR, CIF or XYZ."));
    auto* floorForm = new QFormLayout(floorGroup);

    auto* heightSpin = new QDoubleSpinBox(floorGroup);
    heightSpin->setRange(-100.0, 100.0);
    heightSpin->setDecimals(2);
    heightSpin->setSingleStep(0.25);
    heightSpin->setSuffix(QStringLiteral(" Å"));
    heightSpin->setValue(style.floorOffset);
    heightSpin->setToolTip(
        tr("Raise (+) or lower (−) the plane relative to its automatic "
           "position, which is just under the lowest drawn point — the bottom "
           "of the lowest atom's sphere, or of the unit cell when that is "
           "shown.\n\n"
           "An offset rather than an absolute height, so that the plane keeps "
           "following the structure as it is edited or a trajectory is "
           "scrubbed while your adjustment survives. 0 leaves it exactly on "
           "the automatic level."));
    floorForm->addRow(tr("Height offset:"), heightSpin);

    auto* colorButton = new QPushButton(floorGroup);
    setButtonColor(colorButton, style.floorColor);
    colorButton->setToolTip(
        tr("Plane colour. White by default: a figure's page is white, so a "
           "white floor disappears into it and leaves only what the plane was "
           "added for — the shadow. Shadows stay legible on it because the "
           "shadow term attenuates direct light only, never ambient."));
    floorForm->addRow(tr("Color:"), colorButton);

    auto* finishCombo = new QComboBox(floorGroup);
    // Order matches render::SurfaceFinish, like the Representation panel's.
    finishCombo->addItems({tr("Standard"), tr("Shiny"), tr("Matte"),
                           tr("Glassy")});
    finishCombo->setCurrentIndex(static_cast<int>(style.floorFinish));
    finishCombo->setToolTip(
        tr("Matte (the default): purely diffuse. A specular highlight sliding "
           "across a large flat plane reads as an artifact in a still figure, "
           "which is what this is usually for.\n"
           "Shiny: a polished surface with a crisp highlight.\n"
           "Glassy: translucent with a Fresnel rim.\n\n"
           "These are Blinn-Phong materials, the same four the Representation "
           "panel offers — none of them mirrors the structure, which would "
           "need a reflection pass the viewport does not have."));
    floorForm->addRow(tr("Material:"), finishCombo);

    auto* opacitySpin = new QDoubleSpinBox(floorGroup);
    opacitySpin->setRange(0.05, 1.0);
    opacitySpin->setDecimals(2);
    opacitySpin->setSingleStep(0.05);
    opacitySpin->setValue(style.floorOpacity);
    opacitySpin->setToolTip(
        tr("How solid the plane is. It always fades out toward its edges "
           "whatever this says, so it reads as ground rather than as a tile "
           "the structure is standing on."));
    floorForm->addRow(tr("Opacity:"), opacitySpin);

    // --- Orientation -------------------------------------------------------
    // Two views of ONE value. The plane is stored as a normal vector and
    // nothing else; the dropdown is a read-out of that vector, not a second
    // setting. That is what keeps them from disagreeing — there is no second
    // state to fall out of step, and a project file carries three numbers
    // rather than a preset that a later release might renumber.
    auto* orientationCombo = new QComboBox(floorGroup);
    orientationCombo->addItems({tr("xy (normal z)"), tr("xz (normal y)"),
                                tr("yz (normal x)"), tr("Custom")});
    orientationCombo->setToolTip(
        tr("Which plane the floor lies in. Each preset names the two axes the "
           "plane is spanned by; its normal is the remaining one.\n\n"
           "xy is the default and the horizontal case — the plane perpendicular "
           "to c, which is the axis Calango's default view has pointing up.\n\n"
           "Picking a preset fills the normal below. Typing a normal that is "
           "not an axis selects Custom."));
    floorForm->addRow(tr("Plane:"), orientationCombo);

    const auto makeNormalSpin = [&](float value) {
        auto* spin = new QDoubleSpinBox(floorGroup);
        spin->setRange(-1000.0, 1000.0);
        spin->setDecimals(3);
        spin->setSingleStep(0.1);
        spin->setValue(static_cast<double>(value));
        spin->setMinimumWidth(70);
        return spin;
    };
    auto* nxSpin = makeNormalSpin(style.floorNormal.x());
    auto* nySpin = makeNormalSpin(style.floorNormal.y());
    auto* nzSpin = makeNormalSpin(style.floorNormal.z());
    auto* normalRow = new QWidget(floorGroup);
    auto* normalLayout = new QHBoxLayout(normalRow);
    normalLayout->setContentsMargins(0, 0, 0, 0);
    for (auto* spin : {nxSpin, nySpin, nzSpin})
        normalLayout->addWidget(spin, 1);
    normalRow->setToolTip(
        tr("The plane's normal, in world coordinates. Any direction will do — "
           "the length is irrelevant, since it is normalized before use, so "
           "(0, 0, 2) and (0, 0, 1) are the same plane.\n\n"
           "The floor is placed on the NEGATIVE side of the structure along "
           "this direction, and the height offset above moves it along the "
           "same direction. Reversing the normal therefore puts the plane over "
           "the structure rather than under it — a ceiling, which is a real "
           "choice rather than an error, so it is left available."));
    floorForm->addRow(tr("Normal (x, y, z):"), normalRow);

    // Inline validation, in the app's usual red. A zero vector spans no plane;
    // rather than refuse the keystroke — which would make (0,0,1) → (0,0,0) →
    // (1,0,0) impossible to type through — the previous normal is kept and the
    // input is flagged until it means something again.
    auto* normalWarning = new QLabel(floorGroup);
    normalWarning->setWordWrap(true);
    normalWarning->setStyleSheet(QStringLiteral("color: #d9534f;"));
    normalWarning->setVisible(false);
    floorForm->addRow(QString(), normalWarning);

    // Two lines, not four. This page is a plain widget like every other tab in
    // the panel, so what it holds has to fit the dock's default height — the
    // alternative is a scrollbar on one tab and not the others, or a control
    // stranded below the fold.
    pageLayout->addWidget(floorGroup);
    pageLayout->addStretch(1);

    // Every floor control is a uniform read at draw time — the plane's quad is
    // uploaded once and never rebuilt — so all of these repaint rather than
    // rebuild, height included.
    // The single control for the bit. It briefly had a twin on the viewport
    // toolbar and the two mirrored each other; the toolbar button is gone, so
    // this writes the style and nothing else needs telling.
    connect(floorGroup, &QGroupBox::toggled, this, [this](bool on) {
        viewport_->style().floorEnabled = on;
        viewport_->styleChanged(false);
    });
    connect(heightSpin, &QDoubleSpinBox::valueChanged, this, [this](double v) {
        viewport_->style().floorOffset = static_cast<float>(v);
        viewport_->styleChanged(false);
    });
    connect(colorButton, &QPushButton::clicked, this, [this, colorButton] {
        const QColor chosen =
            QColorDialog::getColor(viewport_->style().floorColor, this,
                                   tr("Floor Color"));
        if (!chosen.isValid())
            return;
        viewport_->style().floorColor = chosen;
        setButtonColor(colorButton, chosen);
        viewport_->styleChanged(false);
    });
    connect(finishCombo, &QComboBox::currentIndexChanged, this, [this](int i) {
        viewport_->style().floorFinish = static_cast<render::SurfaceFinish>(i);
        viewport_->styleChanged(false);
    });
    connect(opacitySpin, &QDoubleSpinBox::valueChanged, this, [this](double v) {
        viewport_->style().floorOpacity = static_cast<float>(v);
        viewport_->styleChanged(false);
    });

    // --- Orientation: the two inputs, kept consistent -----------------------
    //
    // Both directions run through the NORMAL, never through each other: the
    // dropdown writes a normal and the spins write a normal, and each then
    // re-derives the other's display from what the style now holds. Wiring
    // them to each other instead is how a preset ends up disagreeing with the
    // numbers under it.
    // The combo's item order IS render::FloorPreset's enumerator order, so the
    // index and the enum are the same number and no lookup table is needed.
    // The rule itself lives with the renderer beside floorBasis(), where a
    // test can reach it — a copy here would be a second answer to "is this
    // normal an axis?" and the two would eventually differ.
    using render::StructureRenderer;
    constexpr int kCustomIndex =
        static_cast<int>(StructureRenderer::FloorPreset::Custom);
    const auto presetFor = [](const QVector3D& normal) {
        return static_cast<int>(StructureRenderer::floorPreset(normal));
    };
    const auto showNormal = [nxSpin, nySpin, nzSpin, orientationCombo, presetFor](
                                const QVector3D& normal) {
        for (const auto& [spin, value] :
             {std::pair{nxSpin, normal.x()}, std::pair{nySpin, normal.y()},
              std::pair{nzSpin, normal.z()}}) {
            const QSignalBlocker blocker(spin);
            spin->setValue(static_cast<double>(value));
        }
        const QSignalBlocker blocker(orientationCombo);
        orientationCombo->setCurrentIndex(presetFor(normal));
    };
    showNormal(style.floorNormal);

    connect(orientationCombo, &QComboBox::activated, this,
            [this, showNormal, normalWarning](int index) {
                // `activated`, not `currentIndexChanged`: this must fire only
                // for a USER pick. Selecting Custom programmatically — which
                // happens every time the spins are edited into a non-axis
                // direction — would otherwise loop straight back and overwrite
                // the numbers just typed.
                if (index < 0 || index >= kCustomIndex)
                    return; // "Custom" is a read-out, not a command
                normalWarning->setVisible(false);
                viewport_->style().floorNormal =
                    StructureRenderer::floorPresetNormal(
                        static_cast<StructureRenderer::FloorPreset>(index));
                showNormal(viewport_->style().floorNormal);
                viewport_->styleChanged(false);
            });

    const auto normalEdited = [this, nxSpin, nySpin, nzSpin, orientationCombo,
                               normalWarning, presetFor] {
        const QVector3D typed(static_cast<float>(nxSpin->value()),
                              static_cast<float>(nySpin->value()),
                              static_cast<float>(nzSpin->value()));
        if (typed.lengthSquared() < 1e-12f) {
            // Keep the last good plane and say why nothing moved. Clearing the
            // fields back to it would fight anyone typing one component at a
            // time through zero.
            normalWarning->setText(
                tr("A zero normal defines no plane — keeping the previous "
                   "orientation."));
            normalWarning->setVisible(true);
            return;
        }
        normalWarning->setVisible(false);
        viewport_->style().floorNormal = typed;
        // Only the dropdown is refreshed, not the spins: rewriting the numbers
        // under the cursor mid-edit is the one thing this must not do.
        const QSignalBlocker blocker(orientationCombo);
        orientationCombo->setCurrentIndex(presetFor(typed));
        viewport_->styleChanged(false);
    };
    for (auto* spin : {nxSpin, nySpin, nzSpin})
        connect(spin, &QDoubleSpinBox::valueChanged, this,
                [normalEdited](double) { normalEdited(); });
    // Pull, not push: every control re-reads the live style rather than being
    // handed a value, so the one caller (a project restore, which moves all of
    // them at once) needs no per-setting signal, and adding a control here
    // costs nothing at the other end.
    //
    // Each write is blocked, or the setValue() would fire the handlers above
    // and write the value straight back into the style it was just read from.
    connect(this, &VisualEffectsPanel::syncFloorFromViewport, floorGroup,
            [this, floorGroup, heightSpin, colorButton, finishCombo,
             opacitySpin, showNormal, normalWarning] {
                const auto& live = viewport_->style();
                {
                    const QSignalBlocker blocker(floorGroup);
                    floorGroup->setChecked(live.floorEnabled);
                }
                {
                    const QSignalBlocker blocker(heightSpin);
                    heightSpin->setValue(live.floorOffset);
                }
                {
                    const QSignalBlocker blocker(finishCombo);
                    finishCombo->setCurrentIndex(
                        static_cast<int>(live.floorFinish));
                }
                {
                    const QSignalBlocker blocker(opacitySpin);
                    opacitySpin->setValue(live.floorOpacity);
                }
                showNormal(live.floorNormal);
                normalWarning->setVisible(false);
                setButtonColor(colorButton, live.floorColor);
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
        tr("How far a neighboring surface can be and still shade this one. "
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
