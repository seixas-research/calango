#include "gui/PhononPlotWindow.hpp"
#include "gui/GuiUtils.hpp"

#include "gui/BandPdosView.hpp"

#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFile>
#include <QFileDialog>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QPushButton>
#include <QTextStream>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>

namespace calango::gui {

namespace {

} // namespace

PhononPlotWindow::PhononPlotWindow(const QString& directory, QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle(tr("Phonon Band Structure / PhDOS — %1").arg(directory));
    resize(980, 620);

    auto* layout = new QHBoxLayout(this);
    view_ = new BandPdosView(this);
    view_->setPhononMode(true);
    layout->addWidget(view_, 1);

    auto* side = new QVBoxLayout;
    layout->addLayout(side);
    auto* form = new QFormLayout;
    side->addLayout(form);

    minSpin_ = new QDoubleSpinBox(this);
    minSpin_->setRange(-2000.0, 0.0);
    minSpin_->setDecimals(0);
    minSpin_->setValue(-50.0);
    minSpin_->setSuffix(QStringLiteral(" cm⁻¹"));
    minSpin_->setToolTip(tr("Lower frequency shown (slightly negative reveals "
                            "the acoustic branches and any imaginary modes)."));
    form->addRow(tr("ω min:"), minSpin_);

    maxSpin_ = new QDoubleSpinBox(this);
    maxSpin_->setRange(0.0, 100000.0);
    maxSpin_->setDecimals(0);
    maxSpin_->setValue(1600.0);
    maxSpin_->setSuffix(QStringLiteral(" cm⁻¹"));
    form->addRow(tr("ω max:"), maxSpin_);

    side->addWidget(new QLabel(tr("3 acoustic branches vanish at Γ;\n"
                                  "the dashed line marks ω = 0."),
                               this));
    side->addStretch(1);

    auto* exportButton = new QPushButton(tr("Export Data (.csv)"), this);
    connect(exportButton, &QPushButton::clicked, this, &PhononPlotWindow::exportCsv);
    side->addWidget(exportButton);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    side->addWidget(buttons);

    const auto applyWindow = [this] {
        view_->setEnergyWindow(minSpin_->value(), maxSpin_->value());
    };
    connect(minSpin_, &QDoubleSpinBox::valueChanged, this, applyWindow);
    connect(maxSpin_, &QDoubleSpinBox::valueChanged, this, applyWindow);

    loadDirectory(directory);
}

void PhononPlotWindow::loadDirectory(const QString& directory)
{
    const QJsonObject band =
        readJsonObject(directory + QStringLiteral("/phonon_band.json"));
    if (band.isEmpty())
        return;

    BandPdosView::BandData data;
    data.x = toDoubleVector(band[QStringLiteral("x")].toArray());
    data.specialX = toDoubleVector(band[QStringLiteral("special_x")].toArray());
    for (const auto& label : band[QStringLiteral("special_labels")].toArray())
        data.specialLabels << label.toString();
    data.efermi = 0.0;
    // frequencies[kpoint][mode] -> single "spin" channel of energies.
    std::vector<std::vector<double>> kpts;
    double maxFreq = 0.0;
    double minFreq = 0.0;
    for (const auto& row : band[QStringLiteral("frequencies")].toArray()) {
        std::vector<double> modes = toDoubleVector(row.toArray());
        for (double f : modes) {
            maxFreq = std::max(maxFreq, f);
            minFreq = std::min(minFreq, f);
        }
        kpts.push_back(std::move(modes));
    }
    data.energies.push_back(std::move(kpts));
    view_->setBandData(std::move(data));
    hasData_ = true;

    // Auto-fit the frequency window (a little headroom, and negative floor so
    // the acoustic modes at ω = 0 and any imaginary modes are visible).
    maxSpin_->setValue(std::ceil((maxFreq * 1.08 + 50.0) / 50.0) * 50.0);
    minSpin_->setValue(std::min(-50.0, std::floor(minFreq * 1.1 / 50.0) * 50.0));
    view_->setEnergyWindow(minSpin_->value(), maxSpin_->value());

    const QJsonObject dos =
        readJsonObject(directory + QStringLiteral("/phonon_dos.json"));
    if (!dos.isEmpty()) {
        BandPdosView::PdosData pdosData;
        pdosData.energies = toDoubleVector(dos[QStringLiteral("frequencies")].toArray());
        pdosData.projections.emplace_back(
            tr("PhDOS"), toDoubleVector(dos[QStringLiteral("dos")].toArray()));
        view_->setPdosData(std::move(pdosData));
    }
}

void PhononPlotWindow::exportCsv()
{
    const BandPdosView::BandData& band = view_->bandData();
    const BandPdosView::PdosData& dos = view_->pdosData();
    if (!band.valid() && !dos.valid())
        return;

    const QString path = QFileDialog::getSaveFileName(
        this, tr("Export Phonon Data"), QStringLiteral("phonon_data.csv"),
        tr("CSV (*.csv)"));
    if (path.isEmpty())
        return;
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text))
        return;
    QTextStream out(&file);

    // --- Band structure: k-distance + one column per phonon branch ---------
    if (band.valid() && !band.energies.empty() && !band.energies.front().empty()) {
        const auto& kpts = band.energies.front(); // single (non-spin) channel
        const std::size_t branches = kpts.front().size();
        out << "# Phonon band structure (frequency in cm^-1)\n";
        // High-symmetry point labels and their k-path positions.
        out << "# high_symmetry_points:";
        for (int i = 0; i < band.specialLabels.size()
                 && static_cast<std::size_t>(i) < band.specialX.size(); ++i) {
            QString label = band.specialLabels[i];
            if (label == QLatin1String("G"))
                label = QStringLiteral("Gamma");
            out << ' ' << label << '@'
                << QString::number(band.specialX[static_cast<std::size_t>(i)], 'f', 6);
        }
        out << '\n';
        out << "k_distance";
        for (std::size_t b = 0; b < branches; ++b)
            out << ",branch_" << (b + 1) << "_cm1";
        out << '\n';
        for (std::size_t k = 0; k < kpts.size() && k < band.x.size(); ++k) {
            out << QString::number(band.x[k], 'f', 6);
            for (std::size_t b = 0; b < branches && b < kpts[k].size(); ++b)
                out << ',' << QString::number(kpts[k][b], 'f', 4);
            out << '\n';
        }
    }

    // --- Phonon DOS: frequency + intensity ---------------------------------
    if (dos.valid()) {
        out << "\n# Phonon density of states\n";
        out << "frequency_cm1";
        for (const auto& [label, curve] : dos.projections) {
            (void)curve;
            out << ',' << QString(label).replace(QLatin1Char(' '), QLatin1Char('_'));
        }
        out << '\n';
        for (std::size_t i = 0; i < dos.energies.size(); ++i) {
            out << QString::number(dos.energies[i], 'f', 4);
            for (const auto& [label, curve] : dos.projections)
                out << ',' << QString::number(i < curve.size() ? curve[i] : 0.0, 'f', 6);
            out << '\n';
        }
    }
}

} // namespace calango::gui
