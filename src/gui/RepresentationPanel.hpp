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
/// "Cell, Axes & Vectors" dock, and the scalar colour mapping in the "Custom
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
    /// "Complete with hydrogens": same reason as above — filling in the
    /// missing hydrogens ADDS atoms, and only the host owns the mutable
    /// document and the undo stack that has to record them.
    void hydrogenCompletionRequested();

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

    ViewportWidget* viewport_;

    QComboBox* castCombo_;
    QComboBox* modeCombo_;
    QComboBox* colorModeCombo_;
    QSlider* atomScaleSlider_;
    QDoubleSpinBox* atomScaleSpin_;
    QSlider* bondWidthSlider_;
    QDoubleSpinBox* bondWidthSpin_;
    QSlider* opacitySlider_;
    QDoubleSpinBox* opacitySpin_;
    QCheckBox* gradientBondsCheck_;
    QCheckBox* showHydrogensCheck_;
    QComboBox* surfaceFinishCombo_;
};

} // namespace calango::gui
