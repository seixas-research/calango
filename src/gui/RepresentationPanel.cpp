#include "gui/RepresentationPanel.hpp"
#include "gui/GuiUtils.hpp"

#include "gui/ElementSettingsDialog.hpp"
#include "gui/PolyhedralSettingsDialog.hpp"
#include "gui/VectorOverlayDialog.hpp"
#include "ui/IconManager.hpp"
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

    // Four editors that change WHAT is drawn (rather than how it is shaded),
    // on one compact icon row. Icon-only with tooltips: the four labels spelled
    // out consumed four full-width rows of a dock that is already the tallest
    // in the app, and these are recognized by glyph once learned.
    auto* editorRow = new QHBoxLayout;
    editorRow->setSpacing(4);
    const auto makeEditorButton = [this, editorRow](const QString& icon,
                                                    const QString& tip) {
        auto* button = new QPushButton(this);
        button->setIcon(ui::IconManager::icon(icon));
        button->setIconSize(QSize(20, 20));
        button->setToolTip(tip);
        button->setFocusPolicy(Qt::NoFocus);
        editorRow->addWidget(button);
        return button;
    };
    auto* elementsButton = makeEditorButton(
        QStringLiteral("palette-line"),
        tr("Element Settings… — per-element colours and radii, and preset "
           "save/load."));
    auto* bondButton = makeEditorButton(
        QStringLiteral("links-line"),
        tr("Bond Editor… — bond perception, manual bonds, bond order and "
           "hydrogen-bond detection."));
    auto* polyhedralButton = makeEditorButton(
        QStringLiteral("box-1-line"),
        tr("Edit Polyhedral… — coordination-polyhedra opacity, edge wireframe "
           "and per-cation coordination cutoffs."));
    auto* vectorButton = makeEditorButton(
        QStringLiteral("arrow-right-up-line"),
        tr("Edit Vector Overlay… — which per-atom vector field is drawn, its "
           "scale and its colour."));
    editorRow->addStretch(1);
    form->addRow(editorRow);

    connect(elementsButton, &QPushButton::clicked, this, [this] {
        ElementSettingsDialog dialog(viewport_, this);
        dialog.exec();
    });
    connect(bondButton, &QPushButton::clicked, this,
            &RepresentationPanel::bondEditorRequested);
    // Modeless: both edit live, and the user needs to see the viewport change
    // while dragging a slider.
    connect(polyhedralButton, &QPushButton::clicked, this, [this] {
        auto* dialog = new PolyhedralSettingsDialog(viewport_, this);
        dialog->setAttribute(Qt::WA_DeleteOnClose);
        dialog->show();
    });
    connect(vectorButton, &QPushButton::clicked, this, [this] {
        auto* dialog = new VectorOverlayDialog(viewport_, this);
        dialog->setAttribute(Qt::WA_DeleteOnClose);
        dialog->show();
    });

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

    // The per-atom vector overlay (property selector, scale, colour) moved
    // into "Edit Vector Overlay…" on the icon row above: three controls that
    // only matter once an overlay is switched on do not earn permanent space
    // in the panel.

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

    // Overlay availability is now the Vector Overlay dialog's own concern: it
    // re-checks on structureReplaced, so a frame that drops a per-atom column
    // still resets the selection — without this panel holding widgets for it.
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
