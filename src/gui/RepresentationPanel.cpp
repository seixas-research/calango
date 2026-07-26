#include "gui/RepresentationPanel.hpp"
#include "gui/GuiUtils.hpp"

#include "gui/CastSetupDialog.hpp"
#include "gui/CustomGradientColoringDialog.hpp"
#include "gui/ElementSettingsDialog.hpp"
#include "gui/PolyhedralSettingsDialog.hpp"
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
#include <QTabBar>
#include <QTabWidget>
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
    // A single page, not a QTabWidget: Unit cell, Axes triad and Vectors moved
    // to the "Cell, Axes & Vectors" dock and Custom coloring became a dialog,
    // so there is nothing left to tab between. A one-tab tab widget is a header
    // that costs a row and does nothing.
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(buildAppearanceTab());

    syncColoringFromViewport();
    syncCastsFromViewport();
}

QWidget* RepresentationPanel::buildAppearanceTab()
{
    auto* page = new QWidget(this);
    auto* form = new QFormLayout(page);

    // --- Casting -----------------------------------------------------------
    // Which GROUP of atoms the representation controls below apply to. Every
    // atom starts in "Cast: 0", so with casts unused this is a one-entry
    // dropdown and the panel behaves exactly as it always did. Split a
    // substrate and an adsorbate into two casts and each gets its own
    // representation — a CPK metal surface under a ball-and-stick molecule.
    //
    // It sits directly under Background and above Style because it scopes
    // everything below it: reading the panel top-down, you choose what you are
    // styling before you choose how.
    castCombo_ = new QComboBox(page);
    castCombo_->setToolTip(
        tr("The atom group the representation below applies to. Atoms are moved "
           "between casts in \"Cast change…\" on the editor row."));
    form->addRow(tr("Casting:"), castCombo_);
    connect(castCombo_, &QComboBox::currentIndexChanged, this, [this](int) {
        // Switching casts only changes WHICH mode the combo shows; it must not
        // write that mode back onto the newly selected cast.
        const QSignalBlocker blocker(modeCombo_);
        modeCombo_->setCurrentIndex(
            static_cast<int>(viewport_->style().castMode(selectedCast())));
    });

    // --- Style (surface material) ------------------------------------------
    // First control in the panel: the material decides how everything below
    // it reads on screen, and it is the setting most often changed when
    // preparing a figure. Applies to every lit mesh (atom spheres, bond
    // cylinders, cell tubes) so a figure reads as one material.
    surfaceFinishCombo_ = new QComboBox(page);
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

    modeCombo_ = new QComboBox(page);
    modeCombo_->addItems({tr("Ball-and-Stick"), tr("Space-filling (CPK)"),
                          tr("Wireframe"), tr("Polyhedral")});
    form->addRow(tr("Mode:"), modeCombo_);
    connect(modeCombo_, &QComboBox::currentIndexChanged, this, [this](int index) {
        const auto mode = static_cast<render::RepresentationMode>(index);
        const int cast = selectedCast();
        if (cast == 0) {
            // Cast 0's mode IS style.mode, which setRepresentation owns
            // (it also re-derives the scalar mapping and rebuilds).
            viewport_->setRepresentation(mode);
        } else {
            auto& modes = viewport_->style().castModes;
            const auto slot = static_cast<std::size_t>(cast - 1);
            if (slot < modes.size()) {
                modes[slot] = mode;
                viewport_->styleChanged(true);
            }
        }
        // The cast dropdown names each cast by its representation, so the label
        // the user just invalidated has to be re-rendered.
        syncCastsFromViewport();
    });

    // --- Atom coloring -----------------------------------------------------
    colorModeCombo_ = new QComboBox(page);
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
    const auto makeEditorButton = [page, editorRow](const QString& icon,
                                                    const QString& tip) {
        auto* button = new QPushButton(page);
        ui::IconManager::bind(button, icon);
        button->setIconSize(QSize(20, 20));
        button->setToolTip(tip);
        button->setFocusPolicy(Qt::NoFocus);
        editorRow->addWidget(button);
        return button;
    };
    // First on the row, ahead of Element Settings: a cast decides WHICH atoms
    // the rest of the panel is about, so the editor that moves atoms between
    // casts leads the editors that style them.
    auto* castButton = makeEditorButton(
        QStringLiteral("stacked-view"),
        tr("Cast change… — which cast each atom belongs to, and how many casts "
           "there are. Each cast draws in its own representation."));
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
    auto* gradientButton = makeEditorButton(
        QStringLiteral("gradient"),
        tr("Edit gradient coloring… — which per-atom property is mapped, "
           "through which gradient, over which value range."));
    editorRow->addStretch(1);
    form->addRow(editorRow);

    connect(castButton, &QPushButton::clicked, this,
            &RepresentationPanel::openCastSetup);
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
    connect(gradientButton, &QPushButton::clicked, this, [this] {
        auto* dialog = new CustomGradientColoringDialog(viewport_, this);
        dialog->setAttribute(Qt::WA_DeleteOnClose);
        dialog->show();
    });

    connect(colorModeCombo_, &QComboBox::currentIndexChanged,
            this, &RepresentationPanel::applyColorMode);
    // The viewport observers live with the widgets they update, on the Custom
    // coloring tab — connecting them here as well would fire each handler
    // twice per signal.

    // --- Scales ------------------------------------------------------------
    // Slider for coarse adjustment + spinbox for exact typed values,
    // bidirectionally synced (both drive the same style factor).
    const auto makeScaleRow = [this, page](QSlider*& slider, QDoubleSpinBox*& spin,
                                     const std::function<void(float)>& apply) {
        auto* row = new QWidget(page);
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

    gradientBondsCheck_ = new QCheckBox(tr("Gradient bond coloring"), page);
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

    auto* backgroundButton = new QPushButton(page);
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

    return page;
}


int RepresentationPanel::selectedCast() const
{
    return std::max(0, castCombo_->currentIndex());
}

void RepresentationPanel::syncCastsFromViewport()
{
    const auto& style = viewport_->style();
    const int count = style.castCount();
    const int keep = std::clamp(castCombo_->currentIndex(), 0, count - 1);
    {
        const QSignalBlocker blocker(castCombo_);
        castCombo_->clear();
        for (int cast = 0; cast < count; ++cast) {
            // Naming each cast by its representation is what makes the
            // dropdown readable at a glance ("Cast: 1 — Ball-and-Stick");
            // bare indices would say nothing about what they draw.
            const int mode = static_cast<int>(style.castMode(cast));
            castCombo_->addItem(tr("Cast: %1 — %2").arg(cast).arg(
                modeCombo_->itemText(mode)));
        }
        castCombo_->setCurrentIndex(keep);
    }
    const QSignalBlocker blocker(modeCombo_);
    modeCombo_->setCurrentIndex(static_cast<int>(style.castMode(keep)));
}

void RepresentationPanel::openCastSetup()
{
    // Modal: it edits live against the viewport, and letting a second editor
    // resize the cast list underneath it would leave both showing stale
    // indices.
    CastSetupDialog dialog(viewport_, viewport_->structure(), this);
    dialog.exec();
    syncCastsFromViewport();
}

void RepresentationPanel::applyColorMode()
{
    const auto mode = static_cast<render::ColorMode>(colorModeCombo_->currentIndex());
    if (mode != render::ColorMode::CustomScalar) {
        viewport_->setColorMode(mode);
        return;
    }
    // Which property is mapped belongs to the Custom Gradient Coloring dialog.
    // Keep whatever it last selected; when nothing has been chosen yet, seed
    // the structure's first scalar field so switching to "Custom property"
    // shows something instead of silently colouring nothing.
    QString field = viewport_->customScalarField();
    if (field.isEmpty()) {
        if (const auto structure = viewport_->structure()) {
            const auto& fields = structure->scalarFields();
            if (!fields.empty())
                field = QString::fromStdString(fields.begin()->first);
        }
    }
    viewport_->setColorMode(mode, field);
}

void RepresentationPanel::syncColoringFromViewport()
{
    // The mapping can also be driven from outside (the Coordination Analysis
    // dialog, the Custom Gradient Coloring dialog) — mirror the mode here
    // without re-triggering it. Everything else about the mapping lives in
    // that dialog now.
    const QSignalBlocker blocker(colorModeCombo_);
    colorModeCombo_->setCurrentIndex(static_cast<int>(viewport_->colorMode()));
}

} // namespace calango::gui
