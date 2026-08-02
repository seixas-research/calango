#pragma once

#include "render/StructureRenderer.hpp"

#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QLabel>
#include <QPushButton>
#include <QSlider>
#include <QWidget>

namespace calango::gui {

class ViewportWidget;

/// "Representation" dock panel (Zone 8) — how the atoms and bonds are drawn:
///
/// material, representation mode, colour-by, atom/bond scales, gradient bond
/// shading and the viewport background — plus one icon row opening the editors
/// that own the rest.
///
/// The scene OVERLAYS (unit cell, axes triad, per-atom vectors) live in the
/// "Spatial References" dock, and the scalar color mapping in the "Custom
/// Gradient Coloring" dialog: this panel is about how the atoms themselves are
/// drawn.
///
/// The four editors that change WHAT is drawn — Element Settings, Bond Editor,
/// Edit Polyhedral, Edit Vector Overlay — sit on one icon-only row rather than
/// as four labelled full-width buttons: this dock is the tallest in the app and
/// those labels cost four rows for controls opened occasionally.
class RepresentationPanel : public QWidget {
    Q_OBJECT

public:
    explicit RepresentationPanel(ViewportWidget* viewport, QWidget* parent = nullptr);

Q_SIGNALS:
    /// "Bond Editor…": the host opens the dialog against the current mutable
    /// document. The panel only observes the viewport, so it cannot own an
    /// editor that mutates the structure (and pushes undo).
    void bondEditorRequested();

public Q_SLOTS:
    /// Re-read the atom/bond shader profiles into the "Shading" row.
    ///
    /// The same setting is reachable from Preferences → Rendering, which is
    /// modal, so the host calls this when that dialog closes. Two controls on
    /// one setting is only acceptable while neither can go stale.
    void syncShadingFromRegistry();

private Q_SLOTS:
    void applyColorMode();
    void syncColoringFromViewport();
    /// Open the Cast Setup editor against the viewport's current structure,
    /// then re-sync the cast dropdown with whatever it left behind.
    void openCastSetup();

private:
    /// "Appearance" tab: material, mode, colour-by, the editor icon row,
    /// atom/bond scales, gradient bond shading and the background colour.
    QWidget* buildAppearanceTab();
    /// Re-fill the "Casting" combo from the viewport's cast list, then load
    /// the selected cast into the controls.
    void syncCastsFromViewport();
    /// Show the selected cast's settings in the controls without writing them
    /// back — every control edits the current cast, so this must not fire them.
    void loadSelectedCast();
    /// The cast the panel's representation controls currently edit.
    int selectedCast() const;
    render::StructureRenderer::CastStyle selectedCastStyle() const;
    void applyToSelectedCast(const render::StructureRenderer::CastStyle& cast);
    /// Grey out the surface finishes the active shading model does not read.
    /// PBR and Toon bypass the Blinn-Phong branch entirely, so Standard /
    /// Shiny / Matte become dead under them — but Glassy is the translucency
    /// pass rather than a BRDF, so it stays live in all three.
    void syncSurfaceFinishEnabled();

    ViewportWidget* viewport_;

    QComboBox* castCombo_;
    QComboBox* modeCombo_;
    QComboBox* colorModeCombo_;
    QSlider* atomScaleSlider_;
    QDoubleSpinBox* atomScaleSpin_;
    QSlider* bondWidthSlider_;
    QDoubleSpinBox* bondWidthSpin_;
    QSlider* opacitySlider_ = nullptr;
    QDoubleSpinBox* opacitySpin_;
    QComboBox* surfaceFinishCombo_;
    /// Which BRDF shades every atom and bond (Blinn-Phong / PBR / Toon).
    /// Application-wide and persisted, unlike everything else in this panel —
    /// see the note beside it.
    QComboBox* shadingCombo_ = nullptr;
    /// "Show CN / GCN values" — overlays the active "Color by" scalar on the
    /// atoms. Disabled in Element mode, which has no scalar to print.
    QPushButton* scalarLabelsButton_ = nullptr;
    /// "Cast colors…" — the flat colour each cast takes under "Color by:
    /// Cast". Always visible so the mode's editor is discoverable, enabled
    /// only while the selected cast is coloured by Cast.
    QPushButton* castColorsButton_ = nullptr;
};

} // namespace calango::gui
