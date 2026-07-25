#include "gui/OpticsResultsWindow.hpp"

#include <QColor>
#include <QComboBox>
#include <QFile>
#include <QFileDialog>
#include <QFontMetrics>
#include <QHBoxLayout>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QMessageBox>
#include <QPainter>
#include <QPen>
#include <QPolygonF>
#include <QPushButton>
#include <QSaveFile>
#include <QTextStream>
#include <QVBoxLayout>
#include <QWidget>

#include <algorithm>
#include <cmath>
#include <limits>

namespace calango::gui {

// ---------------------------------------------------------------------------
// A minimal dependency-free multi-series line chart. The project ships no
// plotting library and LinePlotWidget only draws a single curve, so the optics
// viewer (ε₁ & ε₂, n & k) carries its own small painter here: autoscaled axes
// with ticks and a grid, N labelled curves with a legend, and a renderTo() the
// window reuses for high-resolution image export.
// ---------------------------------------------------------------------------
class OpticsPlotWidget : public QWidget {
public:
    explicit OpticsPlotWidget(QWidget* parent = nullptr)
        : QWidget(parent)
    {
        setMinimumSize(480, 320);
    }

    void setSeries(const std::vector<double>& x,
                   const std::vector<QPair<QString, std::vector<double>>>& series,
                   const QString& xLabel, const QString& yLabel)
    {
        x_ = x;
        series_ = series;
        xLabel_ = xLabel;
        yLabel_ = yLabel;
        update();
    }

    /// Draw the chart into `painter` filling a logical area of `size`. Returns
    /// false (after drawing a placeholder) when there is nothing to plot.
    bool renderTo(QPainter& painter, QSize size) const;

protected:
    void paintEvent(QPaintEvent*) override
    {
        QPainter painter(this);
        renderTo(painter, size());
    }

private:
    static QColor seriesColor(int index)
    {
        static const QColor palette[] = {
            QColor(0x1f, 0x77, 0xb4), QColor(0xd6, 0x27, 0x28),
            QColor(0x2c, 0xa0, 0x2c), QColor(0xff, 0x7f, 0x0e),
            QColor(0x94, 0x67, 0xbd), QColor(0x8c, 0x56, 0x4b),
        };
        const int n = static_cast<int>(sizeof(palette) / sizeof(palette[0]));
        return palette[((index % n) + n) % n];
    }

    std::vector<double> x_;
    std::vector<QPair<QString, std::vector<double>>> series_;
    QString xLabel_;
    QString yLabel_;
};

bool OpticsPlotWidget::renderTo(QPainter& p, QSize size) const
{
    p.setRenderHint(QPainter::Antialiasing, true);
    p.fillRect(QRect(QPoint(0, 0), size), Qt::white);

    const double W = size.width();
    const double H = size.height();
    const QRectF plot(70.0, 24.0, W - 70.0 - 20.0, H - 24.0 - 52.0);

    if (x_.empty() || series_.empty() || plot.width() < 20.0
        || plot.height() < 20.0) {
        p.setPen(QColor(120, 120, 120));
        p.drawText(QRect(QPoint(0, 0), size), Qt::AlignCenter,
                   QObject::tr("No data to display"));
        return false;
    }

    // Data ranges (ignoring non-finite samples such as poles in the loss fn).
    double xMin = x_.front();
    double xMax = x_.front();
    for (double v : x_) {
        xMin = std::min(xMin, v);
        xMax = std::max(xMax, v);
    }
    double yMin = std::numeric_limits<double>::infinity();
    double yMax = -std::numeric_limits<double>::infinity();
    for (const auto& s : series_)
        for (double v : s.second)
            if (std::isfinite(v)) {
                yMin = std::min(yMin, v);
                yMax = std::max(yMax, v);
            }
    if (!(xMax > xMin))
        xMax = xMin + 1.0;
    if (!std::isfinite(yMin) || !std::isfinite(yMax)) {
        yMin = 0.0;
        yMax = 1.0;
    }
    if (!(yMax > yMin))
        yMax = yMin + 1.0;
    const double pad = (yMax - yMin) * 0.06;
    yMin -= pad;
    yMax += pad;

    const auto mapX = [&](double v) {
        return plot.left() + (v - xMin) / (xMax - xMin) * plot.width();
    };
    const auto mapY = [&](double v) {
        return plot.bottom() - (v - yMin) / (yMax - yMin) * plot.height();
    };

    // Grid, ticks and tick labels.
    const int ticks = 5;
    for (int i = 0; i <= ticks; ++i) {
        const double fx = xMin + (xMax - xMin) * i / ticks;
        const double px = mapX(fx);
        p.setPen(QPen(QColor(228, 228, 228), 1.0));
        p.drawLine(QPointF(px, plot.top()), QPointF(px, plot.bottom()));
        p.setPen(QColor(80, 80, 80));
        p.drawText(QRectF(px - 40.0, plot.bottom() + 4.0, 80.0, 16.0),
                   Qt::AlignHCenter | Qt::AlignTop, QString::number(fx, 'g', 4));

        const double fy = yMin + (yMax - yMin) * i / ticks;
        const double py = mapY(fy);
        p.setPen(QPen(QColor(228, 228, 228), 1.0));
        p.drawLine(QPointF(plot.left(), py), QPointF(plot.right(), py));
        p.setPen(QColor(80, 80, 80));
        p.drawText(QRectF(2.0, py - 8.0, plot.left() - 8.0, 16.0),
                   Qt::AlignRight | Qt::AlignVCenter, QString::number(fy, 'g', 4));
    }

    // ω = 0 (or y = 0) reference line when the range straddles zero.
    if (yMin < 0.0 && yMax > 0.0) {
        p.setPen(QPen(QColor(150, 150, 150), 1.0, Qt::DashLine));
        const double py = mapY(0.0);
        p.drawLine(QPointF(plot.left(), py), QPointF(plot.right(), py));
    }

    // Axis frame on top of the grid.
    p.setPen(QPen(QColor(60, 60, 60), 1.2));
    p.setBrush(Qt::NoBrush);
    p.drawRect(plot);

    // The curves, clipped to the plot rectangle.
    p.save();
    p.setClipRect(plot);
    for (std::size_t si = 0; si < series_.size(); ++si) {
        const std::vector<double>& y = series_[si].second;
        QPen pen(seriesColor(static_cast<int>(si)));
        pen.setWidthF(1.8);
        p.setPen(pen);
        QPolygonF poly;
        const std::size_t n = std::min(x_.size(), y.size());
        poly.reserve(static_cast<int>(n));
        for (std::size_t i = 0; i < n; ++i) {
            if (!std::isfinite(y[i]))
                continue;
            poly << QPointF(mapX(x_[i]), mapY(y[i]));
        }
        p.drawPolyline(poly);
    }
    p.restore();

    // Axis titles.
    p.setPen(QColor(30, 30, 30));
    p.drawText(QRectF(plot.left(), H - 20.0, plot.width(), 18.0),
               Qt::AlignHCenter, xLabel_);
    p.save();
    p.translate(16.0, plot.center().y());
    p.rotate(-90.0);
    p.drawText(QRectF(-plot.height() / 2.0, -8.0, plot.height(), 16.0),
               Qt::AlignHCenter, yLabel_);
    p.restore();

    // Legend, top-right inside the plot.
    QFontMetrics fm(p.font());
    int labelWidth = 0;
    for (const auto& s : series_)
        labelWidth = std::max(labelWidth, fm.horizontalAdvance(s.first));
    const double boxW = labelWidth + 34.0;
    const double boxH = series_.size() * 16.0 + 8.0;
    const QRectF legend(plot.right() - boxW - 8.0, plot.top() + 8.0, boxW, boxH);
    p.setBrush(QColor(255, 255, 255, 220));
    p.setPen(QColor(185, 185, 185));
    p.drawRect(legend);
    for (std::size_t si = 0; si < series_.size(); ++si) {
        const double ly = legend.top() + 6.0 + si * 16.0;
        p.setPen(QPen(seriesColor(static_cast<int>(si)), 2.4));
        p.drawLine(QPointF(legend.left() + 6.0, ly + 6.0),
                   QPointF(legend.left() + 24.0, ly + 6.0));
        p.setPen(QColor(30, 30, 30));
        p.drawText(QRectF(legend.left() + 28.0, ly, labelWidth + 4.0, 14.0),
                   Qt::AlignLeft | Qt::AlignVCenter, series_[si].first);
    }
    return true;
}

// ---------------------------------------------------------------------------
// OpticsResultsWindow
// ---------------------------------------------------------------------------
OpticsResultsWindow::OpticsResultsWindow(const QString& directory,
                                         QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle(tr("Optical Properties — %1").arg(directory));
    resize(880, 620);

    auto* layout = new QVBoxLayout(this);

    auto* controls = new QHBoxLayout;
    controls->addWidget(new QLabel(tr("Quantity:"), this));
    quantityCombo_ = new QComboBox(this);
    const auto addQuantity = [this](const QString& label, Quantity id) {
        quantityCombo_->addItem(label, static_cast<int>(id));
    };
    addQuantity(tr("Dielectric function (ε₁ & ε₂)"), Quantity::Dielectric);
    addQuantity(tr("Absorption α(ω)"), Quantity::Absorption);
    addQuantity(tr("Reflectivity R(ω)"), Quantity::Reflectivity);
    addQuantity(tr("Refractive index (n & k)"), Quantity::RefractiveIndex);
    addQuantity(tr("Energy loss L(ω)"), Quantity::Loss);
    controls->addWidget(quantityCombo_);
    controls->addSpacing(16);
    controls->addWidget(new QLabel(tr("Direction:"), this));
    directionCombo_ = new QComboBox(this);
    controls->addWidget(directionCombo_);
    controls->addStretch(1);
    layout->addLayout(controls);

    plot_ = new OpticsPlotWidget(this);
    layout->addWidget(plot_, 1);

    auto* buttons = new QHBoxLayout;
    auto* csvButton = new QPushButton(tr("Export CSV…"), this);
    auto* imageButton = new QPushButton(tr("Export Image…"), this);
    auto* closeButton = new QPushButton(tr("Close"), this);
    buttons->addWidget(csvButton);
    buttons->addWidget(imageButton);
    buttons->addStretch(1);
    buttons->addWidget(closeButton);
    layout->addLayout(buttons);

    connect(csvButton, &QPushButton::clicked, this,
            &OpticsResultsWindow::exportCsv);
    connect(imageButton, &QPushButton::clicked, this,
            &OpticsResultsWindow::exportImage);
    connect(closeButton, &QPushButton::clicked, this, &QDialog::accept);

    loadDirectory(directory);

    // Only the directions actually present in the file become selectable.
    for (int i = 0; i < directions_.size(); ++i)
        directionCombo_->addItem(directions_[i].first, i);

    // The sheet observables are offered only for a job that computed them.
    // ε₃D of a slab supercell depends on the vacuum padding, so for a 2D run
    // these — not ε — are the quantities that describe the material.
    const bool twoDimensional =
        std::any_of(directions_.cbegin(), directions_.cend(),
                    [](const auto& entry) { return entry.second.twoDimensional; });
    if (twoDimensional) {
        addQuantity(tr("Absorbance A(ω)"), Quantity::Absorbance);
        addQuantity(tr("Polarizability α₂D(ω)"), Quantity::Polarizability);
        addQuantity(tr("Conductivity σ₂D(ω)"), Quantity::Conductivity);
        setWindowTitle(tr("2D Optical Properties"));
        // Open on the quantity the user came for.
        quantityCombo_->setCurrentIndex(
            quantityCombo_->findData(static_cast<int>(Quantity::Absorbance)));
    }

    connect(quantityCombo_, &QComboBox::currentIndexChanged, this,
            &OpticsResultsWindow::updatePlot);
    connect(directionCombo_, &QComboBox::currentIndexChanged, this,
            &OpticsResultsWindow::updatePlot);

    updatePlot();
}

void OpticsResultsWindow::loadDirectory(const QString& directory)
{
    QFile file(directory + QStringLiteral("/optics.json"));
    if (!file.open(QIODevice::ReadOnly))
        return;
    const QByteArray raw = file.readAll();
    file.close();

    QJsonParseError parseError{};
    const QJsonDocument doc = QJsonDocument::fromJson(raw, &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject())
        return;
    const QJsonObject root = doc.object();

    const auto toVector = [](const QJsonArray& array) {
        std::vector<double> values;
        values.reserve(array.size());
        for (const auto& item : array)
            values.push_back(item.toDouble());
        return values;
    };

    energy_ = toVector(root.value(QStringLiteral("energy_eV")).toArray());
    if (energy_.empty())
        return;

    for (const char* key : {"xx", "yy", "zz"}) {
        const QJsonValue value = root.value(QLatin1String(key));
        if (!value.isObject())
            continue;
        const QJsonObject dir = value.toObject();
        DirectionData data;
        data.eps1 = toVector(dir.value(QStringLiteral("eps1")).toArray());
        data.eps2 = toVector(dir.value(QStringLiteral("eps2")).toArray());
        data.absorption =
            toVector(dir.value(QStringLiteral("absorption")).toArray());
        data.reflectivity =
            toVector(dir.value(QStringLiteral("reflectivity")).toArray());
        data.n = toVector(dir.value(QStringLiteral("n")).toArray());
        data.k = toVector(dir.value(QStringLiteral("k")).toArray());
        data.loss = toVector(dir.value(QStringLiteral("loss")).toArray());

        // Sheet observables, written only by the 2D variant of the wizard.
        const QJsonValue sheet =
            root.value(QLatin1String("twod_") + QLatin1String(key));
        if (sheet.isObject()) {
            const QJsonObject twod = sheet.toObject();
            data.alpha2dRe =
                toVector(twod.value(QStringLiteral("alpha_2D_re_A")).toArray());
            data.alpha2dIm =
                toVector(twod.value(QStringLiteral("alpha_2D_im_A")).toArray());
            data.absorbance =
                toVector(twod.value(QStringLiteral("absorbance")).toArray());
            data.sigma2dRe =
                toVector(twod.value(QStringLiteral("sigma_2D_re")).toArray());
            data.sigma2dIm =
                toVector(twod.value(QStringLiteral("sigma_2D_im")).toArray());
            data.twoDimensional = !data.absorbance.empty();
        }

        directions_.append({QString::fromLatin1(key), data});
    }

    hasData_ = !directions_.isEmpty();
}

const OpticsResultsWindow::DirectionData*
OpticsResultsWindow::currentDirection() const
{
    if (!directionCombo_ || directions_.isEmpty())
        return nullptr;
    int index = directionCombo_->currentData().toInt();
    if (index < 0 || index >= directions_.size())
        index = 0;
    return &directions_[index].second;
}

void OpticsResultsWindow::updatePlot()
{
    if (!plot_)
        return;
    const DirectionData* dir = currentDirection();
    if (!dir) {
        plot_->setSeries({}, {}, tr("Photon energy ħω (eV)"), QString());
        return;
    }

    std::vector<QPair<QString, std::vector<double>>> series;
    QString yLabel;
    const auto quantity = static_cast<Quantity>(
        quantityCombo_ ? quantityCombo_->currentData().toInt() : 0);
    switch (quantity) {
    case Quantity::Dielectric:
        series = {{tr("ε₁"), dir->eps1}, {tr("ε₂"), dir->eps2}};
        yLabel = tr("Dielectric function ε");
        break;
    case Quantity::Absorption:
        series = {{tr("α"), dir->absorption}};
        yLabel = tr("Absorption α (cm⁻¹)");
        break;
    case Quantity::Reflectivity:
        series = {{tr("R"), dir->reflectivity}};
        yLabel = tr("Reflectivity R");
        break;
    case Quantity::RefractiveIndex:
        series = {{tr("n"), dir->n}, {tr("k"), dir->k}};
        yLabel = tr("Refractive index");
        break;
    case Quantity::Loss:
        series = {{tr("L"), dir->loss}};
        yLabel = tr("Energy loss L");
        break;
    case Quantity::Absorbance:
        // Dimensionless: the fraction of normally incident light the sheet
        // absorbs. Graphene's πα ≈ 2.3% is the familiar landmark.
        series = {{tr("A"), dir->absorbance}};
        yLabel = tr("Absorbance A");
        break;
    case Quantity::Polarizability:
        series = {{tr("Re α₂D"), dir->alpha2dRe},
                  {tr("Im α₂D"), dir->alpha2dIm}};
        yLabel = tr("Sheet polarizability α₂D (Å)");
        break;
    case Quantity::Conductivity:
        series = {{tr("Re σ₂D"), dir->sigma2dRe},
                  {tr("Im σ₂D"), dir->sigma2dIm}};
        // e²/h is the convention the 2D literature quotes, where graphene's
        // universal conductivity is the familiar π/2 ≈ 1.57.
        yLabel = tr("Sheet conductivity σ₂D (e²/h)");
        break;
    }
    plot_->setSeries(energy_, series, tr("Photon energy ħω (eV)"), yLabel);
}

void OpticsResultsWindow::exportCsv()
{
    const DirectionData* dir = currentDirection();
    if (!dir) {
        QMessageBox::information(this, tr("Export CSV"),
                                 tr("No optical data was loaded from this job."));
        return;
    }
    const QString label = directionCombo_->currentText();
    const QString path = QFileDialog::getSaveFileName(
        this, tr("Export Optical Data"),
        QStringLiteral("optics_%1.csv").arg(label), tr("CSV (*.csv)"));
    if (path.isEmpty())
        return;

    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QMessageBox::warning(this, tr("Export CSV"),
                             tr("Could not open %1 for writing.").arg(path));
        return;
    }
    QTextStream out(&file);
    out << "# Optical properties (direction " << label << ")\n";
    out << "energy_eV,eps1,eps2,absorption_cm-1,reflectivity,n,k,loss";
    // The 2D columns are appended only for a sheet job, so a bulk export keeps
    // exactly the column set it always had.
    if (dir->twoDimensional)
        out << ",alpha_2D_re_A,alpha_2D_im_A,absorbance,sigma_2D_re,sigma_2D_im";
    out << '\n';

    const auto at = [](const std::vector<double>& v, std::size_t i) {
        return i < v.size() ? v[i] : 0.0;
    };
    for (std::size_t i = 0; i < energy_.size(); ++i) {
        out << QString::number(energy_[i], 'f', 6) << ','
            << QString::number(at(dir->eps1, i), 'g', 8) << ','
            << QString::number(at(dir->eps2, i), 'g', 8) << ','
            << QString::number(at(dir->absorption, i), 'g', 8) << ','
            << QString::number(at(dir->reflectivity, i), 'g', 8) << ','
            << QString::number(at(dir->n, i), 'g', 8) << ','
            << QString::number(at(dir->k, i), 'g', 8) << ','
            << QString::number(at(dir->loss, i), 'g', 8);
        if (dir->twoDimensional) {
            out << ',' << QString::number(at(dir->alpha2dRe, i), 'g', 8) << ','
                << QString::number(at(dir->alpha2dIm, i), 'g', 8) << ','
                << QString::number(at(dir->absorbance, i), 'g', 8) << ','
                << QString::number(at(dir->sigma2dRe, i), 'g', 8) << ','
                << QString::number(at(dir->sigma2dIm, i), 'g', 8);
        }
        out << '\n';
    }
    file.commit();
}

void OpticsResultsWindow::exportImage()
{
    if (!plot_)
        return;
    const QString path = QFileDialog::getSaveFileName(
        this, tr("Export Image"), QStringLiteral("optics.png"),
        tr("PNG image (*.png);;JPEG image (*.jpg *.jpeg)"));
    if (path.isEmpty())
        return;

    // Render at 3x the on-screen size for a crisp, print-quality raster.
    const int scale = 3;
    const QSize logical = plot_->size();
    QImage image(logical * scale, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::white);
    QPainter painter(&image);
    painter.scale(scale, scale);
    plot_->renderTo(painter, logical);
    painter.end();

    if (!image.save(path))
        QMessageBox::warning(this, tr("Export Image"),
                             tr("Could not write the image to %1.").arg(path));
}

} // namespace calango::gui
