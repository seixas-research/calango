#include "gui/FloorPanel.hpp"

#include "gui/GuiUtils.hpp"
#include "gui/ViewportWidget.hpp"
#include "render/StructureRenderer.hpp"

#include <QColorDialog>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSlider>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>
#include <utility>

namespace calango::gui {

FloorPanel::FloorPanel(ViewportWidget* viewport, QWidget* parent)
    : QWidget(parent), viewport_(viewport)
{
    auto* pageLayout = new QVBoxLayout(this);
    auto& style = viewport_->style();

    floorGroup_ = new QGroupBox(tr("Ground plane (floor)"), this);
    floorGroup_->setCheckable(true);
    floorGroup_->setChecked(style.floorEnabled);
    floorGroup_->setToolTip(
        tr("A large plane just under the structure, so an isolated molecule "
           "reads as an object resting in a space rather than one floating in "
           "a void. It RECEIVES the shadows from the Shadow tab (Visual "
           "Effects) — the atoms and bonds cast onto it — and casts none of "
           "its own.\n\n"
           "Display only: it is never part of the structure, is not picked by "
           "clicks, and never appears in an exported POSCAR, CIF or XYZ."));
    auto* floorForm = new QFormLayout(floorGroup_);

    // Height: a linked slider + spin box, like the Vectors tab's own scale
    // and width controls. The slider's range is re-derived from the current
    // structure's own scale (refreshHeightRange(), below) rather than fixed,
    // so a single atom and a slab each get a slider that actually resolves
    // to something — the spin box keeps its wide fixed range regardless, so
    // a value further out is still just a matter of typing it.
    auto* heightRow = new QWidget(floorGroup_);
    auto* heightLayout = new QHBoxLayout(heightRow);
    heightLayout->setContentsMargins(0, 0, 0, 0);
    heightSlider_ = new QSlider(Qt::Horizontal, heightRow);
    heightSpin_ = new QDoubleSpinBox(heightRow);
    heightSpin_->setRange(-100.0, 100.0);
    heightSpin_->setDecimals(2);
    heightSpin_->setSingleStep(0.25);
    heightSpin_->setSuffix(QStringLiteral(" Å"));
    heightSpin_->setValue(style.floorOffset);
    heightLayout->addWidget(heightSlider_, 1);
    heightLayout->addWidget(heightSpin_);
    heightRow->setToolTip(
        tr("Raise (+) or lower (−) the plane relative to its automatic "
           "position, which is just under the lowest drawn point — the bottom "
           "of the lowest atom's sphere, or of the unit cell when that is "
           "shown.\n\n"
           "An offset rather than an absolute height, so that the plane keeps "
           "following the structure as it is edited or a trajectory is "
           "scrubbed while your adjustment survives. 0 leaves it exactly on "
           "the automatic level.\n\n"
           "The slider's own range tracks the structure's size; type into the "
           "box for anything further out than it currently reaches."));
    floorForm->addRow(tr("Height:"), heightRow);
    refreshHeightRange(); // also positions the slider from style.floorOffset

    auto* colorButton = new QPushButton(floorGroup_);
    setButtonColor(colorButton, style.floorColor);
    colorButton->setToolTip(
        tr("Plane colour. White by default: a figure's page is white, so a "
           "white floor disappears into it and leaves only what the plane was "
           "added for — the shadow. Shadows stay legible on it because the "
           "shadow term attenuates direct light only, never ambient."));
    floorForm->addRow(tr("Color:"), colorButton);

    auto* finishCombo = new QComboBox(floorGroup_);
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

    auto* opacitySpin = new QDoubleSpinBox(floorGroup_);
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
    auto* orientationCombo = new QComboBox(floorGroup_);
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
        auto* spin = new QDoubleSpinBox(floorGroup_);
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
    auto* normalRow = new QWidget(floorGroup_);
    auto* normalLayout = new QHBoxLayout(normalRow);
    normalLayout->setContentsMargins(0, 0, 0, 0);
    for (auto* spin : {nxSpin, nySpin, nzSpin})
        normalLayout->addWidget(spin, 1);
    normalRow->setToolTip(
        tr("The plane's normal, in world coordinates. Any direction will do — "
           "the length is irrelevant, since it is normalized before use, so "
           "(0, 0, 2) and (0, 0, 1) are the same plane.\n\n"
           "The floor is placed on the NEGATIVE side of the structure along "
           "this direction, and the height above moves it along the same "
           "direction. Reversing the normal therefore puts the plane over the "
           "structure rather than under it — a ceiling, which is a real "
           "choice rather than an error, so it is left available."));
    floorForm->addRow(tr("Normal (x, y, z):"), normalRow);

    // Inline validation, in the app's usual red. A zero vector spans no plane;
    // rather than refuse the keystroke — which would make (0,0,1) → (0,0,0) →
    // (1,0,0) impossible to type through — the previous normal is kept and the
    // input is flagged until it means something again.
    auto* normalWarning = new QLabel(floorGroup_);
    normalWarning->setWordWrap(true);
    normalWarning->setStyleSheet(QStringLiteral("color: #d9534f;"));
    normalWarning->setVisible(false);
    floorForm->addRow(QString(), normalWarning);

    // One page, not several — everything else in the Spatial References dock
    // is a plain widget too, so this has to fit the dock's default height
    // just like they do: the alternative is a scrollbar on one tab and not
    // the others, or a control stranded below the fold.
    pageLayout->addWidget(floorGroup_);
    pageLayout->addStretch(1);

    // Every floor control is a uniform read at draw time — the plane's quad is
    // uploaded once and never rebuilt — so all of these repaint rather than
    // rebuild, height included.
    // The single control for the bit. It briefly had a twin on the viewport
    // toolbar and the two mirrored each other; the toolbar button is gone, so
    // this writes the style and nothing else needs telling.
    connect(floorGroup_, &QGroupBox::toggled, this, [this](bool on) {
        viewport_->style().floorEnabled = on;
        viewport_->styleChanged(false);
    });
    connect(heightSlider_, &QSlider::valueChanged, this, [this](int centiAngstrom) {
        const double v = centiAngstrom / 100.0;
        const QSignalBlocker blocker(heightSpin_);
        heightSpin_->setValue(v);
        viewport_->style().floorOffset = static_cast<float>(v);
        viewport_->styleChanged(false);
    });
    connect(heightSpin_, &QDoubleSpinBox::valueChanged, this, [this](double v) {
        // Clamped by QSlider::setValue() to whatever the slider currently
        // reaches, same as dragging past either end of it would — a value
        // further out just leaves the handle pinned at that end until
        // refreshHeightRange() widens the range again.
        const QSignalBlocker blocker(heightSlider_);
        heightSlider_->setValue(static_cast<int>(std::lround(v * 100.0)));
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
    connect(this, &FloorPanel::syncFromViewport, floorGroup_,
            [this, colorButton, finishCombo, opacitySpin, showNormal,
             normalWarning] {
                const auto& live = viewport_->style();
                {
                    const QSignalBlocker blocker(floorGroup_);
                    floorGroup_->setChecked(live.floorEnabled);
                }
                // Also re-syncs heightSlider_ (its range AND position) from
                // live.floorOffset — a project restore can load an entirely
                // different structure, whose scale the slider's own range
                // has to follow.
                refreshHeightRange();
                {
                    const QSignalBlocker blocker(heightSpin_);
                    heightSpin_->setValue(live.floorOffset);
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
}

void FloorPanel::refreshHeightRange()
{
    using render::StructureRenderer;
    // Static and GL-free (see StructureRenderer::floorBase()'s own doc): safe
    // to call straight from a settings panel with no renderer/GL context of
    // its own, and cheap enough (one pass over the atoms) to call on every
    // sync rather than caching it.
    const float reach =
        StructureRenderer::floorBase(viewport_->structure().get(),
                                     viewport_->style())
            .reach;
    // Centi-Angstrom ticks, so the slider's own units double as the value —
    // no separate scale factor to keep in sync with the spin box's decimals,
    // unlike the Vectors tab's unitless x0.01 scale/width sliders. +-2x the
    // structure's own footprint comfortably clears it in either direction; it
    // never collapses to a zero-width slider even for a single atom, since
    // floorBase() itself floors `reach` at 1.0.
    const int maxCenti =
        std::max(100, static_cast<int>(std::lround(reach * 200.0)));
    const QSignalBlocker blocker(heightSlider_);
    heightSlider_->setRange(-maxCenti, maxCenti);
    const double clamped =
        std::clamp(static_cast<double>(viewport_->style().floorOffset),
                  -maxCenti / 100.0, maxCenti / 100.0);
    heightSlider_->setValue(static_cast<int>(std::lround(clamped * 100.0)));
}

} // namespace calango::gui
