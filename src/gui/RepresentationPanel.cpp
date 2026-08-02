#include "gui/RepresentationPanel.hpp"
#include "gui/GuiUtils.hpp"

#include "gui/CastColorsDialog.hpp"
#include "gui/CastSetupDialog.hpp"
#include "gui/CustomGradientColoringDialog.hpp"
#include "gui/ElementSettingsDialog.hpp"
#include "gui/PolyhedralSettingsDialog.hpp"
#include "ui/IconManager.hpp"
#include "gui/GuiUtils.hpp"
#include "gui/SettingsManager.hpp"
#include "gui/ViewportWidget.hpp"
#include "render/ShaderProfile.hpp"

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
    // to the "Spatial References" dock and Custom coloring became a dialog,
    // so there is nothing left to tab between. A one-tab tab widget is a header
    // that costs a row and does nothing.
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(buildAppearanceTab());

    syncColoringFromViewport();
    syncCastsFromViewport();
    syncShadingFromRegistry();
}

void RepresentationPanel::syncShadingFromRegistry()
{
    if (!shadingCombo_)
        return;
    // The atom slot answers for both: the row writes them together, and if a
    // hand-edited settings file has them disagreeing, the atoms are what the
    // eye reads first.
    const int model = render::ShaderRegistry::activeProfile(
                          render::ShaderSlot::Atoms)
                          .shadingModel;
    const int index = shadingCombo_->findData(model);
    if (index >= 0) {
        const QSignalBlocker blocker(shadingCombo_);
        shadingCombo_->setCurrentIndex(index);
    }
    syncSurfaceFinishEnabled();
}

void RepresentationPanel::syncSurfaceFinishEnabled()
{
    if (!surfaceFinishCombo_ || !shadingCombo_)
        return;
    const bool classic = shadingCombo_->currentData().toInt() == 0;
    if (auto* model =
            qobject_cast<QStandardItemModel*>(surfaceFinishCombo_->model())) {
        // Indices 0..2 are Standard / Shiny / Matte — the three that only
        // configure the Blinn-Phong branch. Index 3 (Glassy) is the
        // translucency pass and applies under every shading model, so it stays
        // selectable: without it there would be no way to make a PBR structure
        // transparent at all.
        for (int i = 0; i < 3; ++i) {
            if (QStandardItem* item = model->item(i))
                item->setEnabled(classic);
        }
    }
    // Greyed rather than removed, and the current selection is left alone: the
    // user's material comes back untouched when they return to Classic. The
    // greying is the whole message — a paragraph of prose under the row said
    // the same thing permanently, in a dock that has no rows to spare.
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
        tr("The atom group the representation below applies to. The button "
           "beside it moves atoms between casts."));
    connect(castCombo_, &QComboBox::currentIndexChanged, this,
            [this](int) { loadSelectedCast(); });

    // The cast editor sits ON this row rather than at the head of the editor
    // icon row further down. It is the only one of those buttons that acts on
    // the control beside it — it decides what the dropdown can even offer —
    // so separating the two put a question and its answer in different places.
    auto* castRow = new QHBoxLayout;
    castRow->setSpacing(4);
    castRow->addWidget(castCombo_, 1);
    auto* castButton = new QPushButton(page);
    ui::IconManager::bind(castButton, QStringLiteral("group-fill"));
    castButton->setIconSize(QSize(20, 20));
    castButton->setFocusPolicy(Qt::NoFocus);
    castButton->setToolTip(
        tr("Cast change… — which cast each atom belongs to, and how many casts "
           "there are. Each cast draws in its own representation."));
    castRow->addWidget(castButton);
    form->addRow(tr("Casting:"), castRow);
    connect(castButton, &QPushButton::clicked, this,
            &RepresentationPanel::openCastSetup);

    // --- Shading (which BRDF) ----------------------------------------------
    // Directly above Style, because it decides which of the two the finish
    // below even feeds: Standard / Shiny / Matte configure Blinn-Phong, and
    // PBR and Toon replace it.
    //
    // These used to be reachable only from Preferences → Rendering, which is
    // the wrong place for them: PBR and Toon are LOOKS, and nobody hunts
    // through Preferences to make a figure look like a cartoon. Preferences
    // keeps the per-slot expert view (atoms and bonds independently, plus the
    // impostor geometry choice) — this row moves the two the user actually
    // picks between into the panel where the rest of the material lives.
    //
    // One row for atoms AND bonds: an atom shaded as metal beside a bond
    // shaded as a cartoon is not a figure anyone wants, and the slot split
    // exists for the geometry paths, not for the shading.
    shadingCombo_ = new QComboBox(page);
    {
        // Item data is the shadingModel index the profiles declare, not a
        // profile id: the id differs per slot for the same look, while the
        // BRDF number is exactly what the two slots share.
        const auto& atomProfiles =
            render::ShaderRegistry::profiles(render::ShaderSlot::Atoms);
        shadingCombo_->addItem(tr("Classic (Blinn-Phong)"), 0);
        shadingCombo_->setItemData(
            0,
            tr("The shading the application has always used: diffuse plus a "
               "Blinn-Phong highlight, configured by the Style row below."),
            Qt::ToolTipRole);
        for (const render::ShaderProfile& profile : atomProfiles) {
            if (profile.shadingModel == 0)
                continue;
            QString reason;
            const bool supported =
                render::ShaderRegistry::isSupported(profile, &reason);
            shadingCombo_->addItem(supported ? profile.displayName
                                             : tr("%1 — unavailable")
                                                   .arg(profile.displayName),
                                   profile.shadingModel);
            const int added = shadingCombo_->count() - 1;
            shadingCombo_->setItemData(added, profile.description,
                                       Qt::ToolTipRole);
            if (!supported) {
                // Listed but disabled with the driver's own reason, the same
                // treatment Preferences gives it. A look that simply vanishes
                // reads as a missing feature; one that says why is actionable.
                if (auto* model = qobject_cast<QStandardItemModel*>(
                        shadingCombo_->model())) {
                    if (QStandardItem* item = model->item(added)) {
                        item->setEnabled(false);
                        item->setToolTip(reason);
                    }
                }
            }
        }
    }
    form->addRow(tr("Shading:"), shadingCombo_);

    connect(shadingCombo_, &QComboBox::currentIndexChanged, this, [this] {
        const int model = shadingCombo_->currentData().toInt();
        for (const render::ShaderSlot slot :
             {render::ShaderSlot::Atoms, render::ShaderSlot::Bonds}) {
            // Classic has TWO profiles (tessellated mesh and impostor) that
            // differ only in geometry, so it resolves to whichever matches the
            // geometry path already in use. That makes PBR → Classic → PBR
            // lossless instead of silently dropping the user back to the
            // tessellated path they had opted out of.
            const bool impostor =
                render::ShaderRegistry::activeProfile(slot).impostorGeometry;
            for (const render::ShaderProfile& profile :
                 render::ShaderRegistry::profiles(slot)) {
                if (profile.shadingModel != model)
                    continue;
                if (model == 0 && profile.impostorGeometry != impostor)
                    continue;
                render::ShaderRegistry::setActiveProfileId(slot, profile.id);
                break;
            }
        }
        SettingsManager::save();
        syncSurfaceFinishEnabled();
        // PBR and Toon are impostor-only, so switching into or out of them can
        // change the vertex layout — rebuild rather than merely redraw.
        viewport_->styleChanged(true);
    });

    // --- Style (surface material) ------------------------------------------
    // The material decides how everything below it reads on screen, and it is
    // the setting most often changed when preparing a figure. Applies to every
    // lit mesh (atom spheres, bond cylinders, cell tubes) so a figure reads as
    // one material.
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
                auto cast = selectedCastStyle();
                cast.surfaceFinish = static_cast<render::SurfaceFinish>(index);
                applyToSelectedCast(cast);
                // The finish now travels in the instance buffer (one value per
                // cast), so changing it IS a geometry rebuild.
                viewport_->styleChanged(true);
            });

    modeCombo_ = new QComboBox(page);
    // Order matches render::RepresentationMode.
    modeCombo_->addItems({tr("Ball-and-Stick"), tr("Space-filling"),
                          tr("Wireframe"), tr("Polyhedral"),
                          tr("Ribbon Diagram"), tr("Molecular Surface"),
                          tr("Licorice")});
    modeCombo_->setToolTip(
        tr("Ribbon Diagram and Molecular Surface need the residue annotation a "
           "PDB / PDBx-mmCIF file carries: the ribbon follows each chain's "
           "α-carbon trace, and both replace the per-atom spheres entirely — "
           "which is the point, since 15 000 atoms drawn individually are an "
           "opaque hairball.\n\n"
           "Licorice drops the atom spheres and draws every bond as one "
           "uniform tube — the connectivity is the model. Standard for "
           "organics, where ball-and-stick's spheres crowd together until the "
           "skeleton disappears behind them."));
    form->addRow(tr("Mode:"), modeCombo_);
    connect(modeCombo_, &QComboBox::currentIndexChanged, this, [this](int index) {
        auto cast = selectedCastStyle();
        cast.mode = static_cast<render::RepresentationMode>(index);
        applyToSelectedCast(cast);
        viewport_->styleChanged(true);
        // The cast dropdown names each cast by its representation, so the label
        // the user just invalidated has to be re-rendered.
        syncCastsFromViewport();
    });

    // --- Atom coloring -----------------------------------------------------
    colorModeCombo_ = new QComboBox(page);
    // Order matches render::ColorMode.
    colorModeCombo_->addItems({tr("Element (CPK)"),
                               tr("Coordination number (CN)"),
                               tr("Generalized CN (GCN)"),
                               tr("Custom property"),
                               tr("Cast")});
    colorModeCombo_->setToolTip(
        tr("Per cast: a coordination-colored slab and a "
           "custom-property-colored adsorbate can share one scene, each "
           "normalized against its own data range.\n\n"
           "\"Cast\" gives every atom its cast's own flat color — the classic "
           "substrate-vs-adsorbate figure where the groups, not the elements, "
           "carry the story. Pick the colors with the button beside this "
           "dropdown."));
    // The colour-by row carries its own three satellites: the per-cast colour
    // picker, the editor that defines the scalar mapping, and the toggle that
    // prints the mapped number on the atoms. All are meaningless without this
    // dropdown and each answers a question it raises — which colour per cast,
    // through which ramp, and by how much — so they belong on its row rather
    // than anonymous in the editor strip below.
    auto* colorRow = new QHBoxLayout;
    colorRow->setSpacing(4);
    colorRow->addWidget(colorModeCombo_, 1);
    const auto makeColorButton = [page, colorRow](const QString& icon,
                                                  const QString& tip) {
        auto* button = new QPushButton(page);
        ui::IconManager::bind(button, icon);
        button->setIconSize(QSize(20, 20));
        button->setToolTip(tip);
        button->setFocusPolicy(Qt::NoFocus);
        colorRow->addWidget(button);
        return button;
    };
    // First satellite, directly beside the dropdown: it configures the mode
    // the dropdown just offered. Always present but only enabled under "Cast"
    // coloring — a button that appears and vanishes with the mode reads as a
    // layout glitch, one that greys in and out reads as a dependency.
    castColorsButton_ = makeColorButton(
        QStringLiteral("palette-line"),
        tr("Cast colors… — the flat color each cast takes under \"Cast\" "
           "coloring. Casts without an explicit pick cycle a default "
           "palette."));
    castColorsButton_->setEnabled(false);
    auto* gradientButton = makeColorButton(
        QStringLiteral("color-filter-fill"),
        tr("Edit gradient coloring… — which per-atom property is mapped, "
           "through which gradient, over which value range."));
    // The ramp says which atoms differ; this says by how much. A GCN of 6.75
    // against 7.50 is a distinction no colour scale conveys.
    scalarLabelsButton_ = makeColorButton(
        QStringLiteral("hashtag"),
        tr("Show CN / GCN values — print each atom's value of the property "
           "selected in \"Color by\" on the 3D viewport."));
    scalarLabelsButton_->setCheckable(true);
    scalarLabelsButton_->setChecked(viewport_->showCoordinationLabels());
    form->addRow(tr("Color by:"), colorRow);

    // The four display toggles that stood here — element symbols, atomic
    // indices, hydrogens, gradient bonds — moved to the viewport toolbar.
    // Each was a single viewport-wide bit rather than a per-cast setting, and
    // they are the switches flipped most often while reading a structure, so
    // a dock the user may have collapsed was the wrong place for them.

    connect(scalarLabelsButton_, &QPushButton::toggled, viewport_,
            &ViewportWidget::setShowCoordinationLabels);
    connect(gradientButton, &QPushButton::clicked, this, [this] {
        auto* dialog = new CustomGradientColoringDialog(viewport_, this);
        dialog->setAttribute(Qt::WA_DeleteOnClose);
        dialog->show();
    });
    // Modal for the same reason Cast Setup is: its rows are the cast list,
    // and letting another editor resize that list underneath it would leave
    // both showing stale indices.
    connect(castColorsButton_, &QPushButton::clicked, this, [this] {
        CastColorsDialog dialog(viewport_, viewport_->structure(), this);
        dialog.exec();
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
                     auto cast = selectedCastStyle();
                     cast.atomScaleFactor = factor;
                     applyToSelectedCast(cast);
                     viewport_->styleChanged(true);
                 }));
    form->addRow(tr("Bond width:"),
                 makeScaleRow(bondWidthSlider_, bondWidthSpin_, [this](float factor) {
                     auto cast = selectedCastStyle();
                     cast.bondWidthFactor = factor;
                     applyToSelectedCast(cast);
                     viewport_->styleChanged(true);
                 }));

    // --- Opacity -----------------------------------------------------------
    // Per cast, like everything above it. The combination this exists for is a
    // faded substrate behind a solid adsorbate: fading the whole scene equally
    // would hide exactly the thing being looked at.
    //
    // Its own row rather than makeScaleRow's 0.20-3.00 factor: opacity is a
    // fraction with hard physical ends at 0 and 1, not a multiplier.
    //
    // A slider, matching Atom radius and Bond width directly above it: three
    // stacked value controls that behave identically should look identical,
    // and the left-to-right travel reads as 0 → 1 without being learned.
    {
        auto* row = new QWidget(page);
        auto* rowLayout = new QHBoxLayout(row);
        rowLayout->setContentsMargins(0, 0, 0, 0);
        opacitySlider_ = new QSlider(Qt::Horizontal, row);
        opacitySlider_->setRange(0, 100); // percent
        opacitySlider_->setValue(100);
        opacitySpin_ = new QDoubleSpinBox(row);
        opacitySpin_->setRange(0.00, 1.00);
        opacitySpin_->setDecimals(2);
        opacitySpin_->setSingleStep(0.05);
        opacitySpin_->setValue(1.00);
        rowLayout->addWidget(opacitySlider_, 1);
        rowLayout->addWidget(opacitySpin_);
        const QString tip =
            tr("How opaque this cast is drawn. 1 is fully opaque (the "
               "default), 0 invisible.\n\n"
               "Distinct from the Glassy style, which is a material with a "
               "view-dependent Fresnel rim; this is a flat transparency you "
               "set. The two compose — a glassy cast faded to 0.3 is fainter "
               "than an opaque glassy one.");
        opacitySlider_->setToolTip(tip);
        opacitySpin_->setToolTip(tip);
        form->addRow(tr("Opacity:"), row);

        const auto applyOpacity = [this](float value) {
            auto cast = selectedCastStyle();
            cast.opacity = value;
            applyToSelectedCast(cast);
            // The alpha travels in the instance buffer, so this is a rebuild.
            viewport_->styleChanged(true);
        };
        connect(opacitySlider_, &QSlider::valueChanged, this,
                [this, applyOpacity](int percent) {
                    const float value = static_cast<float>(percent) / 100.0f;
                    {
                        const QSignalBlocker blocker(opacitySpin_);
                        opacitySpin_->setValue(value);
                    }
                    applyOpacity(value);
                });
        connect(opacitySpin_, &QDoubleSpinBox::valueChanged, this,
                [this, applyOpacity](double value) {
                    {
                        const QSignalBlocker blocker(opacitySlider_);
                        opacitySlider_->setValue(
                            static_cast<int>(std::lround(value * 100.0)));
                    }
                    applyOpacity(static_cast<float>(value));
                });
    }

    // Bond order used to live here as three buttons acting on the viewport
    // selection. It moved into the Bond Editor's "By Atomic Indices" tab,
    // which already owns per-pair bond edits and can also express Aromatic —
    // one place to assign a pair's chemistry instead of two.

    // --- Editors ------------------------------------------------------------
    // The three that open a window, below Opacity rather than mixed in with
    // the display toggles above. They are a different kind of act — a click
    // here costs a dialog, a click up there flips a bit — and mixing the two
    // on one strip of identical square buttons meant the only way to know
    // which you were about to get was to have learned the glyph.
    //
    // Order is by what they edit, widening outward: one element, then the
    // bonds between elements, then the polyhedron a coordination shell forms.
    auto* dialogRow = new QHBoxLayout;
    dialogRow->setSpacing(4);
    const auto makeDialogButton = [page, dialogRow](const QString& icon,
                                                    const QString& tip) {
        auto* button = new QPushButton(page);
        ui::IconManager::bind(button, icon);
        button->setIconSize(QSize(20, 20));
        button->setToolTip(tip);
        button->setFocusPolicy(Qt::NoFocus);
        dialogRow->addWidget(button);
        return button;
    };
    auto* elementsButton = makeDialogButton(
        QStringLiteral("brush-fill"),
        tr("Element Settings… — per-element colors and radii, and preset "
           "save/load."));
    auto* bondButton = makeDialogButton(
        QStringLiteral("share-fill"),
        tr("Bond Editor… — bond perception, manual bonds, bond order and "
           "hydrogen-bond detection."));
    auto* polyhedralButton = makeDialogButton(
        QStringLiteral("box-1-fill"),
        tr("Edit Polyhedral… — coordination-polyhedra opacity, edge wireframe "
           "and per-cation coordination cutoffs."));
    dialogRow->addStretch(1);
    form->addRow(dialogRow);

    connect(elementsButton, &QPushButton::clicked, this, [this] {
        ElementSettingsDialog dialog(viewport_, this);
        dialog.exec();
    });
    connect(bondButton, &QPushButton::clicked, this,
            &RepresentationPanel::bondEditorRequested);
    // Modeless: it edits live, and the user needs to see the viewport change
    // while dragging a slider.
    connect(polyhedralButton, &QPushButton::clicked, this, [this] {
        auto* dialog = new PolyhedralSettingsDialog(viewport_, this);
        dialog->setAttribute(Qt::WA_DeleteOnClose);
        dialog->show();
    });

    // "Complete with hydrogens" used to sit beside the toggle above. It moved
    // to the viewport toolbar, with the other actions that EDIT the structure:
    // it adds atoms and pushes an undo entry, which is a different kind of act
    // from everything else in this panel, all of which only changes how the
    // existing atoms are drawn.

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

render::StructureRenderer::CastStyle RepresentationPanel::selectedCastStyle() const
{
    return viewport_->style().castStyle(selectedCast());
}

void RepresentationPanel::applyToSelectedCast(
    const render::StructureRenderer::CastStyle& cast)
{
    viewport_->style().setCastStyle(selectedCast(), cast);
}

void RepresentationPanel::syncCastsFromViewport()
{
    const auto& style = viewport_->style();
    const int count = style.castCount();
    const int keep = std::clamp(castCombo_->currentIndex(), 0, count - 1);
    {
        const QSignalBlocker blocker(castCombo_);
        castCombo_->clear();
        // Bare numbers. A cast is an index the user assigns atoms to, and the
        // Cast Setup dialog, the atom table and this dropdown all refer to the
        // same thing — spelling it out three different ways just made the
        // dropdown wide enough to truncate the number itself.
        for (int cast = 0; cast < count; ++cast)
            castCombo_->addItem(QString::number(cast));
        castCombo_->setCurrentIndex(keep);
    }
    loadSelectedCast();
}

void RepresentationPanel::loadSelectedCast()
{
    // Show the selected cast's settings WITHOUT writing them back: every one
    // of these controls has a handler that edits the current cast, so an
    // unblocked setCurrentIndex here would copy the previous cast's state onto
    // the newly selected one the moment the user switched.
    const auto cast = selectedCastStyle();
    {
        const QSignalBlocker blocker(modeCombo_);
        modeCombo_->setCurrentIndex(static_cast<int>(cast.mode));
    }
    {
        const QSignalBlocker blocker(surfaceFinishCombo_);
        surfaceFinishCombo_->setCurrentIndex(static_cast<int>(cast.surfaceFinish));
    }
    {
        const QSignalBlocker blocker(colorModeCombo_);
        colorModeCombo_->setCurrentIndex(static_cast<int>(cast.colorMode));
    }
    const auto setScale = [](QSlider* slider, QDoubleSpinBox* spin, float value) {
        const QSignalBlocker sliderBlocker(slider);
        const QSignalBlocker spinBlocker(spin);
        spin->setValue(value);
        slider->setValue(static_cast<int>(std::lround(value * 100.0f)));
    };
    setScale(atomScaleSlider_, atomScaleSpin_, cast.atomScaleFactor);
    setScale(bondWidthSlider_, bondWidthSpin_, cast.bondWidthFactor);
    {
        const QSignalBlocker sliderBlocker(opacitySlider_);
        const QSignalBlocker spinBlocker(opacitySpin_);
        opacitySpin_->setValue(cast.opacity);
        opacitySlider_->setValue(
            static_cast<int>(std::lround(cast.opacity * 100.0f)));
    }
    // Nothing to print when the cast is coloured by element or by cast: CPK
    // is a lookup and a cast is a label — neither is a measured quantity, so
    // the toggle would produce empty labels.
    if (scalarLabelsButton_)
        scalarLabelsButton_->setEnabled(
            cast.colorMode != render::ColorMode::Element
            && cast.colorMode != render::ColorMode::Cast);
    if (castColorsButton_)
        castColorsButton_->setEnabled(cast.colorMode
                                      == render::ColorMode::Cast);
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
    // A non-zero cast writes straight into its own entry; cast 0 goes through
    // setColorMode, which also re-derives the scalar fields and rebuilds.
    if (selectedCast() != 0) {
        auto cast = selectedCastStyle();
        cast.colorMode = mode;
        applyToSelectedCast(cast);
        viewport_->refreshColorScalars();
        syncCastsFromViewport();
        return;
    }
    if (mode != render::ColorMode::CustomScalar) {
        viewport_->setColorMode(mode);
        syncCastsFromViewport();
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
    syncCastsFromViewport();
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
