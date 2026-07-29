#include "gui/XrdDialog.hpp"

#include "gui/LinePlotWidget.hpp"

#include <QDialogButtonBox>
#include <QFile>
#include <QFileDialog>
#include <QFormLayout>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QMessageBox>
#include <QTextStream>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>

namespace calango::gui {

namespace {

struct WavelengthPreset {
    const char* label;
    double angstrom; ///< 0 = custom
};

constexpr WavelengthPreset kWavelengths[] = {
    {"Cu Kα (1.54056 Å)", 1.54056},
    {"Co Kα (1.78897 Å)", 1.78897},
    {"Mo Kα (0.70930 Å)", 0.70930},
    {"Cr Kα (2.28970 Å)", 2.28970},
    {"Custom", 0.0},
};

} // namespace

XrdDialog::XrdDialog(std::shared_ptr<const core::Structure> structure,
                     QWidget* parent)
    : QDialog(parent)
    , structure_(std::move(structure))
    , wavelengthCombo_(new QComboBox(this))
    , wavelengthSpin_(new QDoubleSpinBox(this))
    , thetaMinSpin_(new QDoubleSpinBox(this))
    , thetaMaxSpin_(new QDoubleSpinBox(this))
    , pointsSpin_(new QSpinBox(this))
    , repeatSpin_(new QSpinBox(this))
    , statusLabel_(new QLabel(this))
    , computeButton_(new QPushButton(tr("Simulate"), this))
    , exportCurveButton_(new QPushButton(tr("Export Curve…"), this))
    , exportPeaksButton_(new QPushButton(tr("Export Peaks…"), this))
    , plot_(new LinePlotWidget(this))
{
    setWindowTitle(tr("X-Ray Diffraction Simulation"));
    resize(780, 560);

    for (const WavelengthPreset& preset : kWavelengths)
        wavelengthCombo_->addItem(QString::fromUtf8(preset.label), preset.angstrom);

    wavelengthSpin_->setRange(0.1, 5.0);
    wavelengthSpin_->setDecimals(5);
    wavelengthSpin_->setValue(1.54056);
    wavelengthSpin_->setSuffix(tr(" Å"));
    wavelengthSpin_->setEnabled(false);
    connect(wavelengthCombo_, &QComboBox::currentIndexChanged, this, [this](int index) {
        const double preset = wavelengthCombo_->itemData(index).toDouble();
        wavelengthSpin_->setEnabled(preset == 0.0);
        if (preset > 0.0)
            wavelengthSpin_->setValue(preset);
    });

    thetaMinSpin_->setRange(1.0, 170.0);
    thetaMinSpin_->setValue(10.0);
    thetaMinSpin_->setSuffix(tr(" °"));
    thetaMaxSpin_->setRange(2.0, 175.0);
    thetaMaxSpin_->setValue(90.0);
    thetaMaxSpin_->setSuffix(tr(" °"));

    pointsSpin_->setRange(100, 4000);
    pointsSpin_->setValue(800);

    const bool periodic = structure_->cell().isDefined();
    repeatSpin_->setRange(1, 6);
    repeatSpin_->setValue(periodic ? 3 : 1);
    repeatSpin_->setEnabled(periodic);
    repeatSpin_->setToolTip(
        tr("Supercell repetition along the periodic directions before the "
           "Debye sum.\nMore repeats = sharper Bragg peaks, but the cost "
           "grows as N² in atoms."));

    statusLabel_->setWordWrap(true);

    auto* form = new QFormLayout;
    form->addRow(tr("Wavelength:"), wavelengthCombo_);
    form->addRow(tr("Custom λ:"), wavelengthSpin_);
    auto* thetaRow = new QHBoxLayout;
    thetaRow->addWidget(thetaMinSpin_, 1);
    thetaRow->addWidget(thetaMaxSpin_, 1);
    form->addRow(tr("2θ range:"), thetaRow);
    form->addRow(tr("Points:"), pointsSpin_);
    form->addRow(tr("Supercell repeat:"), repeatSpin_);
    auto* buttonRow = new QHBoxLayout;
    buttonRow->addWidget(computeButton_, 1);
    buttonRow->addWidget(exportCurveButton_, 1);
    buttonRow->addWidget(exportPeaksButton_, 1);
    form->addRow(buttonRow);
    form->addRow(statusLabel_);
    exportCurveButton_->setEnabled(false);
    exportPeaksButton_->setEnabled(false);

    plot_->setAxisLabels(QStringLiteral("2θ [°]"), tr("intensity [arb. u.]"));

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    auto* layout = new QVBoxLayout(this);
    layout->addLayout(form);
    layout->addWidget(plot_, 1);
    layout->addWidget(buttons);

    connect(computeButton_, &QPushButton::clicked, this, &XrdDialog::compute);
    connect(exportCurveButton_, &QPushButton::clicked, this, &XrdDialog::exportCurve);
    connect(exportPeaksButton_, &QPushButton::clicked, this, &XrdDialog::exportPeaks);
}

double XrdDialog::wavelength() const
{
    return wavelengthSpin_->value();
}

void XrdDialog::compute()
{
    const double thetaMin = thetaMinSpin_->value();
    const double thetaMax = std::max(thetaMaxSpin_->value(), thetaMin + 1.0);

    // Embedded Python runs on the GUI thread — busy cursor instead of a
    // worker (the Debye sum is numpy-vectorized and typically seconds).
    computeButton_->setEnabled(false);
    statusLabel_->setText(tr("Simulating…"));
    QGuiApplication::setOverrideCursor(Qt::WaitCursor);
    QGuiApplication::processEvents();
    try {
        lastResult_ = pybridge::AseBridge::simulateXrd(
            *structure_, wavelength(), thetaMin, thetaMax, pointsSpin_->value(),
            repeatSpin_->value());
        plot_->setData(lastResult_.twoTheta, lastResult_.intensity);
        statusLabel_->setStyleSheet(QString());
        statusLabel_->setText(
            lastResult_.method == "Iwasa"
                ? tr("Waasmaier–Kirfel form factors (all species tabulated).")
                : tr("Constant f = Z form factors (species outside the "
                     "Waasmaier–Kirfel table): peak positions exact, "
                     "high-angle intensities approximate."));
    } catch (const std::exception& e) {
        statusLabel_->setStyleSheet(QStringLiteral("color: #d9534f;"));
        statusLabel_->setText(QString::fromUtf8(e.what()));
        lastResult_ = {};
    }
    QGuiApplication::restoreOverrideCursor();
    computeButton_->setEnabled(true);
    exportCurveButton_->setEnabled(!lastResult_.twoTheta.empty());
    exportPeaksButton_->setEnabled(!lastResult_.twoTheta.empty());
}

void XrdDialog::exportCurve()
{
    if (lastResult_.twoTheta.empty())
        return;
    const QString path = QFileDialog::getSaveFileName(
        this, tr("Export XRD Curve"), QStringLiteral("xrd_pattern.csv"),
        tr("CSV (*.csv);;Data file (*.dat)"));
    if (path.isEmpty())
        return;
    const bool csv = !path.endsWith(QStringLiteral(".dat"), Qt::CaseInsensitive);

    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QMessageBox::critical(this, tr("Export XRD Curve"),
                              tr("Could not write %1").arg(path));
        return;
    }
    QTextStream out(&file);
    // '#' comments only in the .dat flavour — a CSV starts with its header.
    if (!csv)
        out << "# Calango simulated XRD (Debye), lambda "
            << QString::number(wavelength(), 'f', 5) << " A, form factors "
            << QString::fromStdString(lastResult_.method) << "\n";
    if (csv) {
        out << "two_theta_deg,intensity\n";
        for (std::size_t i = 0; i < lastResult_.twoTheta.size(); ++i)
            out << QString::number(lastResult_.twoTheta[i], 'f', 4) << ','
                << QString::number(lastResult_.intensity[i], 'g', 8) << '\n';
    } else {
        for (std::size_t i = 0; i < lastResult_.twoTheta.size(); ++i)
            out << QString::asprintf("%12.4f %16.8g\n", lastResult_.twoTheta[i],
                                     lastResult_.intensity[i]);
    }
}

void XrdDialog::exportPeaks()
{
    if (lastResult_.twoTheta.size() < 3)
        return;

    // Local maxima above 2% of the strongest intensity, with the Bragg
    // d-spacing d = λ / (2 sin θ).
    const double top =
        *std::max_element(lastResult_.intensity.begin(), lastResult_.intensity.end());
    const double threshold = 0.02 * top;
    struct Peak {
        double twoTheta, intensity, d;
    };
    std::vector<Peak> peaks;
    for (std::size_t i = 1; i + 1 < lastResult_.twoTheta.size(); ++i) {
        const double y = lastResult_.intensity[i];
        if (y < threshold || y <= lastResult_.intensity[i - 1]
            || y < lastResult_.intensity[i + 1])
            continue;
        const double theta = lastResult_.twoTheta[i] / 2.0 * M_PI / 180.0;
        peaks.push_back({lastResult_.twoTheta[i], y,
                         wavelength() / (2.0 * std::sin(theta))});
    }
    if (peaks.empty()) {
        QMessageBox::information(this, tr("Export XRD Peaks"),
                                 tr("No peaks above the 2%% threshold."));
        return;
    }

    const QString path = QFileDialog::getSaveFileName(
        this, tr("Export XRD Peaks"), QStringLiteral("xrd_peaks.csv"),
        tr("CSV (*.csv);;Data file (*.dat)"));
    if (path.isEmpty())
        return;
    const bool csv = !path.endsWith(QStringLiteral(".dat"), Qt::CaseInsensitive);

    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QMessageBox::critical(this, tr("Export XRD Peaks"),
                              tr("Could not write %1").arg(path));
        return;
    }
    QTextStream out(&file);
    // '#' comments only in the .dat flavour — a CSV starts with its header.
    if (!csv)
        out << "# Calango simulated XRD peaks, lambda "
            << QString::number(wavelength(), 'f', 5) << " A\n";
    if (csv) {
        out << "two_theta_deg,intensity,d_angstrom\n";
        for (const Peak& peak : peaks)
            out << QString::number(peak.twoTheta, 'f', 4) << ','
                << QString::number(peak.intensity, 'g', 8) << ','
                << QString::number(peak.d, 'f', 5) << '\n';
    } else {
        for (const Peak& peak : peaks)
            out << QString::asprintf("%12.4f %16.8g %12.5f\n", peak.twoTheta,
                                     peak.intensity, peak.d);
    }
}

} // namespace calango::gui
