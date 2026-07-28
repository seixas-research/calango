#include "gui/GpawElectronicRows.hpp"

#include <QComboBox>
#include <QCoreApplication>
#include <QHBoxLayout>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QRegularExpression>
#include <QSizePolicy>
#include <QSpinBox>
#include <QWidget>

#include <utility>

namespace calango::gui {

namespace {
// The rows are not a QObject, so tr() is spelled against the wizard base's
// context — the strings belong to the calculator pages that show them.
QString tr(const char* source)
{
    return QCoreApplication::translate("calango::gui::SimulationWizardBase",
                                       source);
}
} // namespace

void GpawElectronicRows::buildConvergenceRows(QFormLayout* form,
                                              QObject* owner)
{
    // Widgets are parented to the group box that owns `form`.
    QWidget* parent = form->parentWidget();

    convForm_ = form;

    smearingCombo_ = new QComboBox(parent);
    // Menu order is the order a user chooses in — the physical default first,
    // then the other broadenings, then the exact BZ integrators and the two
    // special cases. The enum travels as item data rather than as the row
    // number: binding the display order to the declaration order is what would
    // make reordering this list silently select a different method.
    // Per-item tool tips, not one tip on the combo: the difference between
    // these methods is the whole decision, and a single paragraph covering
    // eight of them is one nobody reads.
    const auto addMethod = [this](const QString& label,
                                  core::SmearingMethod method,
                                  const QString& tip) {
        smearingCombo_->addItem(label, static_cast<int>(method));
        smearingCombo_->setItemData(smearingCombo_->count() - 1, tip,
                                    Qt::ToolTipRole);
    };
    addMethod(tr("Fermi-Dirac"), core::SmearingMethod::FermiDirac,
              tr("Occupation at an electronic temperature — the physical "
                 "choice, and the only one whose free energy the reported "
                 "total energy is consistent with."));
    addMethod(tr("Gaussian"), core::SmearingMethod::Gaussian,
              tr("Gaussian broadening. Emitted as Methfessel-Paxton at order "
                 "0, which is what Gaussian smearing is."));
    addMethod(tr("Methfessel-Paxton"), core::SmearingMethod::MethfesselPaxton,
              tr("Hermite expansion of order N. Converges the total energy "
                 "faster than Gaussian for metals, at the cost of occupations "
                 "that can fall outside [0, 1]."));
    addMethod(tr("Marzari-Vanderbilt"),
              core::SmearingMethod::MarzariVanderbilt,
              tr("Cold smearing: a nearly entropy-free broadening, so the "
                 "total energy is close to the σ → 0 limit without "
                 "extrapolation."));
    addMethod(tr("Tetrahedron method"),
              core::SmearingMethod::TetrahedronMethod,
              tr("Linear tetrahedron BZ integration — no broadening at all. "
                 "Requires a Monkhorst-Pack k-grid; it cannot run on "
                 "Γ-only sampling."));
    addMethod(tr("Improved tetrahedron method"),
              core::SmearingMethod::ImprovedTetrahedronMethod,
              tr("Tetrahedron integration with Blöchl's curvature correction. "
                 "The accurate choice for a density of states, and equally "
                 "requires a Monkhorst-Pack grid."));
    addMethod(tr("Orbital-free"), core::SmearingMethod::OrbitalFree,
              tr("Thomas-Fermi occupations, for orbital-free DFT. Only "
                 "meaningful with an orbital-free functional."));
    addMethod(tr("Fixed"), core::SmearingMethod::FixedOccupations,
              tr("Occupation numbers given explicitly instead of derived from "
                 "a Fermi level — how a core hole or a specific excited "
                 "configuration is forced."));
    addMethod(tr("None (no smearing)"), core::SmearingMethod::None,
              tr("Zero-width occupations: integer filling by the aufbau "
                 "principle. Insulators and isolated molecules."));
    // Fermi-Dirac by default: it is the physical occupation function at an
    // electronic temperature, it is what GPAW's own default resolves to, and
    // unlike Gaussian it converges to a free energy that the reported total
    // energy is actually consistent with.
    smearingCombo_->setCurrentIndex(smearingCombo_->findData(
        static_cast<int>(core::SmearingMethod::FermiDirac)));
    smearingCombo_->setToolTip(
        tr("Occupation-number broadening. Use smearing for metals; None for "
           "insulators and isolated molecules. The parameters beside this "
           "dropdown change with the method — each one takes only what GPAW "
           "accepts for it."));
    // Half width: the entries are short ("Fermi-Dirac", "Gaussian") and the
    // combo was stretching to fill the row, pushing the width spin box that
    // belongs beside it out to the margin.
    smearingCombo_->setSizeAdjustPolicy(QComboBox::AdjustToContents);
    smearingCombo_->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    QObject::connect(smearingCombo_, &QComboBox::currentIndexChanged, owner,
                     [this] { updateEnabled(); });

    smearingWidthSpin_ = new QDoubleSpinBox(parent);
    smearingWidthSpin_->setDecimals(3);
    smearingWidthSpin_->setRange(0.0, 5.0);
    smearingWidthSpin_->setSingleStep(0.05);
    smearingWidthSpin_->setValue(0.1);
    smearingWidthSpin_->setSuffix(tr(" eV"));
    smearingWidthSpin_->setToolTip(
        tr("Broadening width σ (electronic temperature) for the smearing."));

    smearingOrderSpin_ = new QSpinBox(parent);
    // GPAW accepts any non-negative order; past ~4 the occupation function
    // oscillates enough that the ringing costs more than the faster
    // convergence buys, so the cap is a guard rail rather than a limit of the
    // method.
    smearingOrderSpin_->setRange(0, 10);
    smearingOrderSpin_->setValue(1);
    smearingOrderSpin_->setToolTip(
        tr("Order N of the Methfessel-Paxton Hermite expansion. N = 1 is the "
           "usual choice; N = 0 is plain Gaussian smearing, which has its own "
           "entry in the list. Higher orders converge the σ → 0 energy faster "
           "but let occupations leave [0, 1], which can destabilize the SCF."));

    // Method and its parameters are one decision — a smearing without its
    // width is half an answer — so they share a row instead of stacking.
    // Which parameters appear is the method's business: updateEnabled() shows
    // only the ones it takes.
    auto* smearingRow = new QWidget(parent);
    auto* smearingLayout = new QHBoxLayout(smearingRow);
    smearingLayout->setContentsMargins(0, 0, 0, 0);
    // Stretch factor 1 on the trailing spacer rather than on the combo, so the
    // combo keeps its content width instead of absorbing the whole row.
    smearingLayout->addWidget(smearingCombo_);
    smearingWidthLabel_ = new QLabel(tr("width σ"), smearingRow);
    smearingLayout->addWidget(smearingWidthLabel_);
    smearingLayout->addWidget(smearingWidthSpin_);
    smearingOrderLabel_ = new QLabel(tr("order N"), smearingRow);
    smearingLayout->addWidget(smearingOrderLabel_);
    smearingLayout->addWidget(smearingOrderSpin_);
    smearingLayout->addStretch(1);
    form->addRow(tr("Smearing method:"), smearingRow);

    // Only "Fixed" needs this, and it needs the full row width — an occupation
    // list is as long as the band count, not a single number.
    fixedOccupationsEdit_ = new QLineEdit(parent);
    fixedOccupationsEdit_->setPlaceholderText(
        tr("e.g.  2 2 2 0 0    (spin-polarized:  1 0 1 0 ; 1 1 0 0)"));
    fixedOccupationsEdit_->setToolTip(
        tr("Occupation number of each band, in order. Separate values with "
           "spaces or commas.\n"
           "A semicolon starts a second spin channel, which a spin-polarized "
           "run needs: \"1 0 1 0 ; 1 1 0 0\".\n\n"
           "There is no default — GPAW cannot guess a configuration you are "
           "overriding it to impose — so this must be filled in before the "
           "script can be generated."));
    form->addRow(tr("Occupation numbers:"), fixedOccupationsEdit_);
    QObject::connect(fixedOccupationsEdit_, &QLineEdit::textChanged, owner,
                     [this] { updateEnabled(); });

    smearingNote_ = new QLabel(parent);
    smearingNote_->setWordWrap(true);
    smearingNote_->setTextFormat(Qt::RichText);
    form->addRow(QString(), smearingNote_);

    scfTolSpin_ = new QDoubleSpinBox(parent);
    scfTolSpin_->setDecimals(8);
    scfTolSpin_->setRange(1e-8, 1.0);
    scfTolSpin_->setValue(1e-4);
    scfTolSpin_->setSuffix(tr(" eV"));
    scfTolSpin_->setToolTip(
        tr("Electronic-energy convergence threshold for the SCF cycle."));
    // NOT added to this form: the base class places it on the "Convergence
    // tolerances" row beside the eigenstates and density thresholds, which are
    // the two it is converged together with. Parented here so it is owned
    // either way.

    scfStepsSpin_ = new QSpinBox(parent);
    scfStepsSpin_->setRange(1, 100000);
    // 500 rather than 100: a magnetic or metallic system routinely needs a few
    // hundred iterations, and a cap that stops a converging SCF costs the whole
    // run while saving nothing — the convergence criteria are what end it.
    scfStepsSpin_->setValue(500);
    scfStepsSpin_->setToolTip(
        tr("Maximum number of self-consistent-field iterations. This is a "
           "runaway guard, not a target: the convergence thresholds are what "
           "normally end the cycle."));
    // Also placed by the base class, beside the eigensolver: how the SCF is
    // solved and how long it is allowed to try are one thought.

    updateEnabled();
}

void GpawElectronicRows::buildSpinRows(QFormLayout* form, QObject* owner)
{
    QWidget* parent = form->parentWidget();

    spinModeCombo_ = new QComboBox(parent);
    // Order matches core::SpinMode.
    spinModeCombo_->addItem(tr("Unpolarized (spin-restricted)"));
    spinModeCombo_->addItem(tr("Collinear Spin-Polarized (↑/↓)"));
    spinModeCombo_->addItem(tr("Non-Collinear Spin (spinors)"));
    spinModeCombo_->setToolTip(
        tr("Unpolarized: no spin degree of freedom.\n"
           "Collinear: spin-up / spin-down densities (scalar magnetic "
           "moments).\n"
           "Non-Collinear: spinor magnetism (vector moments; the list below is "
           "applied along +z)."));
    form->addRow(tr("Spin polarization:"), spinModeCombo_);
    QObject::connect(spinModeCombo_, &QComboBox::currentIndexChanged, owner,
                     [this] { updateEnabled(); });

    // The per-atom initial moments used to be typed here as a comma-separated
    // list. They are not: they are a property OF THE STRUCTURE, they are set
    // per atom in Edit Structure (which knows which atom is which), and they
    // travel with the staged structure file. A second copy in a text box was a
    // second source of truth that silently won — a user who set moments on the
    // right atoms in the editor got the box's "1.0" instead.
    momentsNote_ = new QLabel(
        tr("<i>Taken from the structure. Set them per atom in "
           "<b>Edit Structure…</b> → Spin polarization; they travel with the "
           "staged geometry as its initial magnetic moments.</i>"),
        parent);
    momentsNote_->setWordWrap(true);
    momentsNote_->setTextFormat(Qt::RichText);
    form->addRow(tr("Initial magnetic moments:"), momentsNote_);

    updateEnabled();
}

core::SmearingMethod GpawElectronicRows::selectedMethod() const
{
    if (!smearingCombo_)
        return core::SmearingMethod::FermiDirac;
    return static_cast<core::SmearingMethod>(
        smearingCombo_->currentData().toInt());
}

std::vector<std::vector<double>>
GpawElectronicRows::parseFixedOccupations() const
{
    std::vector<std::vector<double>> channels;
    if (!fixedOccupationsEdit_)
        return channels;
    const QString text = fixedOccupationsEdit_->text().trimmed();
    if (text.isEmpty())
        return channels;
    // ';' separates spin channels; within a channel, any run of whitespace or
    // commas separates numbers. A single malformed token invalidates the whole
    // field rather than being dropped: a silently shortened occupation list
    // would run and give the wrong electron count.
    const QStringList parts = text.split(QLatin1Char(';'), Qt::SkipEmptyParts);
    for (const QString& part : parts) {
        std::vector<double> numbers;
        const QStringList tokens =
            part.split(QRegularExpression(QStringLiteral("[\\s,]+")),
                       Qt::SkipEmptyParts);
        for (const QString& token : tokens) {
            bool ok = false;
            const double value = token.toDouble(&ok);
            if (!ok)
                return {};
            numbers.push_back(value);
        }
        if (numbers.empty())
            return {};
        channels.push_back(std::move(numbers));
    }
    return channels;
}

QString GpawElectronicRows::validationError() const
{
    if (!smearingCombo_
        || !core::smearingUsesFixedOccupations(selectedMethod()))
        return {};
    if (parseFixedOccupations().empty())
        return tr("Fixed occupations need an explicit occupation number per "
                  "band — enter them beside \"Occupation numbers\", or choose "
                  "another smearing method.");
    return {};
}

void GpawElectronicRows::updateEnabled()
{
    // The moments note is only meaningful when spin is enabled (collinear or
    // non-collinear, i.e. any mode past Unpolarized).
    const bool spin = spinModeCombo_
        && spinModeCombo_->currentIndex()
            != static_cast<int>(core::SpinMode::Unpolarized);
    if (momentsNote_)
        momentsNote_->setEnabled(spin);

    if (!smearingCombo_)
        return;
    const core::SmearingMethod method = selectedMethod();
    const bool usesWidth = core::smearingUsesWidth(method);
    const bool usesOrder = core::smearingUsesOrder(method);
    const bool usesNumbers = core::smearingUsesFixedOccupations(method);

    // Hidden, not merely disabled: these are different parameters belonging to
    // different methods, not one parameter that happens to be unavailable.
    smearingWidthLabel_->setVisible(usesWidth);
    smearingWidthSpin_->setVisible(usesWidth);
    smearingOrderLabel_->setVisible(usesOrder);
    smearingOrderSpin_->setVisible(usesOrder);
    // setRowVisible also drops the row's LABEL and its vertical space, which
    // hiding the field widget alone would leave behind as a gap.
    if (convForm_)
        convForm_->setRowVisible(fixedOccupationsEdit_, usesNumbers);

    // The note carries what the method requires of the rest of the setup —
    // the part a user cannot infer from the parameter boxes.
    QString note;
    switch (method) {
    case core::SmearingMethod::TetrahedronMethod:
    case core::SmearingMethod::ImprovedTetrahedronMethod:
        note = tr("Integrates the Brillouin zone exactly, so it takes no "
                  "width — but it needs a <b>Monkhorst-Pack k-grid</b> and "
                  "fails on Γ-only sampling. Set the k-points above "
                  "accordingly.");
        break;
    case core::SmearingMethod::OrbitalFree:
        note = tr("Thomas-Fermi occupations. Only meaningful together with an "
                  "<b>orbital-free functional</b>; on a normal Kohn-Sham run "
                  "this is not the setting you want.");
        break;
    case core::SmearingMethod::FixedOccupations:
        note = validationError().isEmpty()
            ? tr("Occupations are imposed, not derived: the Fermi level plays "
                 "no part and the electron count is whatever you type.")
            : QStringLiteral("<b style='color:#d9534f;'>%1</b>")
                  .arg(validationError().toHtmlEscaped());
        break;
    case core::SmearingMethod::Gaussian:
        note = tr("Generated as <tt>methfessel-paxton</tt> at order 0 — GPAW "
                  "has no separate name for Gaussian smearing, and order 0 is "
                  "its definition.");
        break;
    case core::SmearingMethod::None:
        note = tr("Integer filling by the aufbau principle. Correct for an "
                  "insulator or a molecule; on a metal the SCF will struggle "
                  "or converge to the wrong state.");
        break;
    case core::SmearingMethod::FermiDirac:
    case core::SmearingMethod::MethfesselPaxton:
    case core::SmearingMethod::MarzariVanderbilt:
        break;
    }
    smearingNote_->setText(note);
    if (convForm_)
        convForm_->setRowVisible(smearingNote_, !note.isEmpty());
}

QWidget* GpawElectronicRows::energyToleranceWidget() const
{
    return scfTolSpin_;
}

QWidget* GpawElectronicRows::scfStepsWidget() const
{
    return scfStepsSpin_;
}

void GpawElectronicRows::applyTo(core::CalculatorConfig& config) const
{
    if (scfStepsSpin_)
        config.scfMaxSteps = scfStepsSpin_->value();
    if (scfTolSpin_)
        config.scfEnergyTolEv = scfTolSpin_->value();
    if (spinModeCombo_) {
        config.spinMode =
            static_cast<core::SpinMode>(spinModeCombo_->currentIndex());
        // Keep the boolean in sync for the many callers that only read it.
        config.spinPolarized = config.spinMode != core::SpinMode::Unpolarized;
    }
    // No moments are written here: they live on the structure now and reach the
    // run through the staged geometry file.
    if (smearingCombo_) {
        // currentData(), not currentIndex(): the menu is ordered for the user
        // and no longer mirrors the enum's declaration order.
        config.smearing = selectedMethod();
        // Every parameter is written whether or not the current method reads
        // it, so that switching methods back and forth does not lose the value
        // the user typed. The generator emits only the keys the method takes.
        if (smearingWidthSpin_)
            config.smearingWidthEv = smearingWidthSpin_->value();
        if (smearingOrderSpin_)
            config.smearingOrder = smearingOrderSpin_->value();
        config.fixedOccupations = parseFixedOccupations();
    }
}

} // namespace calango::gui
