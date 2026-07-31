#include "gui/KpointsConvergenceWizard.hpp"

#include <QCheckBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QSpinBox>
#include <QVBoxLayout>

#include <QtGlobal>

#include <algorithm>

namespace calango::gui {

KpointsConvergenceWizard::KpointsConvergenceWizard(QWidget* parent)
    : SimulationWizardBase(parent)
{
    buildUi();
    electronic_.updateEnabled();
}

QString KpointsConvergenceWizard::wizardTitle() const
{
    return tr("K-points Convergence");
}

QWidget* KpointsConvergenceWizard::buildSettingsPage()
{
    auto* page = new QWidget(this);
    auto* layout = new QVBoxLayout(page);

    auto* note = new QLabel(
        tr("One single-point calculation per Monkhorst-Pack mesh, on the "
           "structure as it stands. The run at the <b>densest</b> mesh is "
           "the convergence reference; the result window plots ΔE per atom, "
           "the atom-wise force error and the band-energy drift against the "
           "mesh density, so \"converged\" becomes a number you read off a "
           "curve instead of a guess.<br><br>"
           "The Calculator page therefore offers no k-grid controls — this "
           "sweep is the grid. Every other setting there (mode, cutoff, XC, "
           "convergence targets, spin) is held fixed across the runs, as a "
           "convergence study requires."),
        page);
    note->setWordWrap(true);
    note->setTextFormat(Qt::RichText);
    layout->addWidget(note);

    auto* sweepGroup = new QGroupBox(tr("k-Point Mesh Sweep"), page);
    sweepForm_ = new QFormLayout(sweepGroup);

    const auto makeKSpin = [sweepGroup](int value, int minimum) {
        auto* spin = new QSpinBox(sweepGroup);
        spin->setRange(minimum, 64);
        spin->setValue(value);
        return spin;
    };
    minKSpin_ = makeKSpin(2, 1);
    minKSpin_->setToolTip(
        tr("Coarsest subdivision evaluated. Starting well below the "
           "production candidate is what makes the trend visible."));
    sweepForm_->addRow(tr("Minimum k-points per axis:"), minKSpin_);

    maxKSpin_ = makeKSpin(10, 1);
    maxKSpin_->setToolTip(
        tr("Densest subdivision evaluated — the convergence reference. It "
           "should sit comfortably above where you expect convergence, "
           "since every other run is judged against it."));
    sweepForm_->addRow(tr("Maximum k-points per axis:"), maxKSpin_);

    strideSpin_ = makeKSpin(2, 1);
    strideSpin_->setRange(1, 16);
    strideSpin_->setToolTip(
        tr("Step between consecutive subdivision counts. The maximum is "
           "always included even when the stride does not land on it "
           "exactly."));
    sweepForm_->addRow(tr("Stride:"), strideSpin_);

    // -- Anisotropic mode ----------------------------------------------------
    // A layered or chain-like material has a reciprocal cell far from cubic,
    // so one subdivision count per step misdescribes it: the long real-space
    // axis wants few k-points, the short ones many. Per-axis start + stride,
    // advanced together over a fixed number of steps, expresses that — and a
    // stride of 0 pins an axis outright (a slab's vacuum direction).
    anisotropicCheck_ = new QCheckBox(
        tr("Anisotropic grid (independent start and stride per axis)"),
        sweepGroup);
    anisotropicCheck_->setToolTip(
        tr("For anisotropic materials: each direction advances by its own "
           "stride each step, so k₁×k₂×k₃ can densify unevenly — e.g. a "
           "layered crystal sweeping 4×4×2 → 8×8×3 → 12×12×4. A stride of "
           "0 holds that axis fixed."));
    sweepForm_->addRow(anisotropicCheck_);

    for (int axis = 0; axis < 3; ++axis) {
        auto* row = new QWidget(sweepGroup);
        auto* rowLayout = new QHBoxLayout(row);
        rowLayout->setContentsMargins(0, 0, 0, 0);
        rowLayout->addWidget(new QLabel(tr("start"), row));
        axisStartSpins_[axis] = makeKSpin(2, 1);
        rowLayout->addWidget(axisStartSpins_[axis]);
        rowLayout->addWidget(new QLabel(tr("stride"), row));
        axisStrideSpins_[axis] = makeKSpin(2, 0);
        axisStrideSpins_[axis]->setToolTip(
            tr("0 pins this axis at its start value for the whole sweep."));
        rowLayout->addWidget(axisStrideSpins_[axis]);
        rowLayout->addStretch(1);
        axisRows_[axis] = row;
        sweepForm_->addRow(tr("k%1:").arg(axis + 1), row);
    }
    stepsSpin_ = new QSpinBox(sweepGroup);
    stepsSpin_->setRange(2, 32);
    stepsSpin_->setValue(5);
    stepsSpin_->setToolTip(
        tr("How many meshes the sweep evaluates; the last one is the "
           "reference."));
    sweepForm_->addRow(tr("Steps:"), stepsSpin_);

    // Γ-centering is part of the mesh definition, so it belongs on the
    // sweep stage with the rest of it (the calculator page's BZ toggles are
    // hidden for this wizard). Held fixed across the sweep — toggling it
    // mid-sweep would change the sampling twice per step.
    gammaCheck_ = new QCheckBox(tr("Gamma-centered Grid"), sweepGroup);
    gammaCheck_->setToolTip(
        tr("Shift every mesh in the sweep so it includes the Γ point "
           "(kpts={'size': …, 'gamma': True}). Even Monkhorst-Pack grids "
           "otherwise straddle Γ, which changes how band edges at Γ "
           "converge."));
    sweepForm_->addRow(gammaCheck_);

    layout->addWidget(sweepGroup);

    sweepSummary_ = new QLabel(page);
    sweepSummary_->setWordWrap(true);
    layout->addWidget(sweepSummary_);
    layout->addStretch(1);

    const auto refresh = [this] {
        updateSweepSummary();
        refreshPreview();
    };
    for (QSpinBox* spin : {minKSpin_, maxKSpin_, strideSpin_, stepsSpin_})
        connect(spin, &QSpinBox::valueChanged, this, refresh);
    for (int axis = 0; axis < 3; ++axis) {
        connect(axisStartSpins_[axis], &QSpinBox::valueChanged, this,
                refresh);
        connect(axisStrideSpins_[axis], &QSpinBox::valueChanged, this,
                refresh);
    }
    connect(gammaCheck_, &QCheckBox::toggled, this, refresh);
    connect(anisotropicCheck_, &QCheckBox::toggled, this, [this, refresh] {
        updateModeRows();
        refresh();
    });

    updateModeRows();
    updateSweepSummary();
    return page;
}

void KpointsConvergenceWizard::updateModeRows()
{
    const bool anisotropic = anisotropicCheck_->isChecked();
    const auto setRowVisible = [this](QWidget* field, bool visible) {
        int row = -1;
        QFormLayout::ItemRole role{};
        sweepForm_->getWidgetPosition(field, &row, &role);
        if (row >= 0)
            sweepForm_->setRowVisible(row, visible);
    };
    setRowVisible(minKSpin_, !anisotropic);
    setRowVisible(maxKSpin_, !anisotropic);
    setRowVisible(strideSpin_, !anisotropic);
    for (QWidget* row : axisRows_)
        setRowVisible(row, anisotropic);
    setRowVisible(stepsSpin_, anisotropic);
}

std::vector<core::KpointsConvergenceRunConfig::Mesh>
KpointsConvergenceWizard::meshes() const
{
    std::vector<core::KpointsConvergenceRunConfig::Mesh> result;

    if (anisotropicCheck_->isChecked()) {
        // Per-axis arithmetic progressions, advanced together. kPerAxis —
        // the scalar the viewer plots against — is the step's largest
        // subdivision count: the densest direction is what the cost and the
        // sampling error both follow.
        int steps = stepsSpin_->value();
        bool anyStride = false;
        for (const QSpinBox* stride : axisStrideSpins_)
            anyStride = anyStride || stride->value() > 0;
        // Every stride 0 would evaluate the same mesh N times; one honest
        // point says more than five identical ones.
        if (!anyStride)
            steps = 1;
        for (int step = 0; step < steps; ++step) {
            core::KpointsConvergenceRunConfig::Mesh mesh{};
            int densest = 1;
            for (int axis = 0; axis < 3; ++axis) {
                mesh.kpts[axis] = axisStartSpins_[axis]->value()
                    + step * axisStrideSpins_[axis]->value();
                densest = std::max(densest, mesh.kpts[axis]);
            }
            mesh.kPerAxis = densest;
            result.push_back(mesh);
        }
        return result;
    }

    // Isotropic: read defensively — a swapped min/max is one arrow-key away.
    const int low = qMin(minKSpin_->value(), maxKSpin_->value());
    const int high = qMax(minKSpin_->value(), maxKSpin_->value());
    const int stride = qMax(1, strideSpin_->value());
    const auto append = [&result](int n) {
        core::KpointsConvergenceRunConfig::Mesh mesh{};
        mesh.kPerAxis = n;
        for (int axis = 0; axis < 3; ++axis)
            mesh.kpts[axis] = n;
        result.push_back(mesh);
    };
    for (int n = low; n < high; n += stride)
        append(n);
    // The maximum is the reference — appended unconditionally rather than
    // hoping the stride divides the span.
    append(high);
    return result;
}

void KpointsConvergenceWizard::updateSweepSummary()
{
    const auto values = meshes();
    QStringList shown;
    for (const auto& mesh : values)
        shown << QStringLiteral("%1×%2×%3")
                     .arg(mesh.kpts[0])
                     .arg(mesh.kpts[1])
                     .arg(mesh.kpts[2]);
    sweepSummary_->setText(
        tr("%n calculation(s): %1. Reference: %2%3.", nullptr,
           static_cast<int>(values.size()))
            .arg(shown.join(QStringLiteral(", ")), shown.back(),
                 gammaCheck_ && gammaCheck_->isChecked()
                     ? tr(" (Γ-centered)")
                     : QString()));
}

core::KpointsConvergenceRunConfig KpointsConvergenceWizard::runConfig() const
{
    core::KpointsConvergenceRunConfig config;
    config.meshes = meshes();
    config.calculator = baseCalculatorConfig();
    config.calculator.task = core::TaskKind::SinglePoint;
    // The sweep stage owns the mesh definition, Γ-centering included — the
    // calculator page's own toggle is hidden for this wizard.
    config.calculator.kptsGammaCentered = gammaCheck_->isChecked();
    electronic_.applyTo(config.calculator);
    return config;
}

QString KpointsConvergenceWizard::generateScript() const
{
    return QString::fromStdString(
        core::KpointsConvergenceScriptGenerator::generate(runConfig()));
}

} // namespace calango::gui
