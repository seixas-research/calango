#include "gui/FermiSurfaceDialog.hpp"

#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QLabel>
#include <QSpinBox>
#include <QVBoxLayout>

namespace calango::gui {

FermiSurfaceDialog::FermiSurfaceDialog(QWidget* parent) : QDialog(parent)
{
    setWindowTitle(tr("Fermi Surface"));

    auto* layout = new QVBoxLayout(this);
    auto* intro = new QLabel(
        tr("Interpolate E<sub>n</sub>(k) from the localized Wannier "
           "Hamiltonian onto a dense 3D grid and extract the sheets "
           "E<sub>n</sub>(k) = E<sub>F</sub>."),
        this);
    intro->setWordWrap(true);
    intro->setTextFormat(Qt::RichText);
    layout->addWidget(intro);

    auto* form = new QFormLayout;

    samplesSpin_ = new QSpinBox(this);
    samplesSpin_->setRange(4, 128);
    samplesSpin_->setValue(32);
    samplesSpin_->setToolTip(
        tr("Samples along each reciprocal direction; the grid is N³ and the "
           "cost is cubic.\n\n"
           "This is what decides whether a small pocket is resolved or missed "
           "entirely. 24–32 gives a recognizable surface; a nesting study or a "
           "narrow neck wants 48 or more."));
    form->addRow(tr("Grid samples (N × N × N):"), samplesSpin_);
    connect(samplesSpin_, &QSpinBox::valueChanged, this,
            [this] { refreshCostNote(); });

    costNote_ = new QLabel(this);
    costNote_->setWordWrap(true);
    form->addRow(QString(), costNote_);

    offsetSpin_ = new QDoubleSpinBox(this);
    offsetSpin_->setRange(-20.0, 20.0);
    offsetSpin_->setDecimals(4);
    offsetSpin_->setSingleStep(0.05);
    offsetSpin_->setValue(0.0);
    offsetSpin_->setSuffix(tr(" eV"));
    offsetSpin_->setToolTip(
        tr("Energy the sheets are taken at, relative to the calculation's own "
           "Fermi level. Non-zero is a rigid-band doping study — and the "
           "viewer can scan it afterwards without re-running anything, since "
           "the whole grid is stored."));
    form->addRow(tr("Energy offset:"), offsetSpin_);

    iterationsSpin_ = new QSpinBox(this);
    iterationsSpin_->setRange(1, 1000);
    iterationsSpin_->setValue(50);
    iterationsSpin_->setToolTip(
        tr("Localization iterations before interpolating.\n\n"
           "The interpolation is only as good as the localization: a poorly "
           "localized H(R) is long-ranged, and its interpolated bands ring "
           "between the computed k-points — which shows up as a Fermi surface "
           "with spurious ripples."));
    form->addRow(tr("Localization iterations:"), iterationsSpin_);
    layout->addLayout(form);

    auto* buttons = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);

    refreshCostNote();
}

void FermiSurfaceDialog::refreshCostNote()
{
    const int n = samplesSpin_->value();
    // Stated as a diagonalization count rather than a time: how long one takes
    // depends on the Wannier count, but "this is 32768 of them" makes the
    // cubic growth concrete before the job is queued.
    costNote_->setText(tr("%1³ = %2 interpolated k-points.")
                           .arg(n)
                           .arg(static_cast<qlonglong>(n) * n * n));
}

core::FermiSurfaceConfig FermiSurfaceDialog::config() const
{
    core::FermiSurfaceConfig cfg;
    cfg.gridSamples = samplesSpin_->value();
    cfg.energyOffsetEv = offsetSpin_->value();
    cfg.maxIterations = iterationsSpin_->value();
    return cfg;
}

} // namespace calango::gui
