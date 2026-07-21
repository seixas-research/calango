#include "gui/BandPdosWindow.hpp"

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
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QSaveFile>
#include <QTextStream>
#include <QVBoxLayout>

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

BandPdosWindow::BandPdosWindow(const QString& directory, QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle(tr("Band Structure / PDOS — %1").arg(directory));
    resize(980, 620);

    auto* layout = new QHBoxLayout(this);
    view_ = new BandPdosView(this);
    layout->addWidget(view_, 1);

    auto* side = new QVBoxLayout;
    layout->addLayout(side);
    auto* form = new QFormLayout;
    side->addLayout(form);

    fermiSpin_ = new QDoubleSpinBox(this);
    fermiSpin_->setRange(-1000.0, 1000.0);
    fermiSpin_->setDecimals(4);
    fermiSpin_->setSuffix(QStringLiteral(" eV"));
    fermiSpin_->setToolTip(tr("Reference energy — plots show E − E_ref, "
                              "with the dashed line at zero"));
    form->addRow(tr("Fermi level:"), fermiSpin_);

    minSpin_ = new QDoubleSpinBox(this);
    minSpin_->setRange(-100.0, 0.0);
    minSpin_->setValue(-10.0);
    minSpin_->setSuffix(QStringLiteral(" eV"));
    form->addRow(tr("E min:"), minSpin_);
    maxSpin_ = new QDoubleSpinBox(this);
    maxSpin_->setRange(0.0, 100.0);
    maxSpin_->setValue(10.0);
    maxSpin_->setSuffix(QStringLiteral(" eV"));
    form->addRow(tr("E max:"), maxSpin_);

    side->addWidget(new QLabel(tr("Projections:"), this));
    projectionList_ = new QListWidget(this);
    projectionList_->setMaximumWidth(190);
    side->addWidget(projectionList_, 1);

    auto* exportBandsButton = new QPushButton(tr("Export Bands…"), this);
    auto* exportPdosButton = new QPushButton(tr("Export PDOS…"), this);
    side->addWidget(exportBandsButton);
    side->addWidget(exportPdosButton);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    side->addWidget(buttons);

    connect(fermiSpin_, &QDoubleSpinBox::valueChanged, this,
            [this](double value) { view_->setReference(value); });
    const auto applyWindow = [this] {
        view_->setEnergyWindow(minSpin_->value(), maxSpin_->value());
    };
    connect(minSpin_, &QDoubleSpinBox::valueChanged, this, applyWindow);
    connect(maxSpin_, &QDoubleSpinBox::valueChanged, this, applyWindow);
    connect(projectionList_, &QListWidget::itemChanged, this,
            [this](QListWidgetItem* item) {
                view_->setProjectionVisible(item->text(),
                                            item->checkState() == Qt::Checked);
            });
    connect(exportBandsButton, &QPushButton::clicked,
            this, &BandPdosWindow::exportBands);
    connect(exportPdosButton, &QPushButton::clicked,
            this, &BandPdosWindow::exportPdos);

    loadDirectory(directory);
}

void BandPdosWindow::loadDirectory(const QString& directory)
{
    const QJsonObject bands = readJson(directory + QStringLiteral("/bands.json"));
    if (bands.isEmpty())
        return;

    BandPdosView::BandData data;
    data.x = toVector(bands[QStringLiteral("x")].toArray());
    data.specialX = toVector(bands[QStringLiteral("special_x")].toArray());
    for (const auto& label : bands[QStringLiteral("special_labels")].toArray())
        data.specialLabels << label.toString();
    data.efermi = bands[QStringLiteral("efermi")].toDouble();
    for (const auto& spin : bands[QStringLiteral("energies")].toArray()) {
        std::vector<std::vector<double>> kpts;
        for (const auto& kpt : spin.toArray())
            kpts.push_back(toVector(kpt.toArray()));
        data.energies.push_back(std::move(kpts));
    }
    fermiSpin_->setValue(data.efermi);
    view_->setBandData(std::move(data));
    view_->setEnergyWindow(minSpin_->value(), maxSpin_->value());
    hasData_ = true;

    const QJsonObject pdos = readJson(directory + QStringLiteral("/pdos.json"));
    if (!pdos.isEmpty()) {
        BandPdosView::PdosData pdosData;
        pdosData.energies = toVector(pdos[QStringLiteral("energies")].toArray());
        const QJsonObject projections =
            pdos[QStringLiteral("projections")].toObject();
        for (auto it = projections.begin(); it != projections.end(); ++it)
            pdosData.projections.emplace_back(it.key(),
                                              toVector(it.value().toArray()));
        int index = 0;
        for (const auto& [label, curve] : pdosData.projections) {
            (void)curve;
            auto* item = new QListWidgetItem(label, projectionList_);
            item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
            item->setCheckState(Qt::Checked);
            item->setForeground(BandPdosView::projectionColor(index++));
        }
        view_->setPdosData(std::move(pdosData));
    } else {
        projectionList_->addItem(tr("(no PDOS in this run)"));
        projectionList_->setEnabled(false);
    }
}

void BandPdosWindow::exportBands()
{
    const auto& data = view_->bandData();
    if (!data.valid())
        return;
    const QString path = QFileDialog::getSaveFileName(
        this, tr("Export Band Structure"), QStringLiteral("bands.csv"),
        tr("CSV (*.csv);;Gnuplot data (*.dat)"));
    if (path.isEmpty())
        return;
    const QChar sep = path.endsWith(QLatin1String(".dat")) ? QChar(' ')
                                                           : QChar(',');
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QMessageBox::warning(this, windowTitle(),
                             tr("Could not write %1").arg(path));
        return;
    }
    QTextStream out(&file);
    out << "k_distance";
    const std::size_t bandCount =
        data.energies.front().empty() ? 0 : data.energies.front().front().size();
    for (std::size_t s = 0; s < data.energies.size(); ++s)
        for (std::size_t b = 0; b < bandCount; ++b)
            out << sep
                << QStringLiteral("band_%1%2").arg(b + 1).arg(
                       data.energies.size() > 1
                           ? QStringLiteral("_spin%1").arg(s + 1)
                           : QString());
    out << "\n";
    for (std::size_t k = 0; k < data.x.size(); ++k) {
        out << data.x[k];
        for (const auto& spin : data.energies)
            for (std::size_t b = 0; b < bandCount; ++b)
                out << sep << spin[k][b];
        out << "\n";
    }
    file.commit();
}

void BandPdosWindow::exportPdos()
{
    const auto& data = view_->pdosData();
    if (!data.valid()) {
        QMessageBox::information(this, windowTitle(),
                                 tr("This run has no PDOS data."));
        return;
    }
    const QString path = QFileDialog::getSaveFileName(
        this, tr("Export PDOS"), QStringLiteral("pdos.csv"),
        tr("CSV (*.csv);;Gnuplot data (*.dat)"));
    if (path.isEmpty())
        return;
    const QChar sep = path.endsWith(QLatin1String(".dat")) ? QChar(' ')
                                                           : QChar(',');
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QMessageBox::warning(this, windowTitle(),
                             tr("Could not write %1").arg(path));
        return;
    }
    QTextStream out(&file);
    out << "energy_eV";
    for (const auto& [label, curve] : data.projections) {
        (void)curve;
        out << sep << QString(label).replace(QLatin1Char(' '), QLatin1Char('_'));
    }
    out << "\n";
    for (std::size_t i = 0; i < data.energies.size(); ++i) {
        out << data.energies[i];
        for (const auto& [label, curve] : data.projections)
            out << sep << (i < curve.size() ? curve[i] : 0.0);
        out << "\n";
    }
    file.commit();
}

} // namespace calango::gui
