#include "gui/PeriodicTableDialog.hpp"

#include "core/Element.hpp"

#include <QDialogButtonBox>
#include <QGridLayout>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

#include <initializer_list>

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

bool oneOf(int z, std::initializer_list<int> values)
{
    for (const int v : values)
        if (z == v)
            return true;
    return false;
}

/// Background color by chemical family (pastels; black text stays legible).
QColor familyColor(int z)
{
    if (oneOf(z, {3, 11, 19, 37, 55, 87}))
        return {0xFF, 0x8A, 0x65}; // alkali metals
    if (oneOf(z, {4, 12, 20, 38, 56, 88}))
        return {0xFF, 0xD5, 0x4F}; // alkaline earth metals
    if (z >= 57 && z <= 71)
        return {0x4D, 0xD0, 0xE1}; // lanthanides
    if (z >= 89 && z <= 103)
        return {0x4D, 0xB6, 0xAC}; // actinides
    if ((z >= 21 && z <= 30) || (z >= 39 && z <= 48) || (z >= 72 && z <= 80)
        || (z >= 104 && z <= 112))
        return {0x90, 0xCA, 0xF9}; // transition metals
    if (oneOf(z, {5, 14, 32, 33, 51, 52, 84}))
        return {0xCE, 0x93, 0xD8}; // metalloids
    if (oneOf(z, {1, 6, 7, 8, 15, 16, 34}))
        return {0xA5, 0xD6, 0xA7}; // reactive nonmetals
    if (oneOf(z, {9, 17, 35, 53, 85, 117}))
        return {0xDC, 0xE7, 0x75}; // halogens
    if (oneOf(z, {2, 10, 18, 36, 54, 86, 118}))
        return {0xF4, 0x8F, 0xB1}; // noble gases
    return {0xB0, 0xBE, 0xC5};     // post-transition metals / unclassified
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
        button->setStyleSheet(
            QStringLiteral("QPushButton { background-color: %1; color: #202020; "
                           "border: %2; border-radius: 3px; font-weight: bold; }"
                           "QPushButton:hover { border: 2px solid #202020; }")
                .arg(familyColor(z).name(),
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
