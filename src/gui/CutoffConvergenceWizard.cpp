#include "gui/CutoffConvergenceWizard.hpp"

#include <QDoubleSpinBox>
#include <QGroupBox>
#include <QFormLayout>
#include <QLabel>
#include <QVBoxLayout>

#include <QtGlobal>

namespace calango::gui {

CutoffConvergenceWizard::CutoffConvergenceWizard(QWidget* parent)
    : GpawElectronicWizard(parent)
{
    buildUi();
    electronic_.updateEnabled();
}

QString CutoffConvergenceWizard::wizardTitle() const
{
    return tr("Plane-wave Cutoff Convergence");
}

QWidget* CutoffConvergenceWizard::buildSettingsPage()
{
    auto* page = new QWidget(this);
    auto* layout = new QVBoxLayout(page);

    auto* note = new QLabel(
        tr("One single-point calculation per cutoff, on the structure as it "
           "stands. The run at the <b>highest</b> cutoff is the convergence "
           "reference; the result window plots the total energy per atom and "
           "the maximum force magnitude against the cutoff, so \"converged\" "
           "becomes a number you read off a curve instead of a guess.<br><br>"
           "The Calculator page therefore offers no single cutoff field — "
           "this sweep is the cutoff. Every other setting there (XC, "
           "k-points, convergence targets, spin) is held fixed across the "
           "runs, as a convergence study requires."),
        page);
    note->setWordWrap(true);
    note->setTextFormat(Qt::RichText);
    layout->addWidget(note);

    auto* sweepGroup = new QGroupBox(tr("Plane-wave Cutoff Sweep"), page);
    auto* form = new QFormLayout(sweepGroup);

    const auto makeCutoffSpin = [sweepGroup](double value) {
        auto* spin = new QDoubleSpinBox(sweepGroup);
        spin->setRange(50.0, 5000.0);
        spin->setDecimals(0);
        spin->setSingleStep(50.0);
        spin->setSuffix(QObject::tr(" eV"));
        spin->setValue(value);
        return spin;
    };
    minCutoffSpin_ = makeCutoffSpin(300.0);
    minCutoffSpin_->setToolTip(
        tr("Lowest cutoff evaluated. Starting well below the production "
           "candidate is what makes the trend visible."));
    form->addRow(tr("Minimum cutoff:"), minCutoffSpin_);

    maxCutoffSpin_ = makeCutoffSpin(800.0);
    maxCutoffSpin_->setToolTip(
        tr("Highest cutoff evaluated — the convergence reference. It should "
           "sit comfortably above where you expect convergence, since every "
           "other run is judged against it."));
    form->addRow(tr("Maximum cutoff:"), maxCutoffSpin_);

    strideSpin_ = new QDoubleSpinBox(sweepGroup);
    strideSpin_->setRange(10.0, 1000.0);
    strideSpin_->setDecimals(0);
    strideSpin_->setSingleStep(10.0);
    strideSpin_->setSuffix(tr(" eV"));
    strideSpin_->setValue(100.0);
    strideSpin_->setToolTip(
        tr("Spacing between consecutive cutoffs. The maximum is always "
           "included even when the stride does not land on it exactly."));
    form->addRow(tr("Stride:"), strideSpin_);

    layout->addWidget(sweepGroup);

    sweepSummary_ = new QLabel(page);
    sweepSummary_->setWordWrap(true);
    layout->addWidget(sweepSummary_);
    layout->addStretch(1);

    for (QDoubleSpinBox* spin : {minCutoffSpin_, maxCutoffSpin_, strideSpin_})
        connect(spin, &QDoubleSpinBox::valueChanged, this, [this] {
            updateSweepSummary();
            refreshPreview();
        });
    updateSweepSummary();
    return page;
}

std::vector<double> CutoffConvergenceWizard::cutoffs() const
{
    // Read defensively: buildSettingsPage() has always run by the time a
    // script is generated, but a swapped min/max is one arrow-key away.
    const double low =
        qMin(minCutoffSpin_->value(), maxCutoffSpin_->value());
    const double high =
        qMax(minCutoffSpin_->value(), maxCutoffSpin_->value());
    const double stride = qMax(1.0, strideSpin_->value());

    std::vector<double> values;
    for (double ecut = low; ecut < high - 1e-9; ecut += stride)
        values.push_back(ecut);
    // The maximum is the reference — appended unconditionally rather than
    // hoping the stride divides the span.
    values.push_back(high);
    return values;
}

void CutoffConvergenceWizard::updateSweepSummary()
{
    const std::vector<double> values = cutoffs();
    QStringList shown;
    for (double ecut : values)
        shown << QString::number(ecut, 'f', 0);
    sweepSummary_->setText(
        tr("%n calculation(s): %1 eV. Reference: %2 eV.", nullptr,
           static_cast<int>(values.size()))
            .arg(shown.join(QStringLiteral(", ")),
                 QString::number(values.back(), 'f', 0)));
}

core::CutoffConvergenceRunConfig CutoffConvergenceWizard::runConfig() const
{
    core::CutoffConvergenceRunConfig config;
    config.cutoffsEv = cutoffs();
    config.calculator = baseCalculatorConfig();
    config.calculator.task = core::TaskKind::SinglePoint;
    electronic_.applyTo(config.calculator);
    return config;
}

QString CutoffConvergenceWizard::generateScript() const
{
    return QString::fromStdString(
        core::CutoffConvergenceScriptGenerator::generate(runConfig()));
}

} // namespace calango::gui
