#include "gui/NeighborCellsDialog.hpp"

#include "core/Structure.hpp"
#include "gui/ViewportWidget.hpp"

#include <QCheckBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QGridLayout>
#include <QLabel>
#include <QPushButton>
#include <QSignalBlocker>
#include <QVBoxLayout>

namespace calango::gui {

namespace {
/// Axis captions. The lattice-vector names are what the numbers actually
/// index — a fractional coordinate runs along a, b, c, not along x, y, z —
/// but the request, and every user's mental model, calls them x/y/z, so both
/// are shown.
const char* const kAxisNames[3] = {"x  (a)", "y  (b)", "z  (c)"};
} // namespace

NeighborCellsDialog::NeighborCellsDialog(ViewportWidget* viewport,
                                         QWidget* parent)
    : QDialog(parent), viewport_(viewport)
{
    setWindowTitle(tr("Show Neighboring Cells"));

    auto* layout = new QVBoxLayout(this);

    auto* intro = new QLabel(
        tr("Periodic images over a window in fractional coordinates. "
           "<b>0 → 1</b> is the home cell alone."),
        this);
    intro->setWordWrap(true);
    layout->addWidget(intro);

    // Two columns, minima then maxima, one row per axis — the pair for one
    // axis reads across, and the whole minimum column reads down.
    auto* grid = new QGridLayout;
    grid->addWidget(new QLabel(tr("<b>Minimum</b>"), this), 0, 1,
                    Qt::AlignHCenter);
    grid->addWidget(new QLabel(tr("<b>Maximum</b>"), this), 0, 2,
                    Qt::AlignHCenter);

    const auto& range = viewport_->style().neighborCells;
    for (int axis = 0; axis < 3; ++axis) {
        grid->addWidget(new QLabel(QLatin1String(kAxisNames[axis]), this),
                        axis + 1, 0);
        const auto makeSpin = [this](double value) {
            auto* spin = new QDoubleSpinBox(this);
            // ±10 cells is already 21 copies of the scene along one axis and
            // 9261 in the worst case — far past where the view stays readable,
            // and a sane guard on a control that multiplies the geometry.
            spin->setRange(-10.0, 10.0);
            spin->setDecimals(2);
            // Whole steps: the drawn set changes one cell at a time, so a
            // 0.1 step would move the arrows four times for nothing.
            spin->setSingleStep(1.0);
            spin->setValue(value);
            spin->setKeyboardTracking(false);
            connect(spin, &QDoubleSpinBox::valueChanged, this,
                    [this] { apply(); });
            return spin;
        };
        minSpin_[axis] = makeSpin(range.min[axis]);
        maxSpin_[axis] = makeSpin(range.max[axis]);
        grid->addWidget(minSpin_[axis], axis + 1, 1);
        grid->addWidget(maxSpin_[axis], axis + 1, 2);
    }
    grid->setColumnStretch(1, 1);
    grid->setColumnStretch(2, 1);
    layout->addLayout(grid);

    summary_ = new QLabel(this);
    summary_->setWordWrap(true);
    layout->addWidget(summary_);

    edgesCheck_ = new QCheckBox(tr("Draw the cell edges of the neighbors"), this);
    edgesCheck_->setChecked(range.showEdges);
    edgesCheck_->setToolTip(
        tr("On: every drawn image gets its own wireframe box, so the "
           "periodicity is explicit and each cell's contents are separable.\n"
           "Off: only the atoms and bonds of the neighbors are drawn, which is "
           "what a figure of an extended structure normally wants — the home "
           "cell keeps its own outline (\"Show unit cell\" above)."));
    connect(edgesCheck_, &QCheckBox::toggled, this, [this] { apply(); });
    layout->addWidget(edgesCheck_);

    // The "purely visual" note that stood here is gone: it repeated what the
    // edges tool tip and the summary line already say, and a dialog this small
    // cannot afford a paragraph that only restates its neighbours.

    // Reset, not OK/Cancel: the dialog applies live, so there is no pending
    // state to accept — but there is a default worth one click.
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Reset
                                             | QDialogButtonBox::Close,
                                         this);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(buttons->button(QDialogButtonBox::Reset), &QPushButton::clicked,
            this, [this] {
                const QSignalBlocker blockEdges(edgesCheck_);
                edgesCheck_->setChecked(true);
                for (int axis = 0; axis < 3; ++axis) {
                    const QSignalBlocker blockMin(minSpin_[axis]);
                    const QSignalBlocker blockMax(maxSpin_[axis]);
                    minSpin_[axis]->setValue(0.0);
                    maxSpin_[axis]->setValue(1.0);
                }
                apply();
            });
    layout->addWidget(buttons);

    updateSummary();
}

void NeighborCellsDialog::apply()
{
    if (updating_)
        return;
    updating_ = true;
    render::NeighborCellRange range;
    for (int axis = 0; axis < 3; ++axis) {
        range.min[axis] = minSpin_[axis]->value();
        range.max[axis] = maxSpin_[axis]->value();
    }
    range.showEdges = edgesCheck_->isChecked();
    viewport_->style().neighborCells = range;
    // Extra instances: the geometry buffers have to be rebuilt, not merely
    // redrawn with new uniforms.
    viewport_->styleChanged(true);
    updating_ = false;
    updateSummary();
}

void NeighborCellsDialog::updateSummary()
{
    const auto& range = viewport_->style().neighborCells;
    const auto offsets = range.cellOffsets();
    const auto structure = viewport_->structure();
    const bool periodic = structure && structure->cell().isDefined();

    if (!periodic) {
        // Say why nothing happens rather than letting the user conclude the
        // control is broken: with no lattice there is no vector to repeat by.
        summary_->setText(
            tr("<b style='color:#d9534f;'>This structure has no unit cell</b>, "
               "so there are no periodic images to draw. Define one in "
               "<b>Edit Structure…</b> first."));
        return;
    }
    if (offsets.size() == 1) {
        summary_->setText(tr("Drawing the home cell only."));
        return;
    }
    summary_->setText(
        tr("Drawing <b>%n cell(s)</b> — the geometry on screen is that many "
           "copies of the structure.",
           nullptr, static_cast<int>(offsets.size())));
}

} // namespace calango::gui
