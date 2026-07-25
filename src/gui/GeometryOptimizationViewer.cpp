#include "gui/GeometryOptimizationViewer.hpp"

#include <QClipboard>
#include <QDialogButtonBox>
#include <QFile>
#include <QFileInfo>
#include <QFormLayout>
#include <QGroupBox>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QJsonDocument>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

namespace calango::gui {

namespace {

/// "—" for a JSON null / missing value, so a field the script could not
/// compute reads as absent rather than as a confident 0.000.
QString orDash(const QJsonValue& value, int decimals, const QString& suffix)
{
    if (value.isUndefined() || value.isNull())
        return QStringLiteral("—");
    return QStringLiteral("%1%2")
        .arg(value.toDouble(), 0, 'f', decimals)
        .arg(suffix);
}

} // namespace

GeometryOptimizationViewer::GeometryOptimizationViewer(QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle(tr("Geometry Optimization Viewer"));
    resize(520, 300);

    auto* layout = new QVBoxLayout(this);

    // -- Physical summary ---------------------------------------------------
    auto* summaryGroup = new QGroupBox(tr("Relaxation Summary"), this);
    auto* form = new QFormLayout(summaryGroup);
    const auto addRow = [&](const QString& label, QLabel*& target,
                            const QString& tip = QString()) {
        target = new QLabel(QStringLiteral("—"), summaryGroup);
        target->setTextInteractionFlags(Qt::TextSelectableByMouse);
        if (!tip.isEmpty())
            target->setToolTip(tip);
        form->addRow(label, target);
    };
    addRow(tr("Total Energy:"), energyLabel_);
    addRow(tr("Energy Change:"), energyChangeLabel_,
           tr("Final energy minus the energy of the first recorded step. A "
              "relaxation that barely moved the energy either started at the "
              "minimum or never took a productive step."));
    addRow(tr("Max. Atomic Force:"), forceLabel_,
           tr("Largest per-atom force magnitude, against the fmax criterion "
              "the run was asked to reach."));
    addRow(tr("RMS Force:"), rmsForceLabel_,
           tr("Root mean square over all 3N force components — the "
              "conventional definition, so it is directly comparable with "
              "values quoted by other codes."));
    addRow(tr("Optimizer:"), optimizerLabel_);
    addRow(tr("Convergence:"), convergedLabel_);
    layout->addWidget(summaryGroup);

    // -- Actions ------------------------------------------------------------
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    volumetricButton_ = buttons->addButton(tr("Get Volumetric Data"),
                                           QDialogButtonBox::ActionRole);
    volumetricButton_->setToolTip(
        tr("Send the final electron density / potential grid of this run to "
           "the Volumetric Data dock. Exports it from the saved calculation "
           "when no cube file is present yet."));
    auto* copyButton =
        buttons->addButton(tr("Copy Summary"), QDialogButtonBox::ActionRole);
    connect(copyButton, &QPushButton::clicked, this,
            &GeometryOptimizationViewer::copyToClipboard);
    connect(volumetricButton_, &QPushButton::clicked, this, [this] {
        if (!directory_.isEmpty())
            Q_EMIT getVolumetricDataRequested(directory_);
    });
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);
}

bool GeometryOptimizationViewer::loadResults(const QString& jsonPath)
{
    QFile file(jsonPath);
    if (!file.open(QIODevice::ReadOnly))
        return false;
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    if (!doc.isObject())
        return false;
    data_ = doc.object();
    directory_ = QFileInfo(jsonPath).absolutePath();

    energyLabel_->setText(
        orDash(data_.value(QStringLiteral("energy_eV")), 6, tr(" eV")));

    const QJsonValue change = data_.value(QStringLiteral("energy_change_eV"));
    if (change.isNull() || change.isUndefined()) {
        energyChangeLabel_->setText(QStringLiteral("—"));
    } else {
        // Sign matters here: a relaxation should LOWER the energy, and a
        // positive ΔE is a red flag worth showing explicitly.
        const double dE = change.toDouble();
        energyChangeLabel_->setText(
            QStringLiteral("%1%2 eV")
                .arg(dE > 0.0 ? QStringLiteral("+") : QString())
                .arg(dE, 0, 'f', 6));
    }

    const double fmax = data_.value(QStringLiteral("fmax_eV_per_A")).toDouble();
    const double criterion =
        data_.value(QStringLiteral("fmax_criterion_eV_per_A")).toDouble();
    const int worstAtom = data_.value(QStringLiteral("fmax_atom")).toInt(-1);
    QString forceText = QStringLiteral("%1 eV/Å").arg(fmax, 0, 'f', 4);
    if (worstAtom >= 0)
        forceText += tr(" (Atom #%1)").arg(worstAtom + 1);
    if (criterion > 0.0)
        forceText += tr("  [target %1]").arg(criterion, 0, 'f', 4);
    forceLabel_->setText(forceText);

    rmsForceLabel_->setText(
        orDash(data_.value(QStringLiteral("frms_eV_per_A")), 4, tr(" eV/Å")));

    const QString optimizer =
        data_.value(QStringLiteral("optimizer")).toString(tr("unknown"));
    const int steps = data_.value(QStringLiteral("steps")).toInt();
    const int maxSteps = data_.value(QStringLiteral("max_steps")).toInt();
    optimizerLabel_->setText(
        tr("%1 — %2 of at most %3 steps").arg(optimizer).arg(steps).arg(maxSteps));

    const bool converged = data_.value(QStringLiteral("converged")).toBool();
    convergedLabel_->setText(converged
                                 ? tr("Converged")
                                 : tr("NOT converged — the step cap was "
                                      "reached before fmax"));
    convergedLabel_->setStyleSheet(converged ? QString()
                                             : QStringLiteral("color: #d9534f;"));

    return true;
}

QString GeometryOptimizationViewer::plainTextSummary() const
{
    QStringList lines;
    lines << tr("Geometry Optimization — %1")
                 .arg(data_.value(QStringLiteral("formula")).toString())
          << tr("Total Energy:      %1").arg(energyLabel_->text())
          << tr("Energy Change:     %1").arg(energyChangeLabel_->text())
          << tr("Max. Atomic Force: %1").arg(forceLabel_->text())
          << tr("RMS Force:         %1").arg(rmsForceLabel_->text())
          << tr("Optimizer:         %1").arg(optimizerLabel_->text())
          << tr("Convergence:       %1").arg(convergedLabel_->text());
    return lines.join(QLatin1Char('\n'));
}

void GeometryOptimizationViewer::copyToClipboard()
{
    QGuiApplication::clipboard()->setText(plainTextSummary());
}

} // namespace calango::gui
