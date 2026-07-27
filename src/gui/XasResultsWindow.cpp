#include "gui/XasResultsWindow.hpp"

#include "gui/SpectrumPlotWidget.hpp"

#include <QCheckBox>
#include <QDialogButtonBox>
#include <QFile>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLabel>
#include <QVBoxLayout>

#include <algorithm>
#include <vector>

namespace calango::gui {

namespace {

std::vector<double> toVector(const QJsonValue& value)
{
    std::vector<double> out;
    const QJsonArray array = value.toArray();
    out.reserve(static_cast<std::size_t>(array.size()));
    for (const QJsonValue& entry : array)
        out.push_back(entry.toDouble());
    return out;
}

} // namespace

XasResultsWindow::XasResultsWindow(QWidget* parent) : QDialog(parent)
{
    setWindowTitle(tr("X-ray Absorption Spectrum"));
    resize(760, 560);

    auto* layout = new QVBoxLayout(this);

    summaryLabel_ = new QLabel(this);
    summaryLabel_->setWordWrap(true);
    summaryLabel_->setTextFormat(Qt::RichText);
    layout->addWidget(summaryLabel_);

    plot_ = new SpectrumPlotWidget(this);
    layout->addWidget(plot_, 1);

    auto* controls = new QHBoxLayout;
    polarizationCheck_ =
        new QCheckBox(tr("Show the x, y and z polarizations"), this);
    polarizationCheck_->setToolTip(
        tr("The isotropic spectrum is their average — what a powder or "
           "solution measurement sees. For an oriented sample the difference "
           "between the three is the result."));
    sticksCheck_ = new QCheckBox(tr("Show individual transitions"), this);
    sticksCheck_->setToolTip(
        tr("The discrete transitions the broadened spectrum is built from, "
           "drawn at their own energies. Useful for telling a genuine shoulder "
           "from two peaks that the broadening merged."));
    controls->addWidget(polarizationCheck_);
    controls->addWidget(sticksCheck_);
    controls->addStretch(1);
    layout->addLayout(controls);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);

    for (QCheckBox* check : {polarizationCheck_, sticksCheck_})
        connect(check, &QCheckBox::toggled, this,
                &XasResultsWindow::refreshPlot);
}

bool XasResultsWindow::loadResults(const QString& path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return false;
    data_ = QJsonDocument::fromJson(file.readAll()).object();
    if (data_.isEmpty() || data_.value(QStringLiteral("energy_eV")).toArray().isEmpty())
        return false;

    const double dks = data_.value(QStringLiteral("dks_energy_eV")).toDouble();
    const double hole = data_.value(QStringLiteral("core_hole")).toDouble();
    summaryLabel_->setText(
        tr("<b>%1 atom #%2</b> · core hole %3 e · setup <code>%4</code> · "
           "broadening %5 eV<br>%6")
            .arg(data_.value(QStringLiteral("element")).toString())
            .arg(data_.value(QStringLiteral("absorbing_atom")).toInt() + 1)
            .arg(hole, 0, 'g', 2)
            .arg(data_.value(QStringLiteral("setup")).toString())
            .arg(data_.value(QStringLiteral("fwhm_eV")).toDouble(), 0, 'g', 3)
            .arg(dks > 0.0
                     // Saying which scale the axis is on matters: a relative
                     // spectrum plotted as though it were absolute is off by
                     // hundreds of eV, and nothing about the plot shows it.
                     ? tr("Energies are absolute (delta-Kohn-Sham edge at "
                          "%1 eV).")
                           .arg(dks, 0, 'f', 2)
                     : tr("<i>Energies are RELATIVE to the first unoccupied "
                          "state — no absolute edge position was "
                          "computed.</i>")));

    refreshPlot();
    return true;
}

void XasResultsWindow::refreshPlot()
{
    const std::vector<double> energy =
        toVector(data_.value(QStringLiteral("energy_eV")));
    std::vector<QPair<QString, std::vector<double>>> series;
    series.push_back({tr("Isotropic"),
                   toVector(data_.value(QStringLiteral("isotropic")))});
    if (polarizationCheck_->isChecked()) {
        series.push_back({tr("x"),
                       toVector(data_.value(QStringLiteral("polarization_x")))});
        series.push_back({tr("y"),
                       toVector(data_.value(QStringLiteral("polarization_y")))});
        series.push_back({tr("z"),
                       toVector(data_.value(QStringLiteral("polarization_z")))});
    }
    if (sticksCheck_->isChecked()) {
        // The sticks live on their own energies, so they are resampled onto
        // the spectrum's grid as spikes rather than plotted against the wrong
        // axis. Nearest-bin placement: the grid is far finer than the
        // transition spacing, so nothing merges that was not already merged.
        const std::vector<double> stickE =
            toVector(data_.value(QStringLiteral("stick_energy_eV")));
        const std::vector<double> stickI =
            toVector(data_.value(QStringLiteral("stick_isotropic")));
        std::vector<double> spikes(energy.size(), 0.0);
        if (!energy.empty() && energy.size() > 1) {
            const double lo = energy.front();
            const double hi = energy.back();
            const double span = hi - lo;
            for (std::size_t i = 0; i < stickE.size() && i < stickI.size(); ++i) {
                if (span <= 0.0)
                    break;
                const auto bin = static_cast<std::size_t>(
                    std::clamp((stickE[i] - lo) / span, 0.0, 1.0)
                    * static_cast<double>(energy.size() - 1));
                spikes[bin] = std::max(spikes[bin], stickI[i]);
            }
        }
        series.push_back({tr("Transitions"), std::move(spikes)});
    }

    plot_->setSeries(energy, series, tr("Energy (eV)"),
                     tr("Absorption (arb. units)"));
}

} // namespace calango::gui
