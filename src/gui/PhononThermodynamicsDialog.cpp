#include "gui/PhononThermodynamicsDialog.hpp"

#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QSvgGenerator>
#include <QTextStream>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>

namespace calango::gui {

namespace {
constexpr double kMevPerEv = 1000.0;
} // namespace

/// Dual-axis multi-curve plot for the thermodynamic functions. Drawing lives in
/// render(QPainter, QRectF) so the widget, the PNG export and the SVG export
/// are literally the same code — an export that redraws through a second path
/// is an export that silently drifts from what the user saw.
class ThermoPlotWidget : public QWidget {
public:
    explicit ThermoPlotWidget(QWidget* parent = nullptr) : QWidget(parent)
    {
        setMinimumSize(520, 360);
    }

    void setResult(const core::PhononThermoResult& result)
    {
        result_ = result;
        update();
    }

    /// Draw the whole chart into `target` using `painter`. Fonts scale with the
    /// target height so a 3x PNG export is not a chart with tiny labels.
    void render(QPainter& painter, const QRectF& target) const;

protected:
    void paintEvent(QPaintEvent*) override
    {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.fillRect(rect(), palette().base());
        render(painter, QRectF(rect()));
    }

private:
    core::PhononThermoResult result_;
};

void ThermoPlotWidget::render(QPainter& painter, const QRectF& target) const
{
    if (result_.points.size() < 2)
        return;

    const double scale = target.height() / 360.0;
    QFont font = painter.font();
    font.setPointSizeF(std::max(7.0, 9.0 * scale));
    painter.setFont(font);
    const QFontMetricsF metrics(font);

    // Generous side margins: both axes carry labelled ticks.
    const QRectF plot = target.adjusted(70.0 * scale, 24.0 * scale,
                                        -78.0 * scale, -40.0 * scale);
    if (plot.width() <= 1.0 || plot.height() <= 1.0)
        return;

    // -- Ranges -------------------------------------------------------------
    double tMin = result_.points.front().temperatureK;
    double tMax = result_.points.back().temperatureK;
    if (tMax - tMin < 1e-9)
        tMax = tMin + 1.0;
    double energyMin = 0.0, energyMax = 0.0, entropyMax = 0.0;
    for (const auto& p : result_.points) {
        energyMin = std::min({energyMin, p.internalEnergyEv, p.freeEnergyEv});
        energyMax = std::max({energyMax, p.internalEnergyEv, p.freeEnergyEv});
        entropyMax = std::max(entropyMax, p.entropyEvPerK * kMevPerEv);
    }
    if (energyMax - energyMin < 1e-12)
        energyMax = energyMin + 1e-12;
    if (entropyMax < 1e-12)
        entropyMax = 1e-12;

    const auto xOf = [&](double t) {
        return plot.left() + (t - tMin) / (tMax - tMin) * plot.width();
    };
    const auto yEnergy = [&](double e) {
        return plot.bottom() - (e - energyMin) / (energyMax - energyMin) * plot.height();
    };
    const auto yEntropy = [&](double s) {
        return plot.bottom() - s / entropyMax * plot.height();
    };

    // -- Frame + grid -------------------------------------------------------
    const QColor axisColor = palette().color(QPalette::Text);
    QColor gridColor = axisColor;
    gridColor.setAlphaF(0.15);
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

    // -- Curves -------------------------------------------------------------
    struct Curve {
        QColor color;
        QString label;
        bool entropy;
    };
    const Curve curves[3] = {
        {QColor(214, 96, 77), QStringLiteral("U_vib"), false},
        {QColor(69, 117, 180), QStringLiteral("F_vib"), false},
        {QColor(90, 174, 97), QStringLiteral("S_vib"), true},
    };
    for (const Curve& curve : curves) {
        QPainterPath path;
        for (std::size_t i = 0; i < result_.points.size(); ++i) {
            const auto& p = result_.points[i];
            const double value = curve.entropy
                ? yEntropy(p.entropyEvPerK * kMevPerEv)
                : yEnergy(curve.label == QStringLiteral("U_vib")
                              ? p.internalEnergyEv
                              : p.freeEnergyEv);
            const QPointF point(xOf(p.temperatureK), value);
            if (i == 0)
                path.moveTo(point);
            else
                path.lineTo(point);
        }
        painter.setPen(QPen(curve.color, 2.0 * scale));
        painter.drawPath(path);
    }

    // -- Tick labels + axis titles -----------------------------------------
    painter.setPen(axisColor);
    for (int i = 0; i <= kTicks; ++i) {
        const double t = tMin + (tMax - tMin) * i / kTicks;
        const double x = xOf(t);
        painter.drawText(QRectF(x - 30.0 * scale, plot.bottom() + 2.0 * scale,
                                60.0 * scale, metrics.height()),
                         Qt::AlignCenter, QString::number(t, 'f', 0));

        const double e = energyMin + (energyMax - energyMin) * i / kTicks;
        painter.drawText(QRectF(target.left(), yEnergy(e) - metrics.height() / 2.0,
                                plot.left() - target.left() - 4.0 * scale,
                                metrics.height()),
                         Qt::AlignRight | Qt::AlignVCenter,
                         QString::number(e, 'g', 3));

        const double s = entropyMax * i / kTicks;
        painter.drawText(QRectF(plot.right() + 4.0 * scale,
                                yEntropy(s) - metrics.height() / 2.0,
                                target.right() - plot.right() - 6.0 * scale,
                                metrics.height()),
                         Qt::AlignLeft | Qt::AlignVCenter,
                         QString::number(s, 'g', 3));
    }
    painter.drawText(QRectF(plot.left(), target.bottom() - metrics.height() * 1.2,
                            plot.width(), metrics.height()),
                     Qt::AlignCenter, QStringLiteral("Temperature (K)"));

    // Rotated axis titles, one per side.
    const auto drawRotated = [&](double x, const QString& text, int direction) {
        painter.save();
        painter.translate(x, plot.center().y());
        painter.rotate(direction * 90.0);
        painter.drawText(QRectF(-plot.height() / 2.0, -metrics.height(),
                                plot.height(), metrics.height() * 1.2),
                         Qt::AlignCenter, text);
        painter.restore();
    };
    drawRotated(target.left() + metrics.height() * 0.9,
                QStringLiteral("U, F  (eV / cell)"), -1);
    drawRotated(target.right() - metrics.height() * 0.4,
                QStringLiteral("S  (meV/K / cell)"), 1);

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
                         Qt::AlignLeft | Qt::AlignVCenter,
                         curve.entropy ? curve.label + QStringLiteral(" (right)")
                                       : curve.label);
        legendY += metrics.height() * 1.15;
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
    resize(820, 560);

    auto* layout = new QVBoxLayout(this);

    auto* intro = new QLabel(
        tr("Harmonic vibrational thermodynamics integrated over the phonon "
           "density of states g(ω). U includes the zero-point energy, so "
           "U(0) = F(0) = E_ZPE and S(0) = 0."),
        this);
    intro->setWordWrap(true);
    layout->addWidget(intro);

    plot_ = new ThermoPlotWidget(this);
    layout->addWidget(plot_, 1);

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
    layout->addLayout(controls);
    for (QDoubleSpinBox* spin : {minTempSpin_, maxTempSpin_})
        connect(spin, &QDoubleSpinBox::valueChanged, this,
                [this] { recompute(); });

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    auto* csvButton =
        buttons->addButton(tr("Export CSV…"), QDialogButtonBox::ActionRole);
    auto* imageButton =
        buttons->addButton(tr("Export Image…"), QDialogButtonBox::ActionRole);
    imageButton->setToolTip(
        tr("PNG at 3x for print, or SVG vector art for a figure."));
    connect(csvButton, &QPushButton::clicked, this,
            &PhononThermodynamicsDialog::exportCsv);
    connect(imageButton, &QPushButton::clicked, this,
            &PhononThermodynamicsDialog::exportImage);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);

    recompute();
}

void PhononThermodynamicsDialog::recompute()
{
    // 201 points over the window: fine enough that the curves read as smooth
    // at any window width, cheap enough to recompute on every spin-box edit.
    result_ = core::computePhononThermodynamics(
        frequenciesCm_, dos_, minTempSpin_->value(), maxTempSpin_->value(), 201);
    plot_->setResult(result_);

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

void PhononThermodynamicsDialog::exportCsv()
{
    const QString path = QFileDialog::getSaveFileName(
        this, tr("Export Phonon Thermodynamics"),
        QStringLiteral("phonon_thermodynamics.csv"),
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
    out << "# Harmonic vibrational thermodynamics from the phonon DOS\n"
        << "# zero_point_energy_eV," << result_.zeroPointEnergyEv << "\n"
        << "temperature_K,U_vib_eV,F_vib_eV,S_vib_eV_per_K,Cv_eV_per_K\n";
    for (const auto& p : result_.points) {
        out << p.temperatureK << ',' << p.internalEnergyEv << ','
            << p.freeEnergyEv << ',' << p.entropyEvPerK << ','
            << p.heatCapacityEvPerK << '\n';
    }
}

void PhononThermodynamicsDialog::exportImage()
{
    const QString path = QFileDialog::getSaveFileName(
        this, tr("Export Plot Image"),
        QStringLiteral("phonon_thermodynamics.png"),
        tr("PNG image (*.png);;SVG vector (*.svg)"));
    if (path.isEmpty())
        return;

    const QSizeF logical(plot_->width(), plot_->height());
    if (path.endsWith(QLatin1String(".svg"), Qt::CaseInsensitive)) {
        QSvgGenerator generator;
        generator.setFileName(path);
        generator.setSize(logical.toSize());
        generator.setViewBox(QRectF(QPointF(0, 0), logical));
        generator.setTitle(windowTitle());
        QPainter painter(&generator);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.fillRect(QRectF(QPointF(0, 0), logical), Qt::white);
        plot_->render(painter, QRectF(QPointF(0, 0), logical));
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
    plot_->render(painter, QRectF(QPointF(0, 0), QSizeF(image.size())));
    painter.end();
    if (!image.save(path))
        QMessageBox::warning(this, tr("Export Plot Image"),
                             tr("Could not write %1").arg(path));
}

} // namespace calango::gui
