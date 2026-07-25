#include "gui/RepresentationPanel.hpp"
#include "gui/GuiUtils.hpp"

#include "gui/ElementSettingsDialog.hpp"
#include "gui/GuiUtils.hpp"
#include "gui/ViewportWidget.hpp"

#include <QColorDialog>
#include <QComboBox>
#include <QFrame>
#include <QPushButton>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QSignalBlocker>
#include <QStandardItemModel>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>
#include <functional>

namespace calango::gui {

namespace {

} // namespace

RepresentationPanel::RepresentationPanel(ViewportWidget* viewport, QWidget* parent)
    : QWidget(parent)
    , viewport_(viewport)
{
    auto* form = new QFormLayout(this);

    // --- Style (surface material) ------------------------------------------
    // First control in the panel: the material decides how everything below
    // it reads on screen, and it is the setting most often changed when
    // preparing a figure. Applies to every lit mesh (atom spheres, bond
    // cylinders, cell tubes) so a figure reads as one material.
    surfaceFinishCombo_ = new QComboBox(this);
    // Order matches render::SurfaceFinish.
    surfaceFinishCombo_->addItem(tr("Standard"));
    surfaceFinishCombo_->addItem(tr("Shiny"));
    surfaceFinishCombo_->addItem(tr("Matte"));
    surfaceFinishCombo_->addItem(tr("Glassy"));
    surfaceFinishCombo_->setToolTip(
        tr("Standard: Blinn-Phong with moderate highlights.\n"
           "Shiny: polished — strong, small, crisp highlights (low "
           "roughness).\n"
           "Matte (fosco): diffuse only — best for print figures, where "
           "highlights read as artifacts.\n"
           "Glassy: alpha-blended with a Fresnel rim, so inner atoms stay "
           "visible through outer shells."));
    form->addRow(tr("Style:"), surfaceFinishCombo_);
    connect(surfaceFinishCombo_, &QComboBox::currentIndexChanged, this,
            [this](int index) {
                viewport_->style().surfaceFinish =
                    static_cast<render::SurfaceFinish>(index);
                // Geometry is unchanged — only the shading uniforms — so this
                // is a repaint, not an instance-buffer rebuild.
                viewport_->styleChanged(false);
            });

    modeCombo_ = new QComboBox(this);
    modeCombo_->addItems({tr("Ball-and-Stick"), tr("Space-filling (CPK)"),
                          tr("Wireframe"), tr("Polyhedral")});
    form->addRow(tr("Mode:"), modeCombo_);
    connect(modeCombo_, &QComboBox::currentIndexChanged, this, [this](int index) {
        viewport_->setRepresentation(static_cast<render::RepresentationMode>(index));
    });

    // --- Atom coloring -----------------------------------------------------
    colorModeCombo_ = new QComboBox(this);
    colorModeCombo_->addItems({tr("Element (CPK)"),
                               tr("Coordination number (CN)"),
                               tr("Generalized CN (GCN)"),
                               tr("Custom property")});
    form->addRow(tr("Color by:"), colorModeCombo_);

    // Per-element colors and radii are what "Color by: Element (CPK)" maps,
    // so the editor for them belongs immediately under that selector.
    auto* elementsButton = new QPushButton(tr("Element Settings…"), this);
    form->addRow(elementsButton);
    connect(elementsButton, &QPushButton::clicked, this, [this] {
        ElementSettingsDialog dialog(viewport_, this);
        dialog.exec();
    });
    // Bond perception, orders and hydrogen bonds all live in the Bond Editor;
    // this panel just needs the door to it, next to the other "what is drawn"
    // editor rather than buried in the Edit menu.
    auto* bondEditorButton = new QPushButton(tr("Bond Editor…"), this);
    bondEditorButton->setToolTip(
        tr("Bond perception, manual bonds, bond order (single / double / "
           "triple / aromatic) and hydrogen-bond detection."));
    form->addRow(bondEditorButton);
    connect(bondEditorButton, &QPushButton::clicked, this,
            &RepresentationPanel::bondEditorRequested);

    connect(colorModeCombo_, &QComboBox::currentIndexChanged,
            this, &RepresentationPanel::applyColorMode);

    gradientCombo_ = new QComboBox(this);
    // Same order as render::ColorGradient.
    gradientCombo_->addItems({tr("Viridis"), tr("Plasma"), tr("Turbo"),
                              tr("Inferno"), tr("Magma"), tr("Cividis"),
                              tr("Hot"), tr("Afmhot"), tr("Coolwarm"),
                              tr("Rainbow"), tr("Greys"), tr("Spectral"),
                              tr("Gnuplot")});
    form->addRow(tr("Gradient:"), gradientCombo_);
    connect(gradientCombo_, &QComboBox::currentIndexChanged, this, [this](int index) {
        viewport_->setColorGradient(static_cast<render::ColorGradient>(index));
    });

    invertGradientCheck_ = new QCheckBox(tr("Invert palette"), this);
    invertGradientCheck_->setToolTip(tr("Reverse the scalar-to-color mapping: "
                                        "minimum values take the high end of the\n"
                                        "gradient and maximum values the low end "
                                        "(matplotlib \"_r\" palettes)"));
    form->addRow(invertGradientCheck_);
    connect(invertGradientCheck_, &QCheckBox::toggled, this, [this](bool on) {
        viewport_->setGradientInverted(on);
    });

    propertyCombo_ = new QComboBox(this);
    propertyCombo_->setToolTip(tr("Per-atom scalar fields of the current structure\n"
                                  "(charges, |forces|, extxyz columns, ...)"));
    form->addRow(tr("Property:"), propertyCombo_);
    connect(propertyCombo_, &QComboBox::currentIndexChanged,
            this, &RepresentationPanel::applyColorMode);

    // --- Color range bounds ------------------------------------------------
    // Editable Min/Max rather than a read-only legend: auto-scaling
    // renormalizes the ramp to whatever the current frame/structure happens to
    // contain, which makes two figures on the same property incomparable.
    // Typing bounds pins the scale so the same color means the same value
    // across frames, structures and exported figures.
    auto* rangeRow = new QWidget(this);
    auto* rangeLayout = new QHBoxLayout(rangeRow);
    rangeLayout->setContentsMargins(0, 0, 0, 0);
    rangeLayout->setSpacing(4);
    const auto makeBoundSpin = [rangeRow] {
        // Compact rendering: property ranges span everything from 1e-5 μB to
        // 1e3 eV/Å, and a fixed-decimal spin box either overflows the field
        // ("0.000012345") or shows a misleading "0.000". CompactDoubleSpinBox
        // formats to 3 significant figures, switching to exponential when the
        // magnitude needs it.
        auto* spin = new CompactDoubleSpinBox(rangeRow);
        spin->setRange(-1.0e9, 1.0e9);
        spin->setKeyboardTracking(false); // apply on commit, not per keystroke
        return spin;
    };
    rangeLayout->addWidget(new QLabel(tr("Min"), rangeRow));
    rangeMinSpin_ = makeBoundSpin();
    rangeLayout->addWidget(rangeMinSpin_, 1);
    rangeLayout->addWidget(new QLabel(tr("Max"), rangeRow));
    rangeMaxSpin_ = makeBoundSpin();
    rangeLayout->addWidget(rangeMaxSpin_, 1);
    form->addRow(tr("Range:"), rangeRow);

    autoRangeCheck_ = new QCheckBox(tr("Auto-scale to data"), this);
    autoRangeCheck_->setChecked(true);
    autoRangeCheck_->setToolTip(
        tr("On: the color ramp spans the property's own minimum and maximum in "
           "the current structure, and the fields above track it.\n"
           "Off: the ramp is pinned to the Min/Max you type — values beyond "
           "them clamp to the ramp ends — so several structures or frames can "
           "be compared on one fixed scale."));
    form->addRow(autoRangeCheck_);

    // Editing a bound is itself the intent to override, so it switches off
    // auto-scaling rather than being silently discarded on the next refresh.
    const auto applyCustomRange = [this] {
        if (syncingRange_)
            return;
        if (autoRangeCheck_->isChecked()) {
            const QSignalBlocker blocker(autoRangeCheck_);
            autoRangeCheck_->setChecked(false);
        }
        applyColorRange();
    };
    connect(rangeMinSpin_, &QDoubleSpinBox::valueChanged, this, applyCustomRange);
    connect(rangeMaxSpin_, &QDoubleSpinBox::valueChanged, this, applyCustomRange);
    // Re-syncing covers both directions: switching auto back on refills the
    // fields from the data, and it applies the window either way.
    connect(autoRangeCheck_, &QCheckBox::toggled, this,
            &RepresentationPanel::syncColoringFromViewport);

    connect(viewport_, &ViewportWidget::structureReplaced,
            this, &RepresentationPanel::refreshPropertyList);
    connect(viewport_, &ViewportWidget::colorMappingChanged,
            this, &RepresentationPanel::syncColoringFromViewport);

    // --- Scales ------------------------------------------------------------
    // Slider for coarse adjustment + spinbox for exact typed values,
    // bidirectionally synced (both drive the same style factor).
    const auto makeScaleRow = [this](QSlider*& slider, QDoubleSpinBox*& spin,
                                     const std::function<void(float)>& apply) {
        auto* row = new QWidget(this);
        auto* rowLayout = new QHBoxLayout(row);
        rowLayout->setContentsMargins(0, 0, 0, 0);
        slider = new QSlider(Qt::Horizontal, row);
        slider->setRange(20, 300); // percent
        slider->setValue(100);
        spin = new QDoubleSpinBox(row);
        spin->setRange(0.20, 3.00);
        spin->setDecimals(2);
        spin->setSingleStep(0.05);
        spin->setValue(1.00);
        spin->setSuffix(QStringLiteral("×"));
        rowLayout->addWidget(slider, 1);
        rowLayout->addWidget(spin);

        connect(slider, &QSlider::valueChanged, this, [spin, apply](int percent) {
            const float factor = static_cast<float>(percent) / 100.0f;
            {
                const QSignalBlocker blocker(spin);
                spin->setValue(factor);
            }
            apply(factor);
        });
        connect(spin, &QDoubleSpinBox::valueChanged, this,
                [slider = slider, apply](double factor) {
                    const QSignalBlocker blocker(slider);
                    slider->setValue(static_cast<int>(std::lround(factor * 100.0)));
                    apply(static_cast<float>(factor));
                });
        return row;
    };

    form->addRow(tr("Atom radius:"),
                 makeScaleRow(atomScaleSlider_, atomScaleSpin_, [this](float factor) {
                     viewport_->style().atomScaleFactor = factor;
                     viewport_->styleChanged(true);
                 }));
    form->addRow(tr("Bond width:"),
                 makeScaleRow(bondWidthSlider_, bondWidthSpin_, [this](float factor) {
                     viewport_->style().bondWidthFactor = factor;
                     viewport_->styleChanged(true);
                 }));

    // Bond order used to live here as three buttons acting on the viewport
    // selection. It moved into the Bond Editor's "By Atomic Indices" tab,
    // which already owns per-pair bond edits and can also express Aromatic —
    // one place to assign a pair's chemistry instead of two.

    gradientBondsCheck_ = new QCheckBox(tr("Gradient bond coloring"), this);
    gradientBondsCheck_->setChecked(viewport_->style().gradientBonds);
    gradientBondsCheck_->setToolTip(tr("Blend each bond smoothly from one atom's "
                                       "color to the other's\ninstead of the classic "
                                       "half-and-half split"));
    form->addRow(gradientBondsCheck_);
    connect(gradientBondsCheck_, &QCheckBox::toggled, this, [this](bool on) {
        viewport_->style().gradientBonds = on;
        viewport_->styleChanged(true);
    });

    // --- Per-atom vector overlay -------------------------------------------
    // Rendered inline in the main panel (preceded by a horizontal separator)
    // rather than tucked inside a collapsible accordion.
    auto* vectorSeparator = new QFrame(this);
    vectorSeparator->setFrameShape(QFrame::HLine);
    vectorSeparator->setFrameShadow(QFrame::Sunken);
    form->addRow(vectorSeparator);
    // One selector rather than a checkbox per property: the arrows share a
    // single scale and would overlap illegibly if two were drawn at once, and
    // the list grows naturally as extended-XYZ files carry more columns.
    vectorOverlayCombo_ = new QComboBox(this);
    // Order matches render::VectorOverlay.
    vectorOverlayCombo_->addItem(tr("None"));
    vectorOverlayCombo_->addItem(tr("Velocity"));
    vectorOverlayCombo_->addItem(tr("Force"));
    vectorOverlayCombo_->addItem(tr("Magnetic moment"));
    form->addRow(tr("Vector overlay:"), vectorOverlayCombo_);
    connect(vectorOverlayCombo_, &QComboBox::currentIndexChanged, this,
            [this](int index) {
                viewport_->style().vectorOverlay =
                    static_cast<render::VectorOverlay>(index);
                syncVectorColorButton();
                viewport_->styleChanged(true);
            });

    // Arrow scale: slider (coarse) + spinbox (exact), Å per field unit.
    auto* vectorRow = new QWidget(this);
    auto* vectorLayout = new QHBoxLayout(vectorRow);
    vectorLayout->setContentsMargins(0, 0, 0, 0);
    vectorScaleSlider_ = new QSlider(Qt::Horizontal, vectorRow);
    vectorScaleSlider_->setRange(10, 1000); // ×0.1 .. ×10.0 in hundredths
    vectorScaleSlider_->setValue(100);      // ×1.0 = the calibrated baseline
    vectorScaleSpin_ = new QDoubleSpinBox(vectorRow);
    vectorScaleSpin_->setRange(0.1, 10.0);
    vectorScaleSpin_->setDecimals(2);
    vectorScaleSpin_->setSingleStep(0.1);
    vectorScaleSpin_->setValue(1.00);
    vectorScaleSpin_->setSuffix(QStringLiteral("×"));
    vectorScaleSpin_->setToolTip(
        tr("Arrow length relative to the calibrated baseline (1.0×), which is "
           "half an Å of arrow per field unit\n"
           "(eV/Å for forces, Å/fs·√(amu) for velocities, μB for magnetic "
           "moments). Velocities keep an extra 20× so they stay visible."));
    vectorLayout->addWidget(vectorScaleSlider_, 1);
    vectorLayout->addWidget(vectorScaleSpin_);
    form->addRow(tr("Vector scale:"), vectorRow);

    // One picker that edits whichever overlay is selected: each property
    // keeps its own color (so switching Force -> Velocity restores that
    // property's color rather than carrying one over), and the button always
    // shows the color currently in use.
    vectorColorButton_ = new QPushButton(this);
    vectorColorButton_->setToolTip(
        tr("Arrow color for the selected vector overlay. Each property "
           "(velocity, force, magnetic moment) remembers its own color."));
    form->addRow(tr("Vector color:"), vectorColorButton_);
    connect(vectorColorButton_, &QPushButton::clicked, this, [this] {
        QColor* target = vectorOverlayColor();
        if (!target)
            return;
        const QColor chosen = QColorDialog::getColor(
            *target, this, tr("Vector Overlay Color"));
        if (!chosen.isValid())
            return;
        *target = chosen;
        setButtonColor(vectorColorButton_, chosen);
        viewport_->styleChanged(true); // arrow colors live in the instance buffer
    });
    syncVectorColorButton(); // seed the swatch for the initial selection

    connect(vectorScaleSlider_, &QSlider::valueChanged, this, [this](int hundredths) {
        const float factor = static_cast<float>(hundredths) / 100.0f;
        {
            const QSignalBlocker blocker(vectorScaleSpin_);
            vectorScaleSpin_->setValue(factor);
        }
        viewport_->style().vectorScale = factor;
        viewport_->styleChanged(true);
    });
    connect(vectorScaleSpin_, &QDoubleSpinBox::valueChanged, this, [this](double factor) {
        {
            const QSignalBlocker blocker(vectorScaleSlider_);
            vectorScaleSlider_->setValue(static_cast<int>(std::lround(factor * 100.0)));
        }
        viewport_->style().vectorScale = static_cast<float>(factor);
        viewport_->styleChanged(true);
    });


    auto* backgroundButton = new QPushButton(this);
    backgroundButton->setFixedHeight(22);
    setButtonColor(backgroundButton, viewport_->backgroundColor());
    // Background is the first control in the panel: it frames how every atom,
    // bond and overlay colour below reads, so it belongs at the very top even
    // though it is constructed last (insertRow(0) places it there regardless).
    form->insertRow(0, tr("Background:"), backgroundButton);
    connect(backgroundButton, &QPushButton::clicked, this, [this, backgroundButton] {
        const QColor chosen = QColorDialog::getColor(
            viewport_->backgroundColor(), this, tr("Viewport Background Color"));
        if (!chosen.isValid())
            return;
        setButtonColor(backgroundButton, chosen);
        viewport_->setBackgroundColor(chosen);
    });

    refreshPropertyList();
    syncColoringFromViewport();
}

void RepresentationPanel::applyColorMode()
{
    const auto mode = static_cast<render::ColorMode>(colorModeCombo_->currentIndex());
    const bool custom = mode == render::ColorMode::CustomScalar;
    viewport_->setColorMode(mode, custom ? propertyCombo_->currentText() : QString());
}

void RepresentationPanel::refreshPropertyList()
{
    const QSignalBlocker blocker(propertyCombo_);
    const QString previous = propertyCombo_->currentText();
    propertyCombo_->clear();
    const core::Structure* structure = nullptr;
    const auto held = viewport_->structure();
    if (held) {
        structure = held.get();
        for (const auto& [name, values] : structure->scalarFields()) {
            (void)values;
            propertyCombo_->addItem(QString::fromStdString(name));
        }
    }
    const int index = propertyCombo_->findText(previous);
    if (index >= 0)
        propertyCombo_->setCurrentIndex(index);

    // Grey out overlay entries the current frame has no data for, rather than
    // hiding them: a fixed list keeps the indices aligned with
    // render::VectorOverlay, and the disabled tooltip explains what is
    // missing instead of silently offering nothing.
    {
        const QSignalBlocker overlayBlocker(vectorOverlayCombo_);
        bool currentStillValid = true;
        for (int i = 1; i < vectorOverlayCombo_->count(); ++i) {
            const auto overlay = static_cast<render::VectorOverlay>(i);
            const std::string field = render::vectorFieldName(overlay);
            const bool available =
                structure && structure->vectorFields().count(field) > 0;
            auto* model = qobject_cast<QStandardItemModel*>(
                vectorOverlayCombo_->model());
            if (model) {
                if (QStandardItem* item = model->item(i)) {
                    item->setEnabled(available);
                    item->setToolTip(
                        available
                            ? QString()
                            : tr("This frame carries no per-atom \"%1\" data "
                                 "(load an extended-XYZ trajectory whose "
                                 "frames include that column)")
                                  .arg(QString::fromStdString(field)));
                }
            }
            if (!available && vectorOverlayCombo_->currentIndex() == i)
                currentStillValid = false;
        }
        // Scrubbing to a frame without the selected property must not leave a
        // stale selection pointing at nothing.
        if (!currentStillValid) {
            vectorOverlayCombo_->setCurrentIndex(0);
            viewport_->style().vectorOverlay = render::VectorOverlay::None;
        }
    }
}

QColor* RepresentationPanel::vectorOverlayColor()
{
    auto& style = viewport_->style();
    switch (style.vectorOverlay) {
    case render::VectorOverlay::Velocity: return &style.velocityColor;
    case render::VectorOverlay::Force: return &style.forceColor;
    case render::VectorOverlay::MagneticMoment: return &style.magmomColor;
    case render::VectorOverlay::None: break;
    }
    return nullptr; // nothing is drawn, so there is no color to edit
}

void RepresentationPanel::syncVectorColorButton()
{
    const QColor* color = vectorOverlayColor();
    vectorColorButton_->setEnabled(color != nullptr);
    setButtonColor(vectorColorButton_,
                   color ? *color : palette().color(QPalette::Button));
    if (!color)
        vectorColorButton_->setToolTip(
            tr("Select a vector overlay above to choose its arrow color."));
}

void RepresentationPanel::syncColoringFromViewport()
{
    // The mapping can also be driven from outside (Coordination Analysis
    // dialog) — mirror the viewport state without re-triggering it.
    const auto mode = viewport_->colorMode();
    {
        const QSignalBlocker blocker(colorModeCombo_);
        colorModeCombo_->setCurrentIndex(static_cast<int>(mode));
    }
    {
        const QSignalBlocker blocker(gradientCombo_);
        gradientCombo_->setCurrentIndex(static_cast<int>(viewport_->style().gradient));
    }
    {
        const QSignalBlocker blocker(invertGradientCheck_);
        invertGradientCheck_->setChecked(viewport_->style().invertGradient);
    }
    if (mode == render::ColorMode::CustomScalar) {
        const QSignalBlocker blocker(propertyCombo_);
        const int index = propertyCombo_->findText(viewport_->customScalarField());
        if (index >= 0)
            propertyCombo_->setCurrentIndex(index);
    }

    const bool scalarMode = mode != render::ColorMode::Element;
    gradientCombo_->setEnabled(scalarMode);
    invertGradientCheck_->setEnabled(scalarMode);
    propertyCombo_->setEnabled(mode == render::ColorMode::CustomScalar);

    // The bounds fields stay editable in any scalar mode; they only go dead in
    // Element mode, where no scalar is being mapped at all.
    const bool autoScale = autoRangeCheck_->isChecked();
    autoRangeCheck_->setEnabled(scalarMode);
    rangeMinSpin_->setEnabled(scalarMode);
    rangeMaxSpin_->setEnabled(scalarMode);

    // While auto-scaling, the fields mirror the data's own range so the user
    // starts from the real numbers when they switch to custom bounds. Once
    // pinned, they are the user's values and must not be overwritten.
    if (autoScale) {
        const auto range = viewport_->scalarRange();
        const QSignalBlocker minBlocker(rangeMinSpin_);
        const QSignalBlocker maxBlocker(rangeMaxSpin_);
        syncingRange_ = true;
        rangeMinSpin_->setValue(range.valid ? range.min : 0.0);
        rangeMaxSpin_->setValue(range.valid ? range.max : 1.0);
        syncingRange_ = false;
    }
    applyColorRange();
}

void RepresentationPanel::applyColorRange()
{
    const bool custom = !autoRangeCheck_->isChecked();
    // An inverted or degenerate window would map every atom to one color;
    // order the two bounds rather than silently flattening the figure.
    const auto lo = static_cast<float>(
        std::min(rangeMinSpin_->value(), rangeMaxSpin_->value()));
    const auto hi = static_cast<float>(
        std::max(rangeMinSpin_->value(), rangeMaxSpin_->value()));
    viewport_->setCustomScalarRange(custom, lo, hi);
}

} // namespace calango::gui
