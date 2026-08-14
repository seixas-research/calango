#include "gui/DefectWizard.hpp"

#include "core/DefectScriptGenerator.hpp"
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

DefectWizard::DefectWizard(std::shared_ptr<const core::Structure> structure,
                           QWidget* parent)
    : SimulationWizardBase(parent), structure_(std::move(structure))
{
    buildUi();
    selectCalculator(core::CalculatorKind::Gpaw);
}

QString DefectWizard::wizardTitle() const
{
    return tr("Charged Defects Setup");
}

QStringList DefectWizard::calculatorElements() const
{
    return structureElements(structure_.get());
}

QWidget* DefectWizard::buildSettingsPage()
{
    auto* page = new QWidget(this);
    auto* layout = new QVBoxLayout(page);

    // Prose and formula split, as in Defect2dWizard: the equation stays on
    // screen because it is what the page is about, and only the prose is
    // subject to the length limit.
    auto* intro = new QLabel(
        tr("Formation energy of a defect in each charge state, as a function "
           "of the Fermi level:"),
        page);
    intro->setWordWrap(true);
    intro->setTextFormat(Qt::RichText);
    intro->setToolTip(
        tr("Each line has slope q, so the charge state can be read off the "
           "diagram directly. Where the lower envelope changes slope is a "
           "thermodynamic transition level ε(q/q′)."));
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
    auto* sources = new QGroupBox(tr("Inherited Single-Point Calculations"), page);
    auto* sourcesForm = new QFormLayout(sources);

    pristineCombo_ = new QComboBox(sources);
    pristineCombo_->setToolTip(
        tr("The PRISTINE host supercell — same cell, same settings, no "
           "defect.\n\n"
           "It supplies the total energy the formation energy is measured "
           "against, the valence-band maximum the Fermi-level axis starts at, "
           "and the reference electrostatic potential the FNV alignment is "
           "taken against far from the defect."));
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
            refreshConsistencyNote();
            refreshPreview();
        });
    layout->addWidget(sources);

    // -- Charge states ------------------------------------------------------
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

    // -- Chemical potentials ------------------------------------------------
    auto* speciesLabel = new QLabel(
        tr("<b>Exchanged species.</b> <i>Count</i> is how many atoms the "
           "defect has that the host does not; μ is the reservoir it is "
           "exchanged with."),
        charges);
    speciesLabel->setWordWrap(true);
    speciesLabel->setTextFormat(Qt::RichText);
    speciesLabel->setToolTip(
        tr("Count: −1 for a vacancy, +1 for an interstitial; a substitution is "
           "two rows.\n\n"
           "μ encodes the growth condition, so the same defect has different "
           "formation energies under metal-rich and anion-rich growth."));
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
    addSpeciesButton_ = new QPushButton(tr("Add species"), charges);
    connect(addSpeciesButton_, &QPushButton::clicked, this,
            [this] { addSpeciesRow(QString(), 0, 0.0); });
    speciesButtons->addWidget(addSpeciesButton_);
    removeSpeciesButton_ = new QPushButton(tr("Remove selected"), charges);
    connect(removeSpeciesButton_, &QPushButton::clicked, this, [this] {
        const int row = speciesTable_->currentRow();
        if (row >= 0) {
            speciesTable_->removeRow(row);
            refreshPreview();
        }
    });
    speciesButtons->addWidget(removeSpeciesButton_);
    auto* suggest = new QPushButton(tr("Derive from the two cells"), charges);
    suggest->setToolTip(
        tr("Compare the host and defect compositions and fill the table with "
           "the difference. The counts follow from the two structures; the "
           "chemical potentials do not, and still have to be supplied."));
    connect(suggest, &QPushButton::clicked, this, [this] { suggestSpecies(); });
    speciesButtons->addWidget(suggest);
    speciesButtons->addStretch(1);
    chargesForm->addRow(speciesButtons);
    layout->addWidget(charges);

    // -- FNV correction -----------------------------------------------------
    auto* fnv = new QGroupBox(tr("Freysoldt-Neugebauer-Van de Walle Correction"),
                              page);
    auto* fnvForm = new QFormLayout(fnv);

    fnvCheck_ = new QCheckBox(tr("Apply the FNV correction"), fnv);
    fnvCheck_->setChecked(true);
    fnvCheck_->setToolTip(
        tr("A charged defect in a periodic supercell is not an isolated "
           "defect: it is an infinite array of them plus a neutralizing "
           "background, and the spurious interaction decays only as 1/L while "
           "growing as q².\n\n"
           "For a q = ±2 defect in a workable supercell the error is several "
           "tenths of an eV — the same size as the transition levels being "
           "measured. Turn this off only to see how large that error is, as "
           "part of a supercell convergence study."));
    fnvForm->addRow(QString(), fnvCheck_);

    epsilonSpin_ = new QDoubleSpinBox(fnv);
    epsilonSpin_->setRange(1.0, 1000.0);
    epsilonSpin_->setDecimals(3);
    epsilonSpin_->setValue(1.0);
    epsilonSpin_->setToolTip(
        tr("Macroscopic static dielectric constant ε of the HOST.\n\n"
           "The correction scales as 1/ε, which makes this the parameter it "
           "is most sensitive to. Use the ion-clamped-plus-ionic (static) "
           "constant when the defect is allowed to relax. It cannot be "
           "derived from the supercell — take it from the Optics module or "
           "from experiment.\n\n"
           "Leaving it at 1.0 treats the host as vacuum and OVERCORRECTS "
           "badly for any real material."));
    fnvForm->addRow(tr("Dielectric constant ε:"), epsilonSpin_);

    defectIndexSpin_ = new QSpinBox(fnv);
    defectIndexSpin_->setRange(0, 100000);
    defectIndexSpin_->setToolTip(
        tr("Index of the defect site in the PRISTINE cell. The model charge is "
           "centred here and the potential-alignment region is defined away "
           "from it."));
    fnvForm->addRow(tr("Defect site index:"), defectIndexSpin_);

    rcSpin_ = new QDoubleSpinBox(fnv);
    rcSpin_->setRange(0.1, 20.0);
    rcSpin_->setDecimals(3);
    rcSpin_->setSingleStep(0.1);
    rcSpin_->setValue(1.0);
    rcSpin_->setSuffix(tr(" Å"));
    rcSpin_->setToolTip(
        tr("Width of the Gaussian model charge. The correction is only weakly "
           "sensitive to it as long as the model charge stays well inside the "
           "supercell — which is the point of checking the potential-alignment "
           "plateau."));
    fnvForm->addRow(tr("Model charge radius:"), rcSpin_);

    ravgSpin_ = new QDoubleSpinBox(fnv);
    ravgSpin_->setRange(0.5, 50.0);
    ravgSpin_->setDecimals(2);
    ravgSpin_->setValue(2.5);
    ravgSpin_->setSuffix(tr(" Å"));
    ravgSpin_->setToolTip(
        tr("Radius of the bulk-atom averaging region used for the potential "
           "alignment ΔV."));
    fnvForm->addRow(tr("Averaging radius:"), ravgSpin_);

    modelCutoffSpin_ = new QDoubleSpinBox(fnv);
    modelCutoffSpin_->setRange(50.0, 5000.0);
    modelCutoffSpin_->setDecimals(0);
    modelCutoffSpin_->setSingleStep(50.0);
    modelCutoffSpin_->setValue(500.0);
    modelCutoffSpin_->setSuffix(tr(" eV"));
    modelCutoffSpin_->setToolTip(
        tr("Plane-wave cutoff for the model potential — a numerical parameter "
           "of the correction, unrelated to the SCF cutoff."));
    fnvForm->addRow(tr("Model potential cutoff:"), modelCutoffSpin_);

    fermiPointsSpin_ = new QSpinBox(fnv);
    fermiPointsSpin_->setRange(2, 100000);
    fermiPointsSpin_->setValue(401);
    fermiPointsSpin_->setToolTip(
        tr("Samples on the Fermi-level axis of the diagram. The lines are "
           "exactly straight, so this only sets how finely the lower envelope "
           "and the transition levels are resolved."));
    fnvForm->addRow(tr("Fermi-level samples:"), fermiPointsSpin_);

    for (QDoubleSpinBox* spin : {epsilonSpin_, rcSpin_, ravgSpin_,
                                 modelCutoffSpin_})
        connect(spin, &QDoubleSpinBox::valueChanged, this,
                [this] { refreshPreview(); });
    for (QSpinBox* spin : {defectIndexSpin_, fermiPointsSpin_})
        connect(spin, &QSpinBox::valueChanged, this,
                [this] { refreshPreview(); });
    connect(fnvCheck_, &QCheckBox::toggled, this, [this](bool on) {
        for (QWidget* w : {static_cast<QWidget*>(epsilonSpin_),
                           static_cast<QWidget*>(defectIndexSpin_),
                           static_cast<QWidget*>(rcSpin_),
                           static_cast<QWidget*>(ravgSpin_),
                           static_cast<QWidget*>(modelCutoffSpin_)})
            w->setEnabled(on);
        refreshPreview();
    });
    layout->addWidget(fnv);
    layout->addStretch(1);

    refreshConsistencyNote();
    return page;
}

void DefectWizard::addSpeciesRow(const QString& symbol, int count, double mu)
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

void DefectWizard::suggestSpecies()
{
    // Only the ACTIVE tab's structure is available here, so the composition
    // difference cannot be computed from the two .gpw files without loading
    // them. What can be offered is the defect cell's own species list, with
    // counts left at zero for the user to sign — which is still most of the
    // typing, and never guesses the part that would be wrong.
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

void DefectWizard::refreshConsistencyNote()
{
    if (!consistencyNote_ || !pristineCombo_ || !neutralCombo_)
        return;
    const QString host = pristineCombo_->currentData().toString();
    const QString defect = neutralCombo_->currentData().toString();
    if (host.isEmpty() || defect.isEmpty()) {
        consistencyNote_->clear();
        // Cleared too, or the previous state's explanation would hover over an
        // empty label.
        consistencyNote_->setToolTip(QString());
        return;
    }
    if (host == defect) {
        consistencyNote_->setText(
            tr("<b style='color:#d9534f;'>The same calculation is selected as "
               "both the host and the defect.</b>"));
        consistencyNote_->setToolTip(
            tr("Every formation energy would come out as the "
               "chemical-potential term alone. Pick the pristine supercell for "
               "one and the defect supercell for the other."));
        return;
    }
    consistencyNote_->setText(
        tr("<i>Both cells must be the same size and use the same "
           "settings.</i>"));
    consistencyNote_->setToolTip(
        tr("E_tot[X^q] − E_tot[host] is a difference of two independent total "
           "energies, and it only means anything if everything except the "
           "defect is identical."));
}

void DefectWizard::setDensityBaselines(
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
    refreshConsistencyNote();
    refreshPreview();
}

QString DefectWizard::generateScript() const
{
    core::DefectConfig cfg;
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

    cfg.dielectricConstant = epsilonSpin_ ? epsilonSpin_->value() : 1.0;
    cfg.defectIndex = defectIndexSpin_ ? defectIndexSpin_->value() : 0;
    cfg.modelChargeRadius = rcSpin_ ? rcSpin_->value() : 1.0;
    cfg.averagingRadius = ravgSpin_ ? ravgSpin_->value() : 2.5;
    cfg.modelCutoffEv = modelCutoffSpin_ ? modelCutoffSpin_->value() : 500.0;
    cfg.applyFnvCorrection = fnvCheck_ && fnvCheck_->isChecked();
    cfg.fermiPoints = fermiPointsSpin_ ? fermiPointsSpin_->value() : 401;
    return QString::fromStdString(core::generateDefectScript(cfg));
}

} // namespace calango::gui
