#include "gui/ConvergenceResultsWindow.hpp"

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
#include <QSaveFile>
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
    if (!(xMax > xMin))
        xMax = xMin + 1.0;
    if (!std::isfinite(yMin) || !std::isfinite(yMax)) {
        yMin = 0.0;
        yMax = 1.0;
    }
    if (!(yMax > yMin))
        yMax = yMin + std::max(1e-12, std::abs(yMin) * 1e-6);
    const double pad = (yMax - yMin) * 0.08;
    yMin -= pad;
    yMax += pad;

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

    // -- Threshold + appearance controls -------------------------------------
    auto* controls = new QHBoxLayout;

    thresholdCheck_ = new QCheckBox(tr("Convergence thresholds:"), this);
    thresholdCheck_->setToolTip(
        tr("Hatch the corridor within which a sweep point counts as "
           "converged: |ΔE| ≤ threshold on the energy panel, force error ≤ "
           "threshold on the force panel."));
    // On by default: the corridor is the criterion, and a convergence plot
    // without a criterion invites reading the answer off the flattest-
    // looking stretch.
    thresholdCheck_->setChecked(true);
    controls->addWidget(thresholdCheck_);

    energyThresholdSpin_ = new QDoubleSpinBox(this);
    energyThresholdSpin_->setRange(0.001, 1000.0);
    energyThresholdSpin_->setDecimals(3);
    energyThresholdSpin_->setValue(1.0);
    energyThresholdSpin_->setSuffix(tr(" meV/atom"));
    energyThresholdSpin_->setToolTip(
        tr("Half-width of the energy corridor. 1 meV/atom is a common "
           "production target for total-energy differences."));
    controls->addWidget(new QLabel(tr("Energy"), this));
    controls->addWidget(energyThresholdSpin_);

    forceThresholdSpin_ = new QDoubleSpinBox(this);
    forceThresholdSpin_->setRange(0.001, 1000.0);
    forceThresholdSpin_->setDecimals(3);
    forceThresholdSpin_->setValue(10.0);
    forceThresholdSpin_->setSuffix(tr(" meV/Å"));
    forceThresholdSpin_->setToolTip(
        tr("Ceiling of the force-error corridor. 10 meV/Å (0.01 eV/Å) is a "
           "typical tolerance for forces feeding a relaxation."));
    controls->addWidget(new QLabel(tr("Force"), this));
    controls->addWidget(forceThresholdSpin_);

    controls->addStretch(1);

    auto* styleButton = new QPushButton(tr("Customize Appearance…"), this);
    controls->addWidget(styleButton);
    connect(styleButton, &QPushButton::clicked, this,
            &ConvergenceResultsWindow::customizeAppearance);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    controls->addWidget(buttons);
    layout->addLayout(controls);

    thresholdSummary_ = new QLabel(this);
    thresholdSummary_->setWordWrap(true);
    thresholdSummary_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    layout->addWidget(thresholdSummary_);

    connect(thresholdCheck_, &QCheckBox::toggled, this,
            &ConvergenceResultsWindow::updateThresholdBands);
    for (QDoubleSpinBox* spin : {energyThresholdSpin_, forceThresholdSpin_})
        connect(spin, &QDoubleSpinBox::valueChanged, this,
                &ConvergenceResultsWindow::updateThresholdBands);

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
    energyThresholdSpin_->setEnabled(show);
    forceThresholdSpin_->setEnabled(show);

    // Every curve is a difference against the reference, so the corridors
    // sit on zero: |ΔE| ≤ τ is symmetric, the force error is non-negative
    // so its corridor is one-sided. The eigenvalue panel carries no
    // criterion — it is a diagnostic, not a gate.
    energyPlot_->setThresholdBand(-energyHalfWidthMev, energyHalfWidthMev,
                                  show);
    forcePlot_->setThresholdBand(0.0, forceCeilingMev, show);

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
    const auto describe = [this](std::size_t from) {
        if (from >= xValues_.size())
            return tr("not reached in this sweep");
        if (from + 1 == xValues_.size())
            return tr("only at the reference itself — extend the sweep");
        return tr("from %1").arg(xValueLabel(from));
    };
    thresholdSummary_->setText(
        tr("Within threshold and staying there: energy %1; forces %2.")
            .arg(describe(energyFrom), describe(forceFrom)));
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

    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QMessageBox::warning(this, tr("Export CSV"),
                             tr("Could not open %1 for writing.").arg(path));
        return;
    }
    QTextStream out(&file);
    out << "# " << words.windowTitle << " (reference " << referenceLabel_
        << ")\n";
    out << words.csvXColumn;
    // A mesh is three numbers, not one — the sweep index alone would hide
    // the pinned axes of a slab sweep.
    if (sweep_ == Sweep::KpointGrid)
        out << ",kpts";
    out << ',' << quantityWords.csvColumn << '\n';
    const std::vector<double>& series = values(quantity);
    for (std::size_t i = 0; i < xValues_.size(); ++i) {
        out << QString::number(xValues_[i], 'g', 6);
        if (sweep_ == Sweep::KpointGrid)
            out << ',' << xTexts_[i];
        // An unavailable metric exports as an empty cell, not a fake zero.
        out << ',';
        if (std::isfinite(series[i]))
            out << QString::number(series[i], 'g', 10);
        out << '\n';
    }
    file.commit();
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
