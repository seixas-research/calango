#include "gui/EditVolumetricRenderDialog.hpp"

#include "gui/SettingsManager.hpp"
#include "render/ShaderProfile.hpp"

#include <QCheckBox>
#include <QColorDialog>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QGroupBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QSlider>
#include <QSpinBox>
#include <QSignalBlocker>
#include <QStackedWidget>
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
    resize(430, 470);

    auto* layout = new QVBoxLayout(this);

    // Central mode selector (replaces the former tab widget).
    auto* modeRow = new QFormLayout;
    modeCombo_ = new QComboBox(this);
    modeCombo_->addItems({tr("Isosurfaces"), tr("Color Slice"),
                          tr("Direct Volume (ray march)")});
    modeCombo_->setItemData(
        2,
        tr("Ray-march the whole field instead of extracting one isovalue from "
           "it.\n\nAn isosurface has to pick a level and discard everything "
           "else; this shows the core, the bonding region and the tail at "
           "once, weighted by the colour ramp. It is the only mode whose cost "
           "is paid every frame rather than once per parameter change."),
        Qt::ToolTipRole);
    modeCombo_->setCurrentIndex(static_cast<int>(mode));
    modeRow->addRow(tr("Render Mode:"), modeCombo_);
    layout->addLayout(modeRow);

    stack_ = new QStackedWidget(this);
    stack_->addWidget(buildIsosurfacePage());  // index 0 = Isosurface
    stack_->addWidget(buildColorSlicePage());  // index 1 = ColorSlice
    stack_->addWidget(buildDirectVolumePage()); // index 2 = DirectVolume
    stack_->setCurrentIndex(static_cast<int>(mode));
    layout->addWidget(stack_, 1);

    connect(modeCombo_, &QComboBox::currentIndexChanged, this, [this](int i) {
        stack_->setCurrentIndex(i);
        emitChange(); // the active mode changed
    });

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);
}

VolumetricRenderMode EditVolumetricRenderDialog::mode() const
{
    return static_cast<VolumetricRenderMode>(modeCombo_->currentIndex());
}

void EditVolumetricRenderDialog::emitChange()
{
    if (!updating_)
        Q_EMIT styleChanged(style_, mode());
}

QWidget* EditVolumetricRenderDialog::buildIsosurfacePage()
{
    auto* page = new QWidget(this);
    auto* form = new QFormLayout(page);

    // Isovalue slider + numeric input.
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

    // -- Draw style ---------------------------------------------------------
    // Presentation of the same extracted mesh, not a different extraction.
    drawStyleCombo_ = new QComboBox(page);
    drawStyleCombo_->addItem(tr("Solid surface"),
                             static_cast<int>(IsoDrawStyle::Solid));
    drawStyleCombo_->addItem(tr("Wireframe mesh"),
                             static_cast<int>(IsoDrawStyle::Mesh));
    drawStyleCombo_->addItem(tr("Solid + mesh"),
                             static_cast<int>(IsoDrawStyle::SolidMesh));
    drawStyleCombo_->addItem(tr("Dot cloud"),
                             static_cast<int>(IsoDrawStyle::Dots));
    drawStyleCombo_->setCurrentIndex(
        drawStyleCombo_->findData(static_cast<int>(style_.drawStyle)));
    drawStyleCombo_->setToolTip(
        tr("How the extracted surface is drawn.\n\n"
           "Solid — a filled skin; the clearest shape, but it hides the atoms "
           "it encloses.\n"
           "Wireframe mesh — the triangle edges only, so the structure stays "
           "readable through the surface.\n"
           "Solid + mesh — both, the usual figure convention for showing an "
           "orbital's shape and its curvature at once.\n"
           "Dot cloud — a thinned scatter of marks over the surface, which "
           "reads as a density rather than as a hard boundary."));
    form->addRow(tr("Draw style:"), drawStyleCombo_);
    connect(drawStyleCombo_, &QComboBox::currentIndexChanged, this, [this] {
        if (updating_)
            return;
        style_.drawStyle = static_cast<IsoDrawStyle>(
            drawStyleCombo_->currentData().toInt());
        syncIsoStyleEnabled();
        emitChange();
    });

    dotSizeSpin_ = new QDoubleSpinBox(page);
    dotSizeSpin_->setRange(0.01, 1.0);
    dotSizeSpin_->setDecimals(3);
    dotSizeSpin_->setSingleStep(0.01);
    dotSizeSpin_->setSuffix(tr(" Å"));
    dotSizeSpin_->setValue(style_.dotSize);
    dotSizeSpin_->setToolTip(
        tr("Size of each mark in the dot cloud, in ångström — a real length in "
           "the scene, so the dots keep their scale relative to the structure "
           "as the camera zooms."));
    form->addRow(tr("Dot size:"), dotSizeSpin_);
    connect(dotSizeSpin_, &QDoubleSpinBox::valueChanged, this, [this](double v) {
        style_.dotSize = v;
        emitChange();
    });

    dotStrideSpin_ = new QSpinBox(page);
    dotStrideSpin_->setRange(1, 50);
    dotStrideSpin_->setValue(style_.dotStride);
    dotStrideSpin_->setPrefix(tr("every "));
    dotStrideSpin_->setSuffix(tr(" vertices"));
    dotStrideSpin_->setToolTip(
        tr("Thin the cloud by keeping only every Nth surface vertex. A "
           "refined grid carries hundreds of thousands of them, where a mark "
           "on each is a solid wall of ink; raising this is what turns the "
           "surface back into a cloud."));
    form->addRow(tr("Dot density:"), dotStrideSpin_);
    connect(dotStrideSpin_, &QSpinBox::valueChanged, this, [this](int v) {
        style_.dotStride = v;
        emitChange();
    });

    meshShadeSpin_ = new QDoubleSpinBox(page);
    meshShadeSpin_->setRange(0.0, 1.0);
    meshShadeSpin_->setSingleStep(0.05);
    meshShadeSpin_->setValue(style_.meshShade);
    meshShadeSpin_->setToolTip(
        tr("How dark the wires are drawn relative to the fill they sit on, in "
           "the Solid + mesh style. At 1 the mesh takes the surface color "
           "exactly and disappears into it; lower values are what make the "
           "triangulation legible."));
    form->addRow(tr("Mesh darkening:"), meshShadeSpin_);
    connect(meshShadeSpin_, &QDoubleSpinBox::valueChanged, this,
            [this](double v) {
                style_.meshShade = v;
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

    // -- Shading ------------------------------------------------------------
    // "Lit surface" was a two-entry dropdown in Preferences → Rendering. Two
    // entries IS a checkbox, and the switch belongs here: it decides whether
    // the three controls directly below it still reach the main viewport, and
    // a control whose relevance is decided in another window is not one the
    // user can reason about.
    litSurfaceCheck_ = new QCheckBox(tr("Lit surface (GPU shading)"), page);
    litSurfaceCheck_->setChecked(
        !render::ShaderRegistry::activeProfile(render::ShaderSlot::Isosurfaces)
             .isLegacy);
    litSurfaceCheck_->setToolTip(
        tr("Shade the surface on the GPU from the normals marching cubes "
           "already derives, using the SAME lights as the atoms — so the "
           "highlight tracks the camera and the surface sits in the scene "
           "rather than on top of it. It also writes a real normal into the "
           "G-buffer, so isosurfaces take part in ambient occlusion.\n\n"
           "Off is the historical path: an unlit fill whose shading is "
           "computed on the CPU and baked into the vertex colours by the "
           "Shading row below, with the highlight frozen to a fixed "
           "direction. Kept for reproducing older figures.\n\n"
           "Unlike everything else in this dialog, this applies to every "
           "isosurface in the application and persists between sessions."));
    form->addRow(litSurfaceCheck_);
    connect(litSurfaceCheck_, &QCheckBox::toggled, this, [this](bool on) {
        if (updating_)
            return;
        render::ShaderRegistry::setActiveProfileId(
            render::ShaderSlot::Isosurfaces,
            on ? QStringLiteral("lit") : QStringLiteral("legacy"));
        SettingsManager::save();
        syncIsoStyleEnabled();
        // The vertex COLOURS differ between the two paths — the legacy one
        // bakes the shading into them — so this is a re-extraction, not a
        // repaint. emitChange() is what drives that.
        emitChange();
    });

    shadingCombo_ = new QComboBox(page);
    shadingCombo_->addItem(tr("Flat (unshaded)"),
                           static_cast<int>(IsoShading::Flat));
    shadingCombo_->addItem(tr("Diffuse"), static_cast<int>(IsoShading::Diffuse));
    shadingCombo_->addItem(tr("Glossy"), static_cast<int>(IsoShading::Glossy));
    shadingCombo_->setCurrentIndex(
        shadingCombo_->findData(static_cast<int>(style_.shading)));
    // The tool tip is set by syncIsoStyleEnabled() instead of here: it depends
    // on the Lit surface checkbox above, and two places writing it would let
    // them disagree.
    form->addRow(tr("Shading:"), shadingCombo_);
    connect(shadingCombo_, &QComboBox::currentIndexChanged, this, [this] {
        if (updating_)
            return;
        style_.shading =
            static_cast<IsoShading>(shadingCombo_->currentData().toInt());
        // From here on this tab's material is the user's, and registering
        // another Wannier function must not silently change it back.
        style_.shadingChosen = true;
        syncIsoStyleEnabled();
        emitChange();
    });

    ambientSpin_ = new QDoubleSpinBox(page);
    ambientSpin_->setRange(0.0, 1.0);
    ambientSpin_->setSingleStep(0.05);
    ambientSpin_->setValue(style_.ambient);
    ambientSpin_->setToolTip(
        tr("How much of its color a face turned away from every light keeps. "
           "At 0 unlit faces go black, which on a translucent surface reads as "
           "a hole rather than as shadow; raising it flattens the shading back "
           "toward a uniform fill."));
    form->addRow(tr("Ambient:"), ambientSpin_);
    connect(ambientSpin_, &QDoubleSpinBox::valueChanged, this, [this](double v) {
        style_.ambient = v;
        emitChange();
    });

    specularSpin_ = new QDoubleSpinBox(page);
    specularSpin_->setRange(0.0, 1.0);
    specularSpin_->setSingleStep(0.05);
    specularSpin_->setValue(style_.specular);
    specularSpin_->setToolTip(
        tr("Strength of the Glossy highlight, and the specular material term "
           "used by the lit volume viewers."));
    form->addRow(tr("Specular finish:"), specularSpin_);
    connect(specularSpin_, &QDoubleSpinBox::valueChanged, this,
            [this](double v) {
                style_.specular = v;
                emitChange();
            });

    smoothingSpin_ = new QSpinBox(page);
    smoothingSpin_->setRange(0, 20);
    smoothingSpin_->setValue(style_.smoothing);
    smoothingSpin_->setSuffix(tr(" passes"));
    smoothingSpin_->setToolTip(
        tr("Laplacian smoothing of the extracted mesh: each vertex creeps "
           "toward the average of its neighbors, removing the stair-steps "
           "marching cubes leaves on a coarse voxel grid.\n\n"
           "Cheaper than Grid Interpolation below, which refines the voxels "
           "instead — this touches only the vertices. High counts shrink the "
           "surface slightly: it is a smoother, not a re-extraction, so read "
           "isovalues off an unsmoothed surface."));
    form->addRow(tr("Mesh smoothing:"), smoothingSpin_);
    connect(smoothingSpin_, &QSpinBox::valueChanged, this, [this](int v) {
        style_.smoothing = v;
        emitChange();
    });

    posColorButton_ = new QPushButton(page);
    updateColorButton(posColorButton_, style_.positiveColor);
    posColorButton_->setToolTip(tr("Uniform fill color of the positive lobe."));
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
        tr("Fill color of the negative-phase lobe for signed fields (e.g. "
           "Wannier orbitals ψ<0)."));
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

    // 3D grid scalar interpolation applied before marching cubes.
    isoInterpCombo_ = new QComboBox(page);
    // Order matches core::GridInterpolation.
    isoInterpCombo_->addItem(tr("None (Raw Voxel Grid)"));
    isoInterpCombo_->addItem(tr("Linear Spline Interpolation (Trilinear)"));
    isoInterpCombo_->addItem(tr("Cubic Spline Interpolation (Tricubic)"));
    isoInterpCombo_->setCurrentIndex(static_cast<int>(style_.gridInterpolation));
    isoInterpCombo_->setToolTip(
        tr("Refine the voxel grid before surface extraction for a smoother "
           "mesh (2× upsampling)."));
    form->addRow(tr("Grid Interpolation:"), isoInterpCombo_);
    connect(isoInterpCombo_, &QComboBox::currentIndexChanged, this,
            [this](int i) {
                style_.gridInterpolation =
                    static_cast<core::GridInterpolation>(i);
                emitChange();
            });

    // Periodic continuation of a Wannier isosurface past the cell faces.
    continuationSpin_ = new QDoubleSpinBox(page);
    continuationSpin_->setRange(0.0, core::kMaxContinuationMargin);
    continuationSpin_->setSingleStep(0.25);
    continuationSpin_->setDecimals(2);
    continuationSpin_->setSuffix(tr(" cells"));
    continuationSpin_->setValue(style_.continuationMargin);
    // The tool tip's second half is written by syncIsoStyleEnabled(), which is
    // the only place that knows whether this tab holds a Wannier function.
    form->addRow(tr("Periodic continuation:"), continuationSpin_);
    connect(continuationSpin_, &QDoubleSpinBox::valueChanged, this,
            [this](double v) {
                style_.continuationMargin = v;
                emitChange();
            });

    // -- Potential-map colouring -------------------------------------------
    // The same surface, coloured by a second field instead of by phase. It
    // used to be a third render mode, which meant re-choosing the base field
    // that was already selected and a duplicate set of isosurface controls;
    // as a checkbox here it is what it actually is — an option on this
    // surface.
    auto* potentialGroup = new QGroupBox(tr("Potential Map Color"), page);
    potentialGroup_ = potentialGroup; // setStyle() has to re-check it
    potentialGroup->setCheckable(true);
    potentialGroup->setChecked(style_.potentialColoring);
    potentialGroup->setToolTip(
        tr("Color this isosurface by a second scalar field sampled at each of "
           "its vertices — the classic electrostatic-potential map: the "
           "density shapes the surface, the potential paints it.\n\n"
           "Off, the surface keeps the flat phase colors above."));
    auto* potentialForm = new QFormLayout(potentialGroup);

    potentialSecondaryCombo_ = new QComboBox(potentialGroup);
    potentialSecondaryCombo_->setToolTip(
        tr("Scalar field V(r) sampled at each surface vertex and mapped to "
           "color through the ramp below."));
    potentialForm->addRow(tr("Color by:"), potentialSecondaryCombo_);
    connect(potentialSecondaryCombo_, &QComboBox::currentIndexChanged, this,
            [this] {
                if (updating_)
                    return;
                style_.potentialSecondaryIndex =
                    potentialSecondaryCombo_->currentData().toInt();
                emitChange();
            });

    potentialGradientCombo_ = gradientCombo(potentialGroup, style_.gradient);
    potentialForm->addRow(tr("Color map:"), potentialGradientCombo_);
    connect(potentialGradientCombo_, &QComboBox::currentIndexChanged, this,
            [this](int i) {
                const auto& g = volumetricGradients();
                if (i >= 0 && i < g.size())
                    style_.gradient = g.at(i);
                emitChange();
            });

    potentialInvertCheck_ =
        new QCheckBox(tr("Invert palette"), potentialGroup);
    potentialInvertCheck_->setChecked(style_.invertGradient);
    potentialInvertCheck_->setToolTip(
        tr("Flip the value → color mapping (t → 1 − t), like matplotlib's "
           "\"_r\" palettes."));
    potentialForm->addRow(QString(), potentialInvertCheck_);
    connect(potentialInvertCheck_, &QCheckBox::toggled, this, [this](bool on) {
        style_.invertGradient = on;
        if (sliceInvertCheck_ && sliceInvertCheck_->isChecked() != on) {
            const QSignalBlocker block(sliceInvertCheck_);
            sliceInvertCheck_->setChecked(on);
        }
        emitChange();
    });

    // -- Ramp bounds --------------------------------------------------------
    // A potential runs from deeply negative at the nuclei to nearly flat in
    // the vacuum; on the field's own full range everything interesting sits in
    // a sliver of the ramp. Pinning the window is what makes the map readable,
    // and what lets two molecules be compared on one colour scale.
    potentialBoundsCheck_ =
        new QCheckBox(tr("Custom range"), potentialGroup);
    potentialBoundsCheck_->setChecked(style_.potentialUseBounds);
    potentialBoundsCheck_->setToolTip(
        tr("Off: the ramp spans the coloring field's own minimum and "
           "maximum.\n"
           "On: it is pinned to the values below, with anything outside them "
           "clamped to the ramp ends."));
    potentialForm->addRow(QString(), potentialBoundsCheck_);

    auto* rangeRow = new QWidget(potentialGroup);
    auto* rangeLayout = new QHBoxLayout(rangeRow);
    rangeLayout->setContentsMargins(0, 0, 0, 0);
    potentialMinSpin_ = new QDoubleSpinBox(rangeRow);
    potentialMinSpin_->setDecimals(4);
    potentialMinSpin_->setRange(-1e12, 1e12);
    potentialMinSpin_->setKeyboardTracking(false);
    potentialMinSpin_->setValue(style_.potentialMin);
    potentialMaxSpin_ = new QDoubleSpinBox(rangeRow);
    potentialMaxSpin_->setDecimals(4);
    potentialMaxSpin_->setRange(-1e12, 1e12);
    potentialMaxSpin_->setKeyboardTracking(false);
    potentialMaxSpin_->setValue(style_.potentialMax);
    rangeLayout->addWidget(new QLabel(tr("min"), rangeRow));
    rangeLayout->addWidget(potentialMinSpin_, 1);
    rangeLayout->addWidget(new QLabel(tr("max"), rangeRow));
    rangeLayout->addWidget(potentialMaxSpin_, 1);
    potentialForm->addRow(tr("Range:"), rangeRow);

    const auto syncPotentialBounds = [this] {
        const bool on = potentialBoundsCheck_->isChecked();
        potentialMinSpin_->setEnabled(on);
        potentialMaxSpin_->setEnabled(on);
    };
    syncPotentialBounds();
    connect(potentialBoundsCheck_, &QCheckBox::toggled, this,
            [this, syncPotentialBounds](bool on) {
                style_.potentialUseBounds = on;
                syncPotentialBounds();
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
    connect(potentialGroup, &QGroupBox::toggled, this, [this](bool on) {
        style_.potentialColoring = on;
        emitChange();
    });
    form->addRow(potentialGroup);

    syncIsoStyleEnabled();
    return page;
}

void EditVolumetricRenderDialog::setHasWannier(bool hasWannier)
{
    if (hasWannier_ == hasWannier)
        return;
    hasWannier_ = hasWannier;
    syncIsoStyleEnabled();
}

void EditVolumetricRenderDialog::syncIsoStyleEnabled()
{
    // Grey out what the current draw style and shading model do not read,
    // rather than hiding it: a control that vanishes reads as a missing
    // feature, while a disabled one says "this belongs to another style".
    const bool dots = style_.drawStyle == IsoDrawStyle::Dots;
    const bool solidMesh = style_.drawStyle == IsoDrawStyle::SolidMesh;
    dotSizeSpin_->setEnabled(dots);
    dotStrideSpin_->setEnabled(dots);
    meshShadeSpin_->setEnabled(solidMesh);
    // Shading / Ambient bake the lighting into the vertex colours, which only
    // the legacy path reads. They are not disabled under "Lit surface" because
    // they are not dead: the standalone volume viewer windows shade through
    // them either way. The label says which is which instead.
    const bool baked = !litSurfaceCheck_ || !litSurfaceCheck_->isChecked();
    shadingCombo_->setToolTip(
        baked ? tr("Bake the lighting into the vertex colours, against the "
                   "same fixed studio lights the atoms use.\n\n"
                   "Flat leaves every facet the same colour — a silhouette, "
                   "which is what makes two overlapping lobes "
                   "indistinguishable. Diffuse adds the shape back; Glossy "
                   "adds a highlight on top, scaled by the Specular finish "
                   "below.\n\n"
                   "Baked shading does not swing with the camera. Turn on "
                   "\"Lit surface\" above for shading that does.")
              : tr("Baked shading. With \"Lit surface\" on, the main viewport "
                   "shades on the GPU instead and ignores this — it still "
                   "drives the standalone volume viewer windows."));
    ambientSpin_->setEnabled(style_.shading != IsoShading::Flat);

    // Periodic continuation applies to Wannier functions and nothing else, so
    // it is live only when this tab holds one. Disabled rather than hidden,
    // with the reason in the tool tip: a control that vanishes reads as a
    // missing feature.
    if (continuationSpin_) {
        continuationSpin_->setEnabled(hasWannier_);
        continuationSpin_->setToolTip(
            hasWannier_
                ? tr("How far past the home cell a Wannier function's "
                     "isosurface is followed, in cell units.\n\n"
                     "A Wannier function is localized but not confined: its "
                     "centre lands wherever the wannierization put it and its "
                     "tails cross the cell faces, so extracting over the cell "
                     "alone cuts the lobe flat and strands the rest on the "
                     "opposite side of the box. The neighbouring images hold "
                     "the same function continued, so the surface is extracted "
                     "over a window centred on the Wannier centre instead, and "
                     "the copies that window also covers are dropped.\n\n"
                     "0.5 shows a function whole wherever its centre fell. "
                     "Raise it for longer tails — the cost grows as the cube "
                     "of the window.")
                : tr("Applies to Wannier functions, and this tab holds none.\n\n"
                     "A Wannier function is localized, so there is one lobe to "
                     "follow across the cell boundary and the periodic copies "
                     "can be told apart from it. A charge density fills its "
                     "cell — continuing it would draw the same surface 27 "
                     "times."));
    }
    // Specular still drives the lit volume viewers with Flat shading on, so it
    // is only ever informative here — never disabled.
}

QWidget* EditVolumetricRenderDialog::buildDirectVolumePage()
{
    auto* page = new QWidget(this);
    auto* form = new QFormLayout(page);

    auto* intro = new QLabel(
        tr("The field is uploaded as a 3D texture and integrated along the "
           "view ray. The colour ramp on the <b>Color Slice</b> page is the "
           "transfer function; opacity rises with value, so the dense core "
           "shows through the diffuse tail."),
        page);
    intro->setWordWrap(true);
    intro->setTextFormat(Qt::RichText);
    form->addRow(intro);

    volumeStepsSpin_ = new QSpinBox(page);
    volumeStepsSpin_->setRange(8, 2048);
    volumeStepsSpin_->setValue(256);
    volumeStepsSpin_->setSingleStep(32);
    volumeStepsSpin_->setToolTip(
        tr("Samples along each ray — the quality/cost dial, paid every "
           "frame.\n\n"
           "Too few and the field shows as concentric shells where the step "
           "pattern beats against its own structure. 128 is fluid to orbit; "
           "512 is what a still wants."));
    form->addRow(tr("Ray steps:"), volumeStepsSpin_);

    volumeDensitySpin_ = new QDoubleSpinBox(page);
    volumeDensitySpin_->setRange(0.01, 20.0);
    volumeDensitySpin_->setDecimals(2);
    volumeDensitySpin_->setSingleStep(0.1);
    volumeDensitySpin_->setValue(1.0);
    volumeDensitySpin_->setToolTip(
        tr("Global opacity scale on top of the transfer function. Raise it "
           "until the structure reads; past that the volume goes solid and "
           "hides its own interior."));
    form->addRow(tr("Density:"), volumeDensitySpin_);

    volumeThresholdSpin_ = new QDoubleSpinBox(page);
    volumeThresholdSpin_->setRange(0.0, 1.0);
    volumeThresholdSpin_->setDecimals(4);
    volumeThresholdSpin_->setSingleStep(0.005);
    volumeThresholdSpin_->setValue(0.02);
    volumeThresholdSpin_->setToolTip(
        tr("Normalized value below which a sample contributes nothing.\n\n"
           "A density's vacuum tail fills most of the cell with near-zero "
           "values; without a threshold they fog the whole box grey and bury "
           "everything inside it."));
    form->addRow(tr("Threshold:"), volumeThresholdSpin_);

    volumeLitCheck_ = new QCheckBox(tr("Shade from the field gradient"), page);
    volumeLitCheck_->setChecked(true);
    volumeLitCheck_->setToolTip(
        tr("Light each sample by the local gradient, which is the surface "
           "normal wherever the field has structure. Six extra texture taps "
           "per sample, and what makes an orbital read as a shape rather than "
           "as coloured smoke."));
    form->addRow(QString(), volumeLitCheck_);

    connect(volumeStepsSpin_, &QSpinBox::valueChanged, this, [this](int v) {
        style_.directVolume.steps = v;
        emitChange();
    });
    connect(volumeDensitySpin_, &QDoubleSpinBox::valueChanged, this,
            [this](double v) {
                style_.directVolume.density = v;
                emitChange();
            });
    connect(volumeThresholdSpin_, &QDoubleSpinBox::valueChanged, this,
            [this](double v) {
                style_.directVolume.threshold = v;
                emitChange();
            });
    connect(volumeLitCheck_, &QCheckBox::toggled, this, [this](bool on) {
        style_.directVolume.lit = on;
        emitChange();
    });
    return page;
}

QWidget* EditVolumetricRenderDialog::buildColorSlicePage()
{
    auto* page = new QWidget(this);
    auto* form = new QFormLayout(page);

    // Plane orientation as Miller indices, so the slice is defined against the
    // crystal lattice rather than the Cartesian axes: the normal is the
    // reciprocal-lattice vector G = h·b₁ + k·b₂ + l·b₃. Three dedicated
    // numerical fields (not sliders) — the useful values are small exact
    // integers, and a family like (1 -1 0) has to be typed, not dragged.
    auto* millerRow = new QWidget(page);
    auto* millerLayout = new QHBoxLayout(millerRow);
    millerLayout->setContentsMargins(0, 0, 0, 0);
    int* const millerFields[3] = {&style_.millerH, &style_.millerK,
                                  &style_.millerL};
    const char* const millerNames[3] = {"h", "k", "l"};
    for (int i = 0; i < 3; ++i) {
        millerLayout->addWidget(
            new QLabel(QLatin1String(millerNames[i]), millerRow));
        auto* spin = new QSpinBox(millerRow);
        spin->setRange(-99, 99);
        spin->setValue(*millerFields[i]);
        millerLayout->addWidget(spin);
        millerSpins_[i] = spin;
        connect(spin, &QSpinBox::valueChanged, this,
                [this, target = millerFields[i]](int v) {
                    *target = v;
                    emitChange();
                });
    }
    millerLayout->addStretch(1);
    millerRow->setToolTip(
        tr("Miller indices (h k l) of the slice plane. The plane normal is the "
           "reciprocal-lattice vector G = h·(b×c) + k·(c×a) + l·(a×b), so "
           "(0 0 1) cuts along the a-b plane and (1 1 1) along the close-packed "
           "family. (0 0 0) is degenerate and falls back to the c-axis "
           "normal."));
    form->addRow(tr("Plane — Miller indices (h k l):"), millerRow);

    sliceOffsetSlider_ = new QSlider(Qt::Horizontal, page);
    sliceOffsetSlider_->setRange(0, kSliderSteps);
    sliceOffsetSlider_->setValue(
        static_cast<int>(std::clamp(style_.sliceOffset, 0.0, 1.0) * kSliderSteps));
    sliceOffsetSlider_->setToolTip(
        tr("Displacement of the plane along its own normal, sweeping the whole "
           "cell from one face to the other."));
    form->addRow(tr("Offset:"), sliceOffsetSlider_);
    connect(sliceOffsetSlider_, &QSlider::valueChanged, this, [this](int v) {
        style_.sliceOffset = static_cast<double>(v) / kSliderSteps;
        emitChange();
    });

    // -- Extent -------------------------------------------------------------
    sliceExtentCombo_ = new QComboBox(page);
    sliceExtentCombo_->addItem(tr("This unit cell only"), 1);
    sliceExtentCombo_->addItem(tr("2 × 2 cells"), 2);
    sliceExtentCombo_->addItem(tr("3 × 3 cells"), 3);
    sliceExtentCombo_->addItem(tr("5 × 5 cells"), 5);
    const int extentIndex =
        sliceExtentCombo_->findData(std::clamp(style_.sliceReplicas, 1, 5));
    sliceExtentCombo_->setCurrentIndex(extentIndex >= 0 ? extentIndex : 0);
    sliceExtentCombo_->setToolTip(
        tr("How far the plane is drawn, in unit cells across.\n\n"
           "One cell keeps the slice inside the box the field was computed "
           "in — the honest extent, and the one to use when reading values "
           "off it. The replicated options tile the periodic field over the "
           "neighboring cells, which is what makes a surface reconstruction "
           "or an adsorbate pattern legible instead of showing one cell "
           "floating on its own."));
    form->addRow(tr("Extent:"), sliceExtentCombo_);
    connect(sliceExtentCombo_, &QComboBox::currentIndexChanged, this, [this] {
        style_.sliceReplicas = sliceExtentCombo_->currentData().toInt();
        emitChange();
    });

    sliceBorderCheck_ = new QCheckBox(tr("Outline the slice"), page);
    sliceBorderCheck_->setChecked(style_.sliceShowBorder);
    sliceBorderCheck_->setToolTip(
        tr("Draw the plane's boundary, so its extent is visible where the "
           "field itself has gone to zero and the quad would otherwise fade "
           "into the background."));
    form->addRow(QString(), sliceBorderCheck_);
    connect(sliceBorderCheck_, &QCheckBox::toggled, this, [this](bool on) {
        style_.sliceShowBorder = on;
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

    sliceInvertCheck_ = new QCheckBox(tr("Invert Colormap Scale"), page);
    sliceInvertCheck_->setChecked(style_.invertGradient);
    sliceInvertCheck_->setToolTip(
        tr("Flip the value → color mapping (t → 1 − t): field minima take the "
           "high end of the ramp and maxima the low end, like matplotlib's "
           "\"_r\" palettes."));
    form->addRow(sliceInvertCheck_);
    connect(sliceInvertCheck_, &QCheckBox::toggled, this, [this](bool on) {
        style_.invertGradient = on;
        // One flag serves both modes, so the two checkboxes mirror each other
        // rather than silently disagreeing about what the palette is doing.
        if (potentialInvertCheck_ && potentialInvertCheck_->isChecked() != on) {
            const QSignalBlocker block(potentialInvertCheck_);
            potentialInvertCheck_->setChecked(on);
        }
        emitChange();
    });

    // -- Color-mapping bounds ----------------------------------------------
    // A few outlier voxels (a nuclear cusp in a density, a spike at a boundary)
    // compress everything else into one end of the ramp; pinning the window is
    // what makes the physically interesting range legible, and what lets two
    // slices be compared on the same scale.
    sliceBoundsCheck_ = new QCheckBox(tr("Custom color range"), page);
    sliceBoundsCheck_->setChecked(style_.sliceUseBounds);
    sliceBoundsCheck_->setToolTip(
        tr("Off: the ramp spans the field's own minimum and maximum.\n"
           "On: it is pinned to the Min/Max below, with values outside them "
           "clamped to the ramp ends."));
    form->addRow(sliceBoundsCheck_);

    const auto makeBoundSpin = [page](double value) {
        auto* spin = new QDoubleSpinBox(page);
        spin->setDecimals(5);
        spin->setRange(-1e12, 1e12);
        spin->setKeyboardTracking(false); // apply on commit, not per keystroke
        spin->setValue(value);
        return spin;
    };
    sliceMinSpin_ = makeBoundSpin(style_.sliceMin);
    form->addRow(tr("Min value:"), sliceMinSpin_);
    sliceMaxSpin_ = makeBoundSpin(style_.sliceMax);
    form->addRow(tr("Max value:"), sliceMaxSpin_);

    const auto syncBoundsEnabled = [this] {
        const bool on = sliceBoundsCheck_->isChecked();
        sliceMinSpin_->setEnabled(on);
        sliceMaxSpin_->setEnabled(on);
    };
    syncBoundsEnabled();
    connect(sliceBoundsCheck_, &QCheckBox::toggled, this,
            [this, syncBoundsEnabled](bool on) {
                style_.sliceUseBounds = on;
                syncBoundsEnabled();
                emitChange();
            });
    connect(sliceMinSpin_, &QDoubleSpinBox::valueChanged, this,
            [this](double v) {
                style_.sliceMin = v;
                emitChange();
            });
    connect(sliceMaxSpin_, &QDoubleSpinBox::valueChanged, this,
            [this](double v) {
                style_.sliceMax = v;
                emitChange();
            });

    // -- Voxel grid interpolation ------------------------------------------
    sliceInterpCombo_ = new QComboBox(page);
    // Order matches core::GridInterpolation.
    sliceInterpCombo_->addItem(tr("None (Raw Grid)"));
    sliceInterpCombo_->addItem(tr("Linear Spline Interpolation (Trilinear)"));
    sliceInterpCombo_->addItem(tr("Cubic Spline Interpolation (Tricubic)"));
    sliceInterpCombo_->setCurrentIndex(
        static_cast<int>(style_.sliceInterpolation));
    sliceInterpCombo_->setToolTip(
        tr("Refine the voxel grid (2× upsampling) before sampling the plane. "
           "Tricubic gives the smoothest field but costs the most memory; None "
           "shows the raw data, voxel facets and all."));
    form->addRow(tr("Grid Interpolation:"), sliceInterpCombo_);
    connect(sliceInterpCombo_, &QComboBox::currentIndexChanged, this,
            [this](int i) {
                style_.sliceInterpolation =
                    static_cast<core::GridInterpolation>(i);
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

void EditVolumetricRenderDialog::setDatasets(const QStringList& labels,
                                             int currentIndex)
{
    Q_UNUSED(currentIndex);
    if (!potentialSecondaryCombo_)
        return;
    updating_ = true;
    const int prevSecondary = style_.potentialSecondaryIndex;

    potentialSecondaryCombo_->clear();
    potentialSecondaryCombo_->addItem(tr("None"), -1);
    // An empty label marks a dataset bound to another workspace tab — skipped
    // here, while the surviving entries keep their registry index as data.
    for (int i = 0; i < labels.size(); ++i) {
        if (labels.at(i).isEmpty())
            continue;
        potentialSecondaryCombo_->addItem(labels.at(i), i);
    }
    const int idx = potentialSecondaryCombo_->findData(prevSecondary);
    potentialSecondaryCombo_->setCurrentIndex(idx >= 0 ? idx : 0);
    style_.potentialSecondaryIndex =
        potentialSecondaryCombo_->currentData().toInt();
    updating_ = false;
}

void EditVolumetricRenderDialog::setStyle(const VolumetricStyle& style,
                                          VolumetricRenderMode mode)
{
    // `updating_` suppresses emitChange() for the whole sweep: every setValue()
    // below fires the control's own handler, and without the guard restoring a
    // tab's saved style would emit two dozen styleChanged signals — each one a
    // full re-extraction of the isosurface.
    updating_ = true;
    style_ = style;

    modeCombo_->setCurrentIndex(static_cast<int>(mode));
    stack_->setCurrentIndex(static_cast<int>(mode));

    // Isosurfaces.
    isoSpin_->setValue(std::clamp(style_.isovalue, fieldMin_, fieldMax_));
    syncIsoSlider();
    drawStyleCombo_->setCurrentIndex(
        drawStyleCombo_->findData(static_cast<int>(style_.drawStyle)));
    dotSizeSpin_->setValue(style_.dotSize);
    dotStrideSpin_->setValue(style_.dotStride);
    meshShadeSpin_->setValue(style_.meshShade);
    isoOpacitySpin_->setValue(style_.isoOpacity);
    shadingCombo_->setCurrentIndex(
        shadingCombo_->findData(static_cast<int>(style_.shading)));
    // Not part of `style` — it is the global profile — but re-read here all
    // the same, so a dialog reopened or switched to another tab shows what is
    // actually drawing rather than what it last remembered.
    litSurfaceCheck_->setChecked(
        !render::ShaderRegistry::activeProfile(render::ShaderSlot::Isosurfaces)
             .isLegacy);
    ambientSpin_->setValue(style_.ambient);
    specularSpin_->setValue(style_.specular);
    smoothingSpin_->setValue(style_.smoothing);
    updateColorButton(posColorButton_, style_.positiveColor);
    updateColorButton(negColorButton_, style_.negativeColor);
    isoInterpCombo_->setCurrentIndex(static_cast<int>(style_.gridInterpolation));
    continuationSpin_->setValue(style_.continuationMargin);

    // Potential-map colouring.
    potentialGroup_->setChecked(style_.potentialColoring);
    potentialGradientCombo_->setCurrentIndex(
        static_cast<int>(std::max<qsizetype>(
            0, volumetricGradients().indexOf(style_.gradient))));
    potentialInvertCheck_->setChecked(style_.invertGradient);
    potentialBoundsCheck_->setChecked(style_.potentialUseBounds);
    potentialMinSpin_->setValue(style_.potentialMin);
    potentialMaxSpin_->setValue(style_.potentialMax);
    potentialMinSpin_->setEnabled(style_.potentialUseBounds);
    potentialMaxSpin_->setEnabled(style_.potentialUseBounds);
    // The secondary-field index is a registry position and belongs to the
    // OTHER tab's dataset list; setDatasets() re-resolves it against this
    // tab's, which the panel calls right after restoring.

    // Colour slice.
    for (int i = 0; i < 3; ++i)
        millerSpins_[i]->setValue(i == 0   ? style_.millerH
                                      : i == 1 ? style_.millerK
                                               : style_.millerL);
    sliceOffsetSlider_->setValue(static_cast<int>(
        std::clamp(style_.sliceOffset, 0.0, 1.0) * kSliderSteps));
    const int extentIndex =
        sliceExtentCombo_->findData(std::clamp(style_.sliceReplicas, 1, 5));
    sliceExtentCombo_->setCurrentIndex(extentIndex >= 0 ? extentIndex : 0);
    sliceBorderCheck_->setChecked(style_.sliceShowBorder);
    sliceGradientCombo_->setCurrentIndex(
        static_cast<int>(std::max<qsizetype>(
            0, volumetricGradients().indexOf(style_.gradient))));
    sliceInvertCheck_->setChecked(style_.invertGradient);
    sliceBoundsCheck_->setChecked(style_.sliceUseBounds);
    sliceMinSpin_->setValue(style_.sliceMin);
    sliceMaxSpin_->setValue(style_.sliceMax);
    sliceMinSpin_->setEnabled(style_.sliceUseBounds);
    sliceMaxSpin_->setEnabled(style_.sliceUseBounds);
    sliceInterpCombo_->setCurrentIndex(
        static_cast<int>(style_.sliceInterpolation));
    sliceOpacitySpin_->setValue(style_.sliceOpacity);

    // Direct volume.
    volumeStepsSpin_->setValue(style_.directVolume.steps);
    volumeDensitySpin_->setValue(style_.directVolume.density);
    volumeThresholdSpin_->setValue(style_.directVolume.threshold);
    volumeLitCheck_->setChecked(style_.directVolume.lit);

    syncIsoStyleEnabled();
    updating_ = false;
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
    // While auto-scaled, the slice bounds track the field so the user starts
    // from its real numbers when they switch to a custom window. Once pinned,
    // the values are theirs and a new selection must not overwrite them.
    if (!style_.sliceUseBounds) {
        style_.sliceMin = fieldMin_;
        style_.sliceMax = fieldMax_;
        sliceMinSpin_->setValue(fieldMin_);
        sliceMaxSpin_->setValue(fieldMax_);
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
