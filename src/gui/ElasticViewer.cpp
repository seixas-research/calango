#include "gui/ElasticViewer.hpp"

#include "gui/PlotPalette.hpp"

#include <QApplication>
#include <QClipboard>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLabel>
#include <QPainter>
#include <QPushButton>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QTextStream>
#include <QVBoxLayout>
#include <QWidget>

#include <algorithm>
#include <cmath>

namespace calango::gui {

namespace {

constexpr const char* kVoigtHeaders[6] = {
    "1 (xx)", "2 (yy)", "3 (zz)", "4 (yz)", "5 (xz)", "6 (xy)"};

QVector<QVector<double>> toMatrix6x6(const QJsonValue& value)
{
    QVector<QVector<double>> m;
    for (const QJsonValue& row : value.toArray()) {
        QVector<double> r;
        for (const QJsonValue& cell : row.toArray())
            r.push_back(cell.toDouble());
        m.push_back(r);
    }
    return m;
}

void fillTensorTable(QTableWidget* table, const QVector<QVector<double>>& matrix)
{
    table->setRowCount(6);
    table->setColumnCount(6);
    QStringList headers;
    for (const char* h : kVoigtHeaders)
        headers << QString::fromLatin1(h);
    table->setHorizontalHeaderLabels(headers);
    table->setVerticalHeaderLabels(headers);
    for (int i = 0; i < 6 && i < matrix.size(); ++i)
        for (int j = 0; j < 6 && j < matrix[i].size(); ++j) {
            const double v = matrix[i][j];
            auto* item = new QTableWidgetItem(
                std::isnan(v) ? QStringLiteral("—") : QString::number(v, 'g', 4));
            table->setItem(i, j, item);
        }
    table->resizeColumnsToContents();
    table->horizontalHeader()->setStretchLastSection(true);
}

} // namespace

// --- ElasticPointPlot --------------------------------------------------------
//
// Adapted from PiezoelectricPointPlot, but with two overlay modes rather
// than one: a straight line for the stress-strain method's genuine linear
// fit (sigma_i = C_ij * eps_j), and a parabola for the energy-strain
// method's quadratic one (E = c0 + c1*eps + c2*eps^2, C_jj = 2*c2/V0) — a
// straight-line overlay would misrepresent the second case, whose fit is
// not linear at all. Kept as its own small class rather than extending
// PiezoelectricPointPlot's public contract (its caption text is
// piezoelectric-specific wording, "e_ij", not reusable for C_ij/stress/
// energy without risking that module's own display).
class ElasticPointPlot : public QWidget {
public:
    explicit ElasticPointPlot(QWidget* parent = nullptr) : QWidget(parent)
    {
        setMinimumHeight(220);
    }

    void setLinearSeries(std::vector<double> eps, std::vector<double> values,
                         double slope, double intercept, const QString& caption)
    {
        eps_ = std::move(eps);
        values_ = std::move(values);
        quadratic_ = false;
        a_ = intercept;
        b_ = slope;
        c_ = 0.0;
        caption_ = caption;
        update();
    }

    void setQuadraticSeries(std::vector<double> eps, std::vector<double> values,
                            double c2, double c1, double c0, const QString& caption)
    {
        eps_ = std::move(eps);
        values_ = std::move(values);
        quadratic_ = true;
        a_ = c0;
        b_ = c1;
        c_ = c2;
        caption_ = caption;
        update();
    }

    QSize sizeHint() const override { return {480, 300}; }

protected:
    void paintEvent(QPaintEvent*) override
    {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.fillRect(rect(), PlotPalette::canvas);

        if (eps_.empty() || eps_.size() != values_.size()) {
            painter.setPen(PlotPalette::placeholder);
            painter.drawText(rect(), Qt::AlignCenter,
                             tr("No strain series for this component."));
            return;
        }

        const auto fit = [&](double x) { return a_ + b_ * x + c_ * x * x; };

        const double margin = 48.0;
        const QRectF plotRect(margin, 12.0, width() - margin - 16.0, height() - margin - 12.0);

        double xMin = *std::min_element(eps_.begin(), eps_.end());
        double xMax = *std::max_element(eps_.begin(), eps_.end());
        double yMin = *std::min_element(values_.begin(), values_.end());
        double yMax = *std::max_element(values_.begin(), values_.end());
        yMin = std::min({yMin, fit(xMin), fit(xMax)});
        yMax = std::max({yMax, fit(xMin), fit(xMax)});
        if (xMax - xMin < 1e-12) { xMin -= 1.0; xMax += 1.0; }
        if (yMax - yMin < 1e-18) { yMin -= 1.0; yMax += 1.0; }
        const double xPad = (xMax - xMin) * 0.12;
        const double yPad = (yMax - yMin) * 0.12;
        xMin -= xPad; xMax += xPad; yMin -= yPad; yMax += yPad;

        const auto toPoint = [&](double x, double y) {
            const double px = plotRect.left() + (x - xMin) / (xMax - xMin) * plotRect.width();
            const double py = plotRect.bottom() - (y - yMin) / (yMax - yMin) * plotRect.height();
            return QPointF(px, py);
        };

        painter.setPen(PlotPalette::grid);
        for (int i = 0; i <= 4; ++i) {
            const double frac = i / 4.0;
            painter.drawLine(QPointF(plotRect.left(), plotRect.top() + frac * plotRect.height()),
                             QPointF(plotRect.right(), plotRect.top() + frac * plotRect.height()));
        }

        painter.setPen(PlotPalette::spine);
        painter.drawRect(plotRect);

        painter.setPen(PlotPalette::tickText);
        painter.drawText(QRectF(0, plotRect.top() - 6, margin - 6, 14), Qt::AlignRight,
                         QString::number(yMax, 'g', 3));
        painter.drawText(QRectF(0, plotRect.bottom() - 8, margin - 6, 14), Qt::AlignRight,
                         QString::number(yMin, 'g', 3));
        painter.drawText(QRectF(plotRect.left() - 30, plotRect.bottom() + 4, 60, 14),
                         Qt::AlignCenter, QString::number(xMin, 'g', 3));
        painter.drawText(QRectF(plotRect.right() - 30, plotRect.bottom() + 4, 60, 14),
                         Qt::AlignCenter, QString::number(xMax, 'g', 3));

        // Fitted curve — the SAME coefficients the tensor entry was
        // assembled from, so this plot and the table can never disagree.
        // A straight line needs only its two endpoints; the parabola is
        // sampled at enough points to look smooth.
        painter.setPen(QPen(PlotPalette::seriesAlt, 1.6));
        if (quadratic_) {
            QPolygonF curve;
            constexpr int kSamples = 40;
            for (int i = 0; i <= kSamples; ++i) {
                const double x = xMin + xPad + (xMax - xPad - (xMin + xPad)) * i / kSamples;
                curve << toPoint(x, fit(x));
            }
            painter.drawPolyline(curve);
        } else {
            painter.drawLine(toPoint(xMin + xPad, fit(xMin + xPad)),
                             toPoint(xMax - xPad, fit(xMax - xPad)));
        }

        painter.setPen(QPen(PlotPalette::series, 1.4));
        painter.setBrush(PlotPalette::series);
        for (std::size_t i = 0; i < eps_.size(); ++i) {
            const QPointF p = toPoint(eps_[i], values_[i]);
            painter.drawEllipse(p, 4.0, 4.0);
        }

        painter.setPen(PlotPalette::text);
        painter.drawText(QRectF(plotRect.left(), 0, plotRect.width(), 14), Qt::AlignCenter,
                         caption_);
    }

private:
    std::vector<double> eps_;
    std::vector<double> values_;
    bool quadratic_ = false;
    double a_ = 0.0, b_ = 0.0, c_ = 0.0;
    QString caption_;
};

// --- ElasticViewer -----------------------------------------------------------

ElasticViewer::ElasticViewer(QWidget* parent) : QDialog(parent)
{
    setWindowTitle(tr("Elastic Properties"));
    resize(920, 820);

    auto* layout = new QVBoxLayout(this);

    summaryLabel_ = new QLabel(this);
    summaryLabel_->setWordWrap(true);
    summaryLabel_->setTextFormat(Qt::RichText);
    layout->addWidget(summaryLabel_);

    auto* tensorRow = new QHBoxLayout();
    tensorRow->addWidget(new QLabel(tr("Show:"), this));
    tensorKindCombo_ = new QComboBox(this);
    tensorKindCombo_->addItem(tr("Symmetrized (recommended)"));
    tensorKindCombo_->addItem(tr("Raw"));
    tensorKindCombo_->setToolTip(
        tr("Symmetrized: the point-group average that zeroes whatever C_ij "
           "symmetry forbids and cleans up finite-strain numerical noise in "
           "the components it allows.\n\n"
           "Raw: the unaveraged fit, useful for judging how much noise the "
           "symmetrization actually removed."));
    tensorRow->addWidget(tensorKindCombo_);
    tensorRow->addStretch(1);
    layout->addLayout(tensorRow);

    tensorTable_ = new QTableWidget(this);
    tensorTable_->setSelectionBehavior(QAbstractItemView::SelectItems);
    tensorTable_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    tensorTable_->setMaximumHeight(230);
    layout->addWidget(tensorTable_);

    stabilityLabel_ = new QLabel(this);
    stabilityLabel_->setWordWrap(true);
    stabilityLabel_->setTextFormat(Qt::RichText);
    layout->addWidget(stabilityLabel_);

    stabilityTable_ = new QTableWidget(this);
    stabilityTable_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    stabilityTable_->setColumnCount(3);
    stabilityTable_->setHorizontalHeaderLabels(
        {tr("Criterion"), tr("Value"), tr("Satisfied")});
    stabilityTable_->horizontalHeader()->setStretchLastSection(true);
    stabilityTable_->setMaximumHeight(160);
    layout->addWidget(stabilityTable_);

    moduliLabel_ = new QLabel(this);
    moduliLabel_->setWordWrap(true);
    moduliLabel_->setTextFormat(Qt::RichText);
    layout->addWidget(moduliLabel_);

    auto* plotRow = new QHBoxLayout();
    plotRow->addWidget(new QLabel(tr("Strain component:"), this));
    componentCombo_ = new QComboBox(this);
    plotRow->addWidget(componentCombo_);
    plotRow->addWidget(new QLabel(tr("response:"), this));
    rowCombo_ = new QComboBox(this);
    plotRow->addWidget(rowCombo_);
    plotRow->addStretch(1);
    layout->addLayout(plotRow);
    connect(componentCombo_, &QComboBox::currentIndexChanged, this,
            &ElasticViewer::refreshPlot);
    connect(rowCombo_, &QComboBox::currentIndexChanged, this, &ElasticViewer::refreshPlot);

    plot_ = new ElasticPointPlot(this);
    layout->addWidget(plot_);

    connect(tensorKindCombo_, &QComboBox::currentIndexChanged, this,
            &ElasticViewer::refreshTensorTable);

    auto* buttonRow = new QHBoxLayout;
    auto* copyButton = new QPushButton(tr("Copy"), this);
    auto* csvButton = new QPushButton(tr("Export CSV…"), this);
    buttonRow->addWidget(copyButton);
    buttonRow->addWidget(csvButton);
    buttonRow->addStretch(1);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    buttonRow->addWidget(buttons);
    layout->addLayout(buttonRow);

    connect(copyButton, &QPushButton::clicked, this, &ElasticViewer::copyToClipboard);
    connect(csvButton, &QPushButton::clicked, this, &ElasticViewer::exportCsv);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
}

bool ElasticViewer::loadResults(const QString& jsonPath)
{
    QFile file(jsonPath);
    if (!file.open(QIODevice::ReadOnly))
        return false;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll());
    if (!document.isObject())
        return false;
    data_ = document.object();
    sourcePath_ = jsonPath;

    if (!data_.contains(QStringLiteral("C_symmetrized_GPa")))
        return false;

    const QString method = data_.value(QStringLiteral("method")).toString();
    const QString pointGroup = data_.value(QStringLiteral("point_group"))
                                    .toString(tr("(unknown — spglib unavailable)"));
    const bool relaxIons = data_.value(QStringLiteral("relax_ions")).toBool();
    QString summary =
        tr("<b>Method:</b> %1 &nbsp; <b>Point group:</b> %2 &nbsp; "
           "<b>strain delta:</b> %3 &nbsp; <b>points/component:</b> %4 "
           "&nbsp; <b>ions:</b> %5")
            .arg(method == QStringLiteral("stress") ? tr("stress-strain")
                                                     : tr("energy-strain"))
            .arg(pointGroup)
            .arg(data_.value(QStringLiteral("strain_magnitude")).toDouble(), 0, 'g', 3)
            .arg(data_.value(QStringLiteral("points_per_component")).toInt())
            .arg(relaxIons ? tr("relaxed") : tr("clamped"));

    const bool is2d = data_.value(QStringLiteral("is_2d")).toBool();
    if (is2d) {
        static const char* const axisNames[3] = {"a", "b", "c"};
        const int axis = data_.value(QStringLiteral("vacuum_axis")).toInt(-1);
        summary += tr(" &nbsp; <b style='color:#2a7a2a;'>2D system</b> "
                      "(vacuum along %1) — C<sub>ij</sub> reported per "
                      "volume (GPa) and per area (N/m)")
                       .arg(axis >= 0 && axis < 3 ? QString::fromLatin1(axisNames[axis])
                                                    : tr("?"));
    }
    const QJsonArray missing = data_.value(QStringLiteral("components_with_no_data")).toArray();
    if (!missing.isEmpty()) {
        QStringList names;
        for (const QJsonValue& v : missing) {
            const int voigt = v.toInt();
            names << (voigt >= 0 && voigt < 6 ? QString::fromLatin1(kVoigtHeaders[voigt])
                                               : QString::number(voigt));
        }
        summary += tr(" &nbsp; <b style='color:#c0392b;'>%n component(s) "
                      "have no data</b> (every strain point for %1 failed "
                      "— see log.txt)",
                      nullptr, static_cast<int>(missing.size()))
                       .arg(names.join(QStringLiteral(", ")));
    }
    const QJsonArray pairMissing = data_.value(QStringLiteral("pair_missing")).toArray();
    if (!pairMissing.isEmpty())
        summary += tr(" &nbsp; <b style='color:#c0392b;'>%n off-diagonal "
                      "pair(s) missing</b> (energy-strain combined-strain "
                      "point(s) failed)",
                      nullptr, static_cast<int>(pairMissing.size()));
    summaryLabel_->setText(summary);

    refreshTensorTable();
    refreshStabilityAndModuli();

    const QJsonObject perComponent = data_.value(QStringLiteral("per_component_response")).toObject();
    componentCombo_->clear();
    QStringList keys = perComponent.keys();
    std::sort(keys.begin(), keys.end(), [](const QString& a, const QString& b) {
        return a.toInt() < b.toInt();
    });
    for (const QString& key : keys) {
        const int voigt = key.toInt();
        if (voigt >= 0 && voigt < 6)
            componentCombo_->addItem(QString::fromLatin1(kVoigtHeaders[voigt]), key);
    }

    rowCombo_->blockSignals(true);
    rowCombo_->clear();
    if (method == QStringLiteral("stress")) {
        for (const char* h : kVoigtHeaders)
            rowCombo_->addItem(tr("sigma_%1").arg(QString::fromLatin1(h)));
    } else {
        rowCombo_->addItem(tr("Energy (eV)"));
    }
    rowCombo_->blockSignals(false);

    refreshPlot();
    return true;
}

void ElasticViewer::refreshTensorTable()
{
    const bool symmetrized = tensorKindCombo_->currentIndex() == 0;
    fillTensorTable(tensorTable_,
                    toMatrix6x6(data_.value(symmetrized ? QStringLiteral("C_symmetrized_GPa")
                                                         : QStringLiteral("C_raw_GPa"))));
}

void ElasticViewer::refreshStabilityAndModuli()
{
    const bool is2d = data_.value(QStringLiteral("is_2d")).toBool();

    if (is2d) {
        const QJsonObject stability = data_.value(QStringLiteral("born_stability_2d")).toObject();
        const bool stable = stability.value(QStringLiteral("stable")).toBool();
        stabilityLabel_->setText(
            tr("<b>2D Born stability:</b> %1")
                .arg(stable ? tr("<span style='color:#2a7a2a;'>STABLE</span>")
                            : tr("<span style='color:#c0392b;'>UNSTABLE</span>")));

        stabilityTable_->setRowCount(2);
        const auto fillRow = [&](int row, const QJsonObject& crit) {
            stabilityTable_->setItem(row, 0,
                new QTableWidgetItem(crit.value(QStringLiteral("expression")).toString()));
            stabilityTable_->setItem(row, 1,
                new QTableWidgetItem(QString::number(
                    crit.value(QStringLiteral("value")).toDouble(), 'g', 4)));
            const bool ok = crit.value(QStringLiteral("satisfied")).toBool();
            auto* item = new QTableWidgetItem(ok ? tr("yes") : tr("no"));
            item->setForeground(ok ? QColor(42, 122, 42) : QColor(192, 57, 43));
            stabilityTable_->setItem(row, 2, item);
        };
        fillRow(0, stability.value(QStringLiteral("positive_definite")).toObject());
        fillRow(1, stability.value(QStringLiteral("shear_positive")).toObject());

        const QJsonObject m = data_.value(QStringLiteral("moduli_2d")).toObject();
        moduliLabel_->setText(
            tr("<b>2D moduli:</b> layer modulus = %1 N/m &nbsp; "
               "E<sub>x</sub> = %2 N/m &nbsp; E<sub>y</sub> = %3 N/m &nbsp; "
               "nu<sub>xy</sub> = %4 &nbsp; nu<sub>yx</sub> = %5")
                .arg(m.value(QStringLiteral("layer_modulus_N_per_m")).toDouble(), 0, 'g', 4)
                .arg(m.value(QStringLiteral("young_x_N_per_m")).toDouble(), 0, 'g', 4)
                .arg(m.value(QStringLiteral("young_y_N_per_m")).toDouble(), 0, 'g', 4)
                .arg(m.value(QStringLiteral("poisson_xy")).toDouble(), 0, 'g', 3)
                .arg(m.value(QStringLiteral("poisson_yx")).toDouble(), 0, 'g', 3));
        return;
    }

    const QString crystalClass = data_.value(QStringLiteral("crystal_class")).toString();
    const QJsonValue stableGeneral = data_.value(QStringLiteral("stable_general"));
    const QJsonValue stableClass = data_.value(QStringLiteral("stable_crystal_class"));
    const auto verdictText = [](const QJsonValue& v) {
        if (v.isNull())
            return QObject::tr("not available");
        return v.toBool() ? QObject::tr("<span style='color:#2a7a2a;'>STABLE</span>")
                           : QObject::tr("<span style='color:#c0392b;'>UNSTABLE</span>");
    };
    stabilityLabel_->setText(
        tr("<b>General (eigenvalue) criterion:</b> %1 &nbsp;&nbsp; "
           "<b>Crystal-class (%2) criterion:</b> %3")
            .arg(verdictText(stableGeneral))
            .arg(crystalClass.isEmpty() ? tr("n/a") : crystalClass)
            .arg(verdictText(stableClass)));

    const QJsonArray criteria = data_.value(QStringLiteral("born_criteria")).toArray();
    stabilityTable_->setRowCount(criteria.size());
    for (int i = 0; i < criteria.size(); ++i) {
        const QJsonObject crit = criteria.at(i).toObject();
        stabilityTable_->setItem(i, 0,
            new QTableWidgetItem(crit.value(QStringLiteral("expression")).toString()));
        stabilityTable_->setItem(i, 1,
            new QTableWidgetItem(
                QString::number(crit.value(QStringLiteral("value")).toDouble(), 'g', 4)));
        const bool ok = crit.value(QStringLiteral("satisfied")).toBool();
        auto* item = new QTableWidgetItem(ok ? tr("yes") : tr("no"));
        item->setForeground(ok ? QColor(42, 122, 42) : QColor(192, 57, 43));
        stabilityTable_->setItem(i, 2, item);
    }

    const QJsonValue moduliValue = data_.value(QStringLiteral("moduli"));
    if (moduliValue.isNull() || !moduliValue.isObject()) {
        moduliLabel_->setText(tr("<i>Moduli unavailable (missing components or a "
                                 "singular tensor).</i>"));
        return;
    }
    const QJsonObject m = moduliValue.toObject();
    QString text = tr("<b>Moduli (GPa):</b> B<sub>Voigt</sub> = %1 &nbsp; "
                      "G<sub>Voigt</sub> = %2")
                       .arg(m.value(QStringLiteral("bulk_voigt_GPa")).toDouble(), 0, 'g', 4)
                       .arg(m.value(QStringLiteral("shear_voigt_GPa")).toDouble(), 0, 'g', 4);
    if (m.value(QStringLiteral("reuss_valid")).toBool()) {
        text += tr(" &nbsp; B<sub>Reuss</sub> = %1 &nbsp; G<sub>Reuss</sub> = %2 "
                   "&nbsp; B<sub>Hill</sub> = %3 &nbsp; G<sub>Hill</sub> = %4 "
                   "&nbsp; E<sub>Hill</sub> = %5 &nbsp; nu<sub>Hill</sub> = %6")
                    .arg(m.value(QStringLiteral("bulk_reuss_GPa")).toDouble(), 0, 'g', 4)
                    .arg(m.value(QStringLiteral("shear_reuss_GPa")).toDouble(), 0, 'g', 4)
                    .arg(m.value(QStringLiteral("bulk_hill_GPa")).toDouble(), 0, 'g', 4)
                    .arg(m.value(QStringLiteral("shear_hill_GPa")).toDouble(), 0, 'g', 4)
                    .arg(m.value(QStringLiteral("young_hill_GPa")).toDouble(), 0, 'g', 4)
                    .arg(m.value(QStringLiteral("poisson_hill")).toDouble(), 0, 'g', 3);
    } else {
        text += tr(" &nbsp; <i>Reuss/Hill unavailable (singular compliance).</i>");
    }
    moduliLabel_->setText(text);
}

void ElasticViewer::refreshPlot()
{
    if (componentCombo_->count() == 0) {
        plot_->setLinearSeries({}, {}, 0.0, 0.0, {});
        return;
    }
    const QString key = componentCombo_->currentData().toString();
    const int voigt = key.toInt();
    const QJsonObject entry =
        data_.value(QStringLiteral("per_component_response")).toObject().value(key).toObject();
    std::vector<double> eps;
    for (const QJsonValue& v : entry.value(QStringLiteral("eps")).toArray())
        eps.push_back(v.toDouble());

    const QString method = data_.value(QStringLiteral("method")).toString();
    if (method == QStringLiteral("stress")) {
        const int row = std::clamp(rowCombo_->currentIndex(), 0, 5);
        std::vector<double> values;
        for (const QJsonValue& r : entry.value(QStringLiteral("stress_GPa")).toArray()) {
            const QJsonArray triple = r.toArray();
            values.push_back(row < triple.size() ? triple.at(row).toDouble() : 0.0);
        }
        const bool symmetrized = tensorKindCombo_->currentIndex() == 0;
        const auto matrix = toMatrix6x6(
            data_.value(symmetrized ? QStringLiteral("C_symmetrized_GPa")
                                     : QStringLiteral("C_raw_GPa")));
        double slope = 0.0;
        if (row < matrix.size() && voigt >= 0 && voigt < matrix[row].size())
            slope = matrix[row][voigt];
        double meanEps = 0.0, meanY = 0.0;
        for (std::size_t i = 0; i < eps.size(); ++i) {
            meanEps += eps[i];
            meanY += values[i];
        }
        if (!eps.empty()) {
            meanEps /= double(eps.size());
            meanY /= double(eps.size());
        }
        const double intercept = meanY - slope * meanEps;
        plot_->setLinearSeries(eps, values, slope, intercept,
            tr("sigma_%1 vs. strain — slope (C_%1_%2) = %3 GPa")
                .arg(row + 1)
                .arg(voigt + 1)
                .arg(slope, 0, 'g', 4));
    } else {
        std::vector<double> values;
        for (const QJsonValue& v : entry.value(QStringLiteral("energy_eV")).toArray())
            values.push_back(v.toDouble());
        // Re-derive the SAME quadratic fit the C_jj entry came from (2*c2 =
        // curvature = C_jj * V0), purely for the overlay -- it must match
        // the tensor table, not offer a second opinion.
        double c0 = 0.0, c1 = 0.0, c2 = 0.0;
        if (eps.size() >= 3) {
            // Simple closed-form 3-unknown least squares via normal
            // equations -- mirrors core::quadraticCurvature's own
            // Cramer's-rule solve, restricted here to what the overlay
            // needs (all three coefficients, not just the curvature).
            double sx = 0, sx2 = 0, sx3 = 0, sx4 = 0, sy = 0, sxy = 0, sx2y = 0;
            const double n = double(eps.size());
            for (std::size_t i = 0; i < eps.size(); ++i) {
                const double x = eps[i], y = values[i], x2 = x * x;
                sx += x; sx2 += x2; sx3 += x2 * x; sx4 += x2 * x2;
                sy += y; sxy += x * y; sx2y += x2 * y;
            }
            const double a11 = n, a12 = sx, a13 = sx2;
            const double a21 = sx, a22 = sx2, a23 = sx3;
            const double a31 = sx2, a32 = sx3, a33 = sx4;
            const double det = a11 * (a22 * a33 - a23 * a32) - a12 * (a21 * a33 - a23 * a31)
                + a13 * (a21 * a32 - a22 * a31);
            if (std::abs(det) > 1e-300) {
                const double detA = sy * (a22 * a33 - a23 * a32) - a12 * (sxy * a33 - a23 * sx2y)
                    + a13 * (sxy * a32 - a22 * sx2y);
                const double detB = a11 * (sxy * a33 - a23 * sx2y) - sy * (a21 * a33 - a23 * a31)
                    + a13 * (a21 * sx2y - sxy * a31);
                const double detC = a11 * (a22 * sx2y - sxy * a32) - a12 * (a21 * sx2y - sxy * a31)
                    + sy * (a21 * a32 - a22 * a31);
                c0 = detA / det;
                c1 = detB / det;
                c2 = detC / det;
            }
        }
        plot_->setQuadraticSeries(eps, values, c2, c1, c0,
            tr("Energy vs. strain — curvature (C_%1_%1) shown in the tensor above")
                .arg(voigt + 1));
    }
}

void ElasticViewer::copyToClipboard()
{
    QString text;
    QTextStream stream(&text);
    stream << "Elastic tensor C_ij (GPa), symmetrized\n";
    for (const auto& row : toMatrix6x6(data_.value(QStringLiteral("C_symmetrized_GPa")))) {
        for (double v : row)
            stream << QString::number(v, 'g', 4) << '\t';
        stream << '\n';
    }
    if (data_.value(QStringLiteral("is_2d")).toBool()) {
        stream << "\n2D in-plane tensor (N/m): C11=" << data_.value(QStringLiteral("C11_2D_N_per_m")).toDouble()
               << " C22=" << data_.value(QStringLiteral("C22_2D_N_per_m")).toDouble()
               << " C12=" << data_.value(QStringLiteral("C12_2D_N_per_m")).toDouble()
               << " C66=" << data_.value(QStringLiteral("C66_2D_N_per_m")).toDouble() << '\n';
    }
    QApplication::clipboard()->setText(text);
}

void ElasticViewer::exportCsv()
{
    const QString suggestion =
        QFileInfo(sourcePath_).dir().filePath(QStringLiteral("elastic.csv"));
    const QString path = QFileDialog::getSaveFileName(
        this, tr("Export Elastic Tensor"), suggestion, tr("CSV (*.csv);;All files (*)"));
    if (path.isEmpty())
        return;
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text))
        return;
    QTextStream stream(&file);
    stream << "row,C_1,C_2,C_3,C_4,C_5,C_6\n";
    const auto writeMatrix = [&](const char* label, const char* key) {
        stream << label << "\n";
        int i = 1;
        for (const auto& row : toMatrix6x6(data_.value(QString::fromLatin1(key)))) {
            stream << i++;
            for (double v : row)
                stream << ',' << v;
            stream << '\n';
        }
    };
    writeMatrix("symmetrized_GPa", "C_symmetrized_GPa");
    writeMatrix("raw_GPa", "C_raw_GPa");
    if (data_.value(QStringLiteral("is_2d")).toBool()) {
        stream << "2D_N_per_m,C11,C22,C12,C66\n";
        stream << ',' << data_.value(QStringLiteral("C11_2D_N_per_m")).toDouble()
               << ',' << data_.value(QStringLiteral("C22_2D_N_per_m")).toDouble()
               << ',' << data_.value(QStringLiteral("C12_2D_N_per_m")).toDouble()
               << ',' << data_.value(QStringLiteral("C66_2D_N_per_m")).toDouble() << '\n';
    }
}

} // namespace calango::gui
