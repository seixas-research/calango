#include "gui/WannierInterpolationDialog.hpp"

#include "gui/EmbeddedKPathEditor.hpp"

#include <QCheckBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QSpinBox>
#include <QVBoxLayout>

namespace calango::gui {

WannierInterpolationDialog::WannierInterpolationDialog(
    std::shared_ptr<const core::Structure> structure, QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle(tr("Wannier Interpolation"));
    resize(560, 640);

    auto* layout = new QVBoxLayout(this);

    auto* intro = new QLabel(
        tr("Fast real-space → reciprocal-space interpolation from the localized "
           "Wannier Hamiltonian H(R) → H(k). Configure the band path and PDOS "
           "mesh, then plot the interpolated band structure and PDOS."),
        this);
    intro->setWordWrap(true);
    layout->addWidget(intro);

    // --- Band-structure k-path --------------------------------------------
    auto* bandGroup = new QGroupBox(tr("Band structure — k-path E_n(k)"), this);
    auto* bandLayout = new QVBoxLayout(bandGroup);
    kpath_ = new EmbeddedKPathEditor(std::move(structure), bandGroup);
    bandLayout->addWidget(kpath_);
    auto* bandForm = new QFormLayout;
    bandPointsSpin_ = new QSpinBox(bandGroup);
    bandPointsSpin_->setRange(2, 100000);
    bandPointsSpin_->setValue(200);
    bandPointsSpin_->setToolTip(
        tr("Total number of interpolated k-points along the path."));
    bandForm->addRow(tr("Path samples:"), bandPointsSpin_);
    bandLayout->addLayout(bandForm);
    layout->addWidget(bandGroup);

    // --- PDOS k-mesh -------------------------------------------------------
    auto* pdosGroup = new QGroupBox(tr("PDOS — k-mesh"), this);
    auto* pdosForm = new QFormLayout(pdosGroup);
    auto* meshRow = new QHBoxLayout;
    for (int i = 0; i < 3; ++i) {
        kmeshSpins_[i] = new QSpinBox(pdosGroup);
        kmeshSpins_[i]->setRange(1, 128);
        kmeshSpins_[i]->setValue(8);
        meshRow->addWidget(kmeshSpins_[i]);
        if (i < 2)
            meshRow->addWidget(new QLabel(QStringLiteral("×"), pdosGroup));
    }
    meshRow->addStretch(1);
    pdosForm->addRow(tr("Monkhorst-Pack grid:"), meshRow);
    pdosWidthSpin_ = new QDoubleSpinBox(pdosGroup);
    pdosWidthSpin_->setRange(0.001, 5.0);
    pdosWidthSpin_->setDecimals(3);
    pdosWidthSpin_->setSingleStep(0.01);
    pdosWidthSpin_->setValue(0.1);
    pdosWidthSpin_->setSuffix(tr(" eV"));
    pdosWidthSpin_->setToolTip(tr("Gaussian broadening of the projected DOS."));
    pdosForm->addRow(tr("Broadening:"), pdosWidthSpin_);
    layout->addWidget(pdosGroup);

    // --- Disentanglement / energy windows ---------------------------------
    auto* winGroup =
        new QGroupBox(tr("Disentanglement && energy windows"), this);
    auto* winForm = new QFormLayout(winGroup);

    frozenCheck_ = new QCheckBox(tr("Freeze states within a window"), winGroup);
    frozenCheck_->setToolTip(
        tr("ASE's Wannier(fixedenergy=…): states up to the threshold stay "
           "frozen (fully preserved) during the localization."));
    winForm->addRow(tr("Frozen window:"), frozenCheck_);
    frozenSpin_ = new QDoubleSpinBox(winGroup);
    frozenSpin_->setRange(-100.0, 100.0);
    frozenSpin_->setDecimals(2);
    frozenSpin_->setValue(0.0);
    frozenSpin_->setSuffix(tr(" eV"));
    frozenSpin_->setToolTip(tr("Frozen-window threshold E_f (relative to E_F)."));
    winForm->addRow(tr("E_f (above E_F):"), frozenSpin_);

    disentangleCheck_ =
        new QCheckBox(tr("Set inner / outer windows"), winGroup);
    disentangleCheck_->setToolTip(
        tr("Inner (frozen) and outer (disentanglement) energy windows. ASE's "
           "Wannier disentanglement is limited, so these bound the interpolated "
           "energy range rather than performing a full Wannier90 "
           "disentanglement."));
    winForm->addRow(tr("Disentanglement:"), disentangleCheck_);
    innerSpin_ = new QDoubleSpinBox(winGroup);
    innerSpin_->setRange(-100.0, 100.0);
    innerSpin_->setDecimals(2);
    innerSpin_->setValue(0.0);
    innerSpin_->setSuffix(tr(" eV"));
    winForm->addRow(tr("Inner window (above E_F):"), innerSpin_);
    outerSpin_ = new QDoubleSpinBox(winGroup);
    outerSpin_->setRange(-100.0, 100.0);
    outerSpin_->setDecimals(2);
    outerSpin_->setValue(5.0);
    outerSpin_->setSuffix(tr(" eV"));
    winForm->addRow(tr("Outer window (above E_F):"), outerSpin_);
    layout->addWidget(winGroup);

    layout->addStretch(1);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Cancel, this);
    auto* runButton = buttons->addButton(tr("Plot Bands && PDOS"),
                                         QDialogButtonBox::AcceptRole);
    runButton->setDefault(true);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);

    connect(frozenCheck_, &QCheckBox::toggled, this,
            &WannierInterpolationDialog::updateEnabled);
    connect(disentangleCheck_, &QCheckBox::toggled, this,
            &WannierInterpolationDialog::updateEnabled);
    updateEnabled();
}

void WannierInterpolationDialog::updateEnabled()
{
    frozenSpin_->setEnabled(frozenCheck_->isChecked());
    const bool dis = disentangleCheck_->isChecked();
    innerSpin_->setEnabled(dis);
    outerSpin_->setEnabled(dis);
}

core::WannierInterpolationConfig WannierInterpolationDialog::config() const
{
    core::WannierInterpolationConfig c;
    c.kpath = kpath_->path().toStdString();
    c.bandPoints = bandPointsSpin_->value();
    for (int i = 0; i < 3; ++i)
        c.kmesh[i] = kmeshSpins_[i]->value();
    c.pdosWidth = pdosWidthSpin_->value();
    c.useFrozenWindow = frozenCheck_->isChecked();
    c.frozenEnergyEv = frozenSpin_->value();
    c.useDisentangle = disentangleCheck_->isChecked();
    c.innerWindowEv = innerSpin_->value();
    c.outerWindowEv = outerSpin_->value();
    return c;
}

} // namespace calango::gui
