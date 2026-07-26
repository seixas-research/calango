#include "gui/AddAdsorbateDialog.hpp"

#include "core/Element.hpp"
#include "python_bridge/SurfaceScience.hpp"

#include <QButtonGroup>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QRadioButton>
#include <QTabWidget>
#include <QVBoxLayout>

#include <algorithm>
#include <exception>

namespace calango::gui {

namespace {

/// Human label for a detected site: its type plus where it sits, because on a
/// slab with 40 fcc hollows the type alone identifies nothing.
QString siteLabel(const core::AdsorptionSite& site, int index)
{
    return AddAdsorbateDialog::tr("%1 #%2 — (%3, %4, %5) Å")
        .arg(QString::fromStdString(site.type))
        .arg(index)
        .arg(site.position.x, 0, 'f', 2)
        .arg(site.position.y, 0, 'f', 2)
        .arg(site.position.z, 0, 'f', 2);
}

} // namespace

AddAdsorbateDialog::AddAdsorbateDialog(
    std::shared_ptr<const core::Structure> substrate, QWidget* parent)
    : QDialog(parent)
    , substrate_(std::move(substrate))
{
    setWindowTitle(tr("Add Adsorbate"));
    resize(680, 640);

    // Site detection is geometry, not chemistry: it works on periodic slabs and
    // on finite clusters alike, and it is cheap enough to just do on open. A
    // structure with no undercoordinated outer layer simply yields none, and
    // the placement group falls back to Cartesian.
    if (substrate_ && !substrate_->empty())
        sites_ = core::detectAdsorptionSites(*substrate_);

    auto* layout = new QVBoxLayout(this);

    auto* note = new QLabel(
        tr("Places one adsorbate on the current geometry and opens the result "
           "as a <b>new tab</b>, leaving this one untouched — so the clean "
           "surface stays available for the next adsorbate and as the reference "
           "for an adsorption energy."),
        this);
    note->setWordWrap(true);
    note->setTextFormat(Qt::RichText);
    layout->addWidget(note);

    tabs_ = new QTabWidget(this);
    tabs_->addTab(buildAtomTab(), tr("Single Atom"));
    tabs_->addTab(buildMoleculeTab(), tr("Molecule / Radical"));
    layout->addWidget(tabs_, 1);
    connect(tabs_, &QTabWidget::currentChanged, this,
            &AddAdsorbateDialog::updatePreview);

    previewLabel_ = new QLabel(this);
    previewLabel_->setWordWrap(true);
    previewLabel_->setTextFormat(Qt::RichText);
    layout->addWidget(previewLabel_);

    auto* buttons = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    buttons->button(QDialogButtonBox::Ok)->setText(tr("Add Adsorbate"));
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, this,
            &AddAdsorbateDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    refreshMolecule();
    updatePreview();
}

QWidget* AddAdsorbateDialog::buildAtomTab()
{
    auto* page = new QWidget(this);
    auto* layout = new QVBoxLayout(page);

    auto* form = new QFormLayout;
    elementCombo_ = new QComboBox(page);
    elementCombo_->setEditable(true);
    // The whole table, so any dopant is reachable, but led by the adsorbate
    // elements that come up constantly rather than by hydrogen-through-oganesson
    // in atomic-number order.
    for (const char* common : {"H", "O", "N", "C", "S", "F", "Cl", "Na", "K",
                               "Li", "Pt", "Pd", "Au", "Ag", "Cu", "Ni", "Fe"})
        elementCombo_->addItem(QString::fromLatin1(common));
    elementCombo_->insertSeparator(elementCombo_->count());
    for (int z = 1; z <= core::Elements::maxZ; ++z)
        elementCombo_->addItem(
            QString::fromLatin1(core::Elements::data(z).symbol));
    elementCombo_->setCurrentText(QStringLiteral("H"));
    elementCombo_->setToolTip(
        tr("Chemical symbol of the adatom. Any element in the table; the "
           "entries above the separator are the ones that come up most often."));
    form->addRow(tr("Element:"), elementCombo_);
    layout->addLayout(form);
    connect(elementCombo_, &QComboBox::currentTextChanged, this,
            &AddAdsorbateDialog::updatePreview);

    layout->addWidget(buildPlacementGroup(page, atomPlacement_));
    layout->addStretch(1);
    return page;
}

QWidget* AddAdsorbateDialog::buildMoleculeTab()
{
    auto* page = new QWidget(this);
    auto* layout = new QVBoxLayout(page);

    auto* form = new QFormLayout;
    moleculeCombo_ = new QComboBox(page);
    moleculeCombo_->setEditable(true);
    // Editable and pre-populated: the database covers the common cases, and a
    // typed formula covers the rest (ase.build.molecule falls back to reading
    // an unknown name as a bare formula).
    for (const std::string& name : pybridge::SurfaceScience::moleculeNames())
        moleculeCombo_->addItem(QString::fromStdString(name));
    moleculeCombo_->setCurrentText(QStringLiteral("CO"));
    moleculeCombo_->setToolTip(
        tr("A name from ASE's molecule database, extended with the open-shell "
           "fragments a surface actually binds (OH, OOH, CH3, NH2 …) — or a "
           "plain chemical formula."));
    form->addRow(tr("Molecule / radical:"), moleculeCombo_);
    connect(moleculeCombo_, &QComboBox::currentTextChanged, this,
            &AddAdsorbateDialog::refreshMolecule);

    anchorCombo_ = new QComboBox(page);
    anchorCombo_->setToolTip(
        tr("The atom that faces the surface. It is placed on the site axis and "
           "the rest of the molecule follows rigidly.\n"
           "Which atom binds IS the chemistry of the adsorption, so it stays "
           "editable; the default follows the usual convention (O for OH and "
           "H₂O, C for CO)."));
    form->addRow(tr("Binding atom:"), anchorCombo_);
    connect(anchorCombo_, &QComboBox::currentIndexChanged, this,
            &AddAdsorbateDialog::updatePreview);

    moleculeInfoLabel_ = new QLabel(page);
    moleculeInfoLabel_->setWordWrap(true);
    moleculeInfoLabel_->setTextFormat(Qt::RichText);
    form->addRow(QString(), moleculeInfoLabel_);
    layout->addLayout(form);

    // --- Orientation -------------------------------------------------------
    auto* orientationGroup = new QGroupBox(tr("Orientation"), page);
    auto* orientationForm = new QFormLayout(orientationGroup);

    const auto makeAngle = [&](QDoubleSpinBox*& spin, const QString& label,
                               const QString& tip) {
        spin = new QDoubleSpinBox(orientationGroup);
        spin->setRange(-360.0, 360.0);
        spin->setDecimals(1);
        spin->setSingleStep(5.0);
        spin->setSuffix(tr(" °"));
        spin->setValue(0.0);
        spin->setToolTip(tip);
        orientationForm->addRow(label, spin);
        connect(spin, &QDoubleSpinBox::valueChanged, this,
                &AddAdsorbateDialog::updatePreview);
    };
    makeAngle(tiltSpin_, tr("Tilt from normal:"),
              tr("Angle between the molecule's axis and the surface normal.\n"
                 "0° stands it upright (pointing away from the surface), 90° "
                 "lays it flat, 180° inverts it.\n"
                 "CO adsorbs upright; an aromatic ring lies flat. Not a "
                 "cosmetic setting — the binding energy is a function of it."));
    makeAngle(azimuthSpin_, tr("Azimuth about normal:"),
              tr("Which way a tilted molecule leans, measured about the surface "
                 "normal. Also sets the in-plane orientation of a flat one. No "
                 "effect at 0° tilt."));
    makeAngle(rollSpin_, tr("Roll about own axis:"),
              tr("Spin about the molecule's own axis. Visible only for an "
                 "adsorbate that is not axially symmetric — a methyl group, a "
                 "ring."));
    layout->addWidget(orientationGroup);

    layout->addWidget(buildPlacementGroup(page, moleculePlacement_));
    layout->addStretch(1);
    return page;
}

QWidget* AddAdsorbateDialog::buildPlacementGroup(QWidget* parent,
                                                 Placement& placement)
{
    auto* group = new QGroupBox(tr("Placement"), parent);
    auto* form = new QFormLayout(group);

    placement.siteRadio = new QRadioButton(tr("High-symmetry site"), group);
    placement.cartesianRadio = new QRadioButton(tr("Cartesian position"), group);
    auto* modeGroup = new QButtonGroup(group);
    modeGroup->addButton(placement.siteRadio);
    modeGroup->addButton(placement.cartesianRadio);

    placement.siteCombo = new QComboBox(group);
    for (std::size_t i = 0; i < sites_.size(); ++i)
        placement.siteCombo->addItem(siteLabel(sites_[i], static_cast<int>(i)),
                                     static_cast<int>(i));
    placement.siteCombo->setToolTip(
        tr("Detected top / bridge / fcc / hcp / hollow sites. The adsorbate is "
           "placed along each site's OUTWARD normal, which follows a curved "
           "nanoparticle facet rather than assuming +z."));

    placement.heightSpin = new QDoubleSpinBox(group);
    placement.heightSpin->setRange(0.0, 20.0);
    placement.heightSpin->setDecimals(2);
    placement.heightSpin->setSingleStep(0.1);
    placement.heightSpin->setValue(1.90);
    placement.heightSpin->setSuffix(tr(" Å"));
    placement.heightSpin->setToolTip(
        tr("Distance from the site to the binding atom, along the surface "
           "normal. A starting guess — the relaxation decides the real bond "
           "length; 1.5–2.2 Å is the usual range for a chemisorbed atom."));

    auto* coordRow = new QHBoxLayout;
    const char* axisLabels[3] = {"x", "y", "z"};
    for (int axis = 0; axis < 3; ++axis) {
        coordRow->addWidget(new QLabel(QLatin1String(axisLabels[axis]), group));
        placement.coordSpin[axis] = new QDoubleSpinBox(group);
        placement.coordSpin[axis]->setRange(-1000.0, 1000.0);
        placement.coordSpin[axis]->setDecimals(3);
        placement.coordSpin[axis]->setSingleStep(0.1);
        placement.coordSpin[axis]->setSuffix(tr(" Å"));
        coordRow->addWidget(placement.coordSpin[axis], 1);
        connect(placement.coordSpin[axis], &QDoubleSpinBox::valueChanged, this,
                &AddAdsorbateDialog::updatePreview);
    }
    // Seed the Cartesian fields just above the structure's top face, so the
    // manual mode starts somewhere plausible rather than at the origin — which
    // may be nowhere near the atoms.
    if (substrate_ && !substrate_->empty()) {
        const core::Vec3 centroid = substrate_->centroid();
        double top = substrate_->atoms().front().position.z;
        for (const core::Atom& atom : substrate_->atoms())
            top = std::max(top, atom.position.z);
        placement.coordSpin[0]->setValue(centroid.x);
        placement.coordSpin[1]->setValue(centroid.y);
        placement.coordSpin[2]->setValue(top + 1.90);
    }

    form->addRow(placement.siteRadio);
    form->addRow(tr("Site:"), placement.siteCombo);
    form->addRow(tr("Height above site:"), placement.heightSpin);
    form->addRow(placement.cartesianRadio);
    form->addRow(tr("Position:"), coordRow);

    // No detected sites (a molecule in vacuum, a structure with no distinct
    // outer layer) means the site mode has nothing to offer; the dialog stays
    // usable through the Cartesian mode rather than refusing to open.
    const bool hasSites = !sites_.empty();
    placement.siteRadio->setEnabled(hasSites);
    placement.siteRadio->setChecked(hasSites);
    placement.cartesianRadio->setChecked(!hasSites);

    const auto syncEnabled = [placement] {
        const bool site = placement.siteRadio->isChecked();
        placement.siteCombo->setEnabled(site);
        placement.heightSpin->setEnabled(site);
        for (QDoubleSpinBox* spin : placement.coordSpin)
            spin->setEnabled(!site);
    };
    syncEnabled();
    connect(placement.siteRadio, &QRadioButton::toggled, this,
            [this, syncEnabled] {
                syncEnabled();
                updatePreview();
            });
    connect(placement.siteCombo, &QComboBox::currentIndexChanged, this,
            &AddAdsorbateDialog::updatePreview);
    connect(placement.heightSpin, &QDoubleSpinBox::valueChanged, this,
            &AddAdsorbateDialog::updatePreview);
    return group;
}

bool AddAdsorbateDialog::moleculeTabActive() const
{
    return tabs_ && tabs_->currentIndex() == 1;
}

core::AdsorptionSite AddAdsorbateDialog::resolveSite(
    const Placement& placement) const
{
    if (placement.siteRadio->isChecked() && !sites_.empty()) {
        const int index = std::clamp(placement.siteCombo->currentIndex(), 0,
                                     static_cast<int>(sites_.size()) - 1);
        return sites_[static_cast<std::size_t>(index)];
    }
    // Cartesian mode: a synthetic site at the typed point with a +z normal.
    // The height is then 0 (see resolveHeight) — the position already names
    // the exact spot, and adding an offset on top of it would make the number
    // the user typed a lie.
    core::AdsorptionSite site;
    site.type = "manual";
    site.position = {placement.coordSpin[0]->value(),
                     placement.coordSpin[1]->value(),
                     placement.coordSpin[2]->value()};
    site.normal = {0, 0, 1};
    return site;
}

double AddAdsorbateDialog::resolveHeight(const Placement& placement) const
{
    return placement.siteRadio->isChecked() && !sites_.empty()
        ? placement.heightSpin->value()
        : 0.0;
}

void AddAdsorbateDialog::refreshMolecule()
{
    const QString name = moleculeCombo_->currentText().trimmed();
    moleculeName_ = name;
    moleculeTemplate_ = core::Structure();
    moleculeAnchor_ = 0;
    anchorCombo_->clear();
    if (name.isEmpty()) {
        moleculeInfoLabel_->setText(tr("Enter a molecule name or formula."));
        updatePreview();
        return;
    }
    try {
        const auto resolved =
            pybridge::SurfaceScience::moleculeTemplate(name.toStdString());
        moleculeTemplate_ = resolved.structure;
        moleculeAnchor_ = resolved.anchorIndex;
    } catch (const std::exception&) {
        moleculeInfoLabel_->setText(
            tr("<span style=\"color:#d9534f;\">\"%1\" is neither a database "
               "entry nor a formula ASE can parse.</span>")
                .arg(name));
        updatePreview();
        return;
    }

    const auto& atoms = moleculeTemplate_.atoms();
    for (std::size_t i = 0; i < atoms.size(); ++i)
        anchorCombo_->addItem(tr("%1 (atom %2)")
                                  .arg(QString::fromLatin1(atoms[i].symbol()))
                                  .arg(i),
                              static_cast<int>(i));
    if (moleculeAnchor_ >= 0 && moleculeAnchor_ < anchorCombo_->count())
        anchorCombo_->setCurrentIndex(moleculeAnchor_);
    moleculeInfoLabel_->setText(
        tr("%1 — %2 atom(s).")
            .arg(QString::fromStdString(moleculeTemplate_.chemicalFormula()))
            .arg(atoms.size()));
    updatePreview();
}

core::Structure AddAdsorbateDialog::adsorbate(int& anchorIndex,
                                              QString& label) const
{
    if (moleculeTabActive()) {
        anchorIndex = anchorCombo_->currentIndex() >= 0
            ? anchorCombo_->currentIndex()
            : moleculeAnchor_;
        label = moleculeName_;
        return moleculeTemplate_;
    }
    core::Structure single;
    anchorIndex = 0;
    const int z = core::Elements::atomicNumber(
        elementCombo_->currentText().trimmed().toStdString());
    if (z <= 0) {
        label.clear();
        return single; // unknown symbol: an empty adsorbate, reported by callers
    }
    core::Atom atom;
    atom.atomicNumber = z;
    atom.position = {0, 0, 0};
    single.addAtom(atom);
    label = QString::fromLatin1(core::Elements::data(z).symbol);
    return single;
}

void AddAdsorbateDialog::updatePreview()
{
    if (!previewLabel_)
        return; // still constructing: the preview label comes after the tabs
    int anchor = 0;
    QString label;
    const core::Structure molecule = adsorbate(anchor, label);
    if (molecule.empty()) {
        previewLabel_->setText(
            moleculeTabActive()
                ? tr("<i>No adsorbate resolved yet.</i>")
                : tr("<span style=\"color:#d9534f;\">\"%1\" is not a chemical "
                     "symbol.</span>")
                      .arg(elementCombo_->currentText().trimmed()));
        return;
    }
    const Placement& placement =
        moleculeTabActive() ? moleculePlacement_ : atomPlacement_;
    const core::AdsorptionSite site = resolveSite(placement);
    const core::Vec3 anchorPos =
        site.position + site.normal.normalized() * resolveHeight(placement);

    const std::size_t substrateSize = substrate_ ? substrate_->size() : 0;
    // A lone adatom has nothing to bind THROUGH, so naming a binding atom for
    // it would describe a distinction the tab does not have.
    const QString what = molecule.size() > 1
        ? tr("<b>%1</b> (%2 atoms), binding atom at").arg(label).arg(molecule.size())
        : tr("<b>%1</b> at").arg(label);
    previewLabel_->setText(
        tr("Adds %1 (%2, %3, %4) Å on the <b>%5</b> site → %6 atoms in the new "
           "tab.")
            .arg(what)
            .arg(anchorPos.x, 0, 'f', 2)
            .arg(anchorPos.y, 0, 'f', 2)
            .arg(anchorPos.z, 0, 'f', 2)
            .arg(QString::fromStdString(site.type))
            .arg(substrateSize + molecule.size()));
}

void AddAdsorbateDialog::accept()
{
    int anchor = 0;
    QString label;
    const core::Structure molecule = adsorbate(anchor, label);
    if (molecule.empty()) {
        QMessageBox::warning(
            this, tr("Add Adsorbate"),
            moleculeTabActive()
                ? tr("Enter a molecule name from the database, or a chemical "
                     "formula ASE can parse.")
                : tr("\"%1\" is not a chemical symbol.")
                      .arg(elementCombo_->currentText().trimmed()));
        return;
    }

    const Placement& placement =
        moleculeTabActive() ? moleculePlacement_ : atomPlacement_;
    const core::AdsorptionSite site = resolveSite(placement);

    core::AdsorbateOrientation orientation;
    if (moleculeTabActive()) {
        orientation.tiltDeg = tiltSpin_->value();
        orientation.azimuthDeg = azimuthSpin_->value();
        orientation.rollDeg = rollSpin_->value();
    }

    // The substrate is copied, never mutated: the result becomes its own tab,
    // so the clean surface survives as the reference the adsorption energy is
    // measured against.
    const core::Structure base = substrate_ ? *substrate_ : core::Structure();
    result_ = std::make_shared<core::Structure>(core::placeAdsorbateAt(
        base, site, molecule, anchor, resolveHeight(placement), orientation));
    resultName_ = tr("%1 + %2 (%3)")
                      .arg(QString::fromStdString(base.chemicalFormula()), label,
                           QString::fromStdString(site.type));
    QDialog::accept();
}

} // namespace calango::gui
