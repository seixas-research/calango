#pragma once

#include <QColor>
#include <QDialog>

#include <memory>
#include <vector>

class QTableWidget;

namespace calango::core {
class Structure;
}

namespace calango::gui {

/// Representation → "Cast colors…": the flat colour each cast takes when it
/// is coloured by "Color by: Cast".
///
/// One row per cast — its number, how many atoms it currently holds, a colour
/// swatch opening the colour picker, and a Reset returning it to the default
/// palette. A cast with no explicit pick shows its slot in that default
/// qualitative cycle, so the swatches display exactly what the renderer draws
/// rather than an "unset" placeholder the viewport never shows.
///
/// It edits the colours LIVE, on the contract Cast Setup documents: the
/// viewport is a dock the user is looking at while choosing, and a preview
/// that only appears on OK would make the choice blind. Cancel restores the
/// colours as they were on open.
class CastColorsDialog : public QDialog {
    Q_OBJECT

public:
    /// `viewport` owns the style the cast colours live in and is repainted on
    /// every pick. `structure` only supplies the per-cast atom counts; a null
    /// structure shows every cast with zero atoms but still edits its colour.
    CastColorsDialog(class ViewportWidget* viewport,
                     std::shared_ptr<const core::Structure> structure,
                     QWidget* parent = nullptr);

public Q_SLOTS:
    /// Restores the colours the dialog opened with. Overridden rather than
    /// wired to the Cancel button alone: Esc and the window close button
    /// reach reject() directly, and those must undo the live edits too.
    void reject() override;

private Q_SLOTS:
    /// Open the colour picker for `cast` and apply the pick immediately.
    void pickColor(int cast);
    /// Drop `cast`'s explicit colour, returning it to the default cycle.
    void resetColor(int cast);

private:
    void populate();
    /// Push the edited colours into the viewport and repaint.
    void apply();

    ViewportWidget* viewport_;
    std::shared_ptr<const core::Structure> structure_;
    /// Colours as they were on open — cast 0's (a Style member) and casts
    /// 1..N's — so Cancel is a real undo rather than "close and hope". Only
    /// the colours: this dialog cannot add or remove casts, and it is modal,
    /// so the cast count cannot change underneath these.
    QColor initialCast0Color_;
    std::vector<QColor> initialColors_;

    QTableWidget* table_ = nullptr;
};

} // namespace calango::gui
