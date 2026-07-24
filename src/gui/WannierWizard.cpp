#include "gui/WannierWizard.hpp"

#include "core/Structure.hpp"
#include "core/WannierScriptGenerator.hpp"

#include <QComboBox>
#include <QFormLayout>
#include <QLabel>
#include <QSpinBox>
#include <QVBoxLayout>
#include <QWidget>

namespace calango::gui {

WannierWizard::WannierWizard(std::shared_ptr<core::Structure> structure,
                             QWidget* parent)
    : SimulationWizardBase(parent), structure_(std::move(structure))
{
    buildUi();
    // GPAW is the fully supported backend for the localization; default to it.
    selectCalculator(core::CalculatorKind::Gpaw);
}

void WannierWizard::setDensityBaselines(
    const QList<QPair<QString, QString>>& baselines)
{
    if (!baselineCombo_)
        return;
    for (const auto& [label, dir] : baselines)
        baselineCombo_->addItem(label, dir);
}

QString WannierWizard::wizardTitle() const
{
    return tr("Maximally Localized Wannier Functions (MLWF) Setup");
}

QString WannierWizard::settingsHeader() const
{
    return tr("Wannier Settings");
}

QWidget* WannierWizard::buildSettingsPage()
{
    auto* page = new QWidget(this);
    auto* layout = new QVBoxLayout(page);

    auto* intro = new QLabel(
        tr("Run the Marzari-Vanderbilt localization on top of a converged "
           "ground state and write the Wannier centres/spreads (wannier.json) "
           "plus each real-space orbital (wannier_<n>.cube). When the job "
           "finishes the centres table + orbital viewer opens automatically.\n\n"
           "Pick a completed GPAW single-point whose Bloch wavefunctions "
           "(.gpw) are reused, or run a fresh SCF — its engine and convergence "
           "are set in the next stage."),
        page);
    intro->setWordWrap(true);
    layout->addWidget(intro);

    auto* form = new QFormLayout;
    layout->addLayout(form);

    baselineCombo_ = new QComboBox(page);
    baselineCombo_->addItem(tr("(none — run a fresh SCF)"), QString());
    baselineCombo_->setToolTip(
        tr("Pick the single-point calculation that holds the Bloch "
           "wavefunctions (GPAW .gpw). The localization restarts GPAW from "
           "that directory; otherwise a fresh SCF is run first."));
    form->addRow(tr("Wavefunction source:"), baselineCombo_);

    nWannier_ = new QSpinBox(page);
    nWannier_->setRange(1, 512);
    nWannier_->setValue(4);
    nWannier_->setToolTip(
        tr("Number of Wannier functions to localize (typically the number of "
           "occupied bands / valence orbitals)."));
    form->addRow(tr("Wannier functions:"), nWannier_);

    // Trial-orbital initialization. The untranslated key stored as itemData is
    // what generateScript() maps onto ASE's `initialwannier` argument.
    projectionCombo_ = new QComboBox(page);
    projectionCombo_->addItem(tr("Automatic (orbitals)"),
                              QStringLiteral("orbitals"));
    projectionCombo_->addItem(tr("Bloch"), QStringLiteral("bloch"));
    projectionCombo_->addItem(tr("Random"), QStringLiteral("random"));
    projectionCombo_->addItem(QStringLiteral("s"), QStringLiteral("s"));
    projectionCombo_->addItem(QStringLiteral("p"), QStringLiteral("p"));
    projectionCombo_->addItem(QStringLiteral("d"), QStringLiteral("d"));
    projectionCombo_->addItem(QStringLiteral("sp3"), QStringLiteral("sp3"));
    projectionCombo_->addItem(QStringLiteral("dxy"), QStringLiteral("dxy"));
    projectionCombo_->setToolTip(
        tr("Trial-orbital guess used to seed the localization. The atomic sets "
           "(s, p, d, sp3, dxy) fall back to ASE's 'orbitals' initializer, "
           "which derives the projections from the atomic orbitals."));
    form->addRow(tr("Initial projection:"), projectionCombo_);

    connect(baselineCombo_, &QComboBox::currentIndexChanged, this,
            [this] { refreshPreview(); });
    connect(nWannier_, &QSpinBox::valueChanged, this,
            [this] { refreshPreview(); });
    connect(projectionCombo_, &QComboBox::currentIndexChanged, this,
            [this] { refreshPreview(); });

    layout->addStretch(1);
    return page;
}

bool WannierWizard::calculatorAllowed(core::CalculatorKind kind) const
{
    // DFT engines only: the localization needs the Bloch wavefunctions from a
    // self-consistent ground state. GPAW drives ase.dft.wannier directly;
    // Quantum ESPRESSO / SIESTA select their own env + SCF.
    return kind == core::CalculatorKind::Gpaw
        || kind == core::CalculatorKind::QuantumEspresso
        || kind == core::CalculatorKind::Siesta;
}

QString WannierWizard::generateScript() const
{
    core::WannierConfig cfg;
    cfg.calculator = baseCalculatorConfig();
    cfg.baselineDir =
        baselineCombo_ ? baselineCombo_->currentData().toString().toStdString()
                       : std::string();
    cfg.nWannier = nWannier_ ? nWannier_->value() : 4;
    cfg.initialWannier =
        projectionCombo_
            ? projectionCombo_->currentData().toString().toStdString()
            : std::string("orbitals");
    return QString::fromStdString(core::generateWannierScript(cfg));
}

} // namespace calango::gui
