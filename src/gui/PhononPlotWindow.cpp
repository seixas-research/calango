#include "gui/PhononPlotWindow.hpp"

#include "gui/BandPdosView.hpp"

#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFile>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>

namespace calango::gui {

namespace {

QJsonObject readJson(const QString& path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return {};
    return QJsonDocument::fromJson(file.readAll()).object();
}

std::vector<double> toVector(const QJsonArray& array)
{
    std::vector<double> values;
    values.reserve(static_cast<std::size_t>(array.size()));
    for (const auto& value : array)
        values.push_back(value.toDouble());
    return values;
}

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
        readJson(directory + QStringLiteral("/phonon_band.json"));
    if (band.isEmpty())
        return;

    BandPdosView::BandData data;
    data.x = toVector(band[QStringLiteral("x")].toArray());
    data.specialX = toVector(band[QStringLiteral("special_x")].toArray());
    for (const auto& label : band[QStringLiteral("special_labels")].toArray())
        data.specialLabels << label.toString();
    data.efermi = 0.0;
    // frequencies[kpoint][mode] -> single "spin" channel of energies.
    std::vector<std::vector<double>> kpts;
    double maxFreq = 0.0;
    double minFreq = 0.0;
    for (const auto& row : band[QStringLiteral("frequencies")].toArray()) {
        std::vector<double> modes = toVector(row.toArray());
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
        readJson(directory + QStringLiteral("/phonon_dos.json"));
    if (!dos.isEmpty()) {
        BandPdosView::PdosData pdosData;
        pdosData.energies = toVector(dos[QStringLiteral("frequencies")].toArray());
        pdosData.projections.emplace_back(
            tr("PhDOS"), toVector(dos[QStringLiteral("dos")].toArray()));
        view_->setPdosData(std::move(pdosData));
    }
}

} // namespace calango::gui
