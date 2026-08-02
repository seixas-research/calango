#include "gui/ConvergenceResultsWindow.hpp"

#include "gui/GuiUtils.hpp"

#include "gui/CalculatorParametersDialog.hpp"

#include <QCheckBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFile>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QMessageBox>
#include <QPainter>
#include <QPen>
#include <QPushButton>
#include <QTextStream>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>
#include <limits>

namespace calango::gui {

namespace {

/// Per-sweep vocabulary: the strings and JSON keys that differ between the
/// Parameters Convergence modules. Everything else in this window is shared.
struct SweepVocabulary {
    const char* resultsFile;
    const char* xKey;       ///< per-point JSON member holding the x value
    QString windowTitle;
    QString parameterNoun;  ///< "Cutoff" / "k-point Mesh" — column titles
    QString xAxisLabel;
    QString csvXColumn;
    QString exportBase;     ///< CSV/image file-name stem
};

SweepVocabulary vocabularyFor(ConvergenceResultsWindow::Sweep sweep)
{
    using Sweep = ConvergenceResultsWindow::Sweep;
    if (sweep == Sweep::KpointGrid)
        return {"kpoints_convergence.json",
                "k_per_axis",
                QObject::tr("K-points Convergence"),
                QObject::tr("k-point Mesh"),
                QObject::tr("k-points per swept axis"),
                QStringLiteral("k_per_axis"),
                QStringLiteral("kpoints_convergence")};
    return {"cutoff_convergence.json",
            "ecut_eV",
            QObject::tr("Plane-wave Cutoff Convergence"),
            QObject::tr("Cutoff"),
            QObject::tr("Plane-wave cutoff (eV)"),
            QStringLiteral("ecut_eV"),
            QStringLiteral("cutoff_convergence")};
}

/// Column title, y-axis label, CSV column and export suffix per quantity.
struct QuantityVocabulary {
    QString title;         ///< "%1" is the sweep's parameter noun
    QString yAxisLabel;
    QString csvColumn;
    QString exportSuffix;
};

QuantityVocabulary vocabularyFor(ConvergenceResultsWindow::Quantity quantity)
{
    using Quantity = ConvergenceResultsWindow::Quantity;
    switch (quantity) {
    case Quantity::ForceError:
        return {QObject::tr("Force Error vs. %1"),
                QObject::tr("max |Fᵢ − Fᵢ,ref| (meV/Å)"),
                QStringLiteral("force_error_meV_per_A"),
                QStringLiteral("forces")};
    case Quantity::EigenvalueMad:
        return {QObject::tr("Eigenvalue Convergence vs. %1"),
                QObject::tr("band-energy MAD (meV)"),
                QStringLiteral("eigenvalue_mad_meV"),
                QStringLiteral("eigenvalues")};
    case Quantity::EnergyDelta:
        break;
    }
    return {QObject::tr("Energy Convergence vs. %1"),
            QObject::tr("ΔE per atom (meV)"),
            QStringLiteral("delta_energy_per_atom_meV"),
            QStringLiteral("energy")};
}

} // namespace

// ---------------------------------------------------------------------------
// ConvergencePlotWidget
// ---------------------------------------------------------------------------

ConvergencePlotWidget::ConvergencePlotWidget(QWidget* parent)
    : QWidget(parent)
{
    // Narrow enough that three panels fit a laptop screen side by side.
    setMinimumSize(340, 300);
}

void ConvergencePlotWidget::setData(std::vector<double> x,
                                    std::vector<double> y)
{
    x_ = std::move(x);
    y_ = std::move(y);
    update();
}

void ConvergencePlotWidget::setLabels(const QString& xLabel,
                                      const QString& yLabel)
{
    xLabel_ = xLabel;
    yLabel_ = yLabel;
    update();
}

void ConvergencePlotWidget::setStyle(const OpticsPlotStyle& style)
{
    style_ = style;
    update();
}

void ConvergencePlotWidget::setThresholdBand(double low, double high,
                                             bool visible)
{
    bandLow_ = std::min(low, high);
    bandHigh_ = std::max(low, high);
    bandVisible_ = visible;
    update();
}

void ConvergencePlotWidget::setFixedYRange(double minimum, double maximum,
                                           bool enabled)
{
    fixedYMin_ = std::min(minimum, maximum);
    fixedYMax_ = std::max(minimum, maximum);
    fixedYEnabled_ = enabled && fixedYMax_ > fixedYMin_;
    update();
}

void ConvergencePlotWidget::paintEvent(QPaintEvent*)
{
    QPainter painter(this);
    renderTo(painter, size());
}

bool ConvergencePlotWidget::renderTo(QPainter& p, QSize size) const
{
    p.setRenderHint(QPainter::Antialiasing, true);
    p.fillRect(QRect(QPoint(0, 0), size), style_.canvasBackground);

    const double W = size.width();
    const double H = size.height();
    const QRectF plot(74.0, 24.0, W - 74.0 - 20.0, H - 24.0 - 52.0);

    // A curve needs at least one finite sample; a column whose metric never
    // materialized (eigenvalues unavailable) shows the placeholder instead.
    const bool anyFinite = std::any_of(y_.begin(), y_.end(), [](double v) {
        return std::isfinite(v);
    });
    if (x_.empty() || !anyFinite || plot.width() < 20.0
        || plot.height() < 20.0) {
        p.setPen(QColor(120, 120, 120));
        p.drawText(QRect(QPoint(0, 0), size), Qt::AlignCenter,
                   QObject::tr("No data to display"));
        return false;
    }

    double xMin = x_.front();
    double xMax = x_.front();
    for (double v : x_) {
        xMin = std::min(xMin, v);
        xMax = std::max(xMax, v);
    }
    double yMin = std::numeric_limits<double>::infinity();
    double yMax = -std::numeric_limits<double>::infinity();
    if (fixedYEnabled_) {
        // Pinned by the σ×threshold zoom — exactly the requested window, no
        // padding: the whole point is a scale defined by the criterion
        // rather than by the worst point. Data outside draws clipped.
        yMin = fixedYMin_;
        yMax = fixedYMax_;
    } else {
        for (double v : y_) {
            if (std::isfinite(v)) {
                yMin = std::min(yMin, v);
                yMax = std::max(yMax, v);
            }
        }
        // The threshold corridor belongs in the visible range: judging
        // convergence against a band that is off-scale would be guesswork.
        if (bandVisible_) {
            yMin = std::min(yMin, bandLow_);
            yMax = std::max(yMax, bandHigh_);
        }
    }
    if (!(xMax > xMin))
        xMax = xMin + 1.0;
    if (!std::isfinite(yMin) || !std::isfinite(yMax)) {
        yMin = 0.0;
        yMax = 1.0;
    }
    if (!(yMax > yMin))
        yMax = yMin + std::max(1e-12, std::abs(yMin) * 1e-6);
    if (!fixedYEnabled_) {
        const double pad = (yMax - yMin) * 0.08;
        yMin -= pad;
        yMax += pad;
    }

    const auto mapX = [&](double v) {
        return plot.left() + (v - xMin) / (xMax - xMin) * plot.width();
    };
    const auto mapY = [&](double v) {
        return plot.bottom() - (v - yMin) / (yMax - yMin) * plot.height();
    };

    p.fillRect(plot, style_.plotBackground);

    // Grid, ticks and tick labels. 'g' with 6 digits rather than the usual 4:
    // consecutive deltas can differ in the 5th significant figure, and
    // rounding them to the same label would show a flat axis on a curve that
    // visibly moves.
    const int ticks = 5;
    for (int i = 0; i <= ticks; ++i) {
        const double fx = xMin + (xMax - xMin) * i / ticks;
        const double px = mapX(fx);
        if (style_.showGrid) {
            p.setPen(QPen(style_.effectiveGridColor(), 1.0));
            p.drawLine(QPointF(px, plot.top()), QPointF(px, plot.bottom()));
        }
        p.setFont(style_.axisFont());
        p.setPen(style_.axisLabelColor);
        p.drawText(QRectF(px - 40.0, plot.bottom() + 4.0, 80.0, 16.0),
                   Qt::AlignHCenter | Qt::AlignTop,
                   QString::number(fx, 'g', 5));

        const double fy = yMin + (yMax - yMin) * i / ticks;
        const double py = mapY(fy);
        if (style_.showGrid) {
            p.setPen(QPen(style_.effectiveGridColor(), 1.0));
            p.drawLine(QPointF(plot.left(), py), QPointF(plot.right(), py));
        }
        p.setPen(style_.axisLabelColor);
        p.drawText(QRectF(2.0, py - 8.0, plot.left() - 8.0, 16.0),
                   Qt::AlignRight | Qt::AlignVCenter,
                   QString::number(fy, 'g', 6));
    }

    // The convergence corridor, under the curve so it reads as a region the
    // data passes through rather than a stripe painted over it. Color,
    // pattern and opacity come from Customize Appearance.
    if (bandVisible_) {
        p.save();
        p.setClipRect(plot);
        const QRectF band(plot.left(), mapY(bandHigh_), plot.width(),
                          std::max(1.0, mapY(bandLow_) - mapY(bandHigh_)));
        const QColor hatch = style_.effectiveThresholdBandColor();
        p.fillRect(band, QBrush(hatch, style_.thresholdBandPattern));
        QPen edge(hatch, 1.0, Qt::DashLine);
        p.setPen(edge);
        p.drawLine(QPointF(plot.left(), mapY(bandLow_)),
                   QPointF(plot.right(), mapY(bandLow_)));
        p.drawLine(QPointF(plot.left(), mapY(bandHigh_)),
                   QPointF(plot.right(), mapY(bandHigh_)));
        p.restore();
    }

    // Δ = 0 is the reference itself — always meaningful on a difference
    // plot, so it gets a line whenever it is in range.
    if (yMin < 0.0 && yMax > 0.0) {
        p.setPen(QPen(QColor(150, 150, 150), 1.0, Qt::DashLine));
        const double py = mapY(0.0);
        p.drawLine(QPointF(plot.left(), py), QPointF(plot.right(), py));
    }

    // Axis frame on top of the grid.
    p.setPen(QPen(QColor(60, 60, 60), 1.2));
    p.setBrush(Qt::NoBrush);
    p.drawRect(plot);

    // The curve, clipped to the plot rectangle, with a marker on every
    // evaluated point — each one is one full SCF, and the eye should land on
    // what was computed rather than on the interpolation between.
    p.save();
    p.setClipRect(plot);
    const QColor stroke = style_.overrideCurveColor
        ? style_.curveColor
        : QColor(0x1f, 0x77, 0xb4);
    QPen pen(stroke);
    pen.setWidthF(style_.lineWidth);
    pen.setStyle(style_.lineStyle);
    p.setPen(pen);
    QPolygonF poly;
    const std::size_t n = std::min(x_.size(), y_.size());
    poly.reserve(static_cast<int>(n));
    for (std::size_t i = 0; i < n; ++i) {
        if (!std::isfinite(y_[i]))
            continue;
        poly << QPointF(mapX(x_[i]), mapY(y_[i]));
    }
    p.drawPolyline(poly);
    p.setPen(QPen(stroke, 1.0));
    p.setBrush(stroke);
    const double radius = std::max(2.5, style_.lineWidth + 1.2);
    for (const QPointF& point : poly)
        p.drawEllipse(point, radius, radius);
    p.restore();

    // Axis titles, in the configured face and colour.
    p.setFont(style_.axisFont());
    p.setPen(style_.axisLabelColor);
    p.drawText(QRectF(plot.left(), H - 20.0, plot.width(), 18.0),
               Qt::AlignHCenter, xLabel_);
    p.save();
    p.translate(16.0, plot.center().y());
    p.rotate(-90.0);
    p.drawText(QRectF(-plot.height() / 2.0, -8.0, plot.height(), 16.0),
               Qt::AlignHCenter, yLabel_);
    p.restore();
    return true;
}

// ---------------------------------------------------------------------------
// ConvergenceResultsWindow
// ---------------------------------------------------------------------------

ConvergenceResultsWindow::ConvergenceResultsWindow(Sweep sweep,
                                                   const QString& directory,
                                                   QWidget* parent)
    : QDialog(parent)
    , sweep_(sweep)
    , directory_(directory)
{
    const SweepVocabulary words = vocabularyFor(sweep_);
    hasData_ = loadResults(directory + QLatin1Char('/')
                           + QLatin1String(words.resultsFile));
    if (!hasData_)
        return;

    setWindowTitle(words.windowTitle);
    resize(1380, 620);

    auto* layout = new QVBoxLayout(this);

    auto* intro = new QLabel(
        tr("Everything is differenced against the best run in the set (%1): "
           "ΔE = (E − E_ref)/N per atom, the force error is the largest "
           "atom-wise |F − F_ref| (not divided by N), and the eigenvalue "
           "panel tracks the mean absolute drift of the k-averaged band "
           "energies. \"Converged\" is where each curve enters its hatched "
           "corridor and stays.")
            .arg(referenceLabel_),
        this);
    intro->setWordWrap(true);
    layout->addWidget(intro);

    auto* columns = new QHBoxLayout;
    columns->addWidget(buildColumn(Quantity::EnergyDelta, energyPlot_), 1);
    columns->addWidget(buildColumn(Quantity::ForceError, forcePlot_), 1);
    columns->addWidget(buildColumn(Quantity::EigenvalueMad, eigenPlot_), 1);
    layout->addLayout(columns, 1);

    const auto applyData = [this, &words](Quantity quantity,
                                          ConvergencePlotWidget* plot) {
        plot->setData(xValues_, values(quantity));
        plot->setLabels(words.xAxisLabel,
                        vocabularyFor(quantity).yAxisLabel);
    };
    applyData(Quantity::EnergyDelta, energyPlot_);
    applyData(Quantity::ForceError, forcePlot_);
    applyData(Quantity::EigenvalueMad, eigenPlot_);

    // -- Threshold controls ---------------------------------------------------
    auto* thresholds = new QHBoxLayout;

    thresholdCheck_ = new QCheckBox(tr("Convergence thresholds:"), this);
    thresholdCheck_->setToolTip(
        tr("Hatch the corridor within which a sweep point counts as "
           "converged: |ΔE| ≤ threshold on the energy panel, force error ≤ "
           "threshold on the force panel, band-energy MAD ≤ threshold on "
           "the eigenvalue panel."));
    // On by default: the corridor is the criterion, and a convergence plot
    // without a criterion invites reading the answer off the flattest-
    // looking stretch.
    thresholdCheck_->setChecked(true);
    thresholds->addWidget(thresholdCheck_);

    energyThresholdSpin_ = new QDoubleSpinBox(this);
    energyThresholdSpin_->setRange(0.001, 1000.0);
    energyThresholdSpin_->setDecimals(3);
    energyThresholdSpin_->setValue(1.0);
    energyThresholdSpin_->setSuffix(tr(" meV/atom"));
    energyThresholdSpin_->setToolTip(
        tr("Half-width of the energy corridor. 1 meV/atom is a common "
           "production target for total-energy differences."));
    thresholds->addWidget(new QLabel(tr("Energy"), this));
    thresholds->addWidget(energyThresholdSpin_);

    forceThresholdSpin_ = new QDoubleSpinBox(this);
    forceThresholdSpin_->setRange(0.001, 1000.0);
    forceThresholdSpin_->setDecimals(3);
    forceThresholdSpin_->setValue(10.0);
    forceThresholdSpin_->setSuffix(tr(" meV/Å"));
    forceThresholdSpin_->setToolTip(
        tr("Ceiling of the force-error corridor. 10 meV/Å (0.01 eV/Å) is a "
           "typical tolerance for forces feeding a relaxation."));
    thresholds->addWidget(new QLabel(tr("Force"), this));
    thresholds->addWidget(forceThresholdSpin_);

    eigenThresholdSpin_ = new QDoubleSpinBox(this);
    eigenThresholdSpin_->setRange(0.001, 1000.0);
    eigenThresholdSpin_->setDecimals(3);
    eigenThresholdSpin_->setValue(10.0);
    eigenThresholdSpin_->setSuffix(tr(" meV"));
    eigenThresholdSpin_->setToolTip(
        tr("Ceiling of the eigenvalue corridor: the mean absolute drift of "
           "the k-averaged band energies. 10 meV holds spectral features "
           "(gaps, band edges) steady on the scale optical work reads "
           "them."));
    thresholds->addWidget(new QLabel(tr("Eigenvalues"), this));
    thresholds->addWidget(eigenThresholdSpin_);

    thresholds->addStretch(1);
    layout->addLayout(thresholds);

    // -- y-zoom + appearance + close ------------------------------------------
    auto* controls = new QHBoxLayout;

    scaleCheck_ = new QCheckBox(tr("Clamp y-axis to ±σ × threshold, σ ="),
                                this);
    scaleCheck_->setToolTip(
        tr("Pin each panel's y-axis to [−σ·τ, +σ·τ] of its own threshold τ. "
           "Without it the far-from-converged early points set the scale "
           "and flatten the tail — which is where convergence is decided — "
           "into a line. Off-scale points draw clipped."));
    controls->addWidget(scaleCheck_);
    sigmaSpin_ = new QDoubleSpinBox(this);
    sigmaSpin_->setRange(0.5, 100.0);
    sigmaSpin_->setDecimals(1);
    sigmaSpin_->setSingleStep(0.5);
    sigmaSpin_->setValue(5.0);
    controls->addWidget(sigmaSpin_);

    controls->addStretch(1);

    // The cutoff study ends with a number worth keeping: the converged
    // cutoff. This is the shortest path from reading it off the curve to
    // every future wizard opening on it (per element, via
    // ~/.calango/calculator_parameters.json).
    if (sweep_ == Sweep::PlaneWaveCutoff) {
        auto* parametersButton =
            new QPushButton(tr("Define calculator settings…"), this);
        parametersButton->setToolTip(
            tr("Edit the per-element suggested defaults (plane-wave cutoff, "
               "k-point mesh) the simulation wizards open with."));
        controls->addWidget(parametersButton);
        connect(parametersButton, &QPushButton::clicked, this, [this] {
            auto* dialog = new CalculatorParametersDialog(this);
            dialog->setAttribute(Qt::WA_DeleteOnClose);
            dialog->show();
        });
    }

    auto* styleButton = new QPushButton(tr("Customize Appearance…"), this);
    controls->addWidget(styleButton);
    connect(styleButton, &QPushButton::clicked, this,
            &ConvergenceResultsWindow::customizeAppearance);

    auto* closeButton = new QPushButton(tr("Close"), this);
    // Wired to close() by explicit click, not through QDialogButtonBox
    // accept/reject roles — and see the autoDefault sweep below for why no
    // button in this window may be a default.
    connect(closeButton, &QPushButton::clicked, this, &QWidget::close);
    controls->addWidget(closeButton);
    layout->addLayout(controls);

    thresholdSummary_ = new QLabel(this);
    thresholdSummary_->setWordWrap(true);
    thresholdSummary_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    layout->addWidget(thresholdSummary_);

    connect(thresholdCheck_, &QCheckBox::toggled, this,
            &ConvergenceResultsWindow::updateThresholdBands);
    connect(scaleCheck_, &QCheckBox::toggled, this,
            &ConvergenceResultsWindow::updateThresholdBands);
    for (QDoubleSpinBox* spin : {energyThresholdSpin_, forceThresholdSpin_,
                                 eigenThresholdSpin_, sigmaSpin_})
        connect(spin, &QDoubleSpinBox::valueChanged, this,
                &ConvergenceResultsWindow::updateThresholdBands);

    // In a QDialog every push button is autoDefault, so Return anywhere —
    // including finishing a threshold edit in a spin box — "clicks" the
    // first such button. That is how editing a value could close the whole
    // window. No button here earns Return.
    for (QPushButton* button : findChildren<QPushButton*>()) {
        button->setAutoDefault(false);
        button->setDefault(false);
    }

    for (ConvergencePlotWidget* plot : {energyPlot_, forcePlot_, eigenPlot_})
        plot->setStyle(style_);
    updateThresholdBands();
}

bool ConvergenceResultsWindow::loadResults(const QString& jsonPath)
{
    QFile file(jsonPath);
    if (!file.open(QIODevice::ReadOnly))
        return false;
    const SweepVocabulary words = vocabularyFor(sweep_);
    const QJsonObject root = QJsonDocument::fromJson(file.readAll()).object();
    const QJsonObject summary =
        root.value(QStringLiteral("summary")).toObject();
    const QJsonObject reference =
        summary.value(QStringLiteral("reference")).toObject();

    const auto meshText = [](const QJsonObject& point) {
        const QJsonArray kpts = point.value(QStringLiteral("kpts")).toArray();
        if (kpts.size() != 3)
            return QString();
        return QStringLiteral("%1×%2×%3")
            .arg(kpts.at(0).toInt())
            .arg(kpts.at(1).toInt())
            .arg(kpts.at(2).toInt());
    };
    if (sweep_ == Sweep::KpointGrid)
        referenceLabel_ = tr("%1 mesh").arg(meshText(reference));
    else
        referenceLabel_ = tr("%1 eV cutoff")
                              .arg(reference.value(QLatin1String(words.xKey))
                                       .toDouble(),
                                   0, 'f', 0);

    for (const QJsonValue& value :
         root.value(QStringLiteral("points")).toArray()) {
        const QJsonObject point = value.toObject();
        // A failed SCF has no energy to plot; skipping it keeps the vectors
        // aligned by index.
        if (point.contains(QStringLiteral("error"))
            || !point.contains(QStringLiteral("delta_energy_per_atom_eV")))
            continue;
        xValues_.push_back(
            point.value(QLatin1String(words.xKey)).toDouble());
        xTexts_.push_back(sweep_ == Sweep::KpointGrid ? meshText(point)
                                                      : QString());
        // eV → meV: the numbers on screen should read in the units the
        // criteria are quoted in.
        deltaEnergyMevPerAtom_.push_back(
            point.value(QStringLiteral("delta_energy_per_atom_eV")).toDouble()
            * 1000.0);
        // Vector-wise force error; a results file from before the metric
        // existed falls back to the max|F| difference rather than dropping
        // the panel.
        const QJsonValue forceError =
            point.value(QStringLiteral("force_error_eV_per_A"));
        forceErrorMevPerA_.push_back(
            (forceError.isDouble()
                 ? forceError.toDouble()
                 : std::abs(point.value(QStringLiteral("delta_fmax_eV_per_A"))
                                .toDouble()))
            * 1000.0);
        const QJsonValue eigen =
            point.value(QStringLiteral("eigenvalue_mad_eV"));
        eigenvalueMadMev_.push_back(
            eigen.isDouble() ? eigen.toDouble() * 1000.0
                             : std::numeric_limits<double>::quiet_NaN());
    }
    return !xValues_.empty();
}

const std::vector<double>&
ConvergenceResultsWindow::values(Quantity quantity) const
{
    switch (quantity) {
    case Quantity::ForceError:
        return forceErrorMevPerA_;
    case Quantity::EigenvalueMad:
        return eigenvalueMadMev_;
    case Quantity::EnergyDelta:
        break;
    }
    return deltaEnergyMevPerAtom_;
}

QString ConvergenceResultsWindow::xValueLabel(std::size_t index) const
{
    if (index >= xValues_.size())
        return QString();
    if (sweep_ == Sweep::KpointGrid && !xTexts_[index].isEmpty())
        return xTexts_[index];
    return tr("%1 eV").arg(xValues_[index], 0, 'f', 0);
}

QWidget* ConvergenceResultsWindow::buildColumn(Quantity quantity,
                                               ConvergencePlotWidget*& plot)
{
    const SweepVocabulary words = vocabularyFor(sweep_);
    auto* column = new QWidget(this);
    auto* layout = new QVBoxLayout(column);
    layout->setContentsMargins(0, 0, 0, 0);

    auto* title = new QLabel(
        vocabularyFor(quantity).title.arg(words.parameterNoun), column);
    QFont titleFont = title->font();
    titleFont.setBold(true);
    title->setFont(titleFont);
    title->setAlignment(Qt::AlignCenter);
    layout->addWidget(title);

    plot = new ConvergencePlotWidget(column);
    layout->addWidget(plot, 1);

    // Per-column exports: the panels have different y quantities and units,
    // so one combined file would concatenate tables most tools will not read
    // back as one.
    auto* actions = new QHBoxLayout;
    auto* csvButton = new QPushButton(tr("Export CSV…"), column);
    auto* imageButton = new QPushButton(tr("Export Image…"), column);
    imageButton->setToolTip(tr("PNG at 3x the on-screen size."));
    actions->addWidget(csvButton);
    actions->addWidget(imageButton);
    actions->addStretch(1);
    layout->addLayout(actions);
    connect(csvButton, &QPushButton::clicked, this,
            [this, quantity] { exportCsv(quantity); });
    connect(imageButton, &QPushButton::clicked, this,
            [this, quantity] { exportImage(quantity); });
    return column;
}

void ConvergenceResultsWindow::customizeAppearance()
{
    auto* dialog =
        new OpticsPlotStyleDialog(style_, /*withThresholdBand=*/true, this);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    // Live, not on-accept: the point of a styling dialog is to judge the
    // result against the real plot.
    connect(dialog, &OpticsPlotStyleDialog::styleChanged, this,
            [this](const OpticsPlotStyle& style) {
                style_ = style;
                for (ConvergencePlotWidget* plot :
                     {energyPlot_, forcePlot_, eigenPlot_})
                    plot->setStyle(style_);
            });
    dialog->show();
}

void ConvergenceResultsWindow::updateThresholdBands()
{
    const bool show = thresholdCheck_->isChecked();
    const double energyHalfWidthMev = energyThresholdSpin_->value();
    const double forceCeilingMev = forceThresholdSpin_->value();
    const double eigenCeilingMev = eigenThresholdSpin_->value();
    energyThresholdSpin_->setEnabled(show);
    forceThresholdSpin_->setEnabled(show);
    eigenThresholdSpin_->setEnabled(show);

    // Every curve is a difference against the reference, so the corridors
    // sit on zero: |ΔE| ≤ τ is symmetric; the force error and the
    // band-energy MAD are non-negative, so their corridors are one-sided.
    energyPlot_->setThresholdBand(-energyHalfWidthMev, energyHalfWidthMev,
                                  show);
    forcePlot_->setThresholdBand(0.0, forceCeilingMev, show);
    eigenPlot_->setThresholdBand(0.0, eigenCeilingMev, show);

    // σ×threshold y-zoom, each panel against its own criterion. Independent
    // of whether the corridor is drawn — the scale is useful even with the
    // hatching off.
    const bool clamp = scaleCheck_->isChecked();
    const double sigma = sigmaSpin_->value();
    sigmaSpin_->setEnabled(clamp);
    energyPlot_->setFixedYRange(-sigma * energyHalfWidthMev,
                                sigma * energyHalfWidthMev, clamp);
    forcePlot_->setFixedYRange(-sigma * forceCeilingMev,
                               sigma * forceCeilingMev, clamp);
    eigenPlot_->setFixedYRange(-sigma * eigenCeilingMev,
                               sigma * eigenCeilingMev, clamp);

    if (!show) {
        thresholdSummary_->clear();
        return;
    }
    // Name the first sweep point from which EVERY later point stays inside
    // its corridor — "the curve entered the band and stayed" is the actual
    // convergence criterion, and eyeballing it off the plots invites the
    // one-point-dipped-in trap.
    const auto convergedFrom = [](const std::vector<double>& values,
                                  double ceiling) {
        for (std::size_t i = 0; i < values.size(); ++i) {
            bool inside = true;
            for (std::size_t j = i; j < values.size(); ++j)
                inside = inside && std::abs(values[j]) <= ceiling;
            if (inside)
                return i;
        }
        return values.size();
    };
    const std::size_t energyFrom =
        convergedFrom(deltaEnergyMevPerAtom_, energyHalfWidthMev);
    const std::size_t forceFrom =
        convergedFrom(forceErrorMevPerA_, forceCeilingMev);
    const std::size_t eigenFrom =
        convergedFrom(eigenvalueMadMev_, eigenCeilingMev);
    const auto describe = [this](std::size_t from) {
        if (from >= xValues_.size())
            return tr("not reached in this sweep");
        if (from + 1 == xValues_.size())
            return tr("only at the reference itself — extend the sweep");
        return tr("from %1").arg(xValueLabel(from));
    };
    // A NaN never satisfies |v| ≤ τ, so a sweep without eigenvalue data
    // would read "not reached" — say what actually happened instead.
    const bool anyEigen =
        std::any_of(eigenvalueMadMev_.begin(), eigenvalueMadMev_.end(),
                    [](double v) { return std::isfinite(v); });
    thresholdSummary_->setText(
        tr("Within threshold and staying there: energy %1; forces %2; "
           "eigenvalues %3.")
            .arg(describe(energyFrom), describe(forceFrom),
                 anyEigen ? describe(eigenFrom)
                          : tr("no eigenvalue data in this run")));
}

void ConvergenceResultsWindow::exportCsv(Quantity quantity)
{
    const SweepVocabulary words = vocabularyFor(sweep_);
    const QuantityVocabulary quantityWords = vocabularyFor(quantity);
    const QString path = QFileDialog::getSaveFileName(
        this, tr("Export Convergence Data"),
        QStringLiteral("%1_%2.csv")
            .arg(words.exportBase, quantityWords.exportSuffix),
        tr("CSV (*.csv)"));
    if (path.isEmpty())
        return;

    writeTextFile(this, path, [&](QTextStream& out) {
        // A CSV starts with its header row — the reference is recoverable
        // from the data itself (the row whose deltas are zero).
        out << words.csvXColumn;
        // A mesh is three numbers, not one — the sweep index alone would
        // hide the pinned axes of a slab sweep.
        if (sweep_ == Sweep::KpointGrid)
            out << ",kpts";
        out << ',' << quantityWords.csvColumn << '\n';
        const std::vector<double>& series = values(quantity);
        for (std::size_t i = 0; i < xValues_.size(); ++i) {
            out << QString::number(xValues_[i], 'g', 6);
            if (sweep_ == Sweep::KpointGrid)
                out << ',' << xTexts_[i];
            // An unavailable metric exports as an empty cell, not a fake
            // zero.
            out << ',';
            if (std::isfinite(series[i]))
                out << QString::number(series[i], 'g', 10);
            out << '\n';
        }
    });
}

void ConvergenceResultsWindow::exportImage(Quantity quantity)
{
    const SweepVocabulary words = vocabularyFor(sweep_);
    ConvergencePlotWidget* plot = energyPlot_;
    if (quantity == Quantity::ForceError)
        plot = forcePlot_;
    else if (quantity == Quantity::EigenvalueMad)
        plot = eigenPlot_;
    const QString path = QFileDialog::getSaveFileName(
        this, tr("Export Image"),
        QStringLiteral("%1_%2.png")
            .arg(words.exportBase, vocabularyFor(quantity).exportSuffix),
        tr("PNG image (*.png);;JPEG image (*.jpg *.jpeg)"));
    if (path.isEmpty())
        return;

    // Render at 3x the on-screen size for a crisp, print-quality raster.
    const int scale = 3;
    const QSize logical = plot->size();
    QImage image(logical * scale, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::white);
    QPainter painter(&image);
    painter.scale(scale, scale);
    plot->renderTo(painter, logical);
    painter.end();

    if (!image.save(path))
        QMessageBox::warning(this, tr("Export Image"),
                             tr("Could not write the image to %1.").arg(path));
}

} // namespace calango::gui
