#include "gui/FermiSurfaceDialog.hpp"

#include "gui/MlwfSourceSelector.hpp"

#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QSpinBox>
#include <QVBoxLayout>

namespace calango::gui {

FermiSurfaceDialog::FermiSurfaceDialog(
    const QList<QPair<QString, QString>>& mlwfRuns, QWidget* parent)
    : QDialog(parent)
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

    // Step 1, before anything else: WHICH localization is being interpolated.
    // Every setting below is a detail of how to sample the Hamiltonian this
    // choice supplies, so it comes first and the OK button waits on it.
    auto* sourceGroup = new QGroupBox(tr("Source Wannier Functions process"), this);
    auto* sourceLayout = new QVBoxLayout(sourceGroup);
    source_ = new MlwfSourceSelector(mlwfRuns, sourceGroup);
    sourceLayout->addWidget(source_);
    layout->addWidget(sourceGroup);

    auto* form = new QFormLayout;

    // One count per reciprocal direction, not one for all three. Reciprocal
    // cells are rarely cubic, and on a layered material the out-of-plane
    // direction carries almost no dispersion — sampling it as finely as the
    // other two multiplies the cost to resolve nothing.
    auto* meshRow = new QHBoxLayout;
    for (int axis = 0; axis < 3; ++axis) {
        samplesSpins_[axis] = new QSpinBox(this);
        samplesSpins_[axis]->setRange(4, 128);
        samplesSpins_[axis]->setValue(32);
        samplesSpins_[axis]->setToolTip(
            tr("Samples along b%1.\n\n"
               "The grid is the product of the three, so the cost is too. This "
               "is what decides whether a small pocket is resolved or missed "
               "entirely: 24–32 per direction gives a recognizable surface, "
               "and a nesting study or a narrow neck wants 48 or more along "
               "the directions that carry the structure.")
                .arg(axis + 1));
        meshRow->addWidget(samplesSpins_[axis]);
        if (axis < 2)
            meshRow->addWidget(new QLabel(QStringLiteral("×"), this));
        connect(samplesSpins_[axis], &QSpinBox::valueChanged, this,
                [this] { refreshCostNote(); });
    }
    meshRow->addStretch(1);
    form->addRow(tr("Grid samples (k₁ × k₂ × k₃):"), meshRow);

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
    connect(buttons, &QDialogButtonBox::accepted, this, [this] {
        // The selector already says what is wrong in its status line; repeat it
        // here because a disabled-looking OK with no explanation is worse than
        // a box that names the missing file.
        if (!source_->isValid()) {
            QMessageBox::warning(this, tr("Fermi Surface"),
                                 source_->invalidReason());
            return;
        }
        accept();
    });
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);

    // Gate OK on the source being usable, live: browsing to a directory that
    // turns out not to be an MLWF run greys the button out again.
    const auto syncAccept = [this, buttons] {
        buttons->button(QDialogButtonBox::Ok)->setEnabled(source_->isValid());
    };
    syncAccept();
    connect(source_, &MlwfSourceSelector::changed, this, syncAccept);

    refreshCostNote();
}

QString FermiSurfaceDialog::mlwfDirectory() const
{
    return source_->directory();
}

void FermiSurfaceDialog::refreshCostNote()
{
    const int nx = samplesSpins_[0]->value();
    const int ny = samplesSpins_[1]->value();
    const int nz = samplesSpins_[2]->value();
    // Stated as a diagonalization count rather than a time: how long one takes
    // depends on the Wannier count, but "this is 32768 of them" makes the
    // multiplicative growth concrete before the job is queued.
    costNote_->setText(tr("%1 × %2 × %3 = %4 interpolated k-points.")
                           .arg(nx)
                           .arg(ny)
                           .arg(nz)
                           .arg(static_cast<qlonglong>(nx) * ny * nz));
}

core::FermiSurfaceConfig FermiSurfaceDialog::config() const
{
    core::FermiSurfaceConfig cfg;
    cfg.mlwfDir = source_->directory().toStdString();
    for (int axis = 0; axis < 3; ++axis)
        cfg.gridSamples[axis] = samplesSpins_[axis]->value();
    cfg.energyOffsetEv = offsetSpin_->value();
    cfg.maxIterations = iterationsSpin_->value();
    return cfg;
}

} // namespace calango::gui
