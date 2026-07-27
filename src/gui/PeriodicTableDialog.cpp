#include "gui/PeriodicTableDialog.hpp"

#include "core/Element.hpp"
#include "gui/GuiUtils.hpp"

#include <QDialogButtonBox>
#include <QGridLayout>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

namespace calango::gui {

namespace {

/// (row, column) in the standard 18-column layout; the f-block sits in
/// separate rows 8 (La–Lu) and 9 (Ac–Lr), offset under group 3.
std::pair<int, int> cellFor(int z)
{
    if (z == 1)
        return {0, 0};
    if (z == 2)
        return {0, 17};
    if (z <= 4)
        return {1, z - 3};
    if (z <= 10)
        return {1, z + 7};
    if (z <= 12)
        return {2, z - 11};
    if (z <= 18)
        return {2, z - 1};
    if (z <= 36)
        return {3, z - 19};
    if (z <= 54)
        return {4, z - 37};
    if (z <= 56)
        return {5, z - 55};
    if (z <= 71)
        return {8, z - 57 + 2}; // lanthanides
    if (z <= 86)
        return {5, z - 72 + 3};
    if (z <= 88)
        return {6, z - 87};
    if (z <= 103)
        return {9, z - 89 + 2}; // actinides
    return {6, z - 104 + 3};
}

} // namespace

PeriodicTableDialog::PeriodicTableDialog(int currentZ, QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle(tr("Select Element"));

    auto* grid = new QGridLayout;
    grid->setSpacing(2);

    for (int z = 1; z <= core::Elements::maxZ; ++z) {
        const auto [row, col] = cellFor(z);
        auto* button = new QPushButton(
            QLatin1String(core::Elements::data(z).symbol), this);
        button->setFixedSize(36, 32);
        button->setToolTip(QStringLiteral("Z = %1").arg(z));
        // Standard CPK, the same convention the 3D viewport draws the atoms
        // in. It replaces the chemical-family pastels: this dialog is used to
        // pick an atom to PLACE, so colouring it the way the atom will appear
        // makes the table a preview instead of a second, unrelated key the
        // user has to learn.
        //
        // CPK is not a pastel set — hydrogen is pure white and nitrogen a deep
        // blue — so the label colour has to follow the swatch rather than being
        // fixed, and the selected-cell outline needs a tone that reads against
        // both ends of the range.
        const QColor background = cpkColor(z);
        button->setStyleSheet(
            QStringLiteral("QPushButton { background-color: %1; color: %2; "
                           "border: %3; border-radius: 3px; font-weight: bold; }"
                           "QPushButton:hover { border: 2px solid %2; }")
                .arg(background.name(), readableTextColor(background).name(),
                     z == currentZ ? QStringLiteral("2px solid #d84315")
                                   : QStringLiteral("1px solid #808080")));
        connect(button, &QPushButton::clicked, this, [this, z] {
            selectedZ_ = z;
            accept();
        });
        // f-block rows get one extra row of breathing room via row + 1.
        grid->addWidget(button, row >= 8 ? row + 1 : row, col);
    }
    grid->setRowMinimumHeight(7, 8); // gap above the f-block

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    auto* layout = new QVBoxLayout(this);
    auto* hint = new QLabel(tr("Click an element to select it."), this);
    layout->addWidget(hint);
    layout->addLayout(grid);
    layout->addWidget(buttons);
    setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
}

int PeriodicTableDialog::pickElement(QWidget* parent, int currentZ)
{
    PeriodicTableDialog dialog(currentZ, parent);
    if (dialog.exec() != QDialog::Accepted)
        return 0;
    return dialog.selectedElement();
}

} // namespace calango::gui
