#include "gui/BondEditorDialog.hpp"

#include "gui/ViewportWidget.hpp"

#include "core/Element.hpp"

#include <QDialogButtonBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QTabWidget>
#include <QVBoxLayout>

#include <algorithm>
#include <set>

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
    resize(520, 620);

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

    // --- Manual bonds: two operational modes ------------------------------
    auto* manualGroup = new QGroupBox(tr("Manual Bonds"), this);
    auto* manualLayout = new QVBoxLayout(manualGroup);
    auto* modeTabs = new QTabWidget(manualGroup);

    // ----- Mode 1: By Chemical Elements -----------------------------------
    auto* elementTab = new QWidget(modeTabs);
    auto* elementForm = new QFormLayout(elementTab);
    elementACombo_ = makeElementCombo();
    elementBCombo_ = makeElementCombo();
    // Sensible default second element: the next distinct species when there is
    // more than one, so the pair does not start as a homonuclear A–A.
    if (elementBCombo_->count() > 1)
        elementBCombo_->setCurrentIndex(1);
    auto* elementPairRow = new QHBoxLayout;
    elementPairRow->addWidget(elementACombo_, 1);
    elementPairRow->addWidget(new QLabel(QStringLiteral("–"), elementTab));
    elementPairRow->addWidget(elementBCombo_, 1);
    elementForm->addRow(tr("Element pair:"), elementPairRow);

    minCutoffSpin_ = new QDoubleSpinBox(elementTab);
    minCutoffSpin_->setRange(0.0, 20.0);
    minCutoffSpin_->setDecimals(2);
    minCutoffSpin_->setSingleStep(0.1);
    minCutoffSpin_->setValue(0.0);
    minCutoffSpin_->setSuffix(tr(" Å"));
    elementForm->addRow(tr("Min. distance:"), minCutoffSpin_);

    maxCutoffSpin_ = new QDoubleSpinBox(elementTab);
    maxCutoffSpin_->setRange(0.1, 20.0);
    maxCutoffSpin_->setDecimals(2);
    maxCutoffSpin_->setSingleStep(0.1);
    maxCutoffSpin_->setValue(2.0);
    maxCutoffSpin_->setSuffix(tr(" Å"));
    maxCutoffSpin_->setToolTip(
        tr("Bond every pair of the chosen elements whose separation lies in "
           "the [min, max] window. Distances are direct (no periodic images)."));
    elementForm->addRow(tr("Max. distance:"), maxCutoffSpin_);

    elementMatchLabel_ = new QLabel(elementTab);
    elementMatchLabel_->setWordWrap(true);
    elementForm->addRow(elementMatchLabel_);

    auto* elementActionRow = new QHBoxLayout;
    auto* addElementsButton = new QPushButton(tr("Bond Matching Pairs"), elementTab);
    auto* removeElementsButton =
        new QPushButton(tr("Unbond Matching Pairs"), elementTab);
    elementActionRow->addWidget(addElementsButton);
    elementActionRow->addWidget(removeElementsButton);
    elementActionRow->addStretch(1);
    elementForm->addRow(elementActionRow);
    connect(addElementsButton, &QPushButton::clicked, this,
            &BondEditorDialog::addBondsByElements);
    connect(removeElementsButton, &QPushButton::clicked, this,
            &BondEditorDialog::removeBondsByElements);
    // Live count of how many pairs the current selection/window would affect.
    const auto refreshElementMatch = [this] {
        const std::size_t n = matchingElementPairs().size();
        elementMatchLabel_->setText(
            tr("%1 atom pair(s) match the current element/distance window.")
                .arg(n));
    };
    for (QComboBox* combo : {elementACombo_, elementBCombo_})
        connect(combo, &QComboBox::currentIndexChanged, this,
                [refreshElementMatch](int) { refreshElementMatch(); });
    for (QDoubleSpinBox* spin : {minCutoffSpin_, maxCutoffSpin_})
        connect(spin, &QDoubleSpinBox::valueChanged, this,
                [refreshElementMatch](double) { refreshElementMatch(); });
    refreshElementMatch();
    modeTabs->addTab(elementTab, tr("By Chemical Elements"));

    // ----- Mode 2: By Atomic Indices --------------------------------------
    auto* indexTab = new QWidget(modeTabs);
    auto* indexLayout = new QVBoxLayout(indexTab);

    const int atomCount = static_cast<int>(structure_->size());
    auto* pairRow = new QHBoxLayout;
    pairRow->addWidget(new QLabel(tr("Atom"), indexTab));
    for (auto* spin : {atomISpin_, atomJSpin_}) {
        spin->setRange(1, std::max(1, atomCount)); // 1-based, as displayed in tables
        pairRow->addWidget(spin, 1);
        connect(spin, &QSpinBox::valueChanged, this,
                [this](int) { refreshOverrideList(); });
    }
    atomJSpin_->setValue(std::min(2, std::max(1, atomCount)));
    pairRow->addWidget(useSelectionButton_);
    indexLayout->addLayout(pairRow);

    auto* orderRow = new QHBoxLayout;
    orderRow->addWidget(new QLabel(tr("Bond order:"), indexTab));
    bondOrderCombo_ = new QComboBox(indexTab);
    bondOrderCombo_->addItem(tr("Single"), 1);
    bondOrderCombo_->addItem(tr("Double"), 2);
    bondOrderCombo_->addItem(tr("Triple"), 3);
    bondOrderCombo_->setToolTip(
        tr("Order-n bonds render as n parallel cylinders."));
    orderRow->addWidget(bondOrderCombo_, 1);
    orderRow->addStretch(1);
    indexLayout->addLayout(orderRow);

    pairInfoLabel_->setWordWrap(true);
    indexLayout->addWidget(pairInfoLabel_);

    auto* actionRow = new QHBoxLayout;
    auto* addButton = new QPushButton(tr("Create Bond"), indexTab);
    auto* suppressButton = new QPushButton(tr("Suppress Bond"), indexTab);
    actionRow->addWidget(addButton);
    actionRow->addWidget(suppressButton);
    actionRow->addStretch(1);
    indexLayout->addLayout(actionRow);
    indexLayout->addStretch(1);
    connect(addButton, &QPushButton::clicked, this, &BondEditorDialog::addBond);
    connect(suppressButton, &QPushButton::clicked, this, &BondEditorDialog::suppressBond);
    connect(useSelectionButton_, &QPushButton::clicked,
            this, &BondEditorDialog::useSelection);
    modeTabs->addTab(indexTab, tr("By Atomic Indices"));

    manualLayout->addWidget(modeTabs);

    // ----- Shared override list -------------------------------------------
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

QComboBox* BondEditorDialog::makeElementCombo()
{
    auto* combo = new QComboBox(this);
    // Distinct species actually present, sorted by atomic number, each item
    // carrying its Z in the user-role data.
    std::set<int> species;
    for (const auto& atom : structure_->atoms())
        species.insert(atom.atomicNumber);
    for (const int z : species)
        combo->addItem(QLatin1String(core::Elements::data(z).symbol), z);
    return combo;
}

std::vector<std::pair<int, int>> BondEditorDialog::matchingElementPairs() const
{
    std::vector<std::pair<int, int>> pairs;
    if (!elementACombo_ || !elementBCombo_ || elementACombo_->count() == 0)
        return pairs;
    const int za = elementACombo_->currentData().toInt();
    const int zb = elementBCombo_->currentData().toInt();
    const double dmin = minCutoffSpin_->value();
    const double dmax = maxCutoffSpin_->value();
    const auto& atoms = structure_->atoms();
    for (std::size_t i = 0; i + 1 < atoms.size(); ++i) {
        for (std::size_t j = i + 1; j < atoms.size(); ++j) {
            const int zi = atoms[i].atomicNumber;
            const int zj = atoms[j].atomicNumber;
            const bool match = (zi == za && zj == zb) || (zi == zb && zj == za);
            if (!match)
                continue;
            const double d = (atoms[j].position - atoms[i].position).norm();
            if (d >= dmin && d <= dmax)
                pairs.emplace_back(static_cast<int>(i), static_cast<int>(j));
        }
    }
    return pairs;
}

void BondEditorDialog::addBondsByElements()
{
    const auto pairs = matchingElementPairs();
    for (const auto& [i, j] : pairs)
        structure_->addBondOverride(i, j);
    refreshOverrideList();
    if (!pairs.empty())
        Q_EMIT bondsEdited();
}

void BondEditorDialog::removeBondsByElements()
{
    const auto pairs = matchingElementPairs();
    for (const auto& [i, j] : pairs) {
        // Both directions: drop an explicit "added" override and, failing that,
        // suppress the auto-perceived bond so the window truly unbonds the pair.
        structure_->clearBondOverride(i, j);
        structure_->removeBondOverride(i, j);
    }
    refreshOverrideList();
    if (!pairs.empty())
        Q_EMIT bondsEdited();
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
    if (i == j)
        return;
    const int order = bondOrderCombo_ ? bondOrderCombo_->currentData().toInt() : 1;
    structure_->addBondOverride(i, j);
    structure_->setBondOrder(i, j, order);
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
            const int order = structure_->bondOrder(i, j);
            text += QStringLiteral("   (%1%2 – %3%4, %5 Å")
                        .arg(QLatin1String(atoms[static_cast<std::size_t>(i)].symbol()))
                        .arg(i + 1)
                        .arg(QLatin1String(atoms[static_cast<std::size_t>(j)].symbol()))
                        .arg(j + 1)
                        .arg(d.norm(), 0, 'f', 3);
            if (order > 1)
                text += QStringLiteral(", order %1").arg(order);
            text += QLatin1Char(')');
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
