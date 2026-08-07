#include "gui/CastColorsDialog.hpp"

#include "core/Structure.hpp"
#include "gui/GuiUtils.hpp"
#include "gui/ViewportWidget.hpp"
#include "render/StructureRenderer.hpp"

#include <QColorDialog>
#include <QDialogButtonBox>
#include <QHeaderView>
#include <QLabel>
#include <QPushButton>
#include <QTableWidget>
#include <QVBoxLayout>

#include <algorithm>
#include <cstddef>
#include <utility>
#include <vector>

namespace calango::gui {

namespace {

enum Column { ColCast = 0, ColAtoms, ColColor, ColReset, ColumnCount };

} // namespace

CastColorsDialog::CastColorsDialog(ViewportWidget* viewport,
                                   std::shared_ptr<const core::Structure> structure,
                                   QWidget* parent)
    : QDialog(parent)
    , viewport_(viewport)
    , structure_(std::move(structure))
{
    setWindowTitle(tr("Cast Colors"));
    resize(400, 340);

    const auto& style = viewport_->style();
    initialCast0Color_ = style.castColor;
    initialColors_.reserve(style.castStyles.size());
    for (const auto& cast : style.castStyles)
        initialColors_.push_back(cast.castColor);

    auto* layout = new QVBoxLayout(this);

    auto* note = new QLabel(
        tr("Under <b>Color by: Cast</b> every atom takes its cast's color — "
           "the substrate one flat color, the adsorbate another, whatever the "
           "elements. Casts without an explicit pick cycle a default palette; "
           "Reset returns a cast to it."),
        this);
    note->setWordWrap(true);
    note->setTextFormat(Qt::RichText);
    layout->addWidget(note);

    table_ = new QTableWidget(this);
    table_->setColumnCount(ColumnCount);
    table_->setHorizontalHeaderLabels(
        {tr("Cast"), tr("Atoms"), tr("Color"), QString()});
    table_->horizontalHeader()->setStretchLastSection(true);
    table_->verticalHeader()->setVisible(false);
    // Rows are edited through their widgets, like Element Settings: there is
    // no row-level operation here for a selection to feed.
    table_->setSelectionMode(QAbstractItemView::NoSelection);
    table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    layout->addWidget(table_, 1);

    auto* buttons = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    populate();
}

void CastColorsDialog::populate()
{
    const auto& style = viewport_->style();
    const int count = style.castCount();

    // Atoms per cast, under the renderer's own rule: an assignment that is
    // absent — or stale, which is what a structure replacement leaves behind —
    // means every atom is in cast 0. Read-only here, unlike Cast Setup, which
    // owns the assignment and repairs it in place.
    std::vector<int> populations(static_cast<std::size_t>(count), 0);
    const std::vector<int>& assignment = style.atomCasts;
    const std::size_t atomCount = structure_ ? structure_->size() : 0;
    if (assignment.size() == atomCount) {
        for (const int cast : assignment)
            if (cast >= 0 && cast < count)
                ++populations[static_cast<std::size_t>(cast)];
    } else {
        populations[0] = static_cast<int>(atomCount);
    }

    table_->setRowCount(count);
    for (int cast = 0; cast < count; ++cast) {
        const auto readOnly = [](const QString& text) {
            auto* item = new QTableWidgetItem(text);
            item->setFlags(item->flags() & ~Qt::ItemIsEditable);
            return item;
        };
        // A cast is referred to by its number alone, matching Cast Setup and
        // the Representation panel's dropdown.
        table_->setItem(
            cast, ColCast,
            readOnly(render::StructureRenderer::castLabel(cast, style)));
        table_->setItem(
            cast, ColAtoms,
            readOnly(QString::number(populations[static_cast<std::size_t>(cast)])));

        auto* swatch = qobject_cast<QPushButton*>(
            table_->cellWidget(cast, ColColor));
        if (!swatch) {
            swatch = new QPushButton(table_);
            table_->setCellWidget(cast, ColColor, swatch);
            connect(swatch, &QPushButton::clicked, this,
                    [this, cast] { pickColor(cast); });
        }
        // The EFFECTIVE colour, defaults included — the swatch must show what
        // the viewport draws, and castColor() is the renderer's own answer.
        setButtonColor(swatch,
                       render::StructureRenderer::castColor(cast, style));

        auto* reset = qobject_cast<QPushButton*>(
            table_->cellWidget(cast, ColReset));
        if (!reset) {
            reset = new QPushButton(tr("Reset"), table_);
            reset->setToolTip(
                tr("Drop the explicit color; the cast returns to its slot in "
                   "the default palette."));
            table_->setCellWidget(cast, ColReset, reset);
            connect(reset, &QPushButton::clicked, this,
                    [this, cast] { resetColor(cast); });
        }
    }
    table_->resizeColumnsToContents();
}

void CastColorsDialog::pickColor(int cast)
{
    auto& style = viewport_->style();
    const QColor chosen = QColorDialog::getColor(
        render::StructureRenderer::castColor(cast, style), this,
        tr("Color for Cast %1").arg(cast));
    if (!chosen.isValid())
        return; // picker cancelled — not a reset, which has its own button
    // Through setCastStyle rather than into castStyles directly: cast 0's
    // colour lives as a Style member, and setCastStyle is what writes through
    // to it — there is only ever one copy of cast 0's state.
    auto value = style.castStyle(cast);
    value.castColor = chosen;
    style.setCastStyle(cast, value);
    apply();
    populate();
}

void CastColorsDialog::resetColor(int cast)
{
    auto& style = viewport_->style();
    auto value = style.castStyle(cast);
    // Invalid = "no explicit pick": the renderer falls back to the default
    // qualitative cycle, which is a meaning a picked colour cannot express —
    // hence a Reset button rather than a special entry in the picker.
    value.castColor = QColor();
    style.setCastStyle(cast, value);
    apply();
    populate();
}

void CastColorsDialog::apply()
{
    // Rebuild, not just a repaint: the colours are baked per atom into the
    // instance buffers, so a repaint alone would draw the old ones.
    viewport_->styleChanged(true);
}

void CastColorsDialog::reject()
{
    auto& style = viewport_->style();
    style.castColor = initialCast0Color_;
    // The dialog is modal and cannot resize the cast list, so the sizes
    // match; the guard only protects against a future non-modal caller.
    const std::size_t restorable =
        std::min(style.castStyles.size(), initialColors_.size());
    for (std::size_t i = 0; i < restorable; ++i)
        style.castStyles[i].castColor = initialColors_[i];
    apply();
    QDialog::reject();
}

} // namespace calango::gui
