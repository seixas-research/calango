#include "gui/NonlinearOpticsWizard.hpp"

#include "gui/GuiUtils.hpp"

#include "core/Structure.hpp"

#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QRegularExpression>
#include <QSpinBox>
#include <QVBoxLayout>
#include <QWidget>

namespace calango::gui {

NonlinearOpticsWizard::NonlinearOpticsWizard(
    std::shared_ptr<const core::Structure> structure, QWidget* parent)
    : SimulationWizardBase(parent)
    , structure_(std::move(structure))
{
    buildUi();
    selectCalculator(core::CalculatorKind::Gpaw);
    updateResponseRows();
}

QString NonlinearOpticsWizard::wizardTitle() const
{
    return tr("Nonlinear Optics Setup");
}

QString NonlinearOpticsWizard::settingsHeader() const
{
    return tr("Nonlinear Response Settings");
}

QStringList NonlinearOpticsWizard::calculatorElements() const
{
    return structureElements(structure_.get());
}

QWidget* NonlinearOpticsWizard::buildSettingsPage()
{
    auto* page = new QWidget(this);
    auto* layout = new QVBoxLayout(page);

    auto* intro = new QLabel(
        tr("Second-order response from GPAW's <code>gpaw.nlopt</code> module, "
           "within the independent-particle approximation. <b>Vanishes in any "
           "centrosymmetric crystal.</b>"),
        page);
    intro->setWordWrap(true);
    intro->setToolTip(
        tr("χ⁽²⁾(−2ω; ω, ω) is second-harmonic generation: two photons of "
           "energy ħω in, one of 2ħω out.\n\n"
           "σ⁽²⁾(0; ω, −ω) is the shift current — the DC photocurrent a "
           "homogeneous illuminated crystal carries with no junction and no "
           "field, the bulk photovoltaic effect.\n\n"
           "Both are odd-rank tensors. The generated script tests the cell for "
           "an inversion centre before it converges anything and says so, "
           "because what a finite k-mesh returns in that case looks like a "
           "spectrum and is numerical residue.\n\n"
           "The momentum matrix elements are built once and reused, so a "
           "second tensor component costs a band sum rather than another "
           "ground state."));
    intro->setTextFormat(Qt::RichText);
    layout->addWidget(intro);

    // -- Which responses ----------------------------------------------------
    auto* responseGroup = new QGroupBox(tr("Response"), page);
    auto* responseForm = new QFormLayout(responseGroup);
    responseForm_ = responseForm;

    shgCheck_ = new QCheckBox(
        tr("Second-harmonic generation χ⁽²⁾(−2ω; ω, ω)"), responseGroup);
    shgCheck_->setChecked(true);
    shgCheck_->setToolTip(
        tr("The second-harmonic susceptibility, reported in pm/V (and, for a "
           "sheet, as χ⁽²⁾ × L in nm²/V).\n\n"
           "Its features sit at the absorption edge AND at half of it: the "
           "two-photon resonance is the one with no counterpart in a linear "
           "spectrum, and it is the usual source of a misassigned peak."));
    responseForm->addRow(QString(), shgCheck_);

    shiftCheck_ = new QCheckBox(tr("Shift current σ⁽²⁾(0; ω, −ω)"),
                                responseGroup);
    shiftCheck_->setToolTip(
        tr("The ballistic photocurrent response, in A/V². This is the "
           "intrinsic bulk photovoltaic effect — a current from the shift in "
           "the centre of charge on excitation, not from a built-in field — "
           "so it needs the same broken inversion symmetry that SHG does."));
    responseForm->addRow(QString(), shiftCheck_);

    linearCheck_ = new QCheckBox(
        tr("Linear susceptibility tensor χ⁽¹⁾(ω)"), responseGroup);
    linearCheck_->setToolTip(
        tr("The full 3×3 linear tensor from the same matrix elements, and the "
           "dielectric function ε = 1 + χ⁽¹⁾ that follows from it.\n\n"
           "Cheap once the matrix elements exist, and worth having beside the "
           "nonlinear spectra: it is the curve the χ⁽²⁾ resonances have to be "
           "read against.\n\n"
           "This is the INDEPENDENT-PARTICLE tensor from gpaw.nlopt, not the "
           "local-field-corrected ε(ω) the Optics module computes — the two "
           "are consistent with each other only in the sense that they are "
           "different approximations to the same quantity."));
    responseForm->addRow(QString(), linearCheck_);

    for (QCheckBox* box : {shgCheck_, shiftCheck_, linearCheck_})
        connect(box, &QCheckBox::toggled, this,
                [this] { updateResponseRows(); });

    componentsEdit_ = new QLineEdit(QStringLiteral("yyy"), responseGroup);
    componentsEdit_->setToolTip(
        tr("Tensor components to evaluate — three letters from xyz, several "
           "separated by commas or spaces (e.g. \"yyy, xxy\").\n\n"
           "There is deliberately no \"all 27\": most vanish by symmetry, each "
           "costs a full sum over bands and k-points, and 27 spectra of which "
           "24 are numerical noise is not a better answer. The point group "
           "decides which are allowed — Analysis → Symmetry will name it."));
    responseForm->addRow(tr("Tensor components:"), componentsEdit_);
    connect(componentsEdit_, &QLineEdit::textChanged, this,
            [this] { updateResponseRows(); });

    componentsNote_ = new QLabel(responseGroup);
    componentsNote_->setWordWrap(true);
    componentsNote_->setTextFormat(Qt::RichText);
    responseForm->addRow(QString(), componentsNote_);

    gaugeCombo_ = new QComboBox(responseGroup);
    gaugeCombo_->addItem(tr("Length gauge (lg)"),
                         static_cast<int>(core::NlOpticsGauge::Length));
    gaugeCombo_->addItem(tr("Velocity gauge (vg)"),
                         static_cast<int>(core::NlOpticsGauge::Velocity));
    gaugeCombo_->setToolTip(
        tr("The two are formally equivalent and numerically are not.\n\n"
           "The velocity gauge carries low-frequency divergences that cancel "
           "only for a complete band set, so a truncated sum leaves it visibly "
           "wrong as ω → 0; the length gauge converges with far fewer bands "
           "and is GPAW's default.\n\n"
           "Running both and overlaying them is the standard convergence test "
           "for a χ⁽²⁾ spectrum — their disagreement says more about the band "
           "summation than any single number does."));
    responseForm->addRow(tr("SHG gauge:"), gaugeCombo_);
    connect(gaugeCombo_, &QComboBox::currentIndexChanged, this,
            [this] { refreshPreview(); });
    layout->addWidget(responseGroup);

    // -- Frequency grid ------------------------------------------------------
    auto* gridGroup = new QGroupBox(tr("Photon-Energy Grid"), page);
    auto* gridForm = new QFormLayout(gridGroup);

    etaSpin_ = new QDoubleSpinBox(gridGroup);
    etaSpin_->setRange(0.001, 2.0);
    etaSpin_->setDecimals(3);
    etaSpin_->setSingleStep(0.01);
    etaSpin_->setValue(0.05);
    etaSpin_->setSuffix(tr(" eV"));
    etaSpin_->setToolTip(
        tr("Lorentzian broadening η. A second-order spectrum is far more "
           "sensitive to it than a linear one: the resonances are sharper and "
           "the divergences at ω and 2ω are regularized by exactly this "
           "number, so a small η on a coarse k-mesh produces spikes rather "
           "than structure."));
    gridForm->addRow(tr("Broadening η:"), etaSpin_);

    omegaMinSpin_ = new QDoubleSpinBox(gridGroup);
    omegaMinSpin_->setRange(0.0, 100.0);
    omegaMinSpin_->setDecimals(2);
    omegaMinSpin_->setValue(0.0);
    omegaMinSpin_->setSuffix(tr(" eV"));
    gridForm->addRow(tr("Energy window min:"), omegaMinSpin_);

    omegaMaxSpin_ = new QDoubleSpinBox(gridGroup);
    omegaMaxSpin_->setRange(0.1, 100.0);
    omegaMaxSpin_->setDecimals(2);
    omegaMaxSpin_->setValue(6.0);
    omegaMaxSpin_->setSuffix(tr(" eV"));
    omegaMaxSpin_->setToolTip(
        tr("Upper photon energy. Remember that SHG probes the band structure "
           "at 2ω as well as at ω, so a window that ends at 6 eV is sampling "
           "transitions up to 12 eV — and the band count has to reach them."));
    gridForm->addRow(tr("Energy window max:"), omegaMaxSpin_);

    npointsSpin_ = new QSpinBox(gridGroup);
    npointsSpin_->setRange(2, 100000);
    npointsSpin_->setValue(500);
    npointsSpin_->setToolTip(
        tr("Samples across the window. The band sum is evaluated at every one "
           "of them, so the cost is linear in this count."));
    gridForm->addRow(tr("Number of points:"), npointsSpin_);
    layout->addWidget(gridGroup);

    // -- Band summation ------------------------------------------------------
    auto* bandGroup = new QGroupBox(tr("Band Summation"), page);
    auto* bandForm = new QFormLayout(bandGroup);
    auto* bandNote = new QLabel(
        tr("The ground state is converged with an explicit band count; these "
           "sums run over intermediate states."),
        bandGroup);
    bandNote->setWordWrap(true);
    bandNote->setToolTip(
        tr("nbands=\"nao\" with an explicit converged-band count, because the "
           "empty manifold an ordinary SCF leaves behind is unconverged "
           "noise.\n\n"
           "The window below narrows what the matrix elements are built for; "
           "leave it at the defaults unless memory forces otherwise."));
    bandNote->setTextFormat(Qt::RichText);
    bandForm->addRow(bandNote);

    scissorsSpin_ = new QDoubleSpinBox(bandGroup);
    scissorsSpin_->setRange(-5.0, 10.0);
    scissorsSpin_->setDecimals(2);
    scissorsSpin_->setSingleStep(0.1);
    scissorsSpin_->setValue(0.0);
    scissorsSpin_->setSuffix(tr(" eV"));
    scissorsSpin_->setToolTip(
        tr("Rigid shift of the empty bands (GPAW's `eshift`), for correcting "
           "the semilocal band-gap error.\n\n"
           "χ⁽²⁾ is more sensitive to the gap than the linear response is: the "
           "two-photon resonance sits at HALF of it, so an error there moves a "
           "peak by half as much again and rescales its height through the "
           "1/ω factors. The applied value is recorded in the results, so a "
           "spectrum never silently carries one."));
    bandForm->addRow(tr("Scissors shift:"), scissorsSpin_);

    auto* windowRow = new QHBoxLayout;
    bandFirstSpin_ = new QSpinBox(bandGroup);
    bandFirstSpin_->setRange(0, 100000);
    bandFirstSpin_->setValue(0);
    bandFirstSpin_->setPrefix(tr("from "));
    bandFirstSpin_->setToolTip(
        tr("First band included in the matrix elements (GPAW's `ni`). 0 starts "
           "at the lowest. Raising it drops deep semicore states, which "
           "contribute nothing at optical frequencies and cost memory."));
    bandLastSpin_ = new QSpinBox(bandGroup);
    bandLastSpin_->setRange(-100000, 100000);
    bandLastSpin_->setValue(0);
    bandLastSpin_->setPrefix(tr("to "));
    bandLastSpin_->setSpecialValueText(tr("to last"));
    bandLastSpin_->setToolTip(
        tr("Last band included (GPAW's `nf`). 0 means the last one; a "
           "negative value counts from the top, as GPAW reads it.\n\n"
           "Truncating here is not free: the sum over intermediate states is "
           "what the second-order response IS, and cutting it short does not "
           "make the spectrum slightly worse, it changes its shape."));
    windowRow->addWidget(bandFirstSpin_);
    windowRow->addWidget(bandLastSpin_);
    windowRow->addStretch(1);
    bandForm->addRow(tr("Band window:"), windowRow);
    layout->addWidget(bandGroup);

    // -- Sheet ---------------------------------------------------------------
    auto* sheetGroup = new QGroupBox(tr("2D Sheet"), page);
    auto* sheetForm = new QFormLayout(sheetGroup);
    auto* sheetNote = new QLabel(
        tr("A supercell χ⁽²⁾ is diluted by the vacuum used, so it is not a "
           "property of the sheet."),
        sheetGroup);
    sheetNote->setWordWrap(true);
    sheetNote->setToolTip(
        tr("Double the vacuum and the number halves. Multiplying the vacuum "
           "thickness back in gives the SHEET susceptibility χ⁽²⁾ × L in "
           "nm²/V, which is what the 2D literature quotes and what the GPAW "
           "tutorial plots."));
    sheetForm->addRow(sheetNote);
    vacuumAxisCombo_ = new QComboBox(sheetGroup);
    vacuumAxisCombo_->addItem(tr("(none — bulk crystal)"), -1);
    vacuumAxisCombo_->addItem(tr("a₁ (x)"), 0);
    vacuumAxisCombo_->addItem(tr("a₂ (y)"), 1);
    vacuumAxisCombo_->addItem(tr("a₃ (z)"), 2);
    // Seeded from the cell, but never decided by it: getting this wrong
    // rescales every sheet number silently, so the guess only preselects.
    const int guessed = guessVacuumAxis(structure_.get());
    vacuumAxisCombo_->setCurrentIndex(guessed >= 0 ? guessed + 1 : 0);
    vacuumAxisCombo_->setToolTip(
        tr("Which cell axis carries the vacuum, for a monolayer. Seeded from "
           "the geometry (the axis the atoms only partly occupy) but confirm "
           "it — the bulk numbers are reported either way, and this only adds "
           "the sheet ones."));
    sheetForm->addRow(tr("Vacuum axis:"), vacuumAxisCombo_);
    layout->addWidget(sheetGroup);
    connect(vacuumAxisCombo_, &QComboBox::currentIndexChanged, this,
            [this] { refreshPreview(); });

    for (QDoubleSpinBox* spin : {etaSpin_, omegaMinSpin_, omegaMaxSpin_,
                                 scissorsSpin_})
        connect(spin, &QDoubleSpinBox::valueChanged, this,
                [this] { refreshPreview(); });
    for (QSpinBox* spin : {npointsSpin_, bandFirstSpin_, bandLastSpin_})
        connect(spin, &QSpinBox::valueChanged, this,
                [this] { refreshPreview(); });

    layout->addStretch(1);
    return page;
}

QStringList NonlinearOpticsWizard::typedComponents() const
{
    if (!componentsEdit_)
        return {QStringLiteral("yyy")};
    return componentsEdit_->text().split(QRegularExpression(
                                             QStringLiteral("[,;\\s]+")),
                                         Qt::SkipEmptyParts);
}

void NonlinearOpticsWizard::updateResponseRows()
{
    const bool shg = shgCheck_ && shgCheck_->isChecked();
    const bool shift = shiftCheck_ && shiftCheck_->isChecked();
    // The gauge is a choice within the SHG sum; get_shift and get_chi_tensor
    // take no such argument, so with SHG off the row describes nothing.
    if (responseForm_ && gaugeCombo_) {
        int row = -1;
        QFormLayout::ItemRole role{};
        responseForm_->getWidgetPosition(gaugeCombo_, &row, &role);
        if (row >= 0)
            responseForm_->setRowVisible(row, shg);
    }

    if (componentsNote_) {
        if (!shg && !shift) {
            // Only the linear tensor is left, and it is computed whole.
            componentsNote_->setText(
                tr("<i>The linear tensor is computed whole; the component "
                   "list applies to SHG and the shift current only.</i>"));
        } else {
            QStringList bad;
            int good = 0;
            for (const QString& component : typedComponents()) {
                const QString lower = component.toLower();
                const bool valid =
                    lower.size() == 3
                    && !lower.contains(QRegularExpression(
                        QStringLiteral("[^xyz]")));
                if (valid)
                    ++good;
                else
                    bad << component;
            }
            if (!bad.isEmpty()) {
                // Named rather than dropped: a silently ignored "yyz " looks
                // exactly like a component that produced nothing.
                componentsNote_->setText(
                    tr("<b>Ignored:</b> %1 — a component is exactly three "
                       "letters from x, y and z.")
                        .arg(bad.join(QStringLiteral(", ")).toHtmlEscaped()));
            } else if (good == 0) {
                componentsNote_->setText(
                    tr("<b>No valid component</b> — the run would fall back "
                       "to <code>yyy</code>."));
            } else {
                componentsNote_->setText(
                    tr("%n component(s), each a full sum over bands and "
                       "k-points.",
                       nullptr, good));
            }
        }
    }
    refreshPreview();
}

core::NonlinearOpticsConfig NonlinearOpticsWizard::config() const
{
    core::NonlinearOpticsConfig cfg;
    cfg.calculator = baseCalculatorConfig();
    cfg.calculator.calculator = core::CalculatorKind::Gpaw;
    cfg.calculator.task = core::TaskKind::SinglePoint;
    cfg.computeShg = shgCheck_ && shgCheck_->isChecked();
    cfg.computeShift = shiftCheck_ && shiftCheck_->isChecked();
    cfg.computeLinear = linearCheck_ && linearCheck_->isChecked();
    cfg.gauge = gaugeCombo_
        ? static_cast<core::NlOpticsGauge>(gaugeCombo_->currentData().toInt())
        : core::NlOpticsGauge::Length;
    cfg.components.clear();
    for (const QString& component : typedComponents())
        cfg.components.push_back(component.toLower().toStdString());
    cfg.broadeningEv = etaSpin_ ? etaSpin_->value() : 0.05;
    cfg.omegaMinEv = omegaMinSpin_ ? omegaMinSpin_->value() : 0.0;
    cfg.omegaMaxEv = omegaMaxSpin_ ? omegaMaxSpin_->value() : 6.0;
    cfg.npoints = npointsSpin_ ? npointsSpin_->value() : 500;
    cfg.scissorsEv = scissorsSpin_ ? scissorsSpin_->value() : 0.0;
    cfg.bandsFirst = bandFirstSpin_ ? bandFirstSpin_->value() : 0;
    cfg.bandsLast = bandLastSpin_ ? bandLastSpin_->value() : 0;
    cfg.vacuumAxis =
        vacuumAxisCombo_ ? vacuumAxisCombo_->currentData().toInt() : -1;
    return cfg;
}

QString NonlinearOpticsWizard::generateScript() const
{
    return QString::fromStdString(
        core::generateNonlinearOpticsScript(config()));
}

} // namespace calango::gui
