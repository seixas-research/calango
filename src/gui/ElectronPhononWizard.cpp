#include "gui/ElectronPhononWizard.hpp"

#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QSpinBox>
#include <QVBoxLayout>

namespace calango::gui {

ElectronPhononWizard::ElectronPhononWizard(QWidget* parent)
    : SimulationWizardBase(parent)
{
    buildUi();
    selectCalculator(core::CalculatorKind::Gpaw);
}

QString ElectronPhononWizard::wizardTitle() const
{
    return tr("Electron-Phonon Coupling");
}

QWidget* ElectronPhononWizard::buildSettingsPage()
{
    auto* page = new QWidget(this);
    auto* layout = new QVBoxLayout(page);

    auto* intro = new QLabel(
        tr("Electron-phonon matrix elements <b>g<sub>mn</sub><sup>ν</sup>(k,q)"
           "</b> by supercell finite differences (<tt>gpaw.elph</tt>), and the "
           "quantities derived from them: the Eliashberg spectral function "
           "α²F(ω), the coupling constant λ, and the electron-phonon "
           "<b>relaxation time τ</b> that the Drude term in the Optics module "
           "takes as its input.<br><br>"
           "The run displaces every atom of the cell by ±δ along x, y and z "
           "<i>inside the supercell</i> and records how the effective "
           "potential responds. That is <b>6N+1 supercell SCF runs</b> and is "
           "essentially the whole cost — everything after it is cheap."),
        page);
    intro->setWordWrap(true);
    intro->setTextFormat(Qt::RichText);
    layout->addWidget(intro);

    // -- Displacement supercell ---------------------------------------------
    auto* fdGroup = new QGroupBox(tr("Finite Displacements"), page);
    auto* fdForm = new QFormLayout(fdGroup);

    auto* supercellRow = new QHBoxLayout;
    for (int axis = 0; axis < 3; ++axis) {
        supercellSpins_[axis] = new QSpinBox(fdGroup);
        supercellSpins_[axis]->setRange(1, 8);
        supercellSpins_[axis]->setValue(2);
        supercellRow->addWidget(supercellSpins_[axis]);
    }
    supercellRow->addStretch(1);
    fdForm->addRow(tr("Supercell:"), supercellRow);
    for (QSpinBox* spin : supercellSpins_)
        spin->setToolTip(
            tr("Repetitions of the cell the displacements are made in.\n\n"
               "This is the RANGE of the electron-phonon interaction the "
               "calculation can represent: ∂V/∂u is only known out to the "
               "supercell boundary, so a supercell that is too small "
               "truncates the coupling silently rather than failing. It also "
               "decides which phonon q-points exist at all — only those "
               "commensurate with it.\n\n"
               "Cost is linear in the number of displacements and steeply "
               "superlinear in the supercell's atom count, so each added "
               "repetition is expensive."));

    deltaSpin_ = new QDoubleSpinBox(fdGroup);
    deltaSpin_->setDecimals(3);
    deltaSpin_->setRange(0.001, 0.1);
    deltaSpin_->setSingleStep(0.005);
    deltaSpin_->setValue(0.01);
    deltaSpin_->setSuffix(tr(" Å"));
    deltaSpin_->setToolTip(
        tr("Displacement amplitude δ. The potential derivative is a finite "
           "difference over ±δ, so δ trades truncation error (too large, and "
           "anharmonicity contaminates the derivative) against numerical "
           "noise (too small, and the difference is dominated by the SCF "
           "convergence threshold). 0.01 Å is the standard compromise."));
    fdForm->addRow(tr("Displacement δ:"), deltaSpin_);

    basisCombo_ = new QComboBox(fdGroup);
    basisCombo_->addItem(tr("dzp — double-ζ + polarization (production)"),
                         QStringLiteral("dzp"));
    basisCombo_->addItem(tr("szp(dzp) — single-ζ + polarization"),
                         QStringLiteral("szp(dzp)"));
    basisCombo_->addItem(tr("sz(dzp) — single-ζ (fastest, testing)"),
                         QStringLiteral("sz(dzp)"));
    basisCombo_->setToolTip(
        tr("LCAO basis. Not a mode choice — the supercell stage projects the "
           "potential gradients onto BASIS FUNCTIONS, so this workflow has no "
           "plane-wave path at all.\n\n"
           "The basis enters the matrix elements directly, so a single-ζ set "
           "is a testing tool rather than a cheaper answer."));
    fdForm->addRow(tr("LCAO basis:"), basisCombo_);
    layout->addWidget(fdGroup);

    // -- Meshes -------------------------------------------------------------
    auto* meshGroup = new QGroupBox(tr("Electron and Phonon Meshes"), page);
    auto* meshForm = new QFormLayout(meshGroup);

    auto* kRow = new QHBoxLayout;
    for (int axis = 0; axis < 3; ++axis) {
        kGridSpins_[axis] = new QSpinBox(meshGroup);
        kGridSpins_[axis]->setRange(1, 64);
        kGridSpins_[axis]->setValue(8);
        kRow->addWidget(kGridSpins_[axis]);
    }
    kRow->addStretch(1);
    meshForm->addRow(tr("Electron k-mesh:"), kRow);
    for (QSpinBox* spin : kGridSpins_)
        spin->setToolTip(
            tr("Electronic mesh for the ground state and the Bloch rotation.\n\n"
               "α²F is a FERMI-SURFACE integral — both electron delta "
               "functions sit at E_F — so it converges with this mesh as "
               "slowly as the plasma frequency does in the optics module, and "
               "for the same reason: only states at E_F contribute, and a "
               "coarse mesh has few of them.\n\n"
               "It must also be a multiple of the q-mesh, so that every k+q "
               "is itself a sampled state."));

    auto* qRow = new QHBoxLayout;
    for (int axis = 0; axis < 3; ++axis) {
        qGridSpins_[axis] = new QSpinBox(meshGroup);
        qGridSpins_[axis]->setRange(1, 16);
        qGridSpins_[axis]->setValue(2);
        qRow->addWidget(qGridSpins_[axis]);
    }
    qRow->addStretch(1);
    meshForm->addRow(tr("Phonon q-mesh:"), qRow);
    for (QSpinBox* spin : qGridSpins_)
        spin->setToolTip(
            tr("Phonon momenta the coupling is evaluated at.\n\n"
               "Bounded above by the supercell: a q that is not a reciprocal "
               "vector of the supercell has no dynamical matrix in the "
               "finite-difference cache, so it cannot be evaluated at any "
               "price. Raising it therefore means enlarging the supercell, "
               "not just asking for more points."));
    layout->addWidget(meshGroup);

    // -- Derived properties -------------------------------------------------
    auto* derivedGroup =
        new QGroupBox(tr("Spectral Function and Relaxation Time"), page);
    auto* derivedForm = new QFormLayout(derivedGroup);

    // No Fermi-surface smearing control: the two δ(ε − E_F) factors are
    // integrated with the linear tetrahedron method, which has no width.
    //
    // There used to be one, and it was not a cosmetic setting — λ on fcc Al
    // ran 0.009 to 31 across a 16× change in it, with no plateau, so whatever
    // the user typed here decided the answer. Removing the parameter was the
    // fix; leaving a disabled control would only invite it back.
    auto* integrationNote = new QLabel(
        tr("The Fermi-surface integration uses the linear tetrahedron "
           "method, so it has no smearing parameter to choose. Its accuracy "
           "is set by the k-mesh above."),
        derivedGroup);
    integrationNote->setWordWrap(true);
    derivedForm->addRow(integrationNote);

    phononSmearingSpin_ = new QDoubleSpinBox(derivedGroup);
    phononSmearingSpin_->setDecimals(4);
    phononSmearingSpin_->setRange(0.0005, 0.1);
    phononSmearingSpin_->setSingleStep(0.002);
    phononSmearingSpin_->setValue(0.005);
    phononSmearingSpin_->setSuffix(tr(" eV"));
    phononSmearingSpin_->setToolTip(
        tr("Gaussian width of δ(ω − ω_qν), which bins the modes into α²F(ω).\n\n"
           "Affects only how the spectral function is DRAWN. λ is summed over "
           "the modes exactly and does not change with it at all — the "
           "integral form 2∫α²F/ω dω would, because the Gaussian's tail "
           "reaches below the lowest mode and 1/ω diverges against it."));
    derivedForm->addRow(tr("Phonon smearing:"), phononSmearingSpin_);

    muStarSpin_ = new QDoubleSpinBox(derivedGroup);
    muStarSpin_->setDecimals(3);
    muStarSpin_->setRange(0.0, 0.5);
    muStarSpin_->setSingleStep(0.01);
    muStarSpin_->setValue(0.10);
    muStarSpin_->setToolTip(
        tr("Morel–Anderson Coulomb pseudopotential μ*, for the "
           "superconducting T_c estimate.\n\n"
           "EMPIRICAL — it is not computed by any part of this program and "
           "cannot be. Conventionally 0.10–0.15 for sp metals, higher for "
           "transition metals.\n\n"
           "T_c depends on it EXPONENTIALLY: for aluminium, 0.10 gives 1.8 K "
           "and 0.14 gives 0.7 K against a measured 1.18 K. Quote a range "
           "over 0.10–0.15 rather than a single value."));
    derivedForm->addRow(tr("Coulomb μ* (for T_c):"), muStarSpin_);

    plasmaSpin_ = new QDoubleSpinBox(derivedGroup);
    plasmaSpin_->setDecimals(3);
    plasmaSpin_->setRange(0.0, 100.0);
    plasmaSpin_->setSingleStep(0.5);
    plasmaSpin_->setValue(0.0);
    plasmaSpin_->setSuffix(tr(" eV"));
    plasmaSpin_->setSpecialValueText(tr("not known — skip resistivity"));
    plasmaSpin_->setToolTip(
        tr("Drude plasma frequency ħω_p, for the resistivity ρ(T).\n\n"
           "Take it from the Optics module or from a K-point Convergence run "
           "with the plasma-frequency target enabled — it is not computed "
           "here.\n\n"
           "Left at zero, ρ is skipped rather than estimated: ρ goes as "
           "1/ω_p², so a guessed ω_p would produce a number that looks like "
           "a measurement."));
    derivedForm->addRow(tr("Plasma frequency ħω_p:"), plasmaSpin_);

    temperatureSpin_ = new QDoubleSpinBox(derivedGroup);
    temperatureSpin_->setDecimals(0);
    temperatureSpin_->setRange(1.0, 5000.0);
    temperatureSpin_->setSingleStep(50.0);
    temperatureSpin_->setValue(300.0);
    temperatureSpin_->setSuffix(tr(" K"));
    temperatureSpin_->setToolTip(
        tr("Temperature the relaxation time is reported at.\n\n"
           "τ is temperature-dependent — that is physics, not a preference — "
           "so it has to match the measurement the Drude model is being "
           "compared against. The run uses ħ/τ = 2πλk_BT, which is the "
           "high-temperature limit; below about Θ_D/3 it overestimates the "
           "scattering rate and the run says so."));
    derivedForm->addRow(tr("Temperature:"), temperatureSpin_);
    layout->addWidget(derivedGroup);

    summaryLabel_ = new QLabel(page);
    summaryLabel_->setWordWrap(true);
    summaryLabel_->setTextFormat(Qt::RichText);
    layout->addWidget(summaryLabel_);
    layout->addStretch(1);

    const auto refresh = [this] {
        updateSummary();
        refreshPreview();
    };
    for (int axis = 0; axis < 3; ++axis) {
        connect(supercellSpins_[axis], &QSpinBox::valueChanged, this, refresh);
        connect(qGridSpins_[axis], &QSpinBox::valueChanged, this, refresh);
        connect(kGridSpins_[axis], &QSpinBox::valueChanged, this, refresh);
    }
    connect(basisCombo_, &QComboBox::currentIndexChanged, this, refresh);
    for (QDoubleSpinBox* spin : {deltaSpin_, phononSmearingSpin_,
                                 muStarSpin_, plasmaSpin_,
                                 temperatureSpin_})
        connect(spin, &QDoubleSpinBox::valueChanged, this, refresh);

    updateSummary();
    return page;
}

void ElectronPhononWizard::updateSummary()
{
    if (!summaryLabel_)
        return;
    QStringList problems;
    for (int axis = 0; axis < 3; ++axis) {
        const int supercell = supercellSpins_[axis]->value();
        const int q = qGridSpins_[axis]->value();
        const int k = kGridSpins_[axis]->value();
        if (q > supercell)
            problems << tr("q<sub>%1</sub> = %2 exceeds the supercell "
                           "repetition %3 — that phonon is not in the "
                           "displacement cache at any cost.")
                            .arg(axis + 1).arg(q).arg(supercell);
        else if (supercell % q != 0)
            problems << tr("q<sub>%1</sub> = %2 does not divide the supercell "
                           "repetition %3.")
                            .arg(axis + 1).arg(q).arg(supercell);
        if (k % q != 0)
            problems << tr("k<sub>%1</sub> = %2 is not a multiple of "
                           "q<sub>%3</sub> = %4, so k+q would leave the mesh.")
                            .arg(axis + 1).arg(k).arg(axis + 1).arg(q);
    }

    const int cells = supercellSpins_[0]->value() * supercellSpins_[1]->value()
        * supercellSpins_[2]->value();
    QString text =
        tr("<b>Cost:</b> 6N+1 self-consistent runs on a supercell %1× the "
           "cell — N is the atom count, so a 2-atom cell here means 13 runs "
           "of a %1×-sized system. Everything after that stage is minutes.")
            .arg(cells);
    if (!problems.isEmpty())
        text += tr("<br><br><b style='color:#c0392b'>Will not run:</b> ")
            + problems.join(QStringLiteral("<br>"));
    else
        text += tr("<br><br>Meshes are consistent: every q is commensurate "
                   "with the supercell and every k+q lands on the k-mesh.");
    summaryLabel_->setText(text);
}

core::ElectronPhononConfig ElectronPhononWizard::runConfig() const
{
    core::ElectronPhononConfig config;
    config.calculator = baseCalculatorConfig();
    config.calculator.calculator = core::CalculatorKind::Gpaw;
    for (int axis = 0; axis < 3; ++axis) {
        config.supercell[axis] = supercellSpins_[axis]->value();
        config.qGrid[axis] = qGridSpins_[axis]->value();
        config.kGrid[axis] = kGridSpins_[axis]->value();
    }
    config.basis = basisCombo_->currentData().toString().toStdString();
    config.deltaAngstrom = deltaSpin_->value();
    config.phononSmearingEv = phononSmearingSpin_->value();
    config.muStar = muStarSpin_->value();
    config.plasmaFrequencyEv = plasmaSpin_->value();
    config.temperatureK = temperatureSpin_->value();
    return config;
}

QString ElectronPhononWizard::generateScript() const
{
    return QString::fromStdString(
        core::generateElectronPhononScript(runConfig()));
}

} // namespace calango::gui
