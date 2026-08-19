#include "gui/XasResultsWindow.hpp"

#include "gui/GuiUtils.hpp"
#include "gui/OpticsPlotStyleDialog.hpp"
#include "gui/SpectrumPlotWidget.hpp"

#include <QCheckBox>
#include <QDoubleSpinBox>
#include <QFile>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLabel>
#include <QMessageBox>
#include <QPainter>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSlider>
#include <QTextStream>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>

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
    resize(820, 640);

    auto* layout = new QVBoxLayout(this);

    summaryLabel_ = new QLabel(this);
    summaryLabel_->setWordWrap(true);
    summaryLabel_->setTextFormat(Qt::RichText);
    layout->addWidget(summaryLabel_);

    // Energy window + appearance, on one row — the same pairing the Optics
    // viewer uses for its own Range: row and "Customize Appearance…" button.
    auto* rangeRow = new QHBoxLayout;
    rangeRow->addWidget(new QLabel(tr("Range:"), this));
    xMinSpin_ = new QDoubleSpinBox(this);
    xMaxSpin_ = new QDoubleSpinBox(this);
    for (QDoubleSpinBox* spin : {xMinSpin_, xMaxSpin_}) {
        spin->setDecimals(2);
        spin->setRange(0.0, 100000.0);
        spin->setValue(0.0);
        // Both at zero is the "fit the data" default; the min box says so.
        spin->setSpecialValueText(tr("auto"));
        spin->setSuffix(tr(" eV"));
        connect(spin, &QDoubleSpinBox::valueChanged, this,
                &XasResultsWindow::refreshPlot);
        rangeRow->addWidget(spin);
    }
    xMinSpin_->setToolTip(
        tr("Lower display bound. Leave both at \"auto\" to fit the data."));
    xMaxSpin_->setToolTip(xMinSpin_->toolTip());
    rangeRow->addSpacing(16);
    auto* appearanceButton = new QPushButton(tr("Customize Appearance…"), this);
    connect(appearanceButton, &QPushButton::clicked, this,
            &XasResultsWindow::customizeAppearance);
    rangeRow->addWidget(appearanceButton);
    rangeRow->addStretch(1);
    layout->addLayout(rangeRow);

    // -- Live broadening ------------------------------------------------
    //
    // Unlike the Optics viewer's Lorentzian η, this is not a convolution of
    // an already-broadened curve — it is a fresh Gaussian fold of the RAW
    // discrete transitions (foldSticks()), so it is not constrained to only
    // widen. A stick has no width of its own to deconvolve.
    auto* broadeningRow = new QHBoxLayout;
    broadeningRow->addWidget(new QLabel(tr("Broadening (FWHM):"), this));
    broadeningSlider_ = new QSlider(Qt::Horizontal, this);
    broadeningSpin_ = new QDoubleSpinBox(this);
    broadeningSpin_->setObjectName(QStringLiteral("xasBroadening"));
    broadeningSpin_->setDecimals(3);
    broadeningSpin_->setRange(0.01, 10.0);
    broadeningSpin_->setSingleStep(0.01);
    broadeningSpin_->setSuffix(tr(" eV"));
    broadeningSpin_->setToolTip(
        tr("Gaussian broadening applied to the RAW transitions as the "
           "spectrum is drawn — a fresh fold from the discrete lines, not a "
           "widening of the already-broadened curve, so it can be raised or "
           "lowered freely.\n\n"
           "Every displayed curve is re-derived at this width; the "
           "individual-transition markers (below) are unaffected — they are "
           "the lines themselves, not the folded curve."));
    broadeningSlider_->setRange(1, 1000); // 0.001 eV steps of the spin's range
    broadeningSlider_->setToolTip(broadeningSpin_->toolTip());
    broadeningRow->addWidget(broadeningSlider_, 1);
    broadeningRow->addWidget(broadeningSpin_);
    layout->addLayout(broadeningRow);
    broadeningNote_ = new QLabel(this);
    broadeningNote_->setWordWrap(true);
    broadeningNote_->setTextFormat(Qt::RichText);
    layout->addWidget(broadeningNote_);

    connect(broadeningSlider_, &QSlider::valueChanged, this, [this](int milli) {
        const QSignalBlocker blocker(broadeningSpin_);
        const double fwhm = milli / 100.0;
        broadeningSpin_->setValue(fwhm);
        broadening_ = fwhm;
        refreshPlot();
    });
    connect(broadeningSpin_, &QDoubleSpinBox::valueChanged, this,
            [this](double fwhm) {
                const QSignalBlocker blocker(broadeningSlider_);
                broadeningSlider_->setValue(static_cast<int>(std::lround(fwhm * 100.0)));
                broadening_ = fwhm;
                refreshPlot();
            });

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

    // Export buttons + Close, in the same place and order the Optics viewer
    // puts them.
    auto* buttons = new QHBoxLayout;
    auto* csvButton = new QPushButton(tr("Export CSV…"), this);
    auto* imageButton = new QPushButton(tr("Export Image…"), this);
    auto* closeButton = new QPushButton(tr("Close"), this);
    buttons->addWidget(csvButton);
    buttons->addWidget(imageButton);
    buttons->addStretch(1);
    buttons->addWidget(closeButton);
    layout->addLayout(buttons);

    connect(csvButton, &QPushButton::clicked, this, &XasResultsWindow::exportCsv);
    connect(imageButton, &QPushButton::clicked, this,
            &XasResultsWindow::exportImage);
    connect(closeButton, &QPushButton::clicked, this, &QDialog::accept);

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

    energyGrid_ = toVector(data_.value(QStringLiteral("energy_eV")));
    stickEnergy_ = toVector(data_.value(QStringLiteral("stick_energy_eV")));
    stickIsotropic_ = toVector(data_.value(QStringLiteral("stick_isotropic")));
    stickX_ = toVector(data_.value(QStringLiteral("stick_polarization_x")));
    stickY_ = toVector(data_.value(QStringLiteral("stick_polarization_y")));
    stickZ_ = toVector(data_.value(QStringLiteral("stick_polarization_z")));
    hasPolarizationSticks_ = !stickX_.empty() && stickX_.size() == stickEnergy_.size()
        && stickY_.size() == stickEnergy_.size()
        && stickZ_.size() == stickEnergy_.size();

    fwhmStored_ = data_.value(QStringLiteral("fwhm_eV")).toDouble(0.5);
    broadening_ = fwhmStored_;
    linearBroadeningStored_ =
        data_.value(QStringLiteral("linear_broadening")).toBool(false);

    {
        const QSignalBlocker spinBlocker(broadeningSpin_);
        const QSignalBlocker sliderBlocker(broadeningSlider_);
        // Up to 10 eV, or the stored value if it happens to be wider than
        // that (an unusually broad run should not clamp the slider below
        // where it already is).
        const double hi = std::max(10.0, fwhmStored_);
        broadeningSpin_->setRange(0.01, hi);
        broadeningSpin_->setValue(fwhmStored_);
        broadeningSlider_->setRange(1, static_cast<int>(std::lround(hi * 100.0)));
        broadeningSlider_->setValue(static_cast<int>(std::lround(fwhmStored_ * 100.0)));
        const bool haveSticks = !stickEnergy_.empty() && !stickIsotropic_.empty()
            && stickEnergy_.size() == stickIsotropic_.size();
        broadeningSpin_->setEnabled(haveSticks);
        broadeningSlider_->setEnabled(haveSticks);
        if (!haveSticks)
            broadeningNote_->setText(
                tr("<i>This run recorded no individual transitions, so the "
                   "broadening it already carries cannot be adjusted here. "
                   "Re-run to get an adjustable spectrum.</i>"));
        else if (linearBroadeningStored_)
            broadeningNote_->setText(
                tr("<i>Computed at %1 eV with the linear broadening ramp "
                   "(width grows above the edge) — raising or lowering here "
                   "re-folds the raw transitions at a UNIFORM width instead, "
                   "which will not exactly match the stored curve even at "
                   "%1 eV. The x, y, z curves re-fold only if this job "
                   "recorded their own transitions (%2).</i>")
                    .arg(fwhmStored_, 0, 'g', 3)
                    .arg(hasPolarizationSticks_ ? tr("it does")
                                                : tr("this one does not")));
        else
            broadeningNote_->setText(
                tr("<i>Computed at %1 eV; every curve here re-folds the raw "
                   "transitions directly, so — unlike the Optics viewer's η "
                   "— this can be lowered as well as raised. The x, y, z "
                   "curves re-fold only if this job recorded their own "
                   "transitions (%2).</i>")
                    .arg(fwhmStored_, 0, 'g', 3)
                    .arg(hasPolarizationSticks_ ? tr("it does")
                                                : tr("this one does not")));
    }

    refreshPlot();
    return true;
}

std::vector<double> XasResultsWindow::foldSticks(
    const std::vector<double>& grid, const std::vector<double>& stickEnergy,
    const std::vector<double>& stickIntensity, double fwhm)
{
    std::vector<double> out(grid.size(), 0.0);
    if (fwhm <= 0.0)
        return out;
    // alpha = 4 ln(2) / fwhm^2, matching gpaw.xas.XAS.constant_broadening()
    // exactly (~/Codes/gpaw/gpaw/xas.py) — each transition folds to a
    // normalized Gaussian of that width, summed over every transition.
    const double alpha = 4.0 * std::log(2.0) / (fwhm * fwhm);
    const double prefactor = std::sqrt(alpha / M_PI);
    const std::size_t n = std::min(stickEnergy.size(), stickIntensity.size());
    for (std::size_t s = 0; s < n; ++s) {
        const double eps = stickEnergy[s];
        const double intensity = stickIntensity[s];
        if (intensity == 0.0)
            continue;
        for (std::size_t i = 0; i < grid.size(); ++i) {
            const double dx = grid[i] - eps;
            const double x = std::clamp(-alpha * dx * dx, -100.0, 100.0);
            out[i] += intensity * prefactor * std::exp(x);
        }
    }
    return out;
}

void XasResultsWindow::refreshPlot()
{
    if (!plot_)
        return;
    // Re-fold from the raw sticks only once the control has actually moved
    // away from the stored width — at the stored width, showing the
    // GPAW-computed curve byte-for-byte (rather than this window's own
    // re-derivation of it) is what keeps a run whose broadening is never
    // touched displaying exactly the numbers it computed.
    const bool rebroaden = std::abs(broadening_ - fwhmStored_) > 1e-9
        && !stickEnergy_.empty();

    const std::vector<double> isotropic =
        rebroaden ? foldSticks(energyGrid_, stickEnergy_, stickIsotropic_, broadening_)
                  : toVector(data_.value(QStringLiteral("isotropic")));

    std::vector<QPair<QString, std::vector<double>>> series;
    series.push_back({tr("Isotropic"), isotropic});

    if (polarizationCheck_->isChecked()) {
        const bool rebroadenPol = rebroaden && hasPolarizationSticks_;
        const std::vector<double> x = rebroadenPol
            ? foldSticks(energyGrid_, stickEnergy_, stickX_, broadening_)
            : toVector(data_.value(QStringLiteral("polarization_x")));
        const std::vector<double> y = rebroadenPol
            ? foldSticks(energyGrid_, stickEnergy_, stickY_, broadening_)
            : toVector(data_.value(QStringLiteral("polarization_y")));
        const std::vector<double> z = rebroadenPol
            ? foldSticks(energyGrid_, stickEnergy_, stickZ_, broadening_)
            : toVector(data_.value(QStringLiteral("polarization_z")));
        series.push_back({tr("x"), x});
        series.push_back({tr("y"), y});
        series.push_back({tr("z"), z});
    }
    if (sticksCheck_->isChecked()) {
        // The sticks live on their own energies, so they are resampled onto
        // the spectrum's grid as spikes rather than plotted against the wrong
        // axis. Nearest-bin placement: the grid is far finer than the
        // transition spacing, so nothing merges that was not already merged.
        // Always the RAW stored sticks, regardless of the broadening
        // control: these mark where the lines themselves are, not the
        // folded curve.
        std::vector<double> spikes(energyGrid_.size(), 0.0);
        if (energyGrid_.size() > 1) {
            const double lo = energyGrid_.front();
            const double hi = energyGrid_.back();
            const double span = hi - lo;
            for (std::size_t i = 0; i < stickEnergy_.size() && i < stickIsotropic_.size(); ++i) {
                if (span <= 0.0)
                    break;
                const auto bin = static_cast<std::size_t>(
                    std::clamp((stickEnergy_[i] - lo) / span, 0.0, 1.0)
                    * static_cast<double>(energyGrid_.size() - 1));
                spikes[bin] = std::max(spikes[bin], stickIsotropic_[i]);
            }
        }
        series.push_back({tr("Transitions"), std::move(spikes)});
    }

    plot_->setStyle(style_);
    plot_->setXRange(xMinSpin_ ? xMinSpin_->value() : 0.0,
                     xMaxSpin_ ? xMaxSpin_->value() : 0.0);
    plot_->setSeries(energyGrid_, series, tr("Energy (eV)"),
                     tr("Absorption (arb. units)"));
}

void XasResultsWindow::customizeAppearance()
{
    auto* dialog = new OpticsPlotStyleDialog(style_, this);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    // Live, not on-accept: the point of a styling dialog is to judge the
    // result against the real plot.
    connect(dialog, &OpticsPlotStyleDialog::styleChanged, this,
            [this](const OpticsPlotStyle& style) {
                style_ = style;
                plot_->setStyle(style_);
            });
    dialog->show();
}

void XasResultsWindow::exportCsv()
{
    if (energyGrid_.empty()) {
        QMessageBox::information(this, tr("Export CSV"),
                                 tr("No XAS data was loaded from this job."));
        return;
    }
    const QString element = data_.value(QStringLiteral("element")).toString();
    const QString path = QFileDialog::getSaveFileName(
        this, tr("Export XAS Data"),
        QStringLiteral("xas_%1.csv").arg(element.isEmpty() ? tr("spectrum") : element),
        tr("CSV (*.csv)"));
    if (path.isEmpty())
        return;

    // As-computed by default: exporting the STORED curve (not a live re-fold)
    // is what "as computed" means, and it is also what stays correct for a
    // job with no per-polarization sticks. The raw (unbroadened) sticks are
    // offered as a second, explicit choice rather than silently substituted
    // in — a sum of delta functions is not a spectrum a plotting tool can do
    // much with, so it is its own file.
    QMessageBox choice(this);
    choice.setWindowTitle(tr("Export XAS Data"));
    choice.setText(tr("Which data should the CSV contain?"));
    QPushButton* computedButton =
        choice.addButton(tr("As computed (broadened)"), QMessageBox::AcceptRole);
    QPushButton* rawButton = stickEnergy_.empty()
        ? nullptr
        : choice.addButton(tr("Raw transitions (unbroadened)"),
                           QMessageBox::ActionRole);
    QPushButton* cancelButton = choice.addButton(QMessageBox::Cancel);
    choice.setDefaultButton(computedButton);
    choice.exec();
    if (choice.clickedButton() == nullptr || choice.clickedButton() == cancelButton)
        return;
    const bool exportRaw = rawButton && choice.clickedButton() == rawButton;

    if (exportRaw) {
        writeTextFile(this, path, [&](QTextStream& out) {
            out << "energy_eV,isotropic";
            if (hasPolarizationSticks_)
                out << ",polarization_x,polarization_y,polarization_z";
            out << '\n';
            for (std::size_t i = 0; i < stickEnergy_.size(); ++i) {
                out << QString::number(stickEnergy_[i], 'f', 6) << ','
                    << QString::number(i < stickIsotropic_.size() ? stickIsotropic_[i] : 0.0,
                                       'g', 8);
                if (hasPolarizationSticks_)
                    out << ',' << QString::number(stickX_[i], 'g', 8) << ','
                        << QString::number(stickY_[i], 'g', 8) << ','
                        << QString::number(stickZ_[i], 'g', 8);
                out << '\n';
            }
        });
        return;
    }

    const std::vector<double> isotropic =
        toVector(data_.value(QStringLiteral("isotropic")));
    const std::vector<double> px = toVector(data_.value(QStringLiteral("polarization_x")));
    const std::vector<double> py = toVector(data_.value(QStringLiteral("polarization_y")));
    const std::vector<double> pz = toVector(data_.value(QStringLiteral("polarization_z")));
    writeTextFile(this, path, [&](QTextStream& out) {
        out << "Energy (eV),Isotropic (arb. units),x (arb. units),"
               "y (arb. units),z (arb. units)\n";
        const auto at = [](const std::vector<double>& v, std::size_t i) {
            return i < v.size() ? v[i] : 0.0;
        };
        for (std::size_t i = 0; i < energyGrid_.size(); ++i) {
            out << QString::number(energyGrid_[i], 'f', 6) << ','
                << QString::number(at(isotropic, i), 'g', 8) << ','
                << QString::number(at(px, i), 'g', 8) << ','
                << QString::number(at(py, i), 'g', 8) << ','
                << QString::number(at(pz, i), 'g', 8) << '\n';
        }
    });
}

void XasResultsWindow::exportImage()
{
    if (!plot_)
        return;
    const QString path = QFileDialog::getSaveFileName(
        this, tr("Export Image"), QStringLiteral("xas.png"),
        tr("PNG image (*.png);;JPEG image (*.jpg *.jpeg)"));
    if (path.isEmpty())
        return;

    savePlotImage(this, path, plot_->size(),
                  [this](QPainter& painter, const QSize& logical) {
                      plot_->renderTo(painter, logical);
                  });
}

} // namespace calango::gui
