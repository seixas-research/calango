#pragma once

#include "core/StructuralPhase.hpp"

#include <QColor>
#include <QDialog>

#include <array>

class QCheckBox;
class QTableWidget;

namespace calango::gui {

class ViewportWidget;

/// Representation → "Phase colors…": the flat colour each identified local
/// structure takes when a cast is coloured by "Color by: Phase".
///
/// One row per structure — its name, how many atoms currently carry it, a
/// swatch opening the colour picker, and a Reset returning it to the built-in
/// CNA palette. A structure with no explicit pick shows that default, so the
/// swatches display exactly what the renderer draws rather than an "unset"
/// placeholder that never appears in the viewport.
///
/// The atom counts are the reason this is a table and not four colour buttons
/// in the panel: "how much of this cell is actually fcc" is the number the
/// analysis exists to produce, and reading it off a legend beats counting
/// spheres. They are blank until the analysis has run, i.e. until some cast is
/// set to Phase colouring.
///
/// Edits apply LIVE, on the same contract as Cast Colors: the viewport is
/// visible while choosing, and a preview that only arrives on OK makes the
/// choice blind. Cancel restores what the dialog opened with.
class PhaseColorsDialog : public QDialog {
    Q_OBJECT

public:
    explicit PhaseColorsDialog(ViewportWidget* viewport,
                               QWidget* parent = nullptr);

public Q_SLOTS:
    /// Restores the colours (and the diamond-detection setting) the dialog
    /// opened with. Overridden rather than wired to Cancel alone: Esc and the
    /// close button reach reject() directly and must undo the live edits too.
    void reject() override;

private Q_SLOTS:
    void pickColor(int phase);
    void resetColor(int phase);

private:
    void populate();

    ViewportWidget* viewport_;
    /// State as it was on open, so Cancel is a real undo.
    std::array<QColor, core::kStructuralPhaseCount> initialColors_{};
    core::StructuralPhaseOptions initialOptions_;

    QTableWidget* table_ = nullptr;
    QCheckBox* diamondCheck_ = nullptr;
};

} // namespace calango::gui
