#include "gui/Defect2dWizard.hpp"

#include "core/Defect2dScriptGenerator.hpp"
#include "core/Structure.hpp"
#include "gui/GuiUtils.hpp"

#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QRegularExpression>
#include <QSpinBox>
#include <QTableWidget>
#include <QVBoxLayout>

#include <set>

namespace calango::gui {

namespace {
constexpr int kSymbolColumn = 0;
constexpr int kCountColumn = 1;
constexpr int kMuColumn = 2;
} // namespace

Defect2dWizard::Defect2dWizard(std::shared_ptr<const core::Structure> structure,
                               QWidget* parent)
    : SimulationWizardBase(parent), structure_(std::move(structure))
{
    buildUi();
    selectCalculator(core::CalculatorKind::Gpaw);
}

QString Defect2dWizard::wizardTitle() const
{
    return tr("Charged Defects in 2D Materials Setup");
}

QStringList Defect2dWizard::calculatorElements() const
{
    return structureElements(structure_.get());
}

QWidget* Defect2dWizard::buildSettingsPage()
{
    auto* page = new QWidget(this);
    auto* layout = new QVBoxLayout(page);

    // Prose and formula are separate labels: the equation is reference
    // material that has to stay on screen while the page is used, and it is
    // not a "description" that can be shortened — only the prose around it is.
    auto* intro = new QLabel(
        tr("Formation energy of a defect in a monolayer, in each charge state, "
           "as a function of the Fermi level:"),
        page);
    intro->setWordWrap(true);
    intro->setTextFormat(Qt::RichText);
    intro->setToolTip(
        tr("E_corr is NOT the bulk correction. A charged sheet in a slab "
           "supercell has no single dielectric constant to divide by — the "
           "medium is the sheet inside and vacuum outside — and its energy "
           "against the neutralizing background DIVERGES with the vacuum "
           "thickness rather than converging, so adding vacuum does not settle "
           "the answer.\n\n"
           "The correction used here is Komsa and Pasquarello's: the Poisson "
           "equation ∇·(ε∇V) = −4πρ is solved for a model charge in the "
           "sheet's own dielectric profile, once with the supercell's "
           "periodicity and once for the isolated sheet, and the difference is "
           "E_corr."));
    layout->addWidget(intro);

    auto* formula = new QLabel(
        tr("<p align='center'>E<sub>f</sub>[X<sup>q</sup>](E<sub>F</sub>) = "
           "E<sub>tot</sub>[X<sup>q</sup>] − E<sub>tot</sub>[host] − "
           "Σ<sub>i</sub> n<sub>i</sub>μ<sub>i</sub> + q(E<sub>VBM</sub> + "
           "E<sub>F</sub>) + E<sub>corr</sub>(q)</p>"),
        page);
    formula->setWordWrap(true);
    formula->setTextFormat(Qt::RichText);
    layout->addWidget(formula);

    // -- Inherited runs -----------------------------------------------------
    auto* sources = new QGroupBox(tr("Inherited Single-Point Calculations"),
                                  page);
    auto* sourcesForm = new QFormLayout(sources);

    pristineCombo_ = new QComboBox(sources);
    pristineCombo_->setToolTip(
        tr("The PRISTINE monolayer supercell — same cell, same vacuum, same "
           "settings, no defect.\n\n"
           "It supplies the total energy the formation energy is measured "
           "against and the valence-band maximum the Fermi-level axis starts "
           "at."));
    sourcesForm->addRow(tr("Pristine host:"), pristineCombo_);

    neutralCombo_ = new QComboBox(sources);
    neutralCombo_->setToolTip(
        tr("The NEUTRAL defect supercell.\n\n"
           "Its relaxed geometry is what every charge state is evaluated at, "
           "and its calculator is reused for each of them — so the charge is "
           "the only difference between the total energies being subtracted."));
    sourcesForm->addRow(tr("Neutral defect:"), neutralCombo_);

    consistencyNote_ = new QLabel(sources);
    consistencyNote_->setWordWrap(true);
    consistencyNote_->setTextFormat(Qt::RichText);
    sourcesForm->addRow(consistencyNote_);

    for (QComboBox* combo : {pristineCombo_, neutralCombo_})
        connect(combo, &QComboBox::currentIndexChanged, this, [this] {
            refreshNotes();
            refreshPreview();
        });
    layout->addWidget(sources);

    // -- Charge states and reservoirs ---------------------------------------
    auto* charges = new QGroupBox(tr("Charge States && Reservoirs"), page);
    auto* chargesForm = new QFormLayout(charges);

    chargesEdit_ = new QLineEdit(QStringLiteral("-2, -1, 0, +1, +2"), charges);
    chargesEdit_->setToolTip(
        tr("Charge states q to evaluate, in units of |e|. One fixed-geometry "
           "SCF is run per state, so the cost is linear in how many are "
           "listed.\n\n"
           "q = 0 is always included whatever is typed here: it is the "
           "reference the diagram is anchored on."));
    chargesForm->addRow(tr("Charge states:"), chargesEdit_);
    connect(chargesEdit_, &QLineEdit::textChanged, this,
            [this] { refreshPreview(); });

    auto* speciesLabel = new QLabel(
        tr("<b>Exchanged species.</b> <i>Count</i> is how many atoms the "
           "defect has that the host does not; μ is the reservoir it is "
           "exchanged with."),
        charges);
    speciesLabel->setWordWrap(true);
    speciesLabel->setTextFormat(Qt::RichText);
    speciesLabel->setToolTip(
        tr("Count: −1 for a vacancy, +1 for an adatom; a substitution is two "
           "rows.\n\n"
           "μ encodes the growth condition, so the same defect has different "
           "formation energies under chalcogen-rich and metal-rich growth."));
    chargesForm->addRow(speciesLabel);

    speciesTable_ = new QTableWidget(0, 3, charges);
    speciesTable_->setHorizontalHeaderLabels(
        {tr("species"), tr("count"), tr("μ (eV)")});
    speciesTable_->horizontalHeader()->setSectionResizeMode(
        QHeaderView::Stretch);
    speciesTable_->setSelectionBehavior(QAbstractItemView::SelectRows);
    speciesTable_->setMaximumHeight(140);
    connect(speciesTable_, &QTableWidget::cellChanged, this,
            [this] { refreshPreview(); });
    chargesForm->addRow(speciesTable_);

    auto* speciesButtons = new QHBoxLayout;
    auto* addButton = new QPushButton(tr("Add species"), charges);
    connect(addButton, &QPushButton::clicked, this,
            [this] { addSpeciesRow(QString(), 0, 0.0); });
    speciesButtons->addWidget(addButton);
    auto* removeButton = new QPushButton(tr("Remove selected"), charges);
    connect(removeButton, &QPushButton::clicked, this, [this] {
        const int row = speciesTable_->currentRow();
        if (row >= 0) {
            speciesTable_->removeRow(row);
            refreshPreview();
        }
    });
    speciesButtons->addWidget(removeButton);
    auto* suggest = new QPushButton(tr("List species of this structure"),
                                    charges);
    connect(suggest, &QPushButton::clicked, this, [this] { suggestSpecies(); });
    speciesButtons->addWidget(suggest);
    speciesButtons->addStretch(1);
    chargesForm->addRow(speciesButtons);
    layout->addWidget(charges);

    // -- The dielectric profile ---------------------------------------------
    auto* profile = new QGroupBox(
        tr("2D Image-Charge Correction (Komsa-Pasquarello)"), page);
    auto* profileForm = new QFormLayout(profile);

    correctionCheck_ = new QCheckBox(tr("Apply the 2D image-charge correction"),
                                     profile);
    correctionCheck_->setChecked(true);
    correctionCheck_->setToolTip(
        tr("Without it the formation energies depend on the supercell AND on "
           "how much vacuum it carries — and unlike the bulk case that "
           "dependence does not converge as the vacuum grows, so an "
           "uncorrected 2D number is not an approximation to the isolated "
           "one.\n\n"
           "Turn this off only to measure how large the error is, as part of a "
           "supercell convergence study."));
    profileForm->addRow(QString(), correctionCheck_);

    normalCombo_ = new QComboBox(profile);
    normalCombo_->addItem(tr("a (x)"), 0);
    normalCombo_->addItem(tr("b (y)"), 1);
    normalCombo_->addItem(tr("c (z)"), 2);
    normalCombo_->setCurrentIndex(2);
    normalCombo_->setToolTip(
        tr("The cell axis normal to the sheet — the one carrying the vacuum. "
           "Everything about the correction is anisotropic around it, so "
           "getting it wrong does not degrade the answer gracefully."));
    profileForm->addRow(tr("Surface normal:"), normalCombo_);

    epsParSpin_ = new QDoubleSpinBox(profile);
    epsParSpin_->setRange(1.0, 1000.0);
    epsParSpin_->setDecimals(3);
    epsParSpin_->setValue(1.0);
    epsParSpin_->setToolTip(
        tr("In-plane dielectric constant ε∥ of the SHEET — not of the slab.\n\n"
           "A slab calculation reports an ε diluted by whatever vacuum was "
           "used, and putting that number here would make the correction "
           "depend on the padding it exists to remove. Take it from a 2D "
           "Optics run's sheet polarizability α₂D, or from the literature."));
    profileForm->addRow(tr("ε∥ (in-plane):"), epsParSpin_);

    epsPerpSpin_ = new QDoubleSpinBox(profile);
    epsPerpSpin_->setRange(1.0, 1000.0);
    epsPerpSpin_->setDecimals(3);
    epsPerpSpin_->setValue(1.0);
    epsPerpSpin_->setToolTip(
        tr("Out-of-plane dielectric constant ε⊥ of the sheet.\n\n"
           "For a monolayer this is usually much smaller than ε∥. The "
           "anisotropy is not a refinement here — it is what distinguishes a "
           "sheet from a thin piece of bulk, and setting ε⊥ = ε∥ throws that "
           "distinction away."));
    profileForm->addRow(tr("ε⊥ (out-of-plane):"), epsPerpSpin_);

    thicknessSpin_ = new QDoubleSpinBox(profile);
    thicknessSpin_->setRange(0.1, 100.0);
    thicknessSpin_->setDecimals(3);
    thicknessSpin_->setSingleStep(0.5);
    thicknessSpin_->setValue(6.0);
    thicknessSpin_->setSuffix(tr(" Å"));
    thicknessSpin_->setToolTip(
        tr("Effective thickness of the dielectric slab.\n\n"
           "Conventionally the interlayer spacing of the parent bulk — about "
           "6.15 Å for MoS₂, 3.35 Å for graphene — rather than the covalent "
           "thickness. It has to be paired with the ε it was defined against: "
           "ε and thickness enter the correction together, and mixing a "
           "literature ε with a different convention for the thickness is a "
           "silent error."));
    profileForm->addRow(tr("Layer thickness:"), thicknessSpin_);

    interfaceSpin_ = new QDoubleSpinBox(profile);
    interfaceSpin_->setRange(0.0, 10.0);
    interfaceSpin_->setDecimals(3);
    interfaceSpin_->setSingleStep(0.1);
    interfaceSpin_->setValue(1.0);
    interfaceSpin_->setSuffix(tr(" Å"));
    interfaceSpin_->setToolTip(
        tr("Width over which ε turns from the sheet value down to 1.\n\n"
           "A hard step has Fourier components at every G_z, which makes the "
           "linear system needlessly stiff; a real interface is not a step "
           "either. The result should not depend on it — if it does, the "
           "profile is competing with the cell rather than describing the "
           "sheet."));
    profileForm->addRow(tr("Interface width:"), interfaceSpin_);

    defectIndexSpin_ = new QSpinBox(profile);
    defectIndexSpin_->setRange(0, 1000000);
    defectIndexSpin_->setToolTip(
        tr("Index of an atom at the defect site. Only its coordinate along the "
           "normal is used, to place the sheet within the cell — a slab built "
           "by ase.build.surface is not centred, and assuming it is would put "
           "the model charge in the vacuum."));
    profileForm->addRow(tr("Defect site index:"), defectIndexSpin_);

    sigmaSpin_ = new QDoubleSpinBox(profile);
    sigmaSpin_->setRange(0.1, 20.0);
    sigmaSpin_->setDecimals(3);
    sigmaSpin_->setSingleStep(0.1);
    sigmaSpin_->setValue(1.0);
    sigmaSpin_->setSuffix(tr(" Å"));
    sigmaSpin_->setToolTip(
        tr("Gaussian width of the model charge. It cancels between the "
           "periodic and isolated solutions, so the correction is only weakly "
           "sensitive to it — provided the model charge stays well inside both "
           "the sheet and the supercell."));
    profileForm->addRow(tr("Model charge width σ:"), sigmaSpin_);

    zComponentsSpin_ = new QSpinBox(profile);
    zComponentsSpin_->setRange(8, 512);
    zComponentsSpin_->setValue(64);
    zComponentsSpin_->setToolTip(
        tr("Out-of-plane Fourier components in the periodic solve. The cost is "
           "one dense n×n solve per in-plane G, so this is the knob that sets "
           "the run time. 64 is converged to a few meV for a normal slab."));
    profileForm->addRow(tr("z components:"), zComponentsSpin_);

    inPlaneSpin_ = new QSpinBox(profile);
    inPlaneSpin_->setRange(1, 64);
    inPlaneSpin_->setValue(12);
    inPlaneSpin_->setToolTip(
        tr("In-plane reciprocal-lattice cutoff of the model, in stars of "
           "2π/a. The sum converges as the Gaussian cuts it off, so this only "
           "has to be large enough that the last shells contribute nothing."));
    profileForm->addRow(tr("In-plane cutoff:"), inPlaneSpin_);

    fermiPointsSpin_ = new QSpinBox(profile);
    fermiPointsSpin_->setRange(2, 100000);
    fermiPointsSpin_->setValue(401);
    fermiPointsSpin_->setToolTip(
        tr("Samples on the Fermi-level axis. The lines are exactly straight, "
           "so this only sets how finely the lower envelope is resolved."));
    profileForm->addRow(tr("Fermi-level samples:"), fermiPointsSpin_);

    profileNote_ = new QLabel(profile);
    profileNote_->setWordWrap(true);
    profileNote_->setTextFormat(Qt::RichText);
    profileForm->addRow(profileNote_);

    for (QDoubleSpinBox* spin : {epsParSpin_, epsPerpSpin_, thicknessSpin_,
                                 interfaceSpin_, sigmaSpin_})
        connect(spin, &QDoubleSpinBox::valueChanged, this, [this] {
            refreshNotes();
            refreshPreview();
        });
    for (QSpinBox* spin : {defectIndexSpin_, zComponentsSpin_, inPlaneSpin_,
                           fermiPointsSpin_})
        connect(spin, &QSpinBox::valueChanged, this,
                [this] { refreshPreview(); });
    connect(normalCombo_, &QComboBox::currentIndexChanged, this,
            [this] { refreshPreview(); });
    connect(correctionCheck_, &QCheckBox::toggled, this, [this](bool on) {
        for (QWidget* w : {static_cast<QWidget*>(normalCombo_),
                           static_cast<QWidget*>(epsParSpin_),
                           static_cast<QWidget*>(epsPerpSpin_),
                           static_cast<QWidget*>(thicknessSpin_),
                           static_cast<QWidget*>(interfaceSpin_),
                           static_cast<QWidget*>(defectIndexSpin_),
                           static_cast<QWidget*>(sigmaSpin_),
                           static_cast<QWidget*>(zComponentsSpin_),
                           static_cast<QWidget*>(inPlaneSpin_)})
            w->setEnabled(on);
        refreshNotes();
        refreshPreview();
    });
    layout->addWidget(profile);
    layout->addStretch(1);

    refreshNotes();
    return page;
}

void Defect2dWizard::addSpeciesRow(const QString& symbol, int count, double mu)
{
    const int row = speciesTable_->rowCount();
    speciesTable_->insertRow(row);
    speciesTable_->setItem(row, kSymbolColumn, new QTableWidgetItem(symbol));
    speciesTable_->setItem(row, kCountColumn,
                           new QTableWidgetItem(QString::number(count)));
    speciesTable_->setItem(row, kMuColumn,
                           new QTableWidgetItem(QString::number(mu, 'g', 6)));
    refreshPreview();
}

void Defect2dWizard::suggestSpecies()
{
    const QStringList elements = structureElements(structure_.get());
    if (elements.isEmpty()) {
        consistencyNote_->setText(
            tr("<i>No structure is open, so the species list cannot be "
               "suggested. Add the exchanged species by hand.</i>"));
        return;
    }
    std::set<QString> present;
    for (int row = 0; row < speciesTable_->rowCount(); ++row)
        if (const auto* item = speciesTable_->item(row, kSymbolColumn))
            present.insert(item->text().trimmed());
    for (const QString& symbol : elements)
        if (present.count(symbol) == 0)
            addSpeciesRow(symbol, 0, 0.0);
    consistencyNote_->setText(
        tr("<i>Species added with count 0 — set the count for each one the "
           "defect exchanges.</i>"));
    consistencyNote_->setToolTip(
        tr("−1 for a species the defect removed, +1 for one it added; leave "
           "the rest at zero."));
}

void Defect2dWizard::refreshNotes()
{
    if (consistencyNote_ && pristineCombo_ && neutralCombo_) {
        const QString host = pristineCombo_->currentData().toString();
        const QString defect = neutralCombo_->currentData().toString();
        // Braced: each branch now sets a tooltip as well as the text, and the
        // tooltip has to match the branch — a stale one from the previous
        // state would explain a message that is no longer on screen.
        if (host.isEmpty() || defect.isEmpty()) {
            consistencyNote_->clear();
            consistencyNote_->setToolTip(QString());
        } else if (host == defect) {
            consistencyNote_->setText(
                tr("<b style='color:#d9534f;'>The same calculation is selected "
                   "as both the host and the defect.</b>"));
            consistencyNote_->setToolTip(
                tr("Every formation energy would come out as the "
                   "chemical-potential term alone. Pick the pristine supercell "
                   "for one and the defect supercell for the other."));
        } else {
            consistencyNote_->setText(
                tr("<i>Both cells must be the same size, with the same vacuum "
                   "and the same settings.</i>"));
            consistencyNote_->setToolTip(
                tr("E_tot[X^q] − E_tot[host] is a difference of two "
                   "independent total energies, and it only means anything if "
                   "everything except the defect is identical."));
        }
    }

    if (!profileNote_ || !correctionCheck_)
        return;
    QStringList issues;
    if (correctionCheck_->isChecked()) {
        // ε = 1 is the default and it is never right for a real sheet: it says
        // the layer does not screen at all, which makes the correction the
        // vacuum image energy and overshoots by the full factor of ε.
        if (epsParSpin_ && epsParSpin_->value() <= 1.0 + 1e-9)
            issues << tr("ε∥ = 1 treats the sheet as vacuum, which "
                         "overcorrects by roughly the true ε∥.");
        else if (epsPerpSpin_ && epsParSpin_
                 && epsPerpSpin_->value() >= epsParSpin_->value() - 1e-9)
            issues << tr("ε⊥ ≥ ε∥ — unusual for a monolayer, where the "
                         "out-of-plane response is normally the weaker one. "
                         "Check the two are not swapped.");
    } else {
        issues << tr("The correction is switched off, so the formation "
                     "energies below depend on the supercell <i>and</i> on how "
                     "much vacuum it carries — a dependence that, unlike the "
                     "bulk case, does not converge as the vacuum grows.");
    }
    profileNote_->setText(
        issues.isEmpty()
            ? QString()
            : QStringLiteral("<span style='color:#d9534f;'>⚠ %1</span>")
                  .arg(issues.join(QStringLiteral("<br>⚠ "))));
    profileNote_->setVisible(!issues.isEmpty());
}

void Defect2dWizard::setDensityBaselines(
    const QList<QPair<QString, QString>>& baselines)
{
    for (QComboBox* combo : {pristineCombo_, neutralCombo_}) {
        if (!combo)
            continue;
        combo->clear();
        for (const auto& [label, path] : baselines)
            combo->addItem(label, path);
    }
    // Default the two selectors to DIFFERENT runs when possible: the same run
    // in both is never what is wanted, and starting there hides the choice.
    if (neutralCombo_ && neutralCombo_->count() > 1)
        neutralCombo_->setCurrentIndex(1);
    refreshNotes();
    refreshPreview();
}

QString Defect2dWizard::generateScript() const
{
    core::Defect2dConfig cfg;
    cfg.calculator = baseCalculatorConfig();
    if (pristineCombo_)
        cfg.pristinePath =
            pristineCombo_->currentData().toString().toStdString();
    if (neutralCombo_)
        cfg.neutralDefectPath =
            neutralCombo_->currentData().toString().toStdString();

    cfg.charges.clear();
    if (chargesEdit_) {
        const QStringList tokens = chargesEdit_->text().split(
            QRegularExpression(QStringLiteral("[\\s,;]+")), Qt::SkipEmptyParts);
        for (const QString& token : tokens) {
            bool ok = false;
            const int q = token.toInt(&ok);
            if (ok)
                cfg.charges.push_back(q);
        }
    }

    if (speciesTable_) {
        for (int row = 0; row < speciesTable_->rowCount(); ++row) {
            const auto* symbolItem = speciesTable_->item(row, kSymbolColumn);
            if (!symbolItem || symbolItem->text().trimmed().isEmpty())
                continue;
            core::DefectSpecies species;
            species.symbol = symbolItem->text().trimmed().toStdString();
            if (const auto* countItem = speciesTable_->item(row, kCountColumn))
                species.count = countItem->text().toInt();
            if (const auto* muItem = speciesTable_->item(row, kMuColumn))
                species.chemicalPotentialEv = muItem->text().toDouble();
            // A count of zero exchanges nothing; carrying it would only put a
            // no-op row in the generated script.
            if (species.count != 0)
                cfg.species.push_back(std::move(species));
        }
    }

    cfg.epsilonInPlane = epsParSpin_ ? epsParSpin_->value() : 1.0;
    cfg.epsilonOutOfPlane = epsPerpSpin_ ? epsPerpSpin_->value() : 1.0;
    cfg.layerThickness = thicknessSpin_ ? thicknessSpin_->value() : 6.0;
    cfg.interfaceWidth = interfaceSpin_ ? interfaceSpin_->value() : 1.0;
    cfg.normalAxis = normalCombo_ ? normalCombo_->currentData().toInt() : 2;
    cfg.defectIndex = defectIndexSpin_ ? defectIndexSpin_->value() : 0;
    cfg.modelChargeRadius = sigmaSpin_ ? sigmaSpin_->value() : 1.0;
    cfg.zComponents = zComponentsSpin_ ? zComponentsSpin_->value() : 64;
    cfg.inPlaneCutoff = inPlaneSpin_ ? inPlaneSpin_->value() : 12;
    cfg.applyCorrection = correctionCheck_ && correctionCheck_->isChecked();
    cfg.fermiPoints = fermiPointsSpin_ ? fermiPointsSpin_->value() : 401;
    return QString::fromStdString(core::generateDefect2dScript(cfg));
}

} // namespace calango::gui
