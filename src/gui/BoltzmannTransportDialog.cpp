#include "gui/BoltzmannTransportDialog.hpp"

#include "core/LocaleSafeNumber.hpp"
#include "gui/SpectrumPlotWidget.hpp"
#include "gui/WannierModelSource.hpp"

#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QSpinBox>
#include <QTextStream>
#include <QVBoxLayout>
#include <QFile>

#include <cmath>

namespace calango::gui {

BoltzmannTransportDialog::BoltzmannTransportDialog(QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle(tr("Boltzmann Transport"));
    auto* outer = new QVBoxLayout(this);

    auto* intro = new QLabel(
        tr("Electronic and thermoelectric transport from the "
           "Wannier-interpolated bands, in the <b>constant relaxation-time</b> "
           "approximation."),
        this);
    intro->setWordWrap(true);
    intro->setTextFormat(Qt::RichText);
    intro->setToolTip(
        tr("Band velocities come from dH/dk in the Wannier gauge, taken "
           "analytically rather than by finite differences on the "
           "eigenvalues — at a band crossing the eigenvalue branches swap and "
           "a finite difference reports a velocity that can have the wrong "
           "sign entirely.\n\n"
           "tau enters sigma and kappa_e linearly and cancels exactly in the "
           "Seebeck coefficient, so S is a genuine prediction while sigma is "
           "only as good as the tau you supply."));
    outer->addWidget(intro);

    source_ = new WannierModelSource(this);
    outer->addWidget(source_);
    connect(source_, &WannierModelSource::modelChanged, this,
            [this] { computeButton_->setEnabled(true); });

    auto* settings = new QGroupBox(tr("Integration"), this);
    auto* form = new QFormLayout(settings);

    auto* meshRow = new QHBoxLayout;
    for (int axis = 0; axis < 3; ++axis) {
        kmesh_[axis] = new QSpinBox(settings);
        kmesh_[axis]->setRange(1, 200);
        kmesh_[axis]->setValue(20);
        meshRow->addWidget(kmesh_[axis]);
    }
    meshRow->addStretch(1);
    form->addRow(tr("k-mesh:"), meshRow);
    meshRow->itemAt(0)->widget()->setToolTip(
        tr("Transport integrals converge far more slowly than a band "
           "structure: the Fermi surface has to be resolved, not merely "
           "crossed."));

    smearing_ = new QDoubleSpinBox(settings);
    smearing_->setRange(0.001, 1.0);
    smearing_->setValue(0.02);
    smearing_->setDecimals(3);
    smearing_->setSuffix(tr(" eV"));
    smearing_->setToolTip(
        tr("Width used to resolve the delta function in Sigma(eps). It has to "
           "sit between two bounds: ABOVE the level spacing of the mesh, or "
           "Sigma(eps) breaks into spikes, and BELOW kT, or the smearing acts "
           "as extra temperature and the Wiedemann-Franz ratio comes out low. "
           "At 300 K, kT is 26 meV."));
    form->addRow(tr("Smearing:"), smearing_);

    relaxationTime_ = new QDoubleSpinBox(settings);
    relaxationTime_->setRange(0.01, 10000.0);
    relaxationTime_->setValue(10.0);
    relaxationTime_->setDecimals(2);
    relaxationTime_->setSuffix(tr(" fs"));
    relaxationTime_->setToolTip(
        tr("Constant relaxation time. 10 fs is the usual order for a metal at "
           "room temperature and is a placeholder, not a prediction — sigma "
           "and kappa_e scale with it exactly, S does not depend on it at "
           "all."));
    form->addRow(tr("Relaxation time τ:"), relaxationTime_);

    latticeKappa_ = new QDoubleSpinBox(settings);
    latticeKappa_->setRange(0.0, 5000.0);
    latticeKappa_->setValue(1.0);
    latticeKappa_->setDecimals(3);
    latticeKappa_->setSuffix(tr(" W/(m·K)"));
    latticeKappa_->setToolTip(
        tr("Lattice thermal conductivity, needed for zT. It is a phonon "
           "quantity: nothing in an electronic structure determines it, so it "
           "is an input."));
    form->addRow(tr("Lattice κ_L:"), latticeKappa_);

    temperature_ = new QDoubleSpinBox(settings);
    temperature_->setRange(1.0, 3000.0);
    temperature_->setValue(300.0);
    temperature_->setSuffix(tr(" K"));
    form->addRow(tr("Temperature:"), temperature_);

    auto* muRow = new QHBoxLayout;
    muMin_ = new QDoubleSpinBox(settings);
    muMax_ = new QDoubleSpinBox(settings);
    for (auto* spin : {muMin_, muMax_}) {
        spin->setRange(-50.0, 50.0);
        spin->setDecimals(3);
        spin->setSuffix(tr(" eV"));
    }
    muMin_->setValue(-2.0);
    muMax_->setValue(2.0);
    muRow->addWidget(muMin_);
    muRow->addWidget(muMax_);
    form->addRow(tr("Chemical potential:"), muRow);
    outer->addWidget(settings);

    auto* viewRow = new QHBoxLayout;
    viewRow->addWidget(new QLabel(tr("Quantity:"), this));
    quantity_ = new QComboBox(this);
    quantity_->addItem(tr("Seebeck S"), 0);
    quantity_->addItem(tr("Conductivity σ"), 1);
    quantity_->addItem(tr("Electronic κ_e"), 2);
    quantity_->addItem(tr("Power factor S²σ"), 3);
    quantity_->addItem(tr("Figure of merit zT"), 4);
    quantity_->addItem(tr("Carrier concentration"), 5);
    viewRow->addWidget(quantity_);
    viewRow->addWidget(new QLabel(tr("Component:"), this));
    component_ = new QComboBox(this);
    component_->addItem(tr("xx"), 0);
    component_->addItem(tr("yy"), 4);
    component_->addItem(tr("zz"), 8);
    component_->addItem(tr("average (trace/3)"), -1);
    component_->setCurrentIndex(3);
    viewRow->addWidget(component_);
    viewRow->addStretch(1);
    computeButton_ = new QPushButton(tr("Compute"), this);
    computeButton_->setEnabled(false);
    connect(computeButton_, &QPushButton::clicked, this,
            &BoltzmannTransportDialog::compute);
    viewRow->addWidget(computeButton_);
    outer->addLayout(viewRow);
    // Re-plotting is free once the sweep exists, so the two selectors act live.
    for (auto* combo : {quantity_, component_})
        connect(combo, &QComboBox::currentIndexChanged, this, [this] {
            if (!points_.empty())
                compute();
        });

    summary_ = new QLabel(tr("No transport computed."), this);
    summary_->setWordWrap(true);
    summary_->setTextFormat(Qt::RichText);
    outer->addWidget(summary_);

    plot_ = new SpectrumPlotWidget(this);
    outer->addWidget(plot_, 1);

    auto* buttons = new QHBoxLayout;
    auto* exportButton = new QPushButton(tr("Export Data…"), this);
    connect(exportButton, &QPushButton::clicked, this,
            &BoltzmannTransportDialog::exportData);
    buttons->addWidget(exportButton);
    buttons->addStretch(1);
    auto* close = new QPushButton(tr("Close"), this);
    connect(close, &QPushButton::clicked, this, &QDialog::accept);
    buttons->addWidget(close);
    outer->addLayout(buttons);

    resize(900, 820);
}

core::BoltzmannTransport::Options BoltzmannTransportDialog::readOptions() const
{
    core::BoltzmannTransport::Options options;
    for (int axis = 0; axis < 3; ++axis)
        options.kmesh[axis] = kmesh_[axis]->value();
    options.smearing = smearing_->value();
    options.relaxationTime = relaxationTime_->value() * 1e-15; // fs -> s
    options.latticeThermalConductivity = latticeKappa_->value();
    options.energyMin = muMin_->value() - 1.0;
    options.energyMax = muMax_->value() + 1.0;
    options.energyBins = 900;
    return options;
}

int BoltzmannTransportDialog::componentIndex() const
{
    return component_->currentData().toInt();
}

void BoltzmannTransportDialog::compute()
{
    if (!source_->hasModel())
        return;
    try {
        const core::BoltzmannTransport transport(source_->model(),
                                                 readOptions());
        const double temperature = temperature_->value();
        const int samples = 121;

        mu_.clear();
        points_.clear();
        mu_.reserve(samples);
        points_.reserve(samples);
        const double lo = muMin_->value();
        const double hi = muMax_->value();
        for (int i = 0; i < samples; ++i) {
            const double mu = lo + (hi - lo) * i / (samples - 1);
            mu_.push_back(mu);
            points_.push_back(transport.evaluate(temperature, mu));
        }

        const int index = componentIndex();
        const auto pick = [&](const core::BoltzmannTransport::Point& p,
                              const std::array<double, 9>& tensor,
                              double average) {
            (void)p;
            return index < 0 ? average
                             : tensor[static_cast<std::size_t>(index)];
        };

        std::vector<double> values;
        QString label;
        values.reserve(points_.size());
        for (const auto& p : points_) {
            switch (quantity_->currentData().toInt()) {
            case 0:
                values.push_back(pick(p, p.seebeck, p.seebeckAvg) * 1e6);
                label = tr("S (µV/K)");
                break;
            case 1:
                values.push_back(pick(p, p.sigma, p.sigmaAvg));
                label = tr("σ (S/m)");
                break;
            case 2:
                values.push_back(pick(p, p.kappaElectronic, p.kappaAvg));
                label = tr("κ_e (W/m/K)");
                break;
            case 3:
                values.push_back(pick(p, p.powerFactor, p.powerFactorAvg));
                label = tr("S²σ (W/m/K²)");
                break;
            case 4:
                values.push_back(pick(p, p.zT, p.zTAvg));
                label = tr("zT");
                break;
            default:
                values.push_back(p.carrierConcentration);
                label = tr("n (cm⁻³)");
                break;
            }
        }

        std::vector<QPair<QString, std::vector<double>>> series;
        series.push_back({label, values});
        plot_->setSeries(mu_, series, tr("Chemical potential µ (eV)"), label);
        plot_->setXRange(lo, hi);

        const auto& spectral = transport.spectralConductivity();
        QString text =
            tr("<b>T = %1 K</b>, τ = %2 fs, κ_L = %3 W/(m·K). ")
                .arg(temperature, 0, 'f', 1)
                .arg(relaxationTime_->value(), 0, 'f', 2)
                .arg(latticeKappa_->value(), 0, 'f', 3);
        // The one diagnostic that decides whether any of this is converged.
        text += tr("Mean level spacing %1 meV against %2 meV of smearing — "
                   "the smearing should exceed it, and should also stay below "
                   "kT = %3 meV.")
                    .arg(spectral.meanLevelSpacing * 1000.0, 0, 'f', 2)
                    .arg(smearing_->value() * 1000.0, 0, 'f', 1)
                    .arg(core::BoltzmannTransport::kBoltzmann_eV * temperature
                             * 1000.0,
                         0, 'f', 1);
        summary_->setText(text);
    } catch (const std::exception& e) {
        QMessageBox::warning(this, tr("Boltzmann Transport"),
                             tr("The solver refused this input:\n%1")
                                 .arg(QString::fromUtf8(e.what())));
    }
}

void BoltzmannTransportDialog::exportData()
{
    if (points_.empty()) {
        QMessageBox::information(this, tr("Export Data"),
                                 tr("Nothing computed yet."));
        return;
    }
    const QString path = QFileDialog::getSaveFileName(
        this, tr("Export transport data"),
        QStringLiteral("boltzmann_transport.csv"), tr("CSV (*.csv)"));
    if (path.isEmpty())
        return;
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QMessageBox::warning(this, tr("Export Data"),
                             tr("Could not write %1.").arg(path));
        return;
    }
    QTextStream out(&file);
    // Every tensor component, not only the one on screen: a CSV is read by a
    // script, and re-running the sweep to get yy after exporting xx is exactly
    // the friction an export is supposed to remove.
    out << "# mu(eV),T(K),n(cm^-3),S_xx,S_yy,S_zz(V/K),sigma_xx,sigma_yy,"
           "sigma_zz(S/m),kappa_xx,kappa_yy,kappa_zz(W/m/K),PF_avg,zT_avg\n";
    for (std::size_t i = 0; i < points_.size(); ++i) {
        const auto& p = points_[i];
        out << QString::fromStdString(core::localeSafeFormat(mu_[i])) << ','
            << p.temperature << ',' << p.carrierConcentration << ','
            << p.seebeck[0] << ',' << p.seebeck[4] << ',' << p.seebeck[8] << ','
            << p.sigma[0] << ',' << p.sigma[4] << ',' << p.sigma[8] << ','
            << p.kappaElectronic[0] << ',' << p.kappaElectronic[4] << ','
            << p.kappaElectronic[8] << ',' << p.powerFactorAvg << ','
            << p.zTAvg << '\n';
    }
}

void BoltzmannTransportDialog::setWannierRuns(const QList<QPair<QString, QString>>& runs)
{
    if (source_)
        source_->setWannierRuns(runs);
}

} // namespace calango::gui
