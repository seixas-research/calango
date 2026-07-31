#include "gui/LiquidInterfaceWizard.hpp"

#include "core/Structure.hpp"

#include <QButtonGroup>
#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QPushButton>
#include <QRadioButton>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QTableWidget>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>

namespace calango::gui {

using core::SolvationBuilder;

namespace {

/// Perpendicular height of `cell` along lattice vector `axis`, and the area of
/// the plane spanned by the other two. These are the two numbers that decide
/// how much fluid the region holds, so both pages report them.
void regionGeometry(const core::UnitCell& cell, int axis, double& height,
                    double& area)
{
    const auto& vectors = cell.vectors();
    const core::Vec3 cross =
        vectors[(axis + 1) % 3].cross(vectors[(axis + 2) % 3]);
    area = cross.norm();
    height = area > 1e-9 ? std::abs(vectors[axis].dot(cross / area)) : 0.0;
}

/// Extent of the structure along the same normal — the substrate's thickness,
/// which is what the region is added on top of.
double slabThickness(const core::Structure& structure, int axis)
{
    const auto& vectors = structure.cell().vectors();
    const core::Vec3 cross =
        vectors[(axis + 1) % 3].cross(vectors[(axis + 2) % 3]);
    const double area = cross.norm();
    if (area < 1e-9 || structure.empty())
        return 0.0;
    const core::Vec3 normal = cross / area;
    double lowest = structure.atoms().front().position.dot(normal);
    double highest = lowest;
    for (const core::Atom& atom : structure.atoms()) {
        const double projection = atom.position.dot(normal);
        lowest = std::min(lowest, projection);
        highest = std::max(highest, projection);
    }
    return highest - lowest;
}

QString speciesLabel(const SolvationBuilder::Species& species)
{
    return QStringLiteral("%1 (%2)")
        .arg(QString::fromStdString(species.name),
             QString::fromStdString(species.formula));
}

} // namespace

// ---------------------------------------------------------------------------
// Stage 1 — geometry and region
// ---------------------------------------------------------------------------

InterfaceRegionPage::InterfaceRegionPage(LiquidInterfaceWizard* wizard)
    : QWizardPage(wizard)
    , wizard_(wizard)
{
    setTitle(tr("Geometry & Region"));
    setSubTitle(tr("Choose which direction the fluid layer is opened along, "
                   "how thick it is, and how far the substrate is repeated "
                   "in the plane of the interface."));

    auto* layout = new QVBoxLayout(this);
    auto* form = new QFormLayout;
    layout->addLayout(form);

    axisCombo_ = new QComboBox(this);
    // Lattice vectors rather than Cartesian axes: the region is bounded by
    // planes normal to the other two lattice vectors, and in a tilted cell
    // those are not the Cartesian planes. The Cartesian hint is appended
    // because for the orthogonal cells most slabs have, they coincide.
    axisCombo_->addItem(tr("a  (first lattice vector)"),
                        static_cast<int>(SolvationBuilder::Axis::A));
    axisCombo_->addItem(tr("b  (second lattice vector)"),
                        static_cast<int>(SolvationBuilder::Axis::B));
    axisCombo_->addItem(tr("c  (third lattice vector — the usual slab normal)"),
                        static_cast<int>(SolvationBuilder::Axis::C));
    axisCombo_->setCurrentIndex(2);
    axisCombo_->setToolTip(
        tr("The direction the fluid region is opened along. For a surface slab "
           "built by this application that is c.\n\nThe region is bounded by "
           "planes normal to the OTHER two lattice vectors, so it stays "
           "commensurate with the cell even when that cell is tilted."));
    form->addRow(tr("Interface direction:"), axisCombo_);

    thicknessSpin_ = new QDoubleSpinBox(this);
    thicknessSpin_->setRange(1.0, 500.0);
    thicknessSpin_->setDecimals(2);
    thicknessSpin_->setSingleStep(1.0);
    thicknessSpin_->setValue(20.0);
    thicknessSpin_->setSuffix(tr(" Å"));
    thicknessSpin_->setToolTip(
        tr("Thickness of the fluid region: the gap between the top face of the "
           "structure and the bottom face of its own periodic image.\n\nThe "
           "cell is grown — or shrunk — to make this exact, whatever vacuum "
           "the input already carried. Asking for 20 Å of water and silently "
           "getting 20 Å plus the slab's existing 15 Å of vacuum is the usual "
           "way to end up with an accidentally dilute interface."));
    form->addRow(tr("Region thickness:"), thicknessSpin_);

    auto* lateralRow = new QWidget(this);
    auto* lateralLayout = new QHBoxLayout(lateralRow);
    lateralLayout->setContentsMargins(0, 0, 0, 0);
    for (int i = 0; i < 2; ++i) {
        lateralSpins_[i] = new QSpinBox(lateralRow);
        lateralSpins_[i]->setRange(1, 64);
        lateralSpins_[i]->setValue(1);
        lateralLayout->addWidget(lateralSpins_[i]);
        if (i == 0)
            lateralLayout->addWidget(new QLabel(QStringLiteral("×"),
                                                lateralRow));
    }
    lateralLayout->addStretch(1);
    lateralRow->setToolTip(
        tr("Repeat the substrate in the two directions that lie IN the "
           "interface plane, before the region is opened.\n\nA wider cell is "
           "usually what a solvation study needs: the fluid's correlation "
           "length is several Å, and a 1×1 surface cell forces it to be "
           "periodic on a length shorter than that."));
    form->addRow(tr("Lateral supercell:"), lateralRow);

    clearanceSpin_ = new QDoubleSpinBox(this);
    clearanceSpin_->setRange(0.5, 10.0);
    clearanceSpin_->setDecimals(2);
    clearanceSpin_->setSingleStep(0.1);
    clearanceSpin_->setValue(2.2);
    clearanceSpin_->setSuffix(tr(" Å"));
    clearanceSpin_->setToolTip(
        tr("Closest approach between any fluid atom and any substrate atom.\n\n"
           "Checked atom by atom, unlike the fluid-fluid exclusion: the "
           "surface is fixed, and a molecule started inside it cannot relax "
           "out the way two slightly close fluid molecules can."));
    form->addRow(tr("Surface clearance:"), clearanceSpin_);

    anchorCheck_ = new QCheckBox(
        tr("Move the substrate to the bottom of the cell"), this);
    anchorCheck_->setChecked(true);
    anchorCheck_->setToolTip(
        tr("On: the substrate is translated so it sits at the origin along the "
           "chosen direction and the fluid region is one contiguous block — "
           "the conventional layout for an interface cell.\n\nOff: the input "
           "coordinates are kept, which is what you want when the slab was "
           "positioned deliberately (a symmetric slab centred in its cell, "
           "for instance)."));
    form->addRow(QString(), anchorCheck_);

    summaryLabel_ = new QLabel(this);
    summaryLabel_->setWordWrap(true);
    summaryLabel_->setTextFormat(Qt::RichText);
    summaryLabel_->setFrameShape(QFrame::StyledPanel);
    summaryLabel_->setContentsMargins(8, 6, 8, 6);
    layout->addWidget(summaryLabel_);
    layout->addStretch(1);

    connect(axisCombo_, &QComboBox::currentIndexChanged, this,
            &InterfaceRegionPage::refresh);
    connect(thicknessSpin_, &QDoubleSpinBox::valueChanged, this,
            &InterfaceRegionPage::refresh);
    connect(clearanceSpin_, &QDoubleSpinBox::valueChanged, this,
            &InterfaceRegionPage::refresh);
    for (QSpinBox* spin : lateralSpins_)
        connect(spin, &QSpinBox::valueChanged, this,
                &InterfaceRegionPage::refresh);
    connect(anchorCheck_, &QCheckBox::toggled, this,
            &InterfaceRegionPage::refresh);
}

void InterfaceRegionPage::initializePage()
{
    refresh();
}

bool InterfaceRegionPage::isComplete() const
{
    return valid_;
}

void InterfaceRegionPage::refresh()
{
    const int axis = axisCombo_->currentData().toInt();
    wizard_->params.axis = static_cast<SolvationBuilder::Axis>(axis);
    wizard_->params.regionThickness = thicknessSpin_->value();
    wizard_->params.lateral[0] = lateralSpins_[0]->value();
    wizard_->params.lateral[1] = lateralSpins_[1]->value();
    wizard_->params.surfaceClearance = clearanceSpin_->value();
    wizard_->params.anchorSubstrate = anchorCheck_->isChecked();

    const bool hadValid = valid_;
    valid_ = false;

    if (!wizard_->substrate || !wizard_->substrate->cell().isDefined()) {
        summaryLabel_->setText(
            tr("<b>No periodic cell.</b><br>An interface region is opened "
               "along a lattice vector, so the structure needs one. Add a cell "
               "(Structure panel → Add vacuum) or start from a surface slab "
               "(Build → Surface Slab…)."));
        if (hadValid)
            Q_EMIT completeChanged();
        return;
    }

    double height = 0.0;
    double area = 0.0;
    regionGeometry(wizard_->substrate->cell(), axis, height, area);
    const double repeats = static_cast<double>(lateralSpins_[0]->value())
        * lateralSpins_[1]->value();
    area *= repeats;
    const double thickness = slabThickness(*wizard_->substrate, axis);
    const double fill =
        thicknessSpin_->value() - 2.0 * clearanceSpin_->value();

    if (fill <= 0.0) {
        summaryLabel_->setText(
            tr("<b>The clearance consumes the whole region.</b><br>Twice the "
               "surface clearance (%1 Å) is at least the region thickness "
               "(%2 Å), so there is nowhere to put a molecule. Widen the "
               "region or reduce the clearance.")
                .arg(2.0 * clearanceSpin_->value(), 0, 'f', 2)
                .arg(thicknessSpin_->value(), 0, 'f', 2));
        if (hadValid)
            Q_EMIT completeChanged();
        return;
    }

    valid_ = true;
    const double volume = area * fill;
    // The count a user actually cares about, quoted for water at ambient
    // density: an abstract volume in Å³ says much less about whether the cell
    // is the right size than "about 210 water molecules" does.
    const double waterCount = volume * 0.997 / 1.6605390666 / 18.015;
    QString summary =
        tr("Substrate %1 atoms → <b>%2</b> after the %3×%4 lateral supercell."
           "<br>Cell along %5: <b>%6 Å</b> → <b>%7 Å</b> "
           "(substrate %8 Å + region %9 Å).")
            .arg(wizard_->substrate->size())
            .arg(wizard_->substrate->size() * static_cast<int>(repeats))
            .arg(lateralSpins_[0]->value())
            .arg(lateralSpins_[1]->value())
            .arg(QString::fromStdString(
                SolvationBuilder::toString(wizard_->params.axis)))
            .arg(height, 0, 'f', 2)
            .arg(thickness + thicknessSpin_->value(), 0, 'f', 2)
            .arg(thickness, 0, 'f', 2)
            .arg(thicknessSpin_->value(), 0, 'f', 2);
    summary += tr("<br>Fillable volume <b>%1 Å³</b> (%2 Å² × %3 Å) — about "
                  "<b>%4</b> water molecules at 0.997 g/cm³.")
                   .arg(volume, 0, 'f', 0)
                   .arg(area, 0, 'f', 1)
                   .arg(fill, 0, 'f', 2)
                   .arg(std::lround(waterCount));
    if (thickness + thicknessSpin_->value() < height) {
        summary += tr("<br><span style='color:#d08a4a'>The cell will be "
                      "SHORTENED: the structure already carries more vacuum "
                      "than the requested region.</span>");
    }
    summaryLabel_->setText(summary);
    if (!hadValid)
        Q_EMIT completeChanged();
}

// ---------------------------------------------------------------------------
// Stage 2 — solvation and mixture
// ---------------------------------------------------------------------------

SolvationPage::SolvationPage(LiquidInterfaceWizard* wizard)
    : QWizardPage(wizard)
    , wizard_(wizard)
{
    setTitle(tr("Solvation & Mixture"));
    setSubTitle(tr("Fill the region with a liquid or a gas, mix several, and "
                   "add ionic species to make a solution."));

    auto* layout = new QVBoxLayout(this);

    // -- The mixture --------------------------------------------------------
    auto* mixtureGroup = new QGroupBox(tr("Liquid / gas mixture"), this);
    auto* mixtureLayout = new QVBoxLayout(mixtureGroup);
    componentTable_ = new QTableWidget(0, 2, mixtureGroup);
    componentTable_->setHorizontalHeaderLabels(
        {tr("Species"), tr("Mole fraction")});
    componentTable_->horizontalHeader()->setSectionResizeMode(
        0, QHeaderView::Stretch);
    componentTable_->verticalHeader()->setVisible(false);
    componentTable_->setSelectionBehavior(QAbstractItemView::SelectRows);
    componentTable_->setMaximumHeight(150);
    componentTable_->setToolTip(
        tr("Mole fractions are normalized, so {3 water, 1 ammonia} and "
           "{0.75 water, 0.25 ammonia} request the same mixture."));
    mixtureLayout->addWidget(componentTable_);
    auto* mixtureButtons = new QHBoxLayout;
    auto* addComponentButton = new QPushButton(tr("Add species"), mixtureGroup);
    removeComponentButton_ = new QPushButton(tr("Remove"), mixtureGroup);
    mixtureButtons->addWidget(addComponentButton);
    mixtureButtons->addWidget(removeComponentButton_);
    mixtureButtons->addStretch(1);
    mixtureLayout->addLayout(mixtureButtons);
    layout->addWidget(mixtureGroup);

    // -- How much ----------------------------------------------------------
    auto* amountGroup = new QGroupBox(tr("Amount"), this);
    auto* amountLayout = new QFormLayout(amountGroup);
    densityRadio_ = new QRadioButton(tr("Target density"), amountGroup);
    densityRadio_->setChecked(true);
    countRadio_ = new QRadioButton(tr("Molecule count"), amountGroup);
    auto* amountButtons = new QButtonGroup(amountGroup);
    amountButtons->addButton(densityRadio_);
    amountButtons->addButton(countRadio_);

    densitySpin_ = new QDoubleSpinBox(amountGroup);
    densitySpin_->setRange(1e-6, 25.0);
    densitySpin_->setDecimals(5);
    densitySpin_->setSingleStep(0.01);
    densitySpin_->setValue(0.997);
    densitySpin_->setSuffix(tr(" g/cm³"));
    densitySpin_->setToolTip(
        tr("Mass density of the WHOLE region, ions included. That is what "
           "makes a brine come out at the density of brine rather than at the "
           "density of water with salt added on top of it."));
    countSpin_ = new QSpinBox(amountGroup);
    countSpin_->setRange(0, 100000);
    countSpin_->setValue(64);
    countSpin_->setToolTip(
        tr("Total number of solvent molecules, split between the mixture's "
           "components by mole fraction.\n\nThis is the mode a GAS needs: at "
           "its 1 bar density a cell of interface size holds a fraction of a "
           "molecule, and in a periodic cell the molecule count IS the partial "
           "pressure."));
    amountLayout->addRow(densityRadio_, densitySpin_);
    amountLayout->addRow(countRadio_, countSpin_);
    layout->addWidget(amountGroup);

    // -- Ions ---------------------------------------------------------------
    auto* ionGroup = new QGroupBox(tr("Ionic species (optional)"), this);
    auto* ionLayout = new QVBoxLayout(ionGroup);
    ionTable_ = new QTableWidget(0, 2, ionGroup);
    ionTable_->setHorizontalHeaderLabels({tr("Salt / ion"), tr("Formula units")});
    ionTable_->horizontalHeader()->setSectionResizeMode(0,
                                                        QHeaderView::Stretch);
    ionTable_->verticalHeader()->setVisible(false);
    ionTable_->setSelectionBehavior(QAbstractItemView::SelectRows);
    ionTable_->setMaximumHeight(130);
    ionTable_->setToolTip(
        tr("A SALT is inserted as formula units and expands into its ions, so "
           "the cell stays neutral by construction — three units of "
           "(NH4)2SO4 is six ammonium and three sulfate.\n\nBare ions are "
           "offered too, for the cases where an unbalanced charge is "
           "deliberate; the net charge is reported below."));
    ionLayout->addWidget(ionTable_);
    auto* ionButtons = new QHBoxLayout;
    auto* addIonButton = new QPushButton(tr("Add salt / ion"), ionGroup);
    removeIonButton_ = new QPushButton(tr("Remove"), ionGroup);
    ionButtons->addWidget(addIonButton);
    ionButtons->addWidget(removeIonButton_);
    ionButtons->addStretch(1);
    ionLayout->addLayout(ionButtons);
    layout->addWidget(ionGroup);

    // -- Packing ------------------------------------------------------------
    auto* packingGroup = new QGroupBox(tr("Packing"), this);
    auto* packingLayout = new QFormLayout(packingGroup);
    toleranceSpin_ = new QDoubleSpinBox(packingGroup);
    toleranceSpin_->setRange(-1.0, 5.0);
    toleranceSpin_->setDecimals(2);
    toleranceSpin_->setSingleStep(0.1);
    toleranceSpin_->setValue(0.0);
    toleranceSpin_->setSuffix(tr(" Å"));
    toleranceSpin_->setToolTip(
        tr("Extra separation added to every pair of molecular contact radii.\n\n"
           "Raise it for a looser cell that relaxes more easily; lower it to "
           "reach a higher density at the cost of more rejected attempts. The "
           "atom-level floors that stop two molecules fusing are not affected "
           "by this."));
    packingLayout->addRow(tr("Extra tolerance:"), toleranceSpin_);
    seedSpin_ = new QSpinBox(packingGroup);
    seedSpin_->setRange(0, 1000000);
    seedSpin_->setValue(42);
    seedSpin_->setToolTip(
        tr("Random seed. The packing is reproducible: the same seed and the "
           "same settings give the same cell, which is what makes a result "
           "built here repeatable by someone else."));
    packingLayout->addRow(tr("Seed:"), seedSpin_);
    layout->addWidget(packingGroup);

    estimateLabel_ = new QLabel(this);
    estimateLabel_->setWordWrap(true);
    estimateLabel_->setTextFormat(Qt::RichText);
    estimateLabel_->setFrameShape(QFrame::StyledPanel);
    estimateLabel_->setContentsMargins(8, 6, 8, 6);
    layout->addWidget(estimateLabel_);
    layout->addStretch(1);

    connect(addComponentButton, &QPushButton::clicked, this,
            &SolvationPage::addComponentRow);
    connect(removeComponentButton_, &QPushButton::clicked, this,
            &SolvationPage::removeComponentRow);
    connect(addIonButton, &QPushButton::clicked, this,
            &SolvationPage::addIonRow);
    connect(removeIonButton_, &QPushButton::clicked, this,
            &SolvationPage::removeIonRow);
    connect(densityRadio_, &QRadioButton::toggled, this, [this] {
        amountTouched_ = true;
        refresh();
    });
    connect(densitySpin_, &QDoubleSpinBox::valueChanged, this, [this] {
        amountTouched_ = true;
        refresh();
    });
    connect(countSpin_, &QSpinBox::valueChanged, this, [this] {
        amountTouched_ = true;
        refresh();
    });
    connect(toleranceSpin_, &QDoubleSpinBox::valueChanged, this,
            &SolvationPage::refresh);
    connect(seedSpin_, &QSpinBox::valueChanged, this, &SolvationPage::refresh);

    // One water row to start from — the overwhelmingly common request, and an
    // empty table with a disabled Finish button reads as a broken page.
    addComponentRow();
}

void SolvationPage::populate(
    QComboBox* combo,
    std::initializer_list<SolvationBuilder::Category> categories)
{
    for (SolvationBuilder::Category category : categories) {
        for (const auto& species : SolvationBuilder::library()) {
            if (species.category != category)
                continue;
            combo->addItem(speciesLabel(species),
                           QString::fromStdString(species.key));
        }
        if (combo->count() > 0)
            combo->insertSeparator(combo->count());
    }
    // The trailing separator from the last category is noise.
    if (combo->count() > 0 && combo->itemText(combo->count() - 1).isEmpty())
        combo->removeItem(combo->count() - 1);
}

void SolvationPage::addComponentRow()
{
    const int row = componentTable_->rowCount();
    componentTable_->insertRow(row);
    auto* combo = new QComboBox(componentTable_);
    populate(combo, {SolvationBuilder::Category::Liquid,
                     SolvationBuilder::Category::Gas});
    componentTable_->setCellWidget(row, 0, combo);
    auto* fraction = new QDoubleSpinBox(componentTable_);
    fraction->setRange(0.0, 1000.0);
    fraction->setDecimals(3);
    fraction->setSingleStep(0.1);
    fraction->setValue(1.0);
    componentTable_->setCellWidget(row, 1, fraction);

    connect(combo, &QComboBox::currentIndexChanged, this, [this, combo] {
        // Follow the chosen species' own reference conditions until the user
        // overrides them: water at 0.997 g/cm³, ammonia at 0.682, and an
        // explicit count for a gas, whose 1 bar density comes to a fraction of
        // a molecule in a cell this size.
        if (!amountTouched_ && combo == qobject_cast<QComboBox*>(
                                   componentTable_->cellWidget(0, 0))) {
            const auto* species =
                SolvationBuilder::find(combo->currentData().toString()
                                           .toStdString());
            if (species) {
                const QSignalBlocker blockDensity(densitySpin_);
                const QSignalBlocker blockRadio(densityRadio_);
                const QSignalBlocker blockCount(countRadio_);
                if (species->category == SolvationBuilder::Category::Gas) {
                    countRadio_->setChecked(true);
                } else {
                    densityRadio_->setChecked(true);
                    if (species->referenceDensity > 0.0)
                        densitySpin_->setValue(species->referenceDensity);
                }
            }
        }
        refresh();
    });
    connect(fraction, &QDoubleSpinBox::valueChanged, this,
            &SolvationPage::refresh);
    refresh();
}

void SolvationPage::removeComponentRow()
{
    const int row = componentTable_->currentRow();
    // Never leave the table empty: a mixture of nothing is not a request the
    // builder can satisfy, and the Finish button would simply go dead with no
    // explanation on the page.
    if (row >= 0 && componentTable_->rowCount() > 1)
        componentTable_->removeRow(row);
    refresh();
}

void SolvationPage::addIonRow()
{
    const int row = ionTable_->rowCount();
    ionTable_->insertRow(row);
    auto* combo = new QComboBox(ionTable_);
    populate(combo, {SolvationBuilder::Category::Salt,
                     SolvationBuilder::Category::Ion});
    ionTable_->setCellWidget(row, 0, combo);
    auto* units = new QSpinBox(ionTable_);
    units->setRange(0, 10000);
    units->setValue(1);
    ionTable_->setCellWidget(row, 1, units);
    connect(combo, &QComboBox::currentIndexChanged, this,
            &SolvationPage::refresh);
    connect(units, &QSpinBox::valueChanged, this, &SolvationPage::refresh);
    refresh();
}

void SolvationPage::removeIonRow()
{
    const int row = ionTable_->currentRow();
    if (row >= 0)
        ionTable_->removeRow(row);
    refresh();
}

std::vector<SolvationBuilder::Component> SolvationPage::components() const
{
    std::vector<SolvationBuilder::Component> list;
    for (int row = 0; row < componentTable_->rowCount(); ++row) {
        auto* combo =
            qobject_cast<QComboBox*>(componentTable_->cellWidget(row, 0));
        auto* fraction =
            qobject_cast<QDoubleSpinBox*>(componentTable_->cellWidget(row, 1));
        if (!combo || !fraction || fraction->value() <= 0.0)
            continue;
        list.push_back({combo->currentData().toString().toStdString(),
                        fraction->value()});
    }
    return list;
}

std::vector<SolvationBuilder::IonicComponent> SolvationPage::ions() const
{
    std::vector<SolvationBuilder::IonicComponent> list;
    for (int row = 0; row < ionTable_->rowCount(); ++row) {
        auto* combo = qobject_cast<QComboBox*>(ionTable_->cellWidget(row, 0));
        auto* units = qobject_cast<QSpinBox*>(ionTable_->cellWidget(row, 1));
        if (!combo || !units || units->value() <= 0)
            continue;
        list.push_back({combo->currentData().toString().toStdString(),
                        units->value()});
    }
    return list;
}

void SolvationPage::initializePage()
{
    refresh();
}

bool SolvationPage::isComplete() const
{
    return !components().empty() || !ions().empty();
}

void SolvationPage::refresh()
{
    densitySpin_->setEnabled(densityRadio_->isChecked());
    countSpin_->setEnabled(countRadio_->isChecked());
    removeComponentButton_->setEnabled(componentTable_->rowCount() > 1);
    removeIonButton_->setEnabled(ionTable_->rowCount() > 0);

    wizard_->params.amount = densityRadio_->isChecked()
        ? SolvationBuilder::Amount::Density
        : SolvationBuilder::Amount::Count;
    wizard_->params.targetDensity = densitySpin_->value();
    wizard_->params.moleculeCount = countSpin_->value();
    wizard_->params.components = components();
    wizard_->params.ions = ions();
    wizard_->params.packingTolerance = toleranceSpin_->value();
    wizard_->params.seed = static_cast<unsigned>(seedSpin_->value());

    // The estimate is arithmetic on the requested composition, NOT a trial
    // packing: packing a few thousand molecules takes long enough that doing
    // it on every keystroke would make the page unusable. What the packer can
    // still do is fall short of these numbers, which is why the finished cell
    // reports what it actually placed.
    double area = 0.0;
    double height = 0.0;
    if (wizard_->substrate && wizard_->substrate->cell().isDefined())
        regionGeometry(wizard_->substrate->cell(),
                       static_cast<int>(wizard_->params.axis), height, area);
    area *= static_cast<double>(wizard_->params.lateral[0])
        * wizard_->params.lateral[1];
    const double fill = wizard_->params.regionThickness
        - 2.0 * wizard_->params.surfaceClearance;
    const double volume = std::max(0.0, area * fill);

    double ionMass = 0.0;
    double netCharge = 0.0;
    int ionCount = 0;
    QStringList ionParts;
    for (const auto& entry : wizard_->params.ions) {
        const auto* species = SolvationBuilder::find(entry.key);
        if (!species)
            continue;
        std::vector<std::string> parts = species->expandsTo;
        if (parts.empty())
            parts.push_back(species->key);
        for (const std::string& part : parts) {
            const auto* ion = SolvationBuilder::find(part);
            if (!ion)
                continue;
            ionMass += ion->molarMassU() * entry.units;
            netCharge += ion->charge * entry.units;
            ionCount += entry.units;
        }
        ionParts << tr("%1 × %2")
                        .arg(entry.units)
                        .arg(QString::fromStdString(species->formula));
    }

    double meanMass = 0.0;
    double fractionSum = 0.0;
    for (const auto& entry : wizard_->params.components) {
        const auto* species = SolvationBuilder::find(entry.key);
        if (species)
            fractionSum += entry.fraction;
    }
    for (const auto& entry : wizard_->params.components) {
        const auto* species = SolvationBuilder::find(entry.key);
        if (species && fractionSum > 0.0)
            meanMass += (entry.fraction / fractionSum) * species->molarMassU();
    }

    int solventTotal = 0;
    if (wizard_->params.amount == SolvationBuilder::Amount::Count) {
        solventTotal = wizard_->params.moleculeCount;
    } else if (meanMass > 0.0) {
        const double targetMass =
            wizard_->params.targetDensity * volume / 1.6605390666;
        solventTotal = static_cast<int>(
            std::lround(std::max(0.0, targetMass - ionMass) / meanMass));
    }

    QStringList solventParts;
    for (const auto& entry : wizard_->params.components) {
        const auto* species = SolvationBuilder::find(entry.key);
        if (!species || fractionSum <= 0.0)
            continue;
        solventParts << tr("%1 × %2")
                            .arg(std::lround((entry.fraction / fractionSum)
                                             * solventTotal))
                            .arg(QString::fromStdString(species->formula));
    }

    QString text = tr("Region <b>%1 Å³</b> → <b>%2</b>")
                       .arg(volume, 0, 'f', 0)
                       .arg(solventParts.isEmpty()
                                ? tr("no solvent")
                                : solventParts.join(tr(" + ")));
    if (!ionParts.isEmpty())
        text += tr(", plus <b>%1</b> (%2 ions)")
                    .arg(ionParts.join(tr(" + ")))
                    .arg(ionCount);
    text += QStringLiteral(".");

    if (std::abs(netCharge) > 1e-9) {
        text += tr("<br><span style='color:#d08a4a'>Net charge <b>%1 e</b> — "
                   "the cell is CHARGED. Legal with a compensating background, "
                   "but almost always a slip; add the counter-ion, or use the "
                   "salt rather than the bare ion.</span>")
                    .arg(netCharge, 0, 'f', 1);
    }
    if (wizard_->params.amount == SolvationBuilder::Amount::Density
        && solventTotal == 0 && meanMass > 0.0) {
        text += tr("<br><span style='color:#d08a4a'>That density comes to "
                   "fewer than one molecule in this region — for a gas, switch "
                   "to a molecule count, which is what sets the partial "
                   "pressure in a periodic cell.</span>");
    }
    text += tr("<br><span style='color:#909090'>Random packing: the cell has "
               "the right composition and no overlaps, but it is not an "
               "equilibrated liquid. Run MD before measuring anything from "
               "it.</span>");
    estimateLabel_->setText(text);

    Q_EMIT completeChanged();
}

bool SolvationPage::validatePage()
{
    refresh();
    QString error;
    if (wizard_->build(&error))
        return true;
    // Reported on the page rather than in a message box: the parameter that
    // caused it is right there, and a modal dialog would hide it.
    estimateLabel_->setText(
        QStringLiteral("<span style='color:#d0504a'><b>%1</b></span>")
            .arg(error.toHtmlEscaped()));
    return false;
}

// ---------------------------------------------------------------------------
// The wizard
// ---------------------------------------------------------------------------

LiquidInterfaceWizard::LiquidInterfaceWizard(
    std::shared_ptr<const core::Structure> structure, QWidget* parent)
    : QWizard(parent)
    , substrate(std::move(structure))
{
    setWindowTitle(tr("Liquid / Gas Interface Builder"));
    setWizardStyle(QWizard::ModernStyle);
    setOption(QWizard::NoBackButtonOnStartPage, true);
    setButtonText(QWizard::FinishButton, tr("Generate"));
    resize(760, 720);

    addPage(new InterfaceRegionPage(this));
    addPage(new SolvationPage(this));
}

bool LiquidInterfaceWizard::build(QString* error)
{
    if (!substrate) {
        if (error)
            *error = tr("No structure to build the interface on.");
        return false;
    }
    try {
        result_ = core::SolvationBuilder::generate(*substrate, params);
        return true;
    } catch (const std::exception& e) {
        result_.reset();
        if (error)
            *error = QString::fromUtf8(e.what());
        return false;
    }
}

} // namespace calango::gui
