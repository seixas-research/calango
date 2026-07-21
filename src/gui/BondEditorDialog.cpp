#include "gui/BondEditorDialog.hpp"

#include "gui/ViewportWidget.hpp"

#include <QDialogButtonBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QVBoxLayout>

#include <algorithm>

namespace calango::gui {

BondEditorDialog::BondEditorDialog(std::shared_ptr<core::Structure> structure,
                                   ViewportWidget* viewport, QWidget* parent)
    : QDialog(parent)
    , structure_(std::move(structure))
    , viewport_(viewport)
    , autoBondsCheck_(new QCheckBox(tr("Automatic bond detection"), this))
    , toleranceSpin_(new QDoubleSpinBox(this))
    , atomISpin_(new QSpinBox(this))
    , atomJSpin_(new QSpinBox(this))
    , useSelectionButton_(new QPushButton(tr("From Selection"), this))
    , pairInfoLabel_(new QLabel(this))
    , overrideList_(new QListWidget(this))
{
    setWindowTitle(tr("Bond Editor"));
    resize(480, 520);

    // --- Automatic perception ---------------------------------------------
    auto* autoGroup = new QGroupBox(tr("Automatic Perception"), this);
    auto* autoForm = new QFormLayout(autoGroup);

    autoBondsCheck_->setChecked(viewport_->style().autoBonds);
    autoBondsCheck_->setToolTip(tr("Off: only manually added bonds are drawn"));
    autoForm->addRow(autoBondsCheck_);
    connect(autoBondsCheck_, &QCheckBox::toggled, this, [this](bool on) {
        viewport_->style().autoBonds = on;
        viewport_->styleChanged(true);
    });

    toleranceSpin_->setRange(0.50, 2.50);
    toleranceSpin_->setDecimals(2);
    toleranceSpin_->setSingleStep(0.05);
    toleranceSpin_->setValue(viewport_->style().bondTolerance);
    toleranceSpin_->setToolTip(
        tr("Bond when d < multiplier × (r_cov(i) + r_cov(j))"));
    autoForm->addRow(tr("Covalent cutoff multiplier:"), toleranceSpin_);
    connect(toleranceSpin_, &QDoubleSpinBox::valueChanged, this, [this](double value) {
        viewport_->style().bondTolerance = static_cast<float>(value);
        viewport_->styleChanged(true);
    });

    // --- Manual bonds ------------------------------------------------------
    auto* manualGroup = new QGroupBox(tr("Manual Bonds"), this);
    auto* manualLayout = new QVBoxLayout(manualGroup);

    const int atomCount = static_cast<int>(structure_->size());
    auto* pairRow = new QHBoxLayout;
    for (auto* spin : {atomISpin_, atomJSpin_}) {
        spin->setRange(1, std::max(1, atomCount)); // 1-based, as displayed in tables
        pairRow->addWidget(spin, 1);
        connect(spin, &QSpinBox::valueChanged, this,
                [this](int) { refreshOverrideList(); });
    }
    atomJSpin_->setValue(std::min(2, std::max(1, atomCount)));
    pairRow->addWidget(useSelectionButton_);
    manualLayout->addLayout(pairRow);

    pairInfoLabel_->setWordWrap(true);
    manualLayout->addWidget(pairInfoLabel_);

    auto* actionRow = new QHBoxLayout;
    auto* addButton = new QPushButton(tr("Add Bond"), manualGroup);
    auto* suppressButton = new QPushButton(tr("Suppress Bond"), manualGroup);
    actionRow->addWidget(addButton);
    actionRow->addWidget(suppressButton);
    actionRow->addStretch(1);
    manualLayout->addLayout(actionRow);
    connect(addButton, &QPushButton::clicked, this, &BondEditorDialog::addBond);
    connect(suppressButton, &QPushButton::clicked, this, &BondEditorDialog::suppressBond);
    connect(useSelectionButton_, &QPushButton::clicked,
            this, &BondEditorDialog::useSelection);

    manualLayout->addWidget(new QLabel(tr("Active overrides:"), manualGroup));
    manualLayout->addWidget(overrideList_, 1);

    auto* clearRow = new QHBoxLayout;
    auto* clearButton = new QPushButton(tr("Clear Override"), manualGroup);
    auto* clearAllButton = new QPushButton(tr("Clear All"), manualGroup);
    clearRow->addWidget(clearButton);
    clearRow->addWidget(clearAllButton);
    clearRow->addStretch(1);
    manualLayout->addLayout(clearRow);
    connect(clearButton, &QPushButton::clicked,
            this, &BondEditorDialog::clearSelectedOverride);
    connect(clearAllButton, &QPushButton::clicked,
            this, &BondEditorDialog::clearAllOverrides);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    auto* layout = new QVBoxLayout(this);
    layout->addWidget(autoGroup);
    layout->addWidget(manualGroup, 1);
    layout->addWidget(buttons);

    // Track the viewport selection so "From Selection" lights up exactly
    // when two atoms are picked.
    connect(viewport_, &ViewportWidget::selectionChanged, this,
            [this](int count) { useSelectionButton_->setEnabled(count == 2); });
    useSelectionButton_->setEnabled(viewport_->selection().size() == 2);
    useSelectionButton_->setToolTip(
        tr("Fill the pair from the viewport selection (select exactly two "
           "atoms with Ctrl+click)"));

    refreshOverrideList();
}

std::pair<int, int> BondEditorDialog::currentPair() const
{
    return {atomISpin_->value() - 1, atomJSpin_->value() - 1};
}

void BondEditorDialog::useSelection()
{
    const auto& selection = viewport_->selection();
    if (selection.size() != 2)
        return;
    auto it = selection.begin();
    atomISpin_->setValue(*it + 1);
    atomJSpin_->setValue(*std::next(it) + 1);
}

void BondEditorDialog::addBond()
{
    const auto [i, j] = currentPair();
    structure_->addBondOverride(i, j);
    refreshOverrideList();
    Q_EMIT bondsEdited();
}

void BondEditorDialog::suppressBond()
{
    const auto [i, j] = currentPair();
    structure_->removeBondOverride(i, j);
    refreshOverrideList();
    Q_EMIT bondsEdited();
}

void BondEditorDialog::clearSelectedOverride()
{
    auto* item = overrideList_->currentItem();
    if (!item)
        return;
    const QPoint pair = item->data(Qt::UserRole).toPoint();
    structure_->clearBondOverride(pair.x(), pair.y());
    refreshOverrideList();
    Q_EMIT bondsEdited();
}

void BondEditorDialog::clearAllOverrides()
{
    structure_->clearBondOverrides();
    refreshOverrideList();
    Q_EMIT bondsEdited();
}

void BondEditorDialog::refreshOverrideList()
{
    overrideList_->clear();
    const auto describe = [this](int i, int j) {
        const auto& atoms = structure_->atoms();
        QString text = QStringLiteral("%1 – %2").arg(i + 1).arg(j + 1);
        if (i >= 0 && j >= 0 && i < static_cast<int>(atoms.size())
            && j < static_cast<int>(atoms.size())) {
            const core::Vec3 d = atoms[static_cast<std::size_t>(j)].position
                - atoms[static_cast<std::size_t>(i)].position;
            text += QStringLiteral("   (%1%2 – %3%4, %5 Å)")
                        .arg(QLatin1String(atoms[static_cast<std::size_t>(i)].symbol()))
                        .arg(i + 1)
                        .arg(QLatin1String(atoms[static_cast<std::size_t>(j)].symbol()))
                        .arg(j + 1)
                        .arg(d.norm(), 0, 'f', 3);
        }
        return text;
    };

    for (const auto& [i, j] : structure_->addedBonds()) {
        auto* item = new QListWidgetItem(tr("added      %1").arg(describe(i, j)),
                                         overrideList_);
        item->setData(Qt::UserRole, QPoint(i, j));
    }
    for (const auto& [i, j] : structure_->removedBonds()) {
        auto* item = new QListWidgetItem(tr("suppressed %1").arg(describe(i, j)),
                                         overrideList_);
        item->setData(Qt::UserRole, QPoint(i, j));
    }

    // Live info for the currently entered pair.
    const auto [i, j] = currentPair();
    const auto& atoms = structure_->atoms();
    if (i != j && i >= 0 && j >= 0 && i < static_cast<int>(atoms.size())
        && j < static_cast<int>(atoms.size())) {
        const core::Vec3 d = atoms[static_cast<std::size_t>(j)].position
            - atoms[static_cast<std::size_t>(i)].position;
        const double cutoff = viewport_->style().bondTolerance
            * (atoms[static_cast<std::size_t>(i)].covalentRadius()
               + atoms[static_cast<std::size_t>(j)].covalentRadius());
        pairInfoLabel_->setText(
            tr("%1%2 – %3%4: d = %5 Å (auto cutoff %6 Å)")
                .arg(QLatin1String(atoms[static_cast<std::size_t>(i)].symbol()))
                .arg(i + 1)
                .arg(QLatin1String(atoms[static_cast<std::size_t>(j)].symbol()))
                .arg(j + 1)
                .arg(d.norm(), 0, 'f', 3)
                .arg(cutoff, 0, 'f', 3));
    } else {
        pairInfoLabel_->setText(tr("Pick two different atoms."));
    }
}

} // namespace calango::gui
