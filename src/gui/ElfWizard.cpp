#include "gui/ElfWizard.hpp"

#include "core/ElfScriptGenerator.hpp"
#include "core/Structure.hpp"

#include <QComboBox>
#include <QFormLayout>
#include <QLabel>
#include <QVBoxLayout>
#include <QWidget>

namespace calango::gui {

ElfWizard::ElfWizard(std::shared_ptr<core::Structure> structure,
                     QWidget* parent)
    : SimulationWizardBase(parent), structure_(std::move(structure))
{
    buildUi();
    // GPAW is the fully supported backend for the ELF; open on it by default.
    selectCalculator(core::CalculatorKind::Gpaw);
}

void ElfWizard::setDensityBaselines(
    const QList<QPair<QString, QString>>& baselines)
{
    if (!baselineCombo_)
        return;
    for (const auto& [label, dir] : baselines)
        baselineCombo_->addItem(label, dir);
}

QString ElfWizard::wizardTitle() const
{
    return tr("Electron Localization Function (ELF) Setup");
}

QString ElfWizard::settingsHeader() const
{
    return tr("ELF Source");
}

QWidget* ElfWizard::buildSettingsPage()
{
    auto* page = new QWidget(this);
    auto* layout = new QVBoxLayout(page);

    auto* intro = new QLabel(
        tr("Compute the Electron Localization Function η(r) ∈ [0, 1] and write "
           "elf.cube into the task directory. When the job finishes the "
           "isosurface / slice viewer opens automatically.\n\n"
           "Pick a completed GPAW calculation whose wavefunctions (.gpw) are "
           "reused, or run a fresh ground state — its engine and convergence "
           "are set in the next stage."),
        page);
    intro->setWordWrap(true);
    layout->addWidget(intro);

    auto* form = new QFormLayout;
    layout->addLayout(form);

    baselineCombo_ = new QComboBox(page);
    baselineCombo_->addItem(tr("(none — run a fresh ground state)"), QString());
    baselineCombo_->setToolTip(
        tr("Pick the process / workspace tab that holds a calculated "
           "wavefunction (GPAW .gpw). The ELF is built by restarting GPAW from "
           "that directory; otherwise a fresh ground state is run first."));
    form->addRow(tr("Wavefunction source:"), baselineCombo_);
    connect(baselineCombo_, &QComboBox::currentIndexChanged, this,
            [this] { refreshPreview(); });

    layout->addStretch(1);
    return page;
}

bool ElfWizard::calculatorAllowed(core::CalculatorKind kind) const
{
    // DFT engines only: the ELF needs a self-consistent electronic density.
    // GPAW drives the localization-function evaluation directly; Quantum
    // ESPRESSO / SIESTA select their own env + SCF.
    return kind == core::CalculatorKind::Gpaw
        || kind == core::CalculatorKind::QuantumEspresso
        || kind == core::CalculatorKind::Siesta;
}

QString ElfWizard::generateScript() const
{
    core::ElfConfig cfg;
    cfg.calculator = baseCalculatorConfig();
    cfg.baselineDir =
        baselineCombo_ ? baselineCombo_->currentData().toString().toStdString()
                       : std::string();
    return QString::fromStdString(core::generateElfScript(cfg));
}

} // namespace calango::gui
