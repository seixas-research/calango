#include "gui/FermiSurfaceWindow.hpp"

#include "core/BrillouinZone.hpp"
#include "core/GridInterpolation.hpp"
#include "core/MarchingCubes.hpp"
#include "core/VolumetricData.hpp"
#include "gui/GuiUtils.hpp"
#include "gui/VolumeViewWidget.hpp"
#include "render/ColorMap.hpp"

#include <QCheckBox>
#include <QCloseEvent>
#include <QColorDialog>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLabel>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QSaveFile>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QTextStream>
#include <QVBoxLayout>

#include <algorithm>
#include <limits>

namespace calango::gui {

namespace {

void pushVertex(std::vector<float>& out, const QVector3D& p,
                const QVector3D& n, const QColor& c)
{
    out.insert(out.end(),
               {p.x(), p.y(), p.z(), n.x(), n.y(), n.z(),
                static_cast<float>(c.redF()), static_cast<float>(c.greenF()),
                static_cast<float>(c.blueF())});
}

QVector3D toQVector3D(const core::Vec3& v)
{
    return {static_cast<float>(v.x), static_cast<float>(v.y),
           static_cast<float>(v.z)};
}

} // namespace

FermiSurfaceWindow::FermiSurfaceWindow(QWidget* parent) : QDialog(parent)
{
    setWindowTitle(tr("Fermi Surface"));
    resize(1180, 760);

    auto* layout = new QVBoxLayout(this);
    summary_ = new QLabel(this);
    summary_->setWordWrap(true);
    summary_->setTextFormat(Qt::RichText);
    layout->addWidget(summary_);

    auto* body = new QHBoxLayout;

    // Sidebar: every appearance control, grouped the way FloorPanel and
    // VisualEffectsPanel group theirs — a checkable/plain QGroupBox per
    // topic — inside a scroll area, so the growing option set costs width
    // discipline rather than the dialog's own height.
    auto* sidebar = new QWidget(this);
    auto* sidebarLayout = new QVBoxLayout(sidebar);
    sidebarLayout->addWidget(buildSheetsSection(sidebar));
    sidebarLayout->addWidget(buildMaterialSection(sidebar));
    sidebarLayout->addWidget(buildColorSection(sidebar));
    sidebarLayout->addWidget(buildZoneSection(sidebar));
    sidebarLayout->addWidget(buildQualitySection(sidebar));
    sidebarLayout->addStretch(1);

    auto* scroll = new QScrollArea(this);
    scroll->setWidget(sidebar);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setMinimumWidth(300);
    scroll->setMaximumWidth(340);
    body->addWidget(scroll);

    canvas_ = new VolumeViewWidget(this);
    // Opaque by default. The canvas blends translucent triangles in buffer
    // order with no depth pre-pass, so below 1.0 a closed sheet shows its own
    // far side through its near one — which reads as a torn mesh rather than
    // as transparency. The old hardcoded 0.92 shipped exactly that look; the
    // opacity control below is what makes it a choice, for the case it is
    // actually wanted (seeing a pocket nested inside another sheet).
    canvas_->setMeshOpacity(1.0f);
    body->addWidget(canvas_, 1);
    layout->addLayout(body, 1);

    auto* bottom = new QHBoxLayout;
    bottom->addStretch(1);
    auto* exportImageButton = new QPushButton(tr("Export Image…"), this);
    exportImageButton->setToolTip(
        tr("Save the 3D view exactly as it is on screen, at the canvas's own "
           "resolution."));
    connect(exportImageButton, &QPushButton::clicked, this,
            &FermiSurfaceWindow::exportImage);
    bottom->addWidget(exportImageButton);

    auto* exportDataButton = new QPushButton(tr("Export Data (.csv)…"), this);
    exportDataButton->setToolTip(
        tr("Write the interpolated grid as CSV: one row per k-point, with its "
           "grid indices, its Cartesian k in 1/Å, and one energy column per "
           "band.\n\n"
           "Ordered with k₃ fastest and a full header, so ParaView rebuilds it "
           "with CSV Reader → Table To Structured Grid, and Mayavi with a "
           "numpy reshape — no reconstruction of the layout by guesswork."));
    connect(exportDataButton, &QPushButton::clicked, this,
            &FermiSurfaceWindow::exportData);
    bottom->addWidget(exportDataButton);
    layout->addLayout(bottom);

    applyStyleToControls();
}

QWidget* FermiSurfaceWindow::buildSheetsSection(QWidget* parent)
{
    auto* group = new QGroupBox(tr("Sheets"), parent);
    auto* outer = new QVBoxLayout(group);

    auto* form = new QFormLayout;
    meshModeCombo_ = new QComboBox(group);
    meshModeCombo_->addItem(tr("Separate by band"),
                            static_cast<int>(FermiSurfaceMeshMode::Separate));
    meshModeCombo_->addItem(tr("Combined"),
                            static_cast<int>(FermiSurfaceMeshMode::Combined));
    meshModeCombo_->setToolTip(
        tr("Separate by band — one surface per crossing band, its own "
           "colour and its own visibility toggle in the list below (the "
           "default: an electron pocket and a hole pocket are different "
           "objects).\n\n"
           "Combined — every band's sheet merged into one surface with one "
           "opacity and material. Extraction is unaffected either way: each "
           "band's sheet still comes from its own eigenvalue grid at E_F — "
           "this only changes what happens to the meshes afterward, and the "
           "band list below still decides which bands contribute."));
    connect(meshModeCombo_, &QComboBox::currentIndexChanged, this, [this](int) {
        if (updating_)
            return;
        style_.meshMode = static_cast<FermiSurfaceMeshMode>(
            meshModeCombo_->currentData().toInt());
        // By band only means something with separate meshes; Single color
        // only with one merged surface — switching mesh mode away from the
        // one that motivated the current color mode falls back to the
        // other's own default. Velocity coloring is mode-independent and
        // left alone either way.
        if (style_.meshMode == FermiSurfaceMeshMode::Combined
            && style_.colorMode == FermiSurfaceColorMode::ByBand)
            style_.colorMode = FermiSurfaceColorMode::SingleColor;
        else if (style_.meshMode == FermiSurfaceMeshMode::Separate
                 && style_.colorMode == FermiSurfaceColorMode::SingleColor)
            style_.colorMode = FermiSurfaceColorMode::ByBand;
        {
            const QSignalBlocker blocker(colorModeCombo_);
            colorModeCombo_->setCurrentIndex(
                colorModeCombo_->findData(static_cast<int>(style_.colorMode)));
        }
        syncControlAvailability();
        rebuild();
    });
    form->addRow(tr("Mesh:"), meshModeCombo_);

    colorModeCombo_ = new QComboBox(group);
    colorModeCombo_->addItem(tr("By band"),
                             static_cast<int>(FermiSurfaceColorMode::ByBand));
    colorModeCombo_->addItem(
        tr("Single color"), static_cast<int>(FermiSurfaceColorMode::SingleColor));
    colorModeCombo_->addItem(
        tr("Fermi velocity |∇E(k)|"),
        static_cast<int>(FermiSurfaceColorMode::ByVelocity));
    colorModeCombo_->setToolTip(
        tr("By band — each sheet its own colour (see Colors below for the "
           "palette).\n\n"
           "Single color — one colour for every triangle drawn.\n\n"
           "Fermi velocity — every point of the surface coloured by "
           "|grad E(k)|, the group-velocity magnitude, through the Colors "
           "colormap. Works with either Mesh setting."));
    connect(colorModeCombo_, &QComboBox::currentIndexChanged, this, [this](int) {
        if (updating_)
            return;
        style_.colorMode = static_cast<FermiSurfaceColorMode>(
            colorModeCombo_->currentData().toInt());
        syncControlAvailability();
        rebuild();
    });
    form->addRow(tr("Color by:"), colorModeCombo_);

    combinedColorButton_ = new QPushButton(group);
    combinedColorButton_->setToolTip(
        tr("The colour every triangle is drawn in, in Single color mode."));
    connect(combinedColorButton_, &QPushButton::clicked, this, [this] {
        const QColor chosen = QColorDialog::getColor(style_.combinedColor,
                                                      this, tr("Surface Color"));
        if (!chosen.isValid())
            return;
        style_.combinedColor = chosen;
        setButtonColor(combinedColorButton_, chosen);
        rebuild();
    });
    form->addRow(tr("Color:"), combinedColorButton_);
    outer->addLayout(form);

    bandList_ = new QListWidget(group);
    // Single selection (not the former NoSelection): a row now needs to be
    // selectABLE, separately from checkABLE, so "Recolor Selected Band…"
    // below has something to act on. The checkbox still drives visibility;
    // selecting a row for recoloring does not toggle it.
    bandList_->setSelectionMode(QAbstractItemView::SingleSelection);
    bandList_->setToolTip(
        tr("One row per band that crosses the target energy. Check a row to "
           "draw its sheet; select one (click its text, not the checkbox) to "
           "recolor it below."));
    connect(bandList_, &QListWidget::itemChanged, this, [this] { rebuild(); });
    outer->addWidget(bandList_);

    recolorBandButton_ =
        new QPushButton(tr("Recolor Selected Band…"), group);
    recolorBandButton_->setToolTip(
        tr("Pick an explicit colour for the band selected above, overriding "
           "its default palette colour. Only applies in By band coloring."));
    connect(recolorBandButton_, &QPushButton::clicked, this,
            &FermiSurfaceWindow::recolorSelectedBand);
    outer->addWidget(recolorBandButton_);

    return group;
}

QWidget* FermiSurfaceWindow::buildMaterialSection(QWidget* parent)
{
    auto* group = new QGroupBox(tr("Material"), parent);
    auto* form = new QFormLayout(group);

    shadingCombo_ = new QComboBox(group);
    shadingCombo_->addItem(tr("Flat (unshaded)"));
    shadingCombo_->addItem(tr("Diffuse"));
    shadingCombo_->addItem(tr("Glossy"));
    shadingCombo_->setToolTip(
        tr("Flat: one uniform colour per fragment, no lighting — the "
           "historical look, and what makes a printed figure reproduce "
           "exactly.\n"
           "Diffuse: two-light Lambert shading with an ambient floor.\n"
           "Glossy: Diffuse plus a Blinn-Phong specular highlight.\n\n"
           "Same three choices, in the same order, as the volumetric-"
           "isosurface viewer's own Shading control."));
    connect(shadingCombo_, &QComboBox::currentIndexChanged, this, [this](int i) {
        if (updating_)
            return;
        style_.shading = static_cast<IsoShading>(i);
        syncControlAvailability();
        rebuild();
    });
    form->addRow(tr("Shading:"), shadingCombo_);

    ambientSpin_ = new QDoubleSpinBox(group);
    ambientSpin_->setRange(0.0, 1.0);
    ambientSpin_->setSingleStep(0.05);
    ambientSpin_->setDecimals(2);
    ambientSpin_->setToolTip(
        tr("Ambient floor: the fraction of the base colour a face turned "
           "fully away from every light keeps. Diffuse and Glossy only."));
    connect(ambientSpin_, &QDoubleSpinBox::valueChanged, this, [this](double v) {
        if (updating_)
            return;
        style_.ambient = v;
        rebuild();
    });
    form->addRow(tr("Ambient:"), ambientSpin_);

    specularSpin_ = new QDoubleSpinBox(group);
    specularSpin_->setRange(0.0, 1.0);
    specularSpin_->setSingleStep(0.05);
    specularSpin_->setDecimals(2);
    specularSpin_->setToolTip(tr("Specular highlight strength. Glossy only."));
    connect(specularSpin_, &QDoubleSpinBox::valueChanged, this, [this](double v) {
        if (updating_)
            return;
        style_.specular = v;
        rebuild();
    });
    form->addRow(tr("Specular:"), specularSpin_);

    opacitySpin_ = new QDoubleSpinBox(group);
    opacitySpin_->setRange(0.05, 1.0);
    opacitySpin_->setSingleStep(0.05);
    opacitySpin_->setDecimals(2);
    opacitySpin_->setKeyboardTracking(false);
    opacitySpin_->setToolTip(
        tr("Sheet opacity. Below 1 the inner sheets of a multi-band surface "
           "become visible through the outer ones, which is the only way to "
           "see a nested pocket at all; at 1 the outermost sheet hides "
           "everything behind it.\n\n"
           "The translucent pass blends in draw order without a depth sort, so "
           "below 1 a closed sheet also shows its own far side through its "
           "near one — the speckled look is that, not a hole in the surface."));
    connect(opacitySpin_, &QDoubleSpinBox::valueChanged, this,
            [this](double alpha) {
                if (updating_)
                    return;
                style_.opacity = alpha;
                canvas_->setMeshOpacity(static_cast<float>(alpha));
            });
    form->addRow(tr("Opacity:"), opacitySpin_);

    wireframeCheck_ = new QCheckBox(tr("Wireframe overlay"), group);
    wireframeCheck_->setToolTip(
        tr("Draw the sheet's own triangle edges on top of the solid fill, "
           "so the mesh resolution is visible — the way a CAD wireframe "
           "view sits over a shaded model."));
    connect(wireframeCheck_, &QCheckBox::toggled, this, [this](bool on) {
        if (updating_)
            return;
        style_.wireframeOverlay = on;
        rebuild();
    });
    form->addRow(QString(), wireframeCheck_);

    return group;
}

QWidget* FermiSurfaceWindow::buildColorSection(QWidget* parent)
{
    auto* group = new QGroupBox(tr("Colors"), parent);
    auto* form = new QFormLayout(group);

    gradientCombo_ = new QComboBox(group);
    // The same short list the other result windows offer, plus Turbo (the
    // default here): separating a handful of sheets (or reading a velocity
    // heatmap) wants colours that are easy to tell apart, not necessarily a
    // perceptually-uniform ramp over a continuum.
    gradientCombo_->addItem(tr("Turbo"),
                            static_cast<int>(render::ColorGradient::Turbo));
    gradientCombo_->addItem(tr("Viridis"),
                            static_cast<int>(render::ColorGradient::Viridis));
    gradientCombo_->addItem(tr("Plasma"),
                            static_cast<int>(render::ColorGradient::Plasma));
    gradientCombo_->addItem(tr("Coolwarm"),
                            static_cast<int>(render::ColorGradient::Coolwarm));
    gradientCombo_->addItem(tr("Spectral"),
                            static_cast<int>(render::ColorGradient::Spectral));
    gradientCombo_->addItem(tr("Greys"),
                            static_cast<int>(render::ColorGradient::Greys));
    gradientCombo_->setToolTip(
        tr("By band: the palette a band's default colour is sampled from "
           "(only bands with no explicit override use it).\n"
           "Fermi velocity: the colormap the |grad E(k)| heatmap is drawn "
           "with.\n\n"
           "Not used in Single color mode."));
    connect(gradientCombo_, &QComboBox::currentIndexChanged, this, [this] {
        if (updating_)
            return;
        style_.bandGradient = static_cast<render::ColorGradient>(
            gradientCombo_->currentData().toInt());
        populateBandList();
        rebuild();
    });
    form->addRow(tr("Colormap:"), gradientCombo_);

    invertGradientCheck_ = new QCheckBox(tr("Invert Colormap Scale"), group);
    invertGradientCheck_->setToolTip(
        tr("Flip the value → color mapping (t → 1 − t), matplotlib's \"_r\" "
           "palettes."));
    connect(invertGradientCheck_, &QCheckBox::toggled, this, [this](bool on) {
        if (updating_)
            return;
        style_.invertGradient = on;
        populateBandList();
        rebuild();
    });
    form->addRow(invertGradientCheck_);

    velocityBoundsCheck_ = new QCheckBox(tr("Custom velocity range"), group);
    velocityBoundsCheck_->setToolTip(
        tr("Off: the colormap spans the checked bands' own |grad E(k)| "
           "range. On: pinned to the values below, with values outside them "
           "clamped to the ramp ends."));
    connect(velocityBoundsCheck_, &QCheckBox::toggled, this, [this](bool on) {
        if (updating_)
            return;
        style_.velocityUseBounds = on;
        syncControlAvailability();
        rebuild();
    });
    form->addRow(velocityBoundsCheck_);

    const auto makeVelocitySpin = [group] {
        auto* spin = new QDoubleSpinBox(group);
        spin->setRange(0.0, 1.0e6);
        spin->setDecimals(4);
        spin->setKeyboardTracking(false);
        return spin;
    };
    velocityMinSpin_ = makeVelocitySpin();
    connect(velocityMinSpin_, &QDoubleSpinBox::valueChanged, this,
            [this](double v) {
                if (updating_)
                    return;
                style_.velocityMin = v;
                rebuild();
            });
    form->addRow(tr("Min:"), velocityMinSpin_);

    velocityMaxSpin_ = makeVelocitySpin();
    connect(velocityMaxSpin_, &QDoubleSpinBox::valueChanged, this,
            [this](double v) {
                if (updating_)
                    return;
                style_.velocityMax = v;
                rebuild();
            });
    form->addRow(tr("Max:"), velocityMaxSpin_);

    return group;
}

QWidget* FermiSurfaceWindow::buildZoneSection(QWidget* parent)
{
    auto* group = new QGroupBox(tr("Brillouin zone"), parent);
    auto* form = new QFormLayout(group);

    clipCheck_ = new QCheckBox(tr("Clip to first Brillouin zone"), group);
    clipCheck_->setToolTip(
        tr("Restrict the sheets to the Wigner-Seitz cell of the reciprocal "
           "lattice.\n\n"
           "The bands were interpolated on a grid spanning the reciprocal "
           "UNIT CELL — a parallelepiped, since that is what a regular grid "
           "fits. It covers the same volume as the zone but is not the same "
           "region, so without clipping the sheets extend past the zone "
           "boundary and fold pieces of the next zone into view."));
    connect(clipCheck_, &QCheckBox::toggled, this, [this](bool on) {
        if (updating_)
            return;
        style_.clipToFirstZone = on;
        rebuild();
    });
    form->addRow(clipCheck_);

    zoneCheck_ = new QCheckBox(tr("Zone edges"), group);
    connect(zoneCheck_, &QCheckBox::toggled, this, [this](bool on) {
        if (updating_)
            return;
        style_.showZoneEdges = on;
        syncControlAvailability();
        rebuild();
    });
    form->addRow(zoneCheck_);

    zoneColorButton_ = new QPushButton(group);
    zoneColorButton_->setToolTip(tr("Zone-edge wireframe colour."));
    connect(zoneColorButton_, &QPushButton::clicked, this, [this] {
        const QColor chosen = QColorDialog::getColor(style_.zoneEdgeColor,
                                                      this, tr("Zone Edge Color"));
        if (!chosen.isValid())
            return;
        style_.zoneEdgeColor = chosen;
        setButtonColor(zoneColorButton_, chosen);
        rebuild();
    });
    form->addRow(tr("Edge color:"), zoneColorButton_);

    zoneWidthSpin_ = new QDoubleSpinBox(group);
    zoneWidthSpin_->setRange(1.0, 8.0);
    zoneWidthSpin_->setSingleStep(0.5);
    zoneWidthSpin_->setDecimals(1);
    zoneWidthSpin_->setToolTip(
        tr("Screen-space pen width the zone edges are drawn with. 1 = thin "
           "lines."));
    connect(zoneWidthSpin_, &QDoubleSpinBox::valueChanged, this,
            [this](double v) {
                if (updating_)
                    return;
                style_.zoneEdgeWidth = v;
                rebuild();
            });
    form->addRow(tr("Edge width:"), zoneWidthSpin_);

    labelsCheck_ = new QCheckBox(tr("Axes"), group);
    connect(labelsCheck_, &QCheckBox::toggled, this, [this](bool on) {
        if (updating_)
            return;
        style_.showAxes = on;
        rebuild();
    });
    form->addRow(labelsCheck_);

    return group;
}

QWidget* FermiSurfaceWindow::buildQualitySection(QWidget* parent)
{
    auto* group = new QGroupBox(tr("Energy / Quality"), parent);
    auto* form = new QFormLayout(group);

    energySpin_ = new QDoubleSpinBox(group);
    energySpin_->setRange(-50.0, 50.0);
    energySpin_->setDecimals(4);
    energySpin_->setSingleStep(0.05);
    energySpin_->setSuffix(tr(" eV"));
    energySpin_->setKeyboardTracking(false);
    energySpin_->setToolTip(
        tr("Energy the isosurface is taken at, relative to E_F.\n\n"
           "Scanning it is a rigid-band doping study: the surface at +0.2 eV "
           "is the one an n-doped sample would have, to the extent the bands "
           "do not themselves move."));
    connect(energySpin_, &QDoubleSpinBox::valueChanged, this, [this](double v) {
        if (updating_)
            return;
        style_.energyOffset = v;
        rebuild();
    });
    form->addRow(tr("Energy:"), energySpin_);

    interpolationCombo_ = new QComboBox(group);
    // Order matches core::GridInterpolation.
    interpolationCombo_->addItem(tr("None (raw grid)"));
    interpolationCombo_->addItem(tr("Trilinear"));
    interpolationCombo_->addItem(tr("Tricubic"));
    interpolationCombo_->setToolTip(
        tr("How the band grid is refined before the isosurface is "
           "extracted — the k-mesh density this viewer can actually "
           "control; the ORIGINAL sampled grid is fixed by the completed "
           "Wannier interpolation and cannot be made finer here.\n\n"
           "Marching cubes reproduces the grid it is handed, so a coarse grid "
           "gives a faceted sheet no amount of shading can hide — the facets "
           "are geometry, not lighting. Refining the field first is what "
           "actually smooths it.\n\n"
           "• Trilinear is cheap and removes the staircase.\n"
           "• Tricubic (Catmull-Rom) also matches the slope across cell "
           "boundaries, so a curved sheet stays curved instead of turning "
           "into flats meeting at angles. It is the one to use for a figure — "
           "and, being an interpolant of the same samples, it invents no "
           "features the grid does not contain."));
    connect(interpolationCombo_, &QComboBox::currentIndexChanged, this,
            [this](int i) {
                if (updating_)
                    return;
                style_.interpolation = static_cast<core::GridInterpolation>(i);
                rebuild();
            });
    form->addRow(tr("Interpolation:"), interpolationCombo_);

    refineSpin_ = new QSpinBox(group);
    refineSpin_->setRange(1, 4);
    refineSpin_->setPrefix(tr("×"));
    refineSpin_->setKeyboardTracking(false);
    refineSpin_->setToolTip(
        tr("Refinement factor per axis. ×2 turns an N³ grid into (2N)³ — eight "
           "times the marching-cubes work, which is why this is capped low. "
           "×2 with Tricubic is usually already smooth."));
    connect(refineSpin_, &QSpinBox::valueChanged, this, [this](int v) {
        if (updating_)
            return;
        style_.refine = v;
        rebuild();
    });
    form->addRow(tr("Refine k-mesh:"), refineSpin_);

    smoothingSpin_ = new QSpinBox(group);
    smoothingSpin_->setRange(0, 20);
    smoothingSpin_->setSuffix(tr(" passes"));
    smoothingSpin_->setKeyboardTracking(false);
    smoothingSpin_->setToolTip(
        tr("Laplacian smoothing passes applied to the extracted mesh — same "
           "control, same core::smoothMesh(), as the volumetric-isosurface "
           "viewer's own Mesh smoothing. Removes stair-steps a coarse k-mesh "
           "leaves that no amount of shading hides; shrinks the surface "
           "slightly at high counts, since it is a smoother and not a "
           "re-extraction."));
    connect(smoothingSpin_, &QSpinBox::valueChanged, this, [this](int v) {
        if (updating_)
            return;
        style_.meshSmoothing = v;
        rebuild();
    });
    form->addRow(tr("Mesh smoothing:"), smoothingSpin_);

    return group;
}

void FermiSurfaceWindow::applyStyleToControls()
{
    updating_ = true;
    meshModeCombo_->setCurrentIndex(
        meshModeCombo_->findData(static_cast<int>(style_.meshMode)));
    colorModeCombo_->setCurrentIndex(
        colorModeCombo_->findData(static_cast<int>(style_.colorMode)));
    setButtonColor(combinedColorButton_, style_.combinedColor);

    shadingCombo_->setCurrentIndex(static_cast<int>(style_.shading));
    ambientSpin_->setValue(style_.ambient);
    specularSpin_->setValue(style_.specular);
    opacitySpin_->setValue(style_.opacity);
    canvas_->setMeshOpacity(static_cast<float>(style_.opacity));
    wireframeCheck_->setChecked(style_.wireframeOverlay);

    gradientCombo_->setCurrentIndex(
        gradientCombo_->findData(static_cast<int>(style_.bandGradient)));
    invertGradientCheck_->setChecked(style_.invertGradient);
    velocityBoundsCheck_->setChecked(style_.velocityUseBounds);
    velocityMinSpin_->setValue(style_.velocityMin);
    velocityMaxSpin_->setValue(style_.velocityMax);

    clipCheck_->setChecked(style_.clipToFirstZone);
    zoneCheck_->setChecked(style_.showZoneEdges);
    setButtonColor(zoneColorButton_, style_.zoneEdgeColor);
    zoneWidthSpin_->setValue(style_.zoneEdgeWidth);
    labelsCheck_->setChecked(style_.showAxes);

    energySpin_->setValue(style_.energyOffset);
    interpolationCombo_->setCurrentIndex(static_cast<int>(style_.interpolation));
    refineSpin_->setValue(style_.refine);
    smoothingSpin_->setValue(style_.meshSmoothing);

    updating_ = false;
    syncControlAvailability();
}

void FermiSurfaceWindow::syncControlAvailability()
{
    const bool separate = style_.meshMode == FermiSurfaceMeshMode::Separate;
    const bool byBand = style_.colorMode == FermiSurfaceColorMode::ByBand;
    const bool singleColor = style_.colorMode == FermiSurfaceColorMode::SingleColor;
    const bool byVelocity = style_.colorMode == FermiSurfaceColorMode::ByVelocity;

    recolorBandButton_->setEnabled(separate && byBand);
    combinedColorButton_->setEnabled(singleColor);
    gradientCombo_->setEnabled(byBand || byVelocity);
    invertGradientCheck_->setEnabled(byBand || byVelocity);
    velocityBoundsCheck_->setEnabled(byVelocity);
    const bool customVelocity = byVelocity && style_.velocityUseBounds;
    velocityMinSpin_->setEnabled(customVelocity);
    velocityMaxSpin_->setEnabled(customVelocity);

    const bool shaded = style_.shading != IsoShading::Flat;
    ambientSpin_->setEnabled(shaded);
    specularSpin_->setEnabled(style_.shading == IsoShading::Glossy);

    const bool zoneOn = zoneCheck_->isChecked();
    zoneColorButton_->setEnabled(zoneOn);
    zoneWidthSpin_->setEnabled(zoneOn);
}

QColor FermiSurfaceWindow::bandColor(int index) const
{
    if (index >= 0 && index < style_.perBandColors.size()
        && style_.perBandColors[index].isValid())
        return style_.perBandColors[index];
    return render::ColorMap::sample(
        style_.bandGradient,
        bands_.size() > 1 ? static_cast<float>(index) / (bands_.size() - 1)
                          : 0.5f,
        style_.invertGradient);
}

void FermiSurfaceWindow::recolorSelectedBand()
{
    QListWidgetItem* item = bandList_->currentItem();
    if (!item) {
        QMessageBox::information(this, windowTitle(),
                                 tr("Select a band in the list first — click "
                                    "its text, not its checkbox."));
        return;
    }
    const int index = item->data(Qt::UserRole).toInt();
    const QColor chosen = QColorDialog::getColor(
        bandColor(index), this, tr("Band %1 Color").arg(index));
    if (!chosen.isValid())
        return;
    if (style_.perBandColors.size() <= index)
        style_.perBandColors.resize(index + 1);
    style_.perBandColors[index] = chosen;
    populateBandList();
    rebuild();
}

std::size_t FermiSurfaceWindow::pointCount() const
{
    return static_cast<std::size_t>(samples_[0])
        * static_cast<std::size_t>(samples_[1])
        * static_cast<std::size_t>(samples_[2]);
}

QString FermiSurfaceWindow::viewStylePath() const
{
    if (sourcePath_.isEmpty())
        return {};
    const QFileInfo info(sourcePath_);
    return info.absolutePath() + QStringLiteral("/fermi_surface_view.json");
}

void FermiSurfaceWindow::closeEvent(QCloseEvent* event)
{
    const QString path = viewStylePath();
    if (!path.isEmpty())
        writeTextFile(this, path,
                     QString::fromUtf8(QJsonDocument(writeFermiSurfaceStyle(style_))
                                           .toJson(QJsonDocument::Compact)));
    QDialog::closeEvent(event);
}

bool FermiSurfaceWindow::loadResults(const QString& jsonPath)
{
    data_ = readJsonObject(jsonPath);
    if (data_.isEmpty())
        return false;
    sourcePath_ = jsonPath;

    fermiEv_ = data_.value(QStringLiteral("fermi_eV")).toDouble();
    // "samples" is a three-element array now. Runs from before the mesh became
    // per-axis wrote a single int, and those results are still on disk — so an
    // int is read as an isotropic grid rather than rejected.
    if (const QJsonValue samples = data_.value(QStringLiteral("samples"));
        samples.isArray()) {
        const QJsonArray array = samples.toArray();
        for (int i = 0; i < 3; ++i) {
            samples_[static_cast<std::size_t>(i)] =
                i < array.size() ? array.at(i).toInt() : 0;
        }
    } else {
        samples_.fill(samples.toInt());
    }

    const QJsonArray recip =
        data_.value(QStringLiteral("reciprocal_2pi_per_A")).toArray();
    for (int i = 0; i < 3 && i < recip.size(); ++i) {
        const QJsonArray row = recip.at(i).toArray();
        if (row.size() >= 3)
            reciprocal_[static_cast<std::size_t>(i)] = {
                row.at(0).toDouble(), row.at(1).toDouble(),
                row.at(2).toDouble()};
    }

    bands_.clear();
    for (const QJsonValue& value : data_.value(QStringLiteral("bands")).toArray()) {
        const QJsonObject entry = value.toObject();
        Band band;
        band.index = entry.value(QStringLiteral("band")).toInt();
        band.minEv = entry.value(QStringLiteral("min_eV")).toDouble();
        band.maxEv = entry.value(QStringLiteral("max_eV")).toDouble();
        band.crosses = entry.value(QStringLiteral("crosses_fermi")).toBool();
        for (const QJsonValue& e :
             entry.value(QStringLiteral("energies_eV")).toArray())
            band.energies.push_back(e.toDouble());
        bands_.push_back(std::move(band));
    }
    if (*std::min_element(samples_.begin(), samples_.end()) < 2
        || bands_.empty() || pointCount() == 0) {
        return false;
    }

    // Appearance persists in a sidecar next to fermi_surface.json — a
    // results viewer has no workspace tab of its own to keep VolumetricStyle-
    // style live state alive between reopenings, so the project needs to
    // remember it here instead. Freshly defaulted (not merged) when the file
    // is absent or unreadable, same as VolumetricStyle's own construction.
    style_ = FermiSurfaceStyle{};
    style_.energyOffset =
        data_.value(QStringLiteral("energy_offset_eV")).toDouble();
    const QString stylePath = viewStylePath();
    if (!stylePath.isEmpty() && QFile::exists(stylePath)) {
        QFile file(stylePath);
        if (file.open(QIODevice::ReadOnly)) {
            QJsonParseError error{};
            const QJsonDocument doc =
                QJsonDocument::fromJson(file.readAll(), &error);
            if (error.error == QJsonParseError::NoError && doc.isObject())
                style_ = readFermiSurfaceStyle(doc.object());
        }
    }

    const int crossing =
        data_.value(QStringLiteral("crossing_bands")).toArray().size();
    summary_->setText(
        tr("<b>%1</b> · %2 × %3 × %4 interpolated k-points over %5 Wannier "
           "bands · E<sub>F</sub> = %6 eV · <b>%7</b> band(s) cross it")
            .arg(data_.value(QStringLiteral("formula")).toString())
            .arg(samples_[0])
            .arg(samples_[1])
            .arg(samples_[2])
            .arg(data_.value(QStringLiteral("nwannier")).toInt())
            .arg(fermiEv_, 0, 'f', 4)
            .arg(crossing));

    applyStyleToControls();
    populateBandList();
    rebuild();
    return true;
}

void FermiSurfaceWindow::populateBandList()
{
    const QSignalBlocker blocker(bandList_);
    bandList_->clear();
    for (std::size_t i = 0; i < bands_.size(); ++i) {
        const Band& band = bands_[i];
        auto* item = new QListWidgetItem(
            band.crosses
                ? tr("Band %1 — crosses E_F").arg(band.index)
                : tr("Band %1 — no sheet").arg(band.index),
            bandList_);
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
        // Bands that never reach the target energy cannot produce a sheet, so
        // they are listed (to say they were considered) but not checked.
        item->setCheckState(band.crosses ? Qt::Checked : Qt::Unchecked);
        item->setToolTip(tr("%1 … %2 eV")
                             .arg(band.minEv, 0, 'f', 3)
                             .arg(band.maxEv, 0, 'f', 3));
        item->setData(Qt::UserRole, static_cast<int>(i));
        // The swatch is the sheet's own colour, so the list doubles as the
        // legend — without it, telling which sheet is band 3 means toggling
        // them off one at a time.
        QPixmap swatch(12, 12);
        swatch.fill(bandColor(static_cast<int>(i)));
        item->setIcon(QIcon(swatch));
        if (!band.crosses)
            item->setForeground(palette().color(QPalette::PlaceholderText));
    }
}

void FermiSurfaceWindow::rebuild()
{
    if (!canvas_ || bands_.empty())
        return;
    const double target = fermiEv_ + style_.energyOffset;
    const bool clip = style_.clipToFirstZone;

    // The grid spans one reciprocal cell centred on Γ, sampled without its
    // upper endpoint — exactly how the generator laid it out, so the box has
    // to be described the same way here or the surface lands off-centre.
    core::VolumetricData field;
    field.nx = samples_[0];
    field.ny = samples_[1];
    field.nz = samples_[2];
    field.spanA = reciprocal_[0];
    field.spanB = reciprocal_[1];
    field.spanC = reciprocal_[2];
    field.origin = (reciprocal_[0] + reciprocal_[1] + reciprocal_[2]) * -0.5;

    // Refinement scheme + factor. Applied to the FIELD, before extraction:
    // marching cubes reproduces whatever grid it is handed, so smoothing the
    // triangles afterwards would move the surface off the data, while
    // interpolating the samples keeps it on an interpolant of them.
    const auto scheme = style_.interpolation;
    const int refine = std::max(1, style_.refine);

    struct BandMesh {
        int index = 0;
        core::IsoMesh mesh;
    };
    std::vector<BandMesh> meshes;
    for (int row = 0; row < bandList_->count(); ++row) {
        if (bandList_->item(row)->checkState() != Qt::Checked)
            continue;
        const int index = bandList_->item(row)->data(Qt::UserRole).toInt();
        if (index < 0 || index >= static_cast<int>(bands_.size()))
            continue;
        const Band& band = bands_[static_cast<std::size_t>(index)];
        if (band.energies.size() != pointCount())
            continue;
        field.values = band.energies;
        const core::VolumetricData refined =
            core::refineGrid(field, refine, scheme);
        const core::IsoMesh iso = core::extractIsosurface(refined, target);
        if (iso.positions.empty())
            continue;

        // The sheet is extracted on the reciprocal-cell PARALLELEPIPED, not
        // the Wigner-Seitz cell it is conventionally drawn in — the two are
        // the same volume but a different shape, so clipping a single
        // un-replicated copy to the zone would just delete the corners that
        // reach past the parallelepiped's own faces instead of filling them
        // in. clipToWignerSeitzCell() replicates the periodic images that are
        // actually needed (read off the zone's own vertices) before clipping,
        // which is what recovers them — including gradientMagnitude, needed
        // for velocity coloring below. A degenerate cell falls back to the
        // raw sheet, flattened the same way, so a bad basis loses the zone
        // shape but not the sheet.
        core::IsoMesh drawMesh = [&]() -> core::IsoMesh {
            if (!clip)
                return core::flattenTriangleNormals(iso);
            try {
                return core::clipToWignerSeitzCell(iso, reciprocal_);
            } catch (const std::exception&) {
                return core::flattenTriangleNormals(iso);
            }
        }();
        if (drawMesh.positions.empty())
            continue;
        if (style_.meshSmoothing > 0)
            core::smoothMesh(drawMesh, style_.meshSmoothing);
        meshes.push_back({index, std::move(drawMesh)});
    }

    // Velocity-coloring range: the checked bands' own |grad E(k)| extent,
    // unless pinned — same auto-vs-custom idiom as the volumetric viewer's
    // potential-map bounds. Computed once here rather than per vertex below.
    double veloMin = style_.velocityMin;
    double veloMax = style_.velocityMax;
    if (style_.colorMode == FermiSurfaceColorMode::ByVelocity
        && !style_.velocityUseBounds) {
        veloMin = std::numeric_limits<double>::max();
        veloMax = std::numeric_limits<double>::lowest();
        for (const BandMesh& bm : meshes)
            for (const double g : bm.mesh.gradientMagnitude) {
                veloMin = std::min(veloMin, g);
                veloMax = std::max(veloMax, g);
            }
        if (!(veloMin < veloMax)) {
            veloMin = 0.0;
            veloMax = 1.0;
        }
    }
    const double veloRange = std::max(veloMax - veloMin, 1e-30);

    std::vector<float> mesh;
    std::vector<float> wireMesh;
    int drawn = 0;
    for (const BandMesh& bm : meshes) {
        ++drawn;
        const QColor bandCol = bandColor(bm.index);
        const bool hasVelocity =
            bm.mesh.gradientMagnitude.size() == bm.mesh.positions.size();
        for (std::size_t t = 0; t + 2 < bm.mesh.positions.size(); t += 3) {
            for (std::size_t v = 0; v < 3; ++v) {
                const std::size_t i = t + v;
                QColor color = bandCol;
                if (style_.colorMode == FermiSurfaceColorMode::SingleColor) {
                    color = style_.combinedColor;
                } else if (style_.colorMode == FermiSurfaceColorMode::ByVelocity
                           && hasVelocity) {
                    const double tNorm = std::clamp(
                        (bm.mesh.gradientMagnitude[i] - veloMin) / veloRange,
                        0.0, 1.0);
                    color = render::ColorMap::sample(
                        style_.bandGradient, static_cast<float>(tNorm),
                        style_.invertGradient);
                }
                const QVector3D pos = toQVector3D(bm.mesh.positions[i]);
                const QVector3D normal = toQVector3D(bm.mesh.normals[i]);
                pushVertex(mesh, pos, normal, color);
                if (style_.wireframeOverlay)
                    pushVertex(wireMesh, pos, normal, color.darker(160));
            }
        }
    }
    canvas_->setMesh(std::move(mesh));
    canvas_->setIsoMaterial(static_cast<int>(style_.shading),
                            static_cast<float>(style_.ambient),
                            static_cast<float>(style_.specular));
    if (style_.wireframeOverlay)
        canvas_->setWireframeOverlay(std::move(wireMesh));
    else
        canvas_->clearWireframeOverlay();

    // --- Zone wireframe (width-adjustable overlay) and axes (thin GL lines) -
    std::vector<float> lines;
    const auto line = [&lines](const QVector3D& a, const QVector3D& b,
                               const QColor& color) {
        pushVertex(lines, a, QVector3D(0, 0, 1), color);
        pushVertex(lines, b, QVector3D(0, 0, 1), color);
    };
    double reach = 0.0;
    for (const core::Vec3& b : reciprocal_)
        reach = std::max(reach, 0.5 * b.norm());

    std::vector<VolumeViewWidget::OverlayLine> zoneLines;
    std::vector<VolumeViewWidget::Label> labels;
    if (style_.showZoneEdges) {
        try {
            // The zone is built from the reciprocal vectors as a real cell:
            // computeBrillouinZone takes a UnitCell and returns the
            // Wigner-Seitz cell of ITS reciprocal, so handing it the direct
            // cell gives the zone the sheets live in.
            const QJsonArray cellArray =
                data_.value(QStringLiteral("cell_A")).toArray();
            std::array<core::Vec3, 3> vectors{};
            for (int i = 0; i < 3 && i < cellArray.size(); ++i) {
                const QJsonArray row = cellArray.at(i).toArray();
                if (row.size() >= 3)
                    vectors[static_cast<std::size_t>(i)] = {
                        row.at(0).toDouble(), row.at(1).toDouble(),
                        row.at(2).toDouble()};
            }
            const core::UnitCell cell(vectors[0], vectors[1], vectors[2]);
            const core::BrillouinZoneData zone = core::computeBrillouinZone(cell);
            for (const auto& face : zone.faces) {
                for (std::size_t i = 0; i < face.size(); ++i) {
                    const core::Vec3& a = zone.vertices[static_cast<std::size_t>(
                        face[i])];
                    const core::Vec3& b = zone.vertices[static_cast<std::size_t>(
                        face[(i + 1) % face.size()])];
                    zoneLines.push_back({toQVector3D(a), toQVector3D(b)});
                }
            }
            for (const core::Vec3& v : zone.vertices)
                reach = std::max(reach, v.norm());
        } catch (const std::exception&) {
            // A degenerate cell has no zone to draw; the sheets still do.
        }
    }
    canvas_->setOverlayLines(std::move(zoneLines), style_.zoneEdgeColor,
                             static_cast<float>(style_.zoneEdgeWidth));

    if (style_.showAxes) {
        const auto axis = static_cast<float>(reach * 1.15);
        const QColor axisColor(150, 152, 160);
        line({-axis, 0, 0}, {axis, 0, 0}, axisColor);
        line({0, -axis, 0}, {0, axis, 0}, axisColor);
        line({0, 0, -axis}, {0, 0, axis}, axisColor);
        labels.push_back({{axis, 0, 0}, QStringLiteral("k_x"), axisColor});
        labels.push_back({{0, axis, 0}, QStringLiteral("k_y"), axisColor});
        labels.push_back({{0, 0, axis}, QStringLiteral("k_z"), axisColor});
        labels.push_back({{0, 0, 0}, QStringLiteral("Γ"),
                          QColor(255, 214, 120)});
    }
    canvas_->setLines(std::move(lines));
    canvas_->setLabels(std::move(labels));
    canvas_->setBounds(QVector3D(0, 0, 0),
                       static_cast<float>(std::max(reach, 1e-3) * 1.4));

    if (drawn == 0)
        summary_->setToolTip(
            tr("No sheet at this energy — no band's range contains it."));
}

void FermiSurfaceWindow::exportData()
{
    if (bands_.empty() || pointCount() == 0) {
        QMessageBox::information(this, windowTitle(),
                                 tr("There is no grid loaded to export."));
        return;
    }
    const QString path = QFileDialog::getSaveFileName(
        this, tr("Export Fermi surface data"),
        QStringLiteral("fermi_surface.csv"),
        tr("CSV table (*.csv);;All files (*)"));
    if (path.isEmpty())
        return;

    if (!writeCsv(path)) {
        QMessageBox::warning(this, windowTitle(),
                             tr("Could not write %1").arg(path));
        return;
    }

    const std::array<int, 3> n = samples_;
    // The extent is what ParaView asks for by hand, and it is not in the file
    // — so it is stated here rather than left to be counted off the rows.
    QMessageBox::information(
        this, windowTitle(),
        tr("Wrote %1 — %2 rows over %3 band(s).\n\n"
           "In ParaView: CSV Reader → Table To Structured Grid, with Whole "
           "Extent 0 %4  0 %5  0 %6 and kx/ky/kz as the point coordinates.\n\n"
           "In Mayavi/numpy: loadtxt, then reshape a band column to "
           "(%7, %8, %9).")
            .arg(QFileInfo(path).fileName())
            .arg(pointCount())
            .arg(bands_.size())
            .arg(n[0] - 1)
            .arg(n[1] - 1)
            .arg(n[2] - 1)
            .arg(n[0])
            .arg(n[1])
            .arg(n[2]));
}

bool FermiSurfaceWindow::writeCsv(const QString& path) const
{
    if (bands_.empty() || pointCount() == 0)
        return false;
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text))
        return false;
    QTextStream out(&file);

    // Wide format: one row per k-point, one energy column per band.
    //
    // That shape is what makes it reconstructable without the reader knowing
    // anything about this application. In ParaView it is CSV Reader → Table To
    // Structured Grid with whole extent (0..nx-1, 0..ny-1, 0..nz-1) and the
    // kx/ky/kz columns as the point coordinates; in Mayavi/numpy it is one
    // loadtxt and a reshape. A long ("tidy") table with a band column would be
    // smaller to write and would need pivoting first in both.
    //
    // The index columns are written explicitly rather than left implicit in the
    // row order: they are what lets a reader verify the ordering instead of
    // trusting a convention it cannot see.
    out << "i,j,k,kx_1_per_A,ky_1_per_A,kz_1_per_A";
    for (const Band& band : bands_)
        out << ",band_" << band.index << "_eV";
    out << "\n";

    // Same box the sheets are extracted in: Γ-centred, spanning one reciprocal
    // cell, upper endpoint excluded (−1/2 … 1/2 in fractional coordinates).
    const core::Vec3 origin =
        (reciprocal_[0] + reciprocal_[1] + reciprocal_[2]) * -0.5;
    const std::array<int, 3> n = samples_;

    // k fastest, matching the row-major (i, j, k) storage the bands arrive in —
    // so the CSV row order IS the array order and a reshape needs no transpose.
    for (int i = 0; i < n[0]; ++i) {
        for (int j = 0; j < n[1]; ++j) {
            for (int k = 0; k < n[2]; ++k) {
                const core::Vec3 kvec = origin
                    + reciprocal_[0] * (static_cast<double>(i) / n[0])
                    + reciprocal_[1] * (static_cast<double>(j) / n[1])
                    + reciprocal_[2] * (static_cast<double>(k) / n[2]);
                const std::size_t flat =
                    (static_cast<std::size_t>(i) * n[1] + j) * n[2] + k;
                out << i << ',' << j << ',' << k << ','
                    << QString::number(kvec.x, 'g', 8) << ','
                    << QString::number(kvec.y, 'g', 8) << ','
                    << QString::number(kvec.z, 'g', 8);
                for (const Band& band : bands_) {
                    out << ',';
                    // A band whose array is the wrong length is written as an
                    // empty field rather than silently shifting every column
                    // after it.
                    if (flat < band.energies.size())
                        out << QString::number(band.energies[flat], 'g', 10);
                }
                out << "\n";
            }
        }
    }

    out.flush();
    return file.commit();
}

void FermiSurfaceWindow::exportImage()
{
    const QString path = QFileDialog::getSaveFileName(
        this, tr("Export Fermi surface"), QStringLiteral("fermi_surface.png"),
        tr("PNG image (*.png)"));
    if (path.isEmpty())
        return;
    if (!canvas_->grabFramebuffer().save(path))
        QMessageBox::warning(this, windowTitle(),
                             tr("Could not write %1").arg(path));
}

} // namespace calango::gui
