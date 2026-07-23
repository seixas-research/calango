#include "gui/RepresentationPanel.hpp"
#include "gui/GuiUtils.hpp"

#include "gui/ElementSettingsDialog.hpp"
#include "gui/ViewportWidget.hpp"

#include <QColorDialog>
#include <QComboBox>
#include <QPushButton>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QSignalBlocker>
#include <QStandardItemModel>
#include <QVBoxLayout>

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
                          tr("Wireframe")});
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
    connect(colorModeCombo_, &QComboBox::currentIndexChanged,
            this, &RepresentationPanel::applyColorMode);

    gradientCombo_ = new QComboBox(this);
    // Same order as render::ColorGradient.
    gradientCombo_->addItems({tr("Viridis"), tr("Plasma"), tr("Turbo"),
                              tr("Inferno"), tr("Magma"), tr("Cividis"),
                              tr("Hot"), tr("Afmhot"), tr("Coolwarm"),
                              tr("Rainbow")});
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

    rangeLabel_ = new QLabel(this);
    form->addRow(tr("Range:"), rangeLabel_);

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

    // --- Manual bond order -------------------------------------------------
    // Orders are never auto-perceived; select exactly two atoms in the
    // viewport and assign the multiplicity here.
    auto* orderRow = new QWidget(this);
    auto* orderLayout = new QHBoxLayout(orderRow);
    orderLayout->setContentsMargins(0, 0, 0, 0);
    orderLayout->setSpacing(4);
    const std::array<const char*, 3> orderNames{
        QT_TR_NOOP("Single"), QT_TR_NOOP("Double"), QT_TR_NOOP("Triple")};
    for (int order = 1; order <= 3; ++order) {
        auto* button = new QPushButton(tr(orderNames[order - 1]), orderRow);
        bondOrderButtons_[order - 1] = button;
        orderLayout->addWidget(button);
        connect(button, &QPushButton::clicked, this, [this, order] {
            Q_EMIT bondOrderAssignRequested(order);
        });
    }
    form->addRow(tr("Bond order:"), orderRow);

    const auto syncBondOrderButtons = [this](int selectedCount) {
        const bool pair = selectedCount == 2;
        for (auto* button : bondOrderButtons_) {
            button->setEnabled(pair);
            button->setToolTip(pair
                                   ? tr("Assign this bond order to the "
                                        "selected atom pair")
                                   : tr("Select exactly two atoms in the "
                                        "viewport first (Ctrl+click)"));
        }
    };
    connect(viewport_, &ViewportWidget::selectionChanged,
            this, syncBondOrderButtons);
    syncBondOrderButtons(static_cast<int>(viewport_->selection().size()));

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
    vectorScaleSlider_->setRange(5, 20000); // ×0.05 .. ×200 in hundredths
    vectorScaleSlider_->setValue(1000);     // ×10 default
    vectorScaleSpin_ = new QDoubleSpinBox(vectorRow);
    vectorScaleSpin_->setRange(0.05, 200.0);
    vectorScaleSpin_->setDecimals(2);
    vectorScaleSpin_->setSingleStep(0.5);
    vectorScaleSpin_->setValue(10.00);
    vectorScaleSpin_->setSuffix(QStringLiteral("×"));
    vectorScaleSpin_->setToolTip(tr("Arrow length in Å per field unit\n"
                                    "(eV/Å for forces, Å/fs·√(amu) for "
                                    "velocities, μB for magnetic moments)"));
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
    form->addRow(tr("Background:"), backgroundButton);
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
    rangeLabel_->setEnabled(scalarMode);

    const auto range = viewport_->scalarRange();
    if (!scalarMode)
        rangeLabel_->setText(tr("—"));
    else if (!range.valid)
        rangeLabel_->setText(tr("no data"));
    else if (mode == render::ColorMode::CoordinationNumber)
        rangeLabel_->setText(QStringLiteral("%1 – %2")
                                 .arg(static_cast<int>(range.min))
                                 .arg(static_cast<int>(range.max)));
    else
        rangeLabel_->setText(QStringLiteral("%1 – %2")
                                 .arg(static_cast<double>(range.min), 0, 'f', 3)
                                 .arg(static_cast<double>(range.max), 0, 'f', 3));
}

} // namespace calango::gui
