#include "gui/OpticsWizard.hpp"

#include "core/OpticsScriptGenerator.hpp"
#include "core/Structure.hpp"

#include <QCheckBox>
#include <QDoubleSpinBox>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QComboBox>
#include <QGroupBox>
#include <QLabel>
#include <QSpinBox>
#include <QVBoxLayout>
#include <QWidget>

#include <algorithm>

namespace calango::gui {

OpticsWizard::OpticsWizard(std::shared_ptr<core::Structure> structure,
                           bool twoDimensional, QWidget* parent)
    : SimulationWizardBase(parent)
    , structure_(std::move(structure))
    , twoDimensional_(twoDimensional)
{
    buildUi();
}

QString OpticsWizard::wizardTitle() const
{
    return twoDimensional_ ? tr("2D Optics Setup")
                           : tr("Optical Properties Setup");
}

QString OpticsWizard::settingsHeader() const
{
    return tr("Optical Response Settings");
}

QWidget* OpticsWizard::buildSettingsPage()
{
    auto* page = new QWidget(this);
    auto* layout = new QVBoxLayout(page);

    // -- Mandatory ground-state baseline ------------------------------------
    auto* baselineGroup = new QGroupBox(tr("Ground-State Baseline"), page);
    auto* baselineForm = new QFormLayout(baselineGroup);
    auto* baselineNote = new QLabel(
        tr("The response is evaluated at the FIXED density of a completed "
           "Single-Point Calculation — its SCF is inherited, never re-run. "
           "Re-converging here would give a spectrum from a different ground "
           "state than the one you validated."),
        baselineGroup);
    baselineNote->setWordWrap(true);
    baselineForm->addRow(baselineNote);
    baselineCombo_ = new QComboBox(baselineGroup);
    baselineForm->addRow(tr("Baseline SCF (.gpw):"), baselineCombo_);
    // What is being inherited, spelled out. With no Calculator Settings stage
    // this is the only place the run's cutoff / xc / k-grid are visible, and
    // they are exactly what determines whether the spectrum is trustworthy.
    inheritanceNote_ = new QLabel(baselineGroup);
    inheritanceNote_->setWordWrap(true);
    inheritanceNote_->setTextFormat(Qt::RichText);
    baselineForm->addRow(inheritanceNote_);
    connect(baselineCombo_, &QComboBox::currentIndexChanged, this,
            [this] { onBaselineChanged(); });
    layout->addWidget(baselineGroup);

    if (twoDimensional_) {
        auto* sheetGroup = new QGroupBox(tr("2D Sheet"), page);
        auto* sheetForm = new QFormLayout(sheetGroup);
        auto* sheetNote = new QLabel(
            tr("A supercell dielectric function is diluted by whatever vacuum "
               "was used — double the vacuum and ε₃D moves, so it is not a "
               "property of the sheet. Dividing that thickness back out gives "
               "α₂D, σ₂D and the absorbance, which are."),
            sheetGroup);
        sheetNote->setWordWrap(true);
        sheetForm->addRow(sheetNote);
        vacuumAxisCombo_ = new QComboBox(sheetGroup);
        vacuumAxisCombo_->addItem(tr("a₁ (x)"), 0);
        vacuumAxisCombo_->addItem(tr("a₂ (y)"), 1);
        vacuumAxisCombo_->addItem(tr("a₃ (z)"), 2);
        const int guessed = guessVacuumAxis();
        vacuumAxisCombo_->setCurrentIndex(guessed >= 0 ? guessed : 2);
        vacuumAxisCombo_->setToolTip(
            tr("Which cell axis carries the vacuum. Seeded from the cell (the "
               "long axis the atoms only partly occupy) but confirm it: "
               "getting it wrong rescales every 2D quantity by the wrong "
               "length, silently."));
        sheetForm->addRow(tr("Vacuum axis:"), vacuumAxisCombo_);
        layout->addWidget(sheetGroup);
    }

    auto* intro = new QLabel(
        tr("Compute the frequency-dependent dielectric function ε(ω) and the "
           "derived optical spectra (absorption, reflectivity, refractive "
           "index and energy loss) from GPAW's linear-response module. The "
           "ground-state cutoff and k-grid come from the baseline above — a "
           "spectrum is only as converged as the SCF it is built on, so a "
           "coarse baseline k-grid needs a denser one, not a finer η here."),
        page);
    intro->setWordWrap(true);
    layout->addWidget(intro);

    auto* form = new QFormLayout;
    layout->addLayout(form);

    broadeningSpin_ = new QDoubleSpinBox(page);
    broadeningSpin_->setDecimals(3);
    broadeningSpin_->setRange(0.001, 5.0);
    broadeningSpin_->setSingleStep(0.05);
    broadeningSpin_->setValue(0.1);
    broadeningSpin_->setSuffix(tr(" eV"));
    broadeningSpin_->setToolTip(
        tr("Lorentzian broadening η applied to the dielectric function. "
           "Smaller values resolve sharp features but need a denser k-mesh."));
    form->addRow(tr("Broadening η:"), broadeningSpin_);

    omegaMinSpin_ = new QDoubleSpinBox(page);
    omegaMinSpin_->setDecimals(2);
    omegaMinSpin_->setRange(0.0, 1000.0);
    omegaMinSpin_->setValue(0.0);
    omegaMinSpin_->setSuffix(tr(" eV"));
    omegaMinSpin_->setToolTip(tr("Lower bound of the photon-energy window."));
    form->addRow(tr("Energy window min:"), omegaMinSpin_);

    omegaMaxSpin_ = new QDoubleSpinBox(page);
    omegaMaxSpin_->setDecimals(2);
    omegaMaxSpin_->setRange(0.1, 1000.0);
    omegaMaxSpin_->setValue(20.0);
    omegaMaxSpin_->setSuffix(tr(" eV"));
    omegaMaxSpin_->setToolTip(tr("Upper bound of the photon-energy window."));
    form->addRow(tr("Energy window max:"), omegaMaxSpin_);

    tetrahedronCheck_ =
        new QCheckBox(tr("Tetrahedron integration (Brillouin zone)"), page);
    tetrahedronCheck_->setToolTip(
        tr("Integrate the response by linear tetrahedron interpolation instead "
           "of summing over k-points.\n"
           "Point integration gives every transition a Lorentzian of width η, "
           "so any feature narrower than η — a van Hove singularity, a 2D "
           "absorption edge — is smeared rather than resolved. Tetrahedron "
           "integration resolves those on a mesh where point integration is "
           "still noisy.\n\n"
           "Requires the BASELINE's k-grid to contain every vertex of the "
           "irreducible Brillouin zone (gpaw.bztools."
           "find_high_symmetry_monkhorst_pack). An ordinary Monkhorst-Pack "
           "grid usually does not, and the run will stop with that message "
           "rather than quietly switch back to point integration."));
    form->addRow(QString(), tetrahedronCheck_);

    npointsSpin_ = new QSpinBox(page);
    npointsSpin_->setRange(2, 100000);
    npointsSpin_->setValue(500);
    npointsSpin_->setToolTip(
        tr("Number of samples on the photon-energy grid."));
    form->addRow(tr("Number of points:"), npointsSpin_);

    // The three diagonal components εxx / εyy / εzz. Off-diagonal terms are not
    // requested here, so this covers the full response of an orthorhombic (or
    // higher-symmetry) cell; for isotropic systems the three coincide.
    auto* dirRow = new QHBoxLayout;
    dirXxCheck_ = new QCheckBox(tr("xx"), page);
    dirYyCheck_ = new QCheckBox(tr("yy"), page);
    dirZzCheck_ = new QCheckBox(tr("zz"), page);
    dirXxCheck_->setChecked(true);
    dirYyCheck_->setChecked(true);
    dirZzCheck_->setChecked(true);
    dirRow->addWidget(dirXxCheck_);
    dirRow->addWidget(dirYyCheck_);
    dirRow->addWidget(dirZzCheck_);
    dirRow->addStretch(1);
    form->addRow(tr("Polarization directions:"), dirRow);

    // Keep the script preview live as the user tunes the settings.
    connect(broadeningSpin_, &QDoubleSpinBox::valueChanged, this,
            [this] { refreshPreview(); });
    connect(omegaMinSpin_, &QDoubleSpinBox::valueChanged, this,
            [this] { refreshPreview(); });
    connect(omegaMaxSpin_, &QDoubleSpinBox::valueChanged, this,
            [this] { refreshPreview(); });
    connect(npointsSpin_, &QSpinBox::valueChanged, this,
            [this] { refreshPreview(); });
    connect(tetrahedronCheck_, &QCheckBox::toggled, this,
            [this] { refreshPreview(); });
    connect(dirXxCheck_, &QCheckBox::toggled, this,
            [this] { refreshPreview(); });
    connect(dirYyCheck_, &QCheckBox::toggled, this,
            [this] { refreshPreview(); });
    connect(dirZzCheck_, &QCheckBox::toggled, this,
            [this] { refreshPreview(); });

    layout->addStretch(1);
    return page;
}

bool OpticsWizard::calculatorAllowed(core::CalculatorKind kind) const
{
    return kind == core::CalculatorKind::Gpaw;
}


void OpticsWizard::setDensityBaselines(
    const QList<QPair<QString, QString>>& baselines)
{
    if (!baselineCombo_)
        return;
    baselineCombo_->clear();
    for (const auto& [label, path] : baselines)
        baselineCombo_->addItem(label, path);
    onBaselineChanged();
}

void OpticsWizard::onBaselineChanged()
{
    const QString gpw =
        baselineCombo_ ? baselineCombo_->currentData().toString() : QString();
    // The combo holds the .gpw; the provenance sidecar sits in its job dir.
    const QString dir =
        gpw.isEmpty() ? QString() : QFileInfo(gpw).absolutePath();
    inherited_ = dir.isEmpty() ? std::nullopt : readCalculatorProvenance(dir);

    if (inheritanceNote_) {
        if (inherited_) {
            QString note = tr("Inherited: %1")
                               .arg(inherited_->summary().toHtmlEscaped());
            if (!inherited_->condaEnv.isEmpty())
                note += tr(" — env <code>%1</code>")
                            .arg(inherited_->condaEnv.toHtmlEscaped());
            inheritanceNote_->setText(note);
        } else if (gpw.isEmpty()) {
            inheritanceNote_->clear();
        } else {
            inheritanceNote_->setText(
                tr("This baseline carries no <code>calculator.json</code>, so "
                   "its parameters cannot be shown. GPAW still restores them "
                   "from the <code>.gpw</code> at run time."));
        }
    }
    refreshPreview();
}

QString OpticsWizard::pythonExecutable() const
{
    if (inherited_ && !inherited_->pythonExecutable.isEmpty())
        return inherited_->pythonExecutable;
    return SimulationWizardBase::pythonExecutable();
}

int OpticsWizard::guessVacuumAxis() const
{
    if (!structure_ || !structure_->cell().isDefined() || structure_->empty())
        return -1;
    // A slab's vacuum axis is the one whose atoms span far less than the cell.
    // Reported as a seed only — the combo is the authority, because a thick
    // slab in a modest cell and a thin one in a huge cell are not reliably
    // distinguishable from the geometry alone.
    int best = -1;
    double bestEmptiness = 0.35; // needs to be clearly a vacuum, not a guess
    for (int axis = 0; axis < 3; ++axis) {
        double lo = 1.0;
        double hi = 0.0;
        for (const core::Atom& atom : structure_->atoms()) {
            const core::Vec3 f =
                structure_->cell().cartesianToFractional(atom.position);
            const double v = axis == 0 ? f.x : (axis == 1 ? f.y : f.z);
            lo = std::min(lo, v);
            hi = std::max(hi, v);
        }
        const double emptiness = 1.0 - (hi - lo);
        if (emptiness > bestEmptiness) {
            bestEmptiness = emptiness;
            best = axis;
        }
    }
    return best;
}

QString OpticsWizard::generateScript() const
{
    core::OpticsConfig cfg;
    if (baselineCombo_)
        cfg.baselineDensityPath =
            baselineCombo_->currentData().toString().toStdString();
    if (twoDimensional_ && vacuumAxisCombo_)
        cfg.vacuumAxis = vacuumAxisCombo_->currentData().toInt();
    cfg.calculator = baseCalculatorConfig();
    cfg.broadeningEv = broadeningSpin_->value();
    cfg.omegaMinEv = omegaMinSpin_->value();
    cfg.omegaMaxEv = omegaMaxSpin_->value();
    cfg.npoints = npointsSpin_->value();
    cfg.tetrahedronIntegration = tetrahedronCheck_->isChecked();
    cfg.dirX = dirXxCheck_->isChecked();
    cfg.dirY = dirYyCheck_->isChecked();
    cfg.dirZ = dirZzCheck_->isChecked();
    return QString::fromStdString(core::generateOpticsScript(cfg));
}

} // namespace calango::gui
