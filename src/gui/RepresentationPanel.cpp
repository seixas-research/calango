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
    // to the "Spatial References" dock and Custom coloring became a dialog,
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
    connect(castCombo_, &QComboBox::currentIndexChanged, this,
            [this](int) { loadSelectedCast(); });

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
    colorModeCombo_->addItems({tr("Element (CPK)"),
                               tr("Coordination number (CN)"),
                               tr("Generalized CN (GCN)"),
                               tr("Custom property")});
    colorModeCombo_->setToolTip(
        tr("Per cast: a coordination-colored slab and a "
           "custom-property-colored adsorbate can share one scene, each "
           "normalized against its own data range."));
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
        QStringLiteral("group-fill"),
        tr("Cast change… — which cast each atom belongs to, and how many casts "
           "there are. Each cast draws in its own representation."));
    auto* elementsButton = makeEditorButton(
        QStringLiteral("brush-fill"),
        tr("Element Settings… — per-element colors and radii, and preset "
           "save/load."));
    // Three per-atom text overlays, together and directly after Element
    // Settings. All three answer the same question — what is written ON the
    // atoms — so they read as one group; the first two came from the viewport
    // toolbar, which is about navigating the scene rather than drawing it.
    elementLabelsButton_ = makeEditorButton(
        QStringLiteral("atom-line"),
        tr("Show element symbols — overlay each atom's chemical symbol "
           "(Fe, O, Si…) on the 3D viewport."));
    elementLabelsButton_->setCheckable(true);
    elementLabelsButton_->setChecked(viewport_->showElementLabels());

    indexLabelsButton_ = makeEditorButton(
        QStringLiteral("price-tag-fill"),
        tr("Show atomic indices — overlay each atom's 1-based index "
           "(#1, #2…) on the 3D viewport."));
    indexLabelsButton_->setCheckable(true);
    indexLabelsButton_->setChecked(viewport_->showAtomIndexLabels());

    // The third of the group: it prints the number behind the colour ramp
    // rather than an identity. It follows "Color by" (one row up) because it is
    // that setting's read-out — the ramp says which atoms differ, this says by
    // how much, and a GCN of 6.75 against 7.50 is a distinction no colour scale
    // conveys.
    scalarLabelsButton_ = makeEditorButton(
        QStringLiteral("hashtag"),
        tr("Show CN / GCN values — print each atom's value of the property "
           "selected in \"Color by\" on the 3D viewport."));
    scalarLabelsButton_->setCheckable(true);
    scalarLabelsButton_->setChecked(viewport_->showCoordinationLabels());
    auto* bondButton = makeEditorButton(
        QStringLiteral("share-fill"),
        tr("Bond Editor… — bond perception, manual bonds, bond order and "
           "hydrogen-bond detection."));
    auto* polyhedralButton = makeEditorButton(
        QStringLiteral("box-1-fill"),
        tr("Edit Polyhedral… — coordination-polyhedra opacity, edge wireframe "
           "and per-cation coordination cutoffs."));
    auto* gradientButton = makeEditorButton(
        QStringLiteral("color-filter-fill"),
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
    connect(elementLabelsButton_, &QPushButton::toggled, viewport_,
            &ViewportWidget::setShowElementLabels);
    connect(indexLabelsButton_, &QPushButton::toggled, viewport_,
            &ViewportWidget::setShowAtomIndexLabels);
    connect(scalarLabelsButton_, &QPushButton::toggled, viewport_,
            &ViewportWidget::setShowCoordinationLabels);
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

    // Hydrogens get their own row: hiding them is the single most common way
    // to make an organic or protein structure readable, and adding the missing
    // ones is what you do to a heavy-atom-only model straight out of a CIF or
    // a PDB. They sit together because the second usually follows the first —
    // you notice the hydrogens are absent, then you build them.
    auto* hydrogenRow = new QHBoxLayout;
    hydrogenRow->setSpacing(6);
    showHydrogensCheck_ = new QCheckBox(tr("Show hydrogens"), page);
    showHydrogensCheck_->setChecked(viewport_->style().showHydrogens);
    showHydrogensCheck_->setToolTip(
        tr("Draw hydrogen atoms, their bonds and the hydrogen-bond dashes.\n"
           "Off leaves the heavy-atom skeleton, which is how a crowded organic\n"
           "or protein structure is normally read.\n\n"
           "Display only — the hydrogens stay in the structure, so the formula "
           "and every\ncalculation and exported file are unchanged."));
    hydrogenRow->addWidget(showHydrogensCheck_);
    hydrogenRow->addStretch(1);
    // Icon-only, like the editor row above it: the spelled-out label was the
    // widest thing in the dock and forced the whole panel wider than any other
    // control needed. The tooltip carries what the label said.
    auto* completeHydrogensButton = new QPushButton(page);
    ui::IconManager::bind(completeHydrogensButton, QStringLiteral("heading"));
    completeHydrogensButton->setIconSize(QSize(20, 20));
    completeHydrogensButton->setFocusPolicy(Qt::NoFocus);
    completeHydrogensButton->setToolTip(
        tr("Complete with hydrogens — add the hydrogens each atom's standard "
           "valence implies:\na carbon with three bonds gets one, an sp3 "
           "oxygen with one bond gets one, and so on.\n\n"
           "Positions come from relaxing the new bonds against the existing "
           "ones, so\nthe geometry follows the coordination (tetrahedral, "
           "trigonal, bent).\nMetals and transition metals are left alone. "
           "Undoable."));
    hydrogenRow->addWidget(completeHydrogensButton);
    form->addRow(hydrogenRow);
    connect(showHydrogensCheck_, &QCheckBox::toggled, this, [this](bool on) {
        viewport_->style().showHydrogens = on;
        viewport_->styleChanged(true);
    });
    connect(completeHydrogensButton, &QPushButton::clicked, this, [this] {
        // Building hydrogens you cannot see is a no-op as far as the user can
        // tell, so asking for them turns them back on. A display decision, and
        // this panel owns the display.
        showHydrogensCheck_->setChecked(true);
        Q_EMIT hydrogenCompletionRequested();
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
    // Nothing to print when the cast is coloured by element: CPK is a lookup,
    // not a measured quantity, so the toggle would produce empty labels.
    if (scalarLabelsButton_)
        scalarLabelsButton_->setEnabled(cast.colorMode != render::ColorMode::Element);
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
