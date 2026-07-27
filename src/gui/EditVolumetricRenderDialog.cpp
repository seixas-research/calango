#include "gui/EditVolumetricRenderDialog.hpp"

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
    modeCombo_->addItems({tr("Isosurfaces"), tr("Color Slice")});
    modeCombo_->setCurrentIndex(static_cast<int>(mode));
    modeRow->addRow(tr("Render Mode:"), modeCombo_);
    layout->addLayout(modeRow);

    stack_ = new QStackedWidget(this);
    stack_->addWidget(buildIsosurfacePage());  // index 0 = Isosurface
    stack_->addWidget(buildColorSlicePage());  // index 1 = ColorSlice
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

    // -- Potential-map colouring -------------------------------------------
    // The same surface, coloured by a second field instead of by phase. It
    // used to be a third render mode, which meant re-choosing the base field
    // that was already selected and a duplicate set of isosurface controls;
    // as a checkbox here it is what it actually is — an option on this
    // surface.
    auto* potentialGroup = new QGroupBox(tr("Potential Map Color"), page);
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
