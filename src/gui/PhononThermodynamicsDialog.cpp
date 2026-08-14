#include "gui/PhononThermodynamicsDialog.hpp"

#include "gui/PlotPalette.hpp"

#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QSvgGenerator>
#include <QTextStream>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>
#include <functional>
#include <limits>

namespace calango::gui {

namespace {
constexpr double kMevPerEv = 1000.0;
} // namespace

/// One thermodynamics panel: either the two energy curves (U, F) or the single
/// entropy curve. Drawing lives in render(QPainter, QRectF) so the widget, the
/// PNG export and the SVG export are literally the same code — an export that
/// redraws through a second path is an export that silently drifts from what
/// the user saw.
///
/// The crosshair is driven from OUTSIDE (setCursorTemperature) rather than from
/// this widget's own mouse position, which is what lets both columns track one
/// hover: each plot reports where the pointer is and both are told where to
/// draw.
class ThermoPlotWidget : public QWidget {
public:
    explicit ThermoPlotWidget(bool entropy, QWidget* parent = nullptr)
        : QWidget(parent), entropy_(entropy)
    {
        setMinimumSize(300, 300);
        setMouseTracking(true);
    }

    void setResult(const core::PhononThermoResult& result)
    {
        result_ = result;
        update();
    }

    /// Draw the crosshair at temperature `t`; NaN hides it.
    void setCursorTemperature(double t)
    {
        if (!(std::isnan(cursorT_) && std::isnan(t)) && cursorT_ != t) {
            cursorT_ = t;
            update();
        }
    }

    /// Emitted-by-hand callback (this widget is not a QObject subclass with its
    /// own signals to keep it file-local): the dialog installs one to broadcast
    /// the hovered temperature to both columns.
    std::function<void(double)> onHover;

    void render(QPainter& painter, const QRectF& target) const;

protected:
    void paintEvent(QPaintEvent*) override
    {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.fillRect(rect(), PlotPalette::canvas);
        render(painter, QRectF(rect()));
    }

    void mouseMoveEvent(QMouseEvent* event) override
    {
        if (!onHover || result_.points.size() < 2)
            return;
        const QRectF plot = plotRect(QRectF(rect()));
        if (!plot.contains(event->position())) {
            onHover(std::numeric_limits<double>::quiet_NaN());
            return;
        }
        const double tMin = result_.points.front().temperatureK;
        const double tMax = result_.points.back().temperatureK;
        const double fraction =
            (event->position().x() - plot.left()) / std::max(plot.width(), 1.0);
        onHover(tMin + fraction * (tMax - tMin));
    }

    void leaveEvent(QEvent*) override
    {
        if (onHover)
            onHover(std::numeric_limits<double>::quiet_NaN());
    }

private:
    /// The framed data area inside `target`; shared by drawing and hit-testing
    /// so the crosshair cannot drift from the curves.
    QRectF plotRect(const QRectF& target) const
    {
        const double scale = target.height() / 340.0;
        return target.adjusted(72.0 * scale, 22.0 * scale, -18.0 * scale,
                               -40.0 * scale);
    }
    /// The point nearest `cursorT_`, or -1.
    int cursorIndex() const;

    bool entropy_;
    core::PhononThermoResult result_;
    double cursorT_ = std::numeric_limits<double>::quiet_NaN();
};

int ThermoPlotWidget::cursorIndex() const
{
    if (std::isnan(cursorT_) || result_.points.size() < 2)
        return -1;
    int best = -1;
    double bestDistance = std::numeric_limits<double>::max();
    for (std::size_t i = 0; i < result_.points.size(); ++i) {
        const double d = std::abs(result_.points[i].temperatureK - cursorT_);
        if (d < bestDistance) {
            bestDistance = d;
            best = static_cast<int>(i);
        }
    }
    return best;
}

void ThermoPlotWidget::render(QPainter& painter, const QRectF& target) const
{
    if (result_.points.size() < 2)
        return;

    const double scale = target.height() / 340.0;
    QFont font = painter.font();
    font.setPointSizeF(std::max(7.0, 9.0 * scale));
    painter.setFont(font);
    const QFontMetricsF metrics(font);

    const QRectF plot = plotRect(target);
    if (plot.width() <= 1.0 || plot.height() <= 1.0)
        return;

    double tMin = result_.points.front().temperatureK;
    double tMax = result_.points.back().temperatureK;
    if (tMax - tMin < 1e-9)
        tMax = tMin + 1.0;

    // Value range for whichever series this panel owns.
    double vMin = 0.0, vMax = 0.0;
    for (const auto& p : result_.points) {
        if (entropy_) {
            vMax = std::max(vMax, p.entropyEvPerK * kMevPerEv);
        } else {
            vMin = std::min({vMin, p.internalEnergyEv, p.freeEnergyEv});
            vMax = std::max({vMax, p.internalEnergyEv, p.freeEnergyEv});
        }
    }
    if (vMax - vMin < 1e-12)
        vMax = vMin + 1e-12;

    const auto xOf = [&](double t) {
        return plot.left() + (t - tMin) / (tMax - tMin) * plot.width();
    };
    const auto yOf = [&](double v) {
        return plot.bottom() - (v - vMin) / (vMax - vMin) * plot.height();
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

    struct Curve {
        QColor color;
        QString label;
        double valueOf(const core::PhononThermoPoint& p) const
        {
            return kind == 0 ? p.internalEnergyEv
                             : (kind == 1 ? p.freeEnergyEv
                                          : p.entropyEvPerK * kMevPerEv);
        }
        int kind; // 0 = U, 1 = F, 2 = S
    };
    std::vector<Curve> curves;
    if (entropy_)
        curves.push_back({QColor(0x2c, 0xa0, 0x2c), QStringLiteral("S_vib"), 2});
    else {
        curves.push_back({QColor(0xd6, 0x27, 0x28), QStringLiteral("U_vib"), 0});
        curves.push_back({PlotPalette::series, QStringLiteral("F_vib"), 1});
    }

    for (const Curve& curve : curves) {
        QPainterPath path;
        for (std::size_t i = 0; i < result_.points.size(); ++i) {
            const QPointF point(xOf(result_.points[i].temperatureK),
                                yOf(curve.valueOf(result_.points[i])));
            if (i == 0)
                path.moveTo(point);
            else
                path.lineTo(point);
        }
        painter.setPen(QPen(curve.color, 2.0 * scale));
        painter.drawPath(path);
    }

    // -- Ticks + titles -----------------------------------------------------
    painter.setPen(axisColor);
    for (int i = 0; i <= kTicks; ++i) {
        const double t = tMin + (tMax - tMin) * i / kTicks;
        painter.drawText(QRectF(xOf(t) - 30.0 * scale, plot.bottom() + 2.0 * scale,
                                60.0 * scale, metrics.height()),
                         Qt::AlignCenter, QString::number(t, 'f', 0));
        const double v = vMin + (vMax - vMin) * i / kTicks;
        painter.drawText(QRectF(target.left(), yOf(v) - metrics.height() / 2.0,
                                plot.left() - target.left() - 4.0 * scale,
                                metrics.height()),
                         Qt::AlignRight | Qt::AlignVCenter,
                         QString::number(v, 'g', 3));
    }
    painter.drawText(QRectF(plot.left(), target.bottom() - metrics.height() * 1.2,
                            plot.width(), metrics.height()),
                     Qt::AlignCenter, QStringLiteral("Temperature (K)"));
    painter.save();
    painter.translate(target.left() + metrics.height() * 0.9, plot.center().y());
    painter.rotate(-90.0);
    painter.drawText(QRectF(-plot.height() / 2.0, -metrics.height(),
                            plot.height(), metrics.height() * 1.2),
                     Qt::AlignCenter,
                     entropy_ ? QStringLiteral("S  (meV/K / cell)")
                              : QStringLiteral("U, F  (eV / cell)"));
    painter.restore();

    // -- Legend -------------------------------------------------------------
    double legendY = plot.top() + 6.0 * scale;
    for (const Curve& curve : curves) {
        const double x = plot.left() + 10.0 * scale;
        painter.setPen(QPen(curve.color, 2.5 * scale));
        painter.drawLine(QPointF(x, legendY + metrics.height() / 2.0),
                         QPointF(x + 22.0 * scale, legendY + metrics.height() / 2.0));
        painter.setPen(axisColor);
        painter.drawText(QRectF(x + 28.0 * scale, legendY, 160.0 * scale,
                                metrics.height()),
                         Qt::AlignLeft | Qt::AlignVCenter, curve.label);
        legendY += metrics.height() * 1.15;
    }

    // -- Crosshair ----------------------------------------------------------
    const int index = cursorIndex();
    if (index >= 0) {
        const auto& point = result_.points[static_cast<std::size_t>(index)];
        const double x = xOf(point.temperatureK);
        QColor crosshair = axisColor;
        crosshair.setAlphaF(0.55);
        painter.setPen(QPen(crosshair, 1.0 * scale, Qt::DashLine));
        painter.drawLine(QPointF(x, plot.top()), QPointF(x, plot.bottom()));

        QStringList readout;
        readout << QStringLiteral("T = %1 K").arg(point.temperatureK, 0, 'f', 1);
        for (const Curve& curve : curves) {
            const double value = curve.valueOf(point);
            painter.setBrush(curve.color);
            painter.setPen(Qt::NoPen);
            painter.drawEllipse(QPointF(x, yOf(value)), 3.0 * scale, 3.0 * scale);
            readout << QStringLiteral("%1 = %2").arg(curve.label)
                           .arg(value, 0, 'g', 4);
        }
        painter.setPen(axisColor);
        const QString text = readout.join(QStringLiteral("   "));
        painter.drawText(QRectF(plot.left(), plot.bottom() - metrics.height() * 1.3,
                                plot.width() - 6.0 * scale, metrics.height()),
                         Qt::AlignRight | Qt::AlignVCenter, text);
    }
}

// ---------------------------------------------------------------------------

PhononThermodynamicsDialog::PhononThermodynamicsDialog(
    std::vector<double> frequenciesCm, std::vector<double> dos,
    const QString& label, QWidget* parent)
    : QDialog(parent)
    , frequenciesCm_(std::move(frequenciesCm))
    , dos_(std::move(dos))
{
    setWindowTitle(label.isEmpty() ? tr("Phonon Thermodynamics")
                                   : tr("Phonon Thermodynamics — %1").arg(label));
    resize(1000, 560);

    auto* layout = new QVBoxLayout(this);

    auto* intro = new QLabel(
        tr("Harmonic vibrational thermodynamics integrated over the phonon "
           "density of states g(ω). U includes the zero-point energy, so "
           "U(0) = F(0) = E_ZPE and S(0) = 0. Hovering either plot reads both "
           "out at the same temperature."),
        this);
    intro->setWordWrap(true);
    layout->addWidget(intro);

    auto* columns = new QHBoxLayout;
    columns->addWidget(buildColumn(/*entropy=*/false, energyPlot_), 1);
    columns->addWidget(buildColumn(/*entropy=*/true, entropyPlot_), 1);
    layout->addLayout(columns, 1);

    // One hover drives both crosshairs: reading U, F and S at a temperature is
    // a single gesture rather than two hovers the user has to line up by eye.
    const auto broadcast = [this](double temperature) {
        energyPlot_->setCursorTemperature(temperature);
        entropyPlot_->setCursorTemperature(temperature);
    };
    energyPlot_->onHover = broadcast;
    entropyPlot_->onHover = broadcast;

    summaryLabel_ = new QLabel(this);
    summaryLabel_->setWordWrap(true);
    summaryLabel_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    layout->addWidget(summaryLabel_);

    auto* controls = new QHBoxLayout;
    const auto makeTempSpin = [this](double value) {
        auto* spin = new QDoubleSpinBox(this);
        spin->setRange(0.0, 10000.0);
        spin->setDecimals(0);
        spin->setSingleStep(50.0);
        spin->setSuffix(tr(" K"));
        spin->setValue(value);
        return spin;
    };
    controls->addWidget(new QLabel(tr("T from"), this));
    minTempSpin_ = makeTempSpin(0.0);
    controls->addWidget(minTempSpin_);
    controls->addWidget(new QLabel(tr("to"), this));
    maxTempSpin_ = makeTempSpin(1000.0);
    controls->addWidget(maxTempSpin_);
    controls->addStretch(1);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    controls->addWidget(buttons);
    layout->addLayout(controls);

    for (QDoubleSpinBox* spin : {minTempSpin_, maxTempSpin_})
        connect(spin, &QDoubleSpinBox::valueChanged, this,
                &PhononThermodynamicsDialog::recompute);

    recompute();
}

QWidget* PhononThermodynamicsDialog::buildColumn(bool entropy,
                                                 ThermoPlotWidget*& plot)
{
    auto* column = new QWidget(this);
    auto* layout = new QVBoxLayout(column);
    layout->setContentsMargins(0, 0, 0, 0);

    auto* title = new QLabel(entropy ? tr("Vibrational Entropy")
                                     : tr("Vibrational Energy"),
                             column);
    QFont titleFont = title->font();
    titleFont.setBold(true);
    title->setFont(titleFont);
    title->setAlignment(Qt::AlignCenter);
    layout->addWidget(title);

    plot = new ThermoPlotWidget(entropy, column);
    layout->addWidget(plot, 1);

    // Per-column exports: the two panels have different y quantities and
    // units, so one combined file would concatenate two tables that most
    // tools will not read back as one.
    auto* actions = new QHBoxLayout;
    auto* csvButton = new QPushButton(tr("Export CSV…"), column);
    auto* imageButton = new QPushButton(tr("Export Image…"), column);
    imageButton->setToolTip(
        tr("PNG at 3x for print, or SVG vector art for a figure."));
    actions->addWidget(csvButton);
    actions->addWidget(imageButton);
    actions->addStretch(1);
    layout->addLayout(actions);
    connect(csvButton, &QPushButton::clicked, this,
            [this, entropy] { exportCsv(entropy); });
    connect(imageButton, &QPushButton::clicked, this,
            [this, entropy] { exportImage(entropy); });
    return column;
}

void PhononThermodynamicsDialog::recompute()
{
    // 201 points over the window: fine enough that the curves read as smooth
    // at any window width, cheap enough to recompute on every spin-box edit.
    result_ = core::computePhononThermodynamics(
        frequenciesCm_, dos_, minTempSpin_->value(), maxTempSpin_->value(), 201);
    energyPlot_->setResult(result_);
    entropyPlot_->setResult(result_);

    if (result_.points.empty()) {
        summaryLabel_->setText(
            tr("No usable phonon DOS — nothing to integrate."));
        return;
    }
    QString text = tr("Zero-point energy: %1 eV/cell   ·   modes ∫g(ω)dω = %2")
                       .arg(result_.zeroPointEnergyEv, 0, 'f', 4)
                       .arg(result_.totalModes, 0, 'f', 2);
    // A structure with imaginary modes is not at a minimum, so its harmonic
    // thermodynamics are not meaningful. Say so rather than quietly returning
    // numbers computed from the remaining modes.
    if (result_.imaginaryWeight > 1e-6)
        text += tr("\n⚠ %1% of the DOS lies at ω ≤ 0 (imaginary modes) and was "
                   "excluded. The structure is not at a dynamical minimum, so "
                   "these harmonic properties are not physically meaningful.")
                    .arg(result_.imaginaryWeight * 100.0, 0, 'f', 1);
    summaryLabel_->setText(text);
}

void PhononThermodynamicsDialog::exportCsv(bool entropy)
{
    const QString path = QFileDialog::getSaveFileName(
        this, tr("Export Phonon Thermodynamics"),
        entropy ? QStringLiteral("phonon_thermodynamics_entropy.csv")
                : QStringLiteral("phonon_thermodynamics_energy.csv"),
        tr("CSV files (*.csv);;All files (*)"));
    if (path.isEmpty())
        return;
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QMessageBox::warning(this, tr("Export Phonon Thermodynamics"),
                             tr("Could not write %1").arg(path));
        return;
    }
    QTextStream out(&file);
    // A CSV starts with its header row — no '#' comment lines. The zero-
    // point energy used to be a comment; it rides as a constant column now,
    // so the file still carries it without breaking standard readers.
    if (entropy) {
        // Cv rides with the entropy column: both are temperature derivatives
        // of the same curve, and a user plotting S almost always wants Cv too.
        out << "temperature_K,S_vib_eV_per_K,Cv_eV_per_K,zero_point_energy_eV\n";
        for (const auto& p : result_.points)
            out << p.temperatureK << ',' << p.entropyEvPerK << ','
                << p.heatCapacityEvPerK << ',' << result_.zeroPointEnergyEv
                << '\n';
    } else {
        out << "temperature_K,U_vib_eV,F_vib_eV,zero_point_energy_eV\n";
        for (const auto& p : result_.points)
            out << p.temperatureK << ',' << p.internalEnergyEv << ','
                << p.freeEnergyEv << ',' << result_.zeroPointEnergyEv << '\n';
    }
}

void PhononThermodynamicsDialog::exportImage(bool entropy)
{
    const QString path = QFileDialog::getSaveFileName(
        this, tr("Export Plot Image"),
        entropy ? QStringLiteral("phonon_entropy.png")
                : QStringLiteral("phonon_energy.png"),
        tr("PNG image (*.png);;SVG vector (*.svg)"));
    if (path.isEmpty())
        return;

    ThermoPlotWidget* plot = entropy ? entropyPlot_ : energyPlot_;
    const QSizeF logical(plot->width(), plot->height());
    if (path.endsWith(QLatin1String(".svg"), Qt::CaseInsensitive)) {
        QSvgGenerator generator;
        generator.setFileName(path);
        generator.setSize(logical.toSize());
        generator.setViewBox(QRectF(QPointF(0, 0), logical));
        generator.setTitle(windowTitle());
        QPainter painter(&generator);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.fillRect(QRectF(QPointF(0, 0), logical), Qt::white);
        plot->render(painter, QRectF(QPointF(0, 0), logical));
        return;
    }

    // 3x for print: the render() path scales fonts and strokes with the target
    // height, so this is a genuinely higher-resolution figure rather than an
    // upscaled screenshot.
    constexpr int kScale = 3;
    QImage image(logical.toSize() * kScale, QImage::Format_ARGB32_Premultiplied);
    image.setDevicePixelRatio(1.0);
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
