#include "gui/OpticsWizard.hpp"

#include "core/OpticsScriptGenerator.hpp"
#include "core/Structure.hpp"

#include <QCheckBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QSpinBox>
#include <QVBoxLayout>
#include <QWidget>

namespace calango::gui {

OpticsWizard::OpticsWizard(std::shared_ptr<core::Structure> structure,
                           QWidget* parent)
    : SimulationWizardBase(parent)
    , structure_(std::move(structure))
{
    buildUi();
}

QString OpticsWizard::wizardTitle() const
{
    return tr("Optical Properties Setup");
}

QString OpticsWizard::settingsHeader() const
{
    return tr("Optical Response Settings");
}

QWidget* OpticsWizard::buildSettingsPage()
{
    auto* page = new QWidget(this);
    auto* layout = new QVBoxLayout(page);

    auto* intro = new QLabel(
        tr("Compute the frequency-dependent dielectric function ε(ω) and the "
           "derived optical spectra (absorption, reflectivity, refractive "
           "index and energy loss) from GPAW's linear-response module. The "
           "ground-state cutoff and k-grid are set in the next stage; a denser "
           "k-grid and more empty bands sharpen the spectrum."),
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

QString OpticsWizard::generateScript() const
{
    core::OpticsConfig cfg;
    cfg.calculator = baseCalculatorConfig();
    cfg.broadeningEv = broadeningSpin_->value();
    cfg.omegaMinEv = omegaMinSpin_->value();
    cfg.omegaMaxEv = omegaMaxSpin_->value();
    cfg.npoints = npointsSpin_->value();
    cfg.dirX = dirXxCheck_->isChecked();
    cfg.dirY = dirYyCheck_->isChecked();
    cfg.dirZ = dirZzCheck_->isChecked();
    return QString::fromStdString(core::generateOpticsScript(cfg));
}

} // namespace calango::gui
