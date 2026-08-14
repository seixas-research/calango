#include "gui/MolecularDynamicsViewer.hpp"

#include "core/Rdf.hpp"
#include "gui/GuiUtils.hpp"
#include "gui/PlotPalette.hpp"
#include "gui/ViewportWidget.hpp"
#include "python_bridge/AseBridge.hpp"

#include <QDialogButtonBox>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonObject>
#include <QLabel>
#include <QMessageBox>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QSlider>
#include <QSvgGenerator>
#include <QTabWidget>
#include <QTextStream>
#include <QTimer>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>
#include <limits>

namespace calango::gui {

/// A multi-series XY plot. Drawing lives in render(QPainter, QRectF) so the
/// widget, the PNG export and the SVG export are the same code — an export that
/// redraws through a second path silently drifts from what the user saw.
class SeriesPlotWidget : public QWidget {
public:
    struct Series {
        QString label;
        QColor color;
        std::vector<double> y;
    };

    explicit SeriesPlotWidget(QWidget* parent = nullptr) : QWidget(parent)
    {
        setMinimumSize(420, 260);
    }

    void setData(std::vector<double> x, std::vector<Series> series,
                 const QString& xLabel, const QString& yLabel)
    {
        x_ = std::move(x);
        series_ = std::move(series);
        xLabel_ = xLabel;
        yLabel_ = yLabel;
        update();
    }

    const std::vector<double>& x() const { return x_; }
    const std::vector<Series>& series() const { return series_; }

    void render(QPainter& painter, const QRectF& target) const;

protected:
    void paintEvent(QPaintEvent*) override
    {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.fillRect(rect(), PlotPalette::canvas);
        render(painter, QRectF(rect()));
    }

private:
    std::vector<double> x_;
    std::vector<Series> series_;
    QString xLabel_;
    QString yLabel_;
};

void SeriesPlotWidget::render(QPainter& painter, const QRectF& target) const
{
    if (x_.size() < 2 || series_.empty())
        return;
    const double scale = target.height() / 300.0;
    QFont font = painter.font();
    font.setPointSizeF(std::max(7.0, 9.0 * scale));
    painter.setFont(font);
    const QFontMetricsF metrics(font);

    const QRectF plot = target.adjusted(74.0 * scale, 22.0 * scale,
                                        -18.0 * scale, -40.0 * scale);
    if (plot.width() <= 1.0 || plot.height() <= 1.0)
        return;

    const double xMin = x_.front();
    double xMax = x_.back();
    if (xMax - xMin < 1e-12)
        xMax = xMin + 1.0;
    double yMin = std::numeric_limits<double>::max();
    double yMax = std::numeric_limits<double>::lowest();
    for (const Series& s : series_)
        for (const double v : s.y) {
            yMin = std::min(yMin, v);
            yMax = std::max(yMax, v);
        }
    if (!(yMax > yMin)) {
        yMin -= 1.0;
        yMax += 1.0;
    }
    // A little headroom so a flat series does not sit on the frame.
    const double pad = 0.06 * (yMax - yMin);
    yMin -= pad;
    yMax += pad;

    const auto xOf = [&](double v) {
        return plot.left() + (v - xMin) / (xMax - xMin) * plot.width();
    };
    const auto yOf = [&](double v) {
        return plot.bottom() - (v - yMin) / (yMax - yMin) * plot.height();
    };

    const QColor axisColor = PlotPalette::text;
    const QColor gridColor = PlotPalette::grid;
    painter.setPen(QPen(gridColor, 1.0));
    constexpr int kTicks = 5;
    for (int i = 0; i <= kTicks; ++i) {
        const double fx = plot.left() + plot.width() * i / kTicks;
        const double fy = plot.top() + plot.height() * i / kTicks;
        painter.drawLine(QPointF(fx, plot.top()), QPointF(fx, plot.bottom()));
        painter.drawLine(QPointF(plot.left(), fy), QPointF(plot.right(), fy));
    }
    painter.setPen(QPen(axisColor, 1.2 * scale));
    painter.drawRect(plot);

    for (const Series& s : series_) {
        QPainterPath path;
        for (std::size_t i = 0; i < x_.size() && i < s.y.size(); ++i) {
            const QPointF point(xOf(x_[i]), yOf(s.y[i]));
            if (i == 0)
                path.moveTo(point);
            else
                path.lineTo(point);
        }
        painter.setPen(QPen(s.color, 1.8 * scale));
        painter.drawPath(path);
    }

    painter.setPen(axisColor);
    for (int i = 0; i <= kTicks; ++i) {
        const double xv = xMin + (xMax - xMin) * i / kTicks;
        painter.drawText(QRectF(xOf(xv) - 34.0 * scale, plot.bottom() + 2.0 * scale,
                                68.0 * scale, metrics.height()),
                         Qt::AlignCenter, QString::number(xv, 'g', 4));
        const double yv = yMin + (yMax - yMin) * i / kTicks;
        painter.drawText(QRectF(target.left(), yOf(yv) - metrics.height() / 2.0,
                                plot.left() - target.left() - 4.0 * scale,
                                metrics.height()),
                         Qt::AlignRight | Qt::AlignVCenter,
                         QString::number(yv, 'g', 4));
    }
    painter.drawText(QRectF(plot.left(), target.bottom() - metrics.height() * 1.2,
                            plot.width(), metrics.height()),
                     Qt::AlignCenter, xLabel_);
    painter.save();
    painter.translate(target.left() + metrics.height() * 0.9, plot.center().y());
    painter.rotate(-90.0);
    painter.drawText(QRectF(-plot.height() / 2.0, -metrics.height(),
                            plot.height(), metrics.height() * 1.2),
                     Qt::AlignCenter, yLabel_);
    painter.restore();

    if (series_.size() > 1) {
        double legendY = plot.top() + 6.0 * scale;
        for (const Series& s : series_) {
            const double x = plot.left() + 10.0 * scale;
            painter.setPen(QPen(s.color, 2.5 * scale));
            painter.drawLine(QPointF(x, legendY + metrics.height() / 2.0),
                             QPointF(x + 22.0 * scale,
                                     legendY + metrics.height() / 2.0));
            painter.setPen(axisColor);
            painter.drawText(QRectF(x + 28.0 * scale, legendY, 200.0 * scale,
                                    metrics.height()),
                             Qt::AlignLeft | Qt::AlignVCenter, s.label);
            legendY += metrics.height() * 1.15;
        }
    }
}

// ---------------------------------------------------------------------------

MolecularDynamicsViewer::MolecularDynamicsViewer(QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle(tr("Molecular Dynamics Viewer"));
    resize(880, 640);
    buildUi();
}

void MolecularDynamicsViewer::buildUi()
{
    auto* layout = new QVBoxLayout(this);

    summaryLabel_ = new QLabel(this);
    summaryLabel_->setWordWrap(true);
    summaryLabel_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    layout->addWidget(summaryLabel_);

    tabs_ = new QTabWidget(this);
    temperaturePlot_ = new SeriesPlotWidget(tabs_);
    energyPlot_ = new SeriesPlotWidget(tabs_);
    pressurePlot_ = new SeriesPlotWidget(tabs_);
    rdfPlot_ = new SeriesPlotWidget(tabs_);
    tabs_->addTab(temperaturePlot_, tr("Temperature"));
    tabs_->addTab(energyPlot_, tr("Energy"));
    tabs_->addTab(pressurePlot_, tr("Pressure && Volume"));
    tabs_->addTab(rdfPlot_, tr("g(r)"));
    layout->addWidget(tabs_, 1);

    // No frame player here: the run's trajectory is opened by the host as a
    // workspace tab, and the main viewport's timeline (slider + play/pause)
    // scrubs it — one set of playback controls for every trajectory, rather
    // than a second slider in this dialog fighting the global one over the
    // same viewport.
    auto* playbackNote = new QLabel(
        tr("Trajectory playback: use the main viewport's timeline — this "
           "run's frames are open there as a scrubbable tab."),
        this);
    playbackNote->setWordWrap(true);
    layout->addWidget(playbackNote);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    auto* csvButton =
        buttons->addButton(tr("Export CSV…"), QDialogButtonBox::ActionRole);
    auto* imageButton =
        buttons->addButton(tr("Export Image…"), QDialogButtonBox::ActionRole);
    imageButton->setToolTip(
        tr("Exports the plot on the ACTIVE tab: PNG at 3x for print, or SVG "
           "vector art."));
    connect(csvButton, &QPushButton::clicked, this,
            &MolecularDynamicsViewer::exportCsv);
    connect(imageButton, &QPushButton::clicked, this,
            &MolecularDynamicsViewer::exportImage);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);
}

bool MolecularDynamicsViewer::loadDirectory(const QString& directory)
{
    directory_ = directory;
    const QJsonObject root =
        readJsonObject(directory + QStringLiteral("/metrics.json"));
    const QJsonArray metrics = root.value(QStringLiteral("metrics")).toArray();

    // The logger records a sparse set of named fields per sample, so a series
    // is only meaningful when the run actually logged it (pressure exists only
    // for a barostatted ensemble, volume only for a periodic cell).
    for (const auto& entry : metrics) {
        const QJsonObject sample = entry.toObject();
        // metrics.json carries the MD step; without the timestep in the file
        // the honest x-axis is the step number scaled to a nominal 1 fs, which
        // is what the label says.
        time_.push_back(sample.value(QStringLiteral("step")).toDouble() * 0.001);
        temperature_.push_back(
            sample.value(QStringLiteral("temperature")).toDouble());
        const double epot = sample.value(QStringLiteral("energy")).toDouble();
        const double ekin = sample.value(QStringLiteral("kinetic")).toDouble();
        potential_.push_back(epot);
        kinetic_.push_back(ekin);
        total_.push_back(epot + ekin);
        if (sample.contains(QStringLiteral("target_temperature")))
            targetTemperature_.push_back(
                sample.value(QStringLiteral("target_temperature")).toDouble());
        if (sample.contains(QStringLiteral("pressure")))
            pressure_.push_back(sample.value(QStringLiteral("pressure")).toDouble());
        if (sample.contains(QStringLiteral("volume")))
            volume_.push_back(sample.value(QStringLiteral("volume")).toDouble());
    }

    // -- Trajectory ---------------------------------------------------------
    for (const QString& name : {QStringLiteral("md.extxyz"),
                                QStringLiteral("md.traj"),
                                QStringLiteral("md_final.extxyz")}) {
        const QString path = directory + QLatin1Char('/') + name;
        if (!QFile::exists(path))
            continue;
        try {
            for (auto& frame :
                 pybridge::AseBridge::readTrajectory(path.toStdString()))
                frames_.push_back(
                    std::make_shared<core::Structure>(std::move(frame)));
        } catch (const std::exception&) {
            frames_.clear();
            continue;
        }
        if (!frames_.empty())
            break;
    }

    if (time_.empty() && frames_.empty())
        return false;

    // -- Plots ---------------------------------------------------------------
    if (!time_.empty()) {
        // An annealing run logged the setpoint it was aiming at when each
        // sample was taken. Drawing it under the measured trace is what turns
        // the plot into an answer: a thermostat that lags the ramp, or a
        // system whose latent heat stalls it at a plateau, is invisible
        // against the temperature alone.
        std::vector<SeriesPlotWidget::Series> thermal{
            {tr("T"), QColor(0xd6, 0x27, 0x28), temperature_}};
        if (targetTemperature_.size() == temperature_.size()
            && !targetTemperature_.empty())
            thermal.push_back(
                {tr("T target"), QColor(0x7a, 0x7f, 0x88), targetTemperature_});
        temperaturePlot_->setData(time_, thermal, tr("Time (ps)"),
                                  tr("Temperature (K)"));
        energyPlot_->setData(
            time_,
            {{tr("E_tot"), QColor(0x1b, 0x1e, 0x23), total_},
             {tr("E_pot"), PlotPalette::series, potential_},
             {tr("E_kin"), QColor(0xd6, 0x27, 0x28), kinetic_}},
            tr("Time (ps)"), tr("Energy (eV)"));

        std::vector<SeriesPlotWidget::Series> pv;
        if (!pressure_.empty())
            pv.push_back({tr("P (GPa)"), QColor(0x2c, 0xa0, 0x2c), pressure_});
        if (!volume_.empty()) {
            // Volume is ~10³ Å³ against a pressure of ~1 GPa; plotting the raw
            // pair would flatten the pressure onto the axis, so volume is shown
            // as its fractional change, which is what an NPT run is watched for.
            const double reference = volume_.front();
            std::vector<double> relative;
            relative.reserve(volume_.size());
            for (const double v : volume_)
                relative.push_back(reference > 0.0 ? 100.0 * (v / reference - 1.0)
                                                   : 0.0);
            pv.push_back({tr("ΔV/V₀ (%)"), QColor(0x94, 0x67, 0xbd), relative});
        }
        if (!pv.empty())
            pressurePlot_->setData(time_, pv, tr("Time (ps)"),
                                   tr("Pressure (GPa) / ΔV (%)"));
        else
            tabs_->setTabEnabled(2, false);
    }

    recomputeRdf();

    // -- Summary -------------------------------------------------------------
    double meanT = 0.0, rmsT = 0.0;
    statistics(temperature_, meanT, rmsT);
    QString summary =
        tr("%1 samples").arg(static_cast<int>(time_.size()));
    if (!temperature_.empty())
        summary += tr("   ·   T = %1 ± %2 K")
                       .arg(meanT, 0, 'f', 1)
                       .arg(rmsT, 0, 'f', 1);
    if (total_.size() > 1) {
        // Total-energy drift is the integrator health check: in NVE it should
        // be flat, and a steadily climbing E_tot means the timestep is too
        // large however sensible the temperature looks.
        const double drift = total_.back() - total_.front();
        summary += tr("   ·   E_tot drift = %1 eV over the run")
                       .arg(drift, 0, 'g', 3);
    }
    if (!frames_.empty())
        summary += tr("   ·   %1 frames").arg(static_cast<int>(frames_.size()));
    summaryLabel_->setText(summary);
    return true;
}

void MolecularDynamicsViewer::statistics(const std::vector<double>& values,
                                         double& mean, double& rms)
{
    mean = 0.0;
    rms = 0.0;
    if (values.empty())
        return;
    for (const double v : values)
        mean += v;
    mean /= static_cast<double>(values.size());
    for (const double v : values)
        rms += (v - mean) * (v - mean);
    rms = std::sqrt(rms / static_cast<double>(values.size()));
}

void MolecularDynamicsViewer::recomputeRdf()
{
    if (frames_.empty())
        return;
    // Computed from the LAST frame: the early part of a run is still
    // equilibrating, and a g(r) averaged over that would blur the structure
    // the run settled into.
    const auto& frame = frames_.back();
    if (!frame || frame->empty())
        return;
    core::RdfOptions options;
    options.rMax = 10.0;
    options.bins = 200;
    options.usePbc = frame->cell().isDefined();
    const core::RdfResult result = core::computeRdf(*frame, options);
    rdfPlot_->setData(result.r, {{tr("g(r)"), PlotPalette::series, result.g}},
                      tr("r (Å)"), tr("g(r)"));
}


void MolecularDynamicsViewer::exportCsv()
{
    const QString path = QFileDialog::getSaveFileName(
        this, tr("Export MD Time Series"),
        QStringLiteral("md_timeseries.csv"),
        tr("CSV files (*.csv);;All files (*)"));
    if (path.isEmpty())
        return;
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QMessageBox::warning(this, tr("Export MD Time Series"),
                             tr("Could not write %1").arg(path));
        return;
    }
    QTextStream out(&file);
    // A CSV starts with its header row — no '#' comment lines.
    out << "time_ps,temperature_K,E_pot_eV,E_kin_eV,E_tot_eV";
    if (!targetTemperature_.empty())
        out << ",target_temperature_K";
    if (!pressure_.empty())
        out << ",pressure_GPa";
    if (!volume_.empty())
        out << ",volume_A3";
    out << '\n';
    for (std::size_t i = 0; i < time_.size(); ++i) {
        out << time_[i] << ',' << temperature_[i] << ',' << potential_[i] << ','
            << kinetic_[i] << ',' << total_[i];
        if (!targetTemperature_.empty())
            out << ','
                << (i < targetTemperature_.size() ? targetTemperature_[i] : 0.0);
        if (!pressure_.empty())
            out << ',' << (i < pressure_.size() ? pressure_[i] : 0.0);
        if (!volume_.empty())
            out << ',' << (i < volume_.size() ? volume_[i] : 0.0);
        out << '\n';
    }
}

void MolecularDynamicsViewer::exportImage()
{
    // The four tab pages are the four plot members, so map the index rather
    // than qobject_cast — this class is file-local and deliberately not a moc
    // type.
    SeriesPlotWidget* const pages[4] = {temperaturePlot_, energyPlot_,
                                        pressurePlot_, rdfPlot_};
    const int index = tabs_->currentIndex();
    if (index < 0 || index >= 4)
        return;
    SeriesPlotWidget* plot = pages[index];
    if (!plot)
        return;
    const QString path = QFileDialog::getSaveFileName(
        this, tr("Export Plot Image"),
        QStringLiteral("md_%1.png").arg(tabs_->tabText(tabs_->currentIndex())
                                            .toLower()
                                            .replace(QLatin1Char(' '),
                                                     QLatin1Char('_'))),
        tr("PNG image (*.png);;SVG vector (*.svg)"));
    if (path.isEmpty())
        return;

    const QSizeF logical(plot->width(), plot->height());
    if (path.endsWith(QLatin1String(".svg"), Qt::CaseInsensitive)) {
        QSvgGenerator generator;
        generator.setFileName(path);
        generator.setSize(logical.toSize());
        generator.setViewBox(QRectF(QPointF(0, 0), logical));
        QPainter painter(&generator);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.fillRect(QRectF(QPointF(0, 0), logical), Qt::white);
        plot->render(painter, QRectF(QPointF(0, 0), logical));
        return;
    }
    constexpr int kScale = 3;
    QImage image(logical.toSize() * kScale, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::white);
    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing);
    plot->render(painter, QRectF(QPointF(0, 0), QSizeF(image.size())));
    painter.end();
    if (!image.save(path))
        QMessageBox::warning(this, tr("Export Plot Image"),
                             tr("Could not write %1").arg(path));
}

} // namespace calango::gui
