#include "gui/CellRelaxationControls.hpp"

#include <QCheckBox>
#include <QComboBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QSignalBlocker>
#include <QWidget>

#include <utility>

namespace calango::gui {

CellRelaxationControls::CellRelaxationControls(QObject* parent)
    : QObject(parent)
{
}

void CellRelaxationControls::build(QWidget* parent, QFormLayout* form,
                                   std::function<void()> onChanged)
{
    form_ = form;
    onChanged_ = std::move(onChanged);

    relaxCellCheck_ =
        new QCheckBox(tr("Relax the unit cell (variable-cell)"), parent);
    relaxCellCheck_->setToolTip(
        tr("Also optimize the lattice via an ASE cell filter."));
    form->addRow(relaxCellCheck_);

    cellFilterCombo_ = new QComboBox(parent);
    cellFilterCombo_->addItem(tr("FrechetCellFilter (recommended)"));
    cellFilterCombo_->addItem(tr("UnitCellFilter"));
    form->addRow(tr("Cell filter:"), cellFilterCombo_);

    stressMaskCombo_ = new QComboBox(parent);
    stressMaskCombo_->addItem(tr("Anisotropic (full stress)"));
    stressMaskCombo_->addItem(tr("Hydrostatic (isotropic)"));
    // A named preset rather than "set the six ticks yourself": relaxing a 2D
    // sheet in-plane is the single most common non-trivial mask, and getting
    // it wrong is silent — leaving zz free lets the vacuum gap collapse onto
    // the slab over a few dozen steps, which looks like a converging
    // relaxation until the structure is unrecognizable.
    stressMaskCombo_->addItem(tr("2Dxy (in-plane only)"));
    stressMaskCombo_->addItem(tr("Custom (Voigt mask)"));
    stressMaskCombo_->setItemData(
        kStressMask2Dxy,
        tr("Relax only the components that live in the xy plane — xx, yy and "
           "xy — and hold zz, xz and yz fixed.\n\n"
           "For a slab or a 2D material with a vacuum gap along z: the "
           "in-plane lattice constants are physical and should relax, while "
           "the cell height is an arbitrary padding whose stress is "
           "meaningless. It also keeps the layer flat, since a free xz or yz "
           "shears the vacuum."),
        Qt::ToolTipRole);
    form->addRow(tr("Stress mask:"), stressMaskCombo_);

    // Custom Voigt mask [xx, yy, zz, yz, xz, xy]: tick the components to relax
    // (e.g. only zz for a 2D layered material / heterostructure).
    voigtRow_ = new QWidget(parent);
    auto* voigtLayout = new QHBoxLayout(voigtRow_);
    voigtLayout->setContentsMargins(0, 0, 0, 0);
    const char* labels[6] = {"xx", "yy", "zz", "yz", "xz", "xy"};
    for (int i = 0; i < 6; ++i) {
        voigtChecks_[i] = new QCheckBox(QLatin1String(labels[i]), voigtRow_);
        voigtChecks_[i]->setChecked(true);
        voigtLayout->addWidget(voigtChecks_[i]);
    }
    form->addRow(tr("Voigt components:"), voigtRow_);

    const auto changed = [this] {
        updateEnabled();
        if (onChanged_)
            onChanged_();
    };
    connect(relaxCellCheck_, &QCheckBox::toggled, this, changed);
    connect(stressMaskCombo_, &QComboBox::currentIndexChanged, this, changed);
    connect(cellFilterCombo_, &QComboBox::currentIndexChanged, this, [this] {
        if (onChanged_)
            onChanged_();
    });
    for (QCheckBox* check : voigtChecks_)
        connect(check, &QCheckBox::toggled, this, [this] {
            if (onChanged_)
                onChanged_();
        });

    updateEnabled();
}

void CellRelaxationControls::setAvailable(bool available)
{
    available_ = available;
    updateEnabled();
}

bool CellRelaxationControls::relaxesCell() const
{
    return available_ && relaxCellCheck_ && relaxCellCheck_->isChecked();
}

void CellRelaxationControls::updateEnabled()
{
    if (!relaxCellCheck_)
        return;
    relaxCellCheck_->setEnabled(available_);
    const bool relax = relaxesCell();
    cellFilterCombo_->setEnabled(relax);
    stressMaskCombo_->setEnabled(relax);

    const int mask = stressMaskCombo_->currentIndex();
    if (mask == kStressMask2Dxy) {
        // Voigt order is [xx, yy, zz, yz, xz, xy]: relax 0, 1 and 5; hold
        // 2, 3 and 4. Written into the ticks rather than only into the config
        // so the preset is VISIBLE — the row below is the read-out of what
        // "2Dxy" means, which is how a user learns the mask instead of
        // trusting a label.
        static constexpr bool kInPlane[6] = {true, true, false,
                                             false, false, true};
        for (int i = 0; i < 6; ++i) {
            const QSignalBlocker blocker(voigtChecks_[i]);
            voigtChecks_[i]->setChecked(kInPlane[i]);
        }
    }
    // Shown for both mask-carrying modes, editable only for Custom: under the
    // preset the ticks are a read-out, and letting them be edited would leave
    // the combo claiming "2Dxy" for an arbitrary mask.
    voigtRow_->setVisible(relax && mask >= kStressMask2Dxy);
    voigtRow_->setEnabled(relax && mask == kStressMaskCustom);
}

void CellRelaxationControls::applyTo(core::CalculatorConfig& config) const
{
    if (!relaxCellCheck_)
        return;
    config.relaxCell = relaxesCell();
    config.cellFilter = cellFilterCombo_->currentIndex() == 1
        ? core::CellFilter::UnitCell
        : core::CellFilter::FrechetCell;
    config.cellHydrostatic =
        stressMaskCombo_->currentIndex() == kStressMaskHydrostatic;
    // Both the 2Dxy preset and Custom write an explicit Voigt mask; they
    // differ only in who chose the six bits. updateEnabled() has already put
    // the preset's mask into the checkboxes, so one read serves both.
    config.cellCustomMask =
        stressMaskCombo_->currentIndex() >= kStressMask2Dxy;
    for (int i = 0; i < 6; ++i)
        config.cellMask[i] = voigtChecks_[i]->isChecked();
}

} // namespace calango::gui
