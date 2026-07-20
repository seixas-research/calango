#include "gui/AtomColorDialog.hpp"

#include "core/Structure.hpp"
#include "gui/ViewportWidget.hpp"
#include "render/StructureRenderer.hpp"

#include <QColorDialog>
#include <QDialogButtonBox>
#include <QHeaderView>
#include <QPushButton>
#include <QTableWidget>
#include <QVBoxLayout>

#include <set>

namespace calango::gui {

namespace {

/// Elements listed when no structure is loaded.
constexpr int kCommonElements[] = {1, 6, 7, 8, 9, 14, 15, 16, 17, 26, 29, 79};

QString swatchStyle(const QColor& color)
{
    return QStringLiteral("background-color: %1; border: 1px solid #666;")
        .arg(color.name());
}

} // namespace

AtomColorDialog::AtomColorDialog(ViewportWidget* viewport, QWidget* parent)
    : QDialog(parent)
    , viewport_(viewport)
    , table_(new QTableWidget(this))
{
    setWindowTitle(tr("Atom Color Editor"));
    resize(360, 420);

    table_->setColumnCount(3);
    table_->setHorizontalHeaderLabels({tr("Element"), tr("Color"), QString()});
    table_->horizontalHeader()->setStretchLastSection(true);
    table_->verticalHeader()->setVisible(false);
    table_->setSelectionMode(QAbstractItemView::NoSelection);
    table_->setEditTriggers(QAbstractItemView::NoEditTriggers);

    auto* buttons = new QDialogButtonBox(this);
    auto* resetAllButton = buttons->addButton(tr("Reset All"), QDialogButtonBox::ResetRole);
    buttons->addButton(QDialogButtonBox::Close);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(resetAllButton, &QPushButton::clicked, this, [this] { resetAll(); });

    auto* layout = new QVBoxLayout(this);
    layout->addWidget(table_);
    layout->addWidget(buttons);

    populate();
}

void AtomColorDialog::populate()
{
    // Elements present in the current structure, ordered by Z; a sensible
    // default set when nothing is loaded.
    std::set<int> elements;
    if (const auto structure = viewport_->structure(); structure && !structure->empty()) {
        for (const core::Atom& atom : structure->atoms())
            elements.insert(atom.atomicNumber);
    } else {
        elements.insert(std::begin(kCommonElements), std::end(kCommonElements));
    }

    table_->setRowCount(static_cast<int>(elements.size()));
    int row = 0;
    for (const int z : elements) {
        auto* symbolItem = new QTableWidgetItem(
            QString::fromLatin1(core::Elements::data(z).symbol));
        table_->setItem(row, 0, symbolItem);

        auto* colorButton = new QPushButton(table_);
        colorButton->setStyleSheet(
            swatchStyle(render::StructureRenderer::atomColor(z, viewport_->style())));
        connect(colorButton, &QPushButton::clicked, this, [this, z] { editColor(z); });
        table_->setCellWidget(row, 1, colorButton);

        auto* resetButton = new QPushButton(tr("Reset"), table_);
        connect(resetButton, &QPushButton::clicked, this, [this, z] { resetColor(z); });
        table_->setCellWidget(row, 2, resetButton);
        ++row;
    }
}

void AtomColorDialog::editColor(int atomicNumber)
{
    const QColor current =
        render::StructureRenderer::atomColor(atomicNumber, viewport_->style());
    const QColor chosen = QColorDialog::getColor(
        current, this,
        tr("Color for %1").arg(QLatin1String(core::Elements::data(atomicNumber).symbol)));
    if (!chosen.isValid())
        return;
    viewport_->style().colorOverrides[atomicNumber] = chosen;
    viewport_->styleChanged(true);
    populate();
}

void AtomColorDialog::resetColor(int atomicNumber)
{
    viewport_->style().colorOverrides.erase(atomicNumber);
    viewport_->styleChanged(true);
    populate();
}

void AtomColorDialog::resetAll()
{
    viewport_->style().colorOverrides.clear();
    viewport_->styleChanged(true);
    populate();
}

} // namespace calango::gui
