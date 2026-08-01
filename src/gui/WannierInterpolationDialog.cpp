#include "gui/WannierInterpolationDialog.hpp"

#include "gui/EmbeddedKPathEditor.hpp"
#include "gui/MlwfSourceSelector.hpp"

#include <QCheckBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QSpinBox>
#include <QVBoxLayout>

namespace calango::gui {

WannierInterpolationDialog::WannierInterpolationDialog(
    const QList<QPair<QString, QString>>& mlwfRuns,
    std::shared_ptr<const core::Structure> structure, QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle(tr("Wannier Interpolation"));
    resize(560, 700);

    auto* layout = new QVBoxLayout(this);

    auto* intro = new QLabel(
        tr("Fast real-space → reciprocal-space interpolation from the localized "
           "Wannier Hamiltonian H(R) → H(k). Configure the band path and PDOS "
           "mesh, then plot the interpolated band structure and PDOS."),
        this);
    intro->setWordWrap(true);
    layout->addWidget(intro);

    // Step 1: the localization being interpolated. The Wannier count and the
    // trial projections are read back from this run's wannier.json — the
    // interpolation rebuilds the SAME localization, so it is not a setting
    // that could be overridden below.
    auto* sourceGroup = new QGroupBox(tr("Source Wannier Functions process"), this);
    auto* sourceLayout = new QVBoxLayout(sourceGroup);
    source_ = new MlwfSourceSelector(mlwfRuns, sourceGroup);
    sourceLayout->addWidget(source_);
    layout->addWidget(sourceGroup);

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

    // --- Disentanglement windows ------------------------------------------
    //
    // Two windows, two ASE parameters, one control each. There used to be
    // three controls for two quantities: "frozen window" and "inner window"
    // were both fixedenergy, and only the first reached ASE — the inner/outer
    // pair was written into the generated script as a comment and changed
    // nothing about the calculation.
    auto* winGroup = new QGroupBox(tr("Disentanglement windows"), this);
    auto* winForm = new QFormLayout(winGroup);

    auto* winNote = new QLabel(
        tr("The <b>inner</b> window is the frozen manifold — states below it "
           "are reproduced exactly. The <b>outer</b> window is the pool those "
           "states may be drawn from. Both are measured from the conduction "
           "band minimum in a gapped system and from the Fermi level in a "
           "metal, which is ASE's convention, not a choice made here."),
        winGroup);
    winNote->setWordWrap(true);
    winNote->setTextFormat(Qt::RichText);
    winForm->addRow(winNote);

    innerCheck_ =
        new QCheckBox(tr("Freeze states below an inner window"), winGroup);
    innerCheck_->setToolTip(
        tr("ASE's Wannier(fixedenergy=…). States below the threshold are "
           "reproduced exactly by the Wannier manifold; the rest are free to "
           "mix.\n\n"
           "Fixing more states than there are Wannier functions is an error — "
           "ASE needs at least as many Wannier functions as fixed states, and "
           "the run reports which two numbers disagree."));
    winForm->addRow(innerCheck_);
    innerSpin_ = new QDoubleSpinBox(winGroup);
    innerSpin_->setRange(-100.0, 100.0);
    innerSpin_->setDecimals(2);
    innerSpin_->setValue(0.0);
    innerSpin_->setSuffix(tr(" eV"));
    winForm->addRow(tr("Inner window:"), innerSpin_);

    outerCheck_ =
        new QCheckBox(tr("Restrict the pool to an outer window"), winGroup);
    outerCheck_->setToolTip(
        tr("Excludes Bloch states above the threshold from the localization "
           "entirely, by converting the cutoff into ASE's Wannier(nbands=…) — "
           "\"bands to include in localization\".\n\n"
           "Off, every band the calculator holds is available. Narrowing it is "
           "what keeps high-lying free-electron-like states out of a manifold "
           "meant to describe a few valence bands."));
    winForm->addRow(outerCheck_);
    outerSpin_ = new QDoubleSpinBox(winGroup);
    outerSpin_->setRange(-100.0, 100.0);
    outerSpin_->setDecimals(2);
    outerSpin_->setValue(5.0);
    outerSpin_->setSuffix(tr(" eV"));
    winForm->addRow(tr("Outer window:"), outerSpin_);
    layout->addWidget(winGroup);

    layout->addStretch(1);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Cancel, this);
    auto* runButton = buttons->addButton(tr("Plot Bands && PDOS"),
                                         QDialogButtonBox::AcceptRole);
    runButton->setDefault(true);
    connect(buttons, &QDialogButtonBox::accepted, this, [this] {
        if (!source_->isValid()) {
            QMessageBox::warning(this, tr("Wannier Interpolation"),
                                 source_->invalidReason());
            return;
        }
        accept();
    });
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);

    const auto syncAccept = [this, runButton] {
        runButton->setEnabled(source_->isValid());
    };
    syncAccept();
    connect(source_, &MlwfSourceSelector::changed, this, syncAccept);

    connect(innerCheck_, &QCheckBox::toggled, this,
            &WannierInterpolationDialog::updateEnabled);
    connect(outerCheck_, &QCheckBox::toggled, this,
            &WannierInterpolationDialog::updateEnabled);
    updateEnabled();
}

QString WannierInterpolationDialog::mlwfDirectory() const
{
    return source_->directory();
}

void WannierInterpolationDialog::updateEnabled()
{
    innerSpin_->setEnabled(innerCheck_->isChecked());
    outerSpin_->setEnabled(outerCheck_->isChecked());
}

core::WannierInterpolationConfig WannierInterpolationDialog::config() const
{
    core::WannierInterpolationConfig c;
    c.kpath = kpath_->path().toStdString();
    c.bandPoints = bandPointsSpin_->value();
    for (int i = 0; i < 3; ++i)
        c.kmesh[i] = kmeshSpins_[i]->value();
    c.pdosWidth = pdosWidthSpin_->value();
    c.useInnerWindow = innerCheck_->isChecked();
    c.innerWindowEv = innerSpin_->value();
    c.useOuterWindow = outerCheck_->isChecked();
    c.outerWindowEv = outerSpin_->value();
    return c;
}

} // namespace calango::gui
