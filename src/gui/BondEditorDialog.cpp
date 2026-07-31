#include "gui/BondEditorDialog.hpp"

#include "gui/ViewportWidget.hpp"

#include "core/Element.hpp"

#include <QDialogButtonBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QTabWidget>
#include <QColorDialog>
#include <QVBoxLayout>

#include <algorithm>
#include <set>

namespace calango::gui {

BondEditorDialog::BondEditorDialog(
    std::shared_ptr<core::Structure> structure,
    std::vector<std::shared_ptr<core::Structure>> frames,
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

    collectTargets(std::move(frames));

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
    // Counted on the DISPLAYED frame, and said so: the rule is re-evaluated
    // per frame, so a single number cannot stand for the whole trajectory and
    // pretending otherwise would be the more misleading answer.
    const auto refreshElementMatch = [this] {
        const std::size_t n =
            core::matchingPairs(*structure_, currentElementRule()).size();
        elementMatchLabel_->setText(
            targets_.size() > 1
                ? tr("%1 atom pair(s) match on this frame; the rule is "
                     "re-evaluated on each of the %2 frames.")
                      .arg(n)
                      .arg(targets_.size())
                : tr("%1 atom pair(s) match the current element/distance "
                     "window.")
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
    bondOrderCombo_->addItem(tr("Aromatic"), 4);
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

    // ----- Mode 3: Hydrogen Bonds -----------------------------------------
    // Hydrogen bonds are perceived geometrically rather than listed as
    // overrides: they are a property of the arrangement, so they must
    // re-evaluate whenever the geometry moves (a trajectory frame, a
    // relaxation step) instead of being frozen at the moment they were added.
    auto* hbondTab = new QWidget(modeTabs);
    auto* hbondForm = new QFormLayout(hbondTab);
    auto& hbond = viewport_->hydrogenBondStyle();

    hbondEnableCheck_ = new QCheckBox(tr("Detect and show hydrogen bonds"),
                                      hbondTab);
    hbondEnableCheck_->setChecked(hbond.enabled);
    hbondEnableCheck_->setToolTip(
        tr("Drawn as dashed lines from the hydrogen to the acceptor, so they "
           "read as distinct from the covalent bonds around them."));
    hbondForm->addRow(hbondEnableCheck_);

    hbondDistanceSpin_ = new QDoubleSpinBox(hbondTab);
    hbondDistanceSpin_->setRange(2.0, 6.0);
    hbondDistanceSpin_->setDecimals(2);
    hbondDistanceSpin_->setSingleStep(0.1);
    hbondDistanceSpin_->setValue(hbond.options.maxDonorAcceptor);
    hbondDistanceSpin_->setSuffix(tr(" Å"));
    hbondDistanceSpin_->setToolTip(
        tr("Maximum donor-acceptor separation d(D···A). Beyond about 3.5 Å the "
           "interaction is negligible for the usual N/O donors."));
    hbondForm->addRow(tr("Max. D···A distance:"), hbondDistanceSpin_);

    hbondAngleSpin_ = new QDoubleSpinBox(hbondTab);
    hbondAngleSpin_->setRange(90.0, 180.0);
    hbondAngleSpin_->setDecimals(1);
    hbondAngleSpin_->setSingleStep(5.0);
    hbondAngleSpin_->setValue(hbond.options.minAngle);
    hbondAngleSpin_->setSuffix(tr("°"));
    hbondAngleSpin_->setToolTip(
        tr("Minimum D–H···A angle. A hydrogen bond is close to linear (180°); "
           "admitting strongly bent geometries turns every nearby polar pair "
           "into a spurious \"bond\"."));
    hbondForm->addRow(tr("Min. D–H···A angle:"), hbondAngleSpin_);

    hbondColorButton_ = new QPushButton(hbondTab);
    hbondForm->addRow(tr("Color:"), hbondColorButton_);

    hbondCountLabel_ = new QLabel(hbondTab);
    hbondCountLabel_->setWordWrap(true);
    hbondForm->addRow(hbondCountLabel_);

    const auto applyHydrogenBonds = [this] {
        auto& style = viewport_->hydrogenBondStyle();
        style.enabled = hbondEnableCheck_->isChecked();
        style.options.maxDonorAcceptor = hbondDistanceSpin_->value();
        style.options.minAngle = hbondAngleSpin_->value();
        viewport_->refreshHydrogenBonds();
        // Report the count: with no feedback, "nothing appeared" is
        // indistinguishable between criteria that are too strict and a
        // structure that genuinely has no hydrogen bonds.
        hbondCountLabel_->setText(
            !style.enabled
                ? tr("Detection is off.")
                : (viewport_->hydrogenBondCount() == 0
                       ? tr("No hydrogen bonds match these criteria.")
                       : tr("%1 hydrogen bond(s) detected.")
                             .arg(viewport_->hydrogenBondCount())));
    };
    connect(hbondEnableCheck_, &QCheckBox::toggled, this, applyHydrogenBonds);
    connect(hbondDistanceSpin_, &QDoubleSpinBox::valueChanged, this,
            applyHydrogenBonds);
    connect(hbondAngleSpin_, &QDoubleSpinBox::valueChanged, this,
            applyHydrogenBonds);
    const auto paintHbondSwatch = [this] {
        const QColor c = viewport_->hydrogenBondStyle().color;
        hbondColorButton_->setText(c.name(QColor::HexRgb).toUpper());
        hbondColorButton_->setStyleSheet(
            QStringLiteral("background-color: %1; color: %2;")
                .arg(c.name(), c.lightnessF() > 0.5 ? QStringLiteral("#000")
                                                    : QStringLiteral("#fff")));
    };
    paintHbondSwatch();
    connect(hbondColorButton_, &QPushButton::clicked, this,
            [this, paintHbondSwatch, applyHydrogenBonds] {
                const QColor picked = QColorDialog::getColor(
                    viewport_->hydrogenBondStyle().color, this,
                    tr("Hydrogen Bond Color"));
                if (!picked.isValid())
                    return;
                viewport_->hydrogenBondStyle().color = picked;
                paintHbondSwatch();
                applyHydrogenBonds();
            });
    applyHydrogenBonds();
    modeTabs->addTab(hbondTab, tr("Hydrogen Bonds"));

    manualLayout->addWidget(modeTabs);

    // The scope of every rule above, stated where the rules are entered. A
    // trajectory-wide edit that announces itself only in the status bar is one
    // the user finds out about on frame 200.
    if (const QString scope = scopeSummary(); !scope.isEmpty()) {
        auto* scopeLabel = new QLabel(scope, manualGroup);
        scopeLabel->setWordWrap(true);
        scopeLabel->setStyleSheet(QStringLiteral("color: palette(mid);"));
        manualLayout->addWidget(scopeLabel);
    }

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

void BondEditorDialog::collectTargets(
    std::vector<std::shared_ptr<core::Structure>> frames)
{
    // The displayed frame leads, so a rule that can only be applied to one
    // structure still lands on the one the user is looking at.
    targets_.push_back(structure_);
    for (auto& frame : frames) {
        if (!frame || frame == structure_)
            continue;
        // An index rule names atom 12; on a frame with a different atom count
        // that is a different atom (or none). Skipping is the only honest
        // option — and the count is reported rather than swallowed.
        if (frame->size() != structure_->size()) {
            ++skippedFrames_;
            continue;
        }
        // Frames are shared_ptrs and the same object can legitimately appear
        // twice in a trajectory; applying an edit to it twice is harmless for
        // the override lists but not for anything counting.
        if (std::find(targets_.begin(), targets_.end(), frame) != targets_.end())
            continue;
        targets_.push_back(frame);
    }
}

std::vector<core::Structure*> BondEditorDialog::targetFrames() const
{
    std::vector<core::Structure*> frames;
    frames.reserve(targets_.size());
    for (const auto& frame : targets_)
        frames.push_back(frame.get());
    return frames;
}

core::ElementBondRule BondEditorDialog::currentElementRule() const
{
    core::ElementBondRule rule;
    if (!elementACombo_ || !elementBCombo_ || elementACombo_->count() == 0)
        return rule;
    rule.elementA = elementACombo_->currentData().toInt();
    rule.elementB = elementBCombo_->currentData().toInt();
    rule.minDistance = minCutoffSpin_->value();
    rule.maxDistance = maxCutoffSpin_->value();
    return rule;
}

QString BondEditorDialog::scopeSummary() const
{
    if (targets_.size() <= 1)
        return {};
    QString text = tr("Rules apply to all %n frame(s) of this trajectory, not "
                      "only the one on screen. Element rules are re-matched "
                      "against each frame's own geometry.",
                      nullptr, static_cast<int>(targets_.size()));
    if (skippedFrames_ > 0)
        text += QLatin1Char(' ')
            + tr("%n frame(s) with a different atom count are left "
                 "untouched.",
                 nullptr, skippedFrames_);
    return text;
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

void BondEditorDialog::addBondsByElements()
{
    // Re-matched per frame: the window is a geometric condition, and the
    // geometry is what a trajectory varies.
    const int total =
        core::applyElementRule(targetFrames(), currentElementRule(), true);
    refreshOverrideList();
    if (total > 0)
        Q_EMIT bondsEdited();
}

void BondEditorDialog::removeBondsByElements()
{
    const int total =
        core::applyElementRule(targetFrames(), currentElementRule(), false);
    refreshOverrideList();
    if (total > 0)
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
    // An index pair means the same two atoms on every frame — atoms keep their
    // index for the whole run — so the rule is copied rather than re-derived.
    core::applyIndexBond(targetFrames(), i, j, order);
    refreshOverrideList();
    Q_EMIT bondsEdited();
}

void BondEditorDialog::suppressBond()
{
    const auto [i, j] = currentPair();
    core::applyIndexSuppression(targetFrames(), i, j);
    refreshOverrideList();
    Q_EMIT bondsEdited();
}

void BondEditorDialog::clearSelectedOverride()
{
    auto* item = overrideList_->currentItem();
    if (!item)
        return;
    const QPoint pair = item->data(Qt::UserRole).toPoint();
    core::clearPairOnAllFrames(targetFrames(), pair.x(), pair.y());
    refreshOverrideList();
    Q_EMIT bondsEdited();
}

void BondEditorDialog::clearAllOverrides()
{
    core::clearAllOnAllFrames(targetFrames());
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
