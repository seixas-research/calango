#include "gui/PiezoelectricViewer.hpp"

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
#include <QTextStream>
#include <QVBoxLayout>

#include <algorithm>

namespace calango::gui {

namespace {

constexpr const char* kVoigtHeaders[6] = {
    "eps_1 (xx)", "eps_2 (yy)", "eps_3 (zz)", "eps_4 (yz)", "eps_5 (xz)", "eps_6 (xy)"};
constexpr const char* kRowHeaders[3] = {"P_x", "P_y", "P_z"};

/// The tensor-kind combo's entries and the JSON key each reads from — the
/// first 4 (C/m^2, volume-normalized) always present; the last 4 (C/m,
/// vacuum divided out) only added when the run detected a 2D structure. One
/// table indexed by combo row, rather than two separate paths, so
/// refreshTensorTable()/refreshPlot() cannot disagree about which key a
/// given row means.
struct TensorKindEntry {
    const char* label;
    const char* jsonKey;
};
constexpr TensorKindEntry kTensorKinds[8] = {
    {"Proper, symmetrized (recommended)", "proper_tensor_symmetrized_Cm2"},
    {"Proper, raw", "proper_tensor_Cm2"},
    {"Improper (clamped-cell-shape), symmetrized",
     "improper_tensor_symmetrized_Cm2"},
    {"Improper (clamped-cell-shape), raw", "improper_tensor_Cm2"},
    {"Proper, symmetrized — 2D (C/m)", "proper_tensor_symmetrized_2d_Cm"},
    {"Proper, raw — 2D (C/m)", "proper_tensor_2d_Cm"},
    {"Improper, symmetrized — 2D (C/m)",
     "improper_tensor_symmetrized_2d_Cm"},
    {"Improper, raw — 2D (C/m)", "improper_tensor_2d_Cm"},
};

QVector<QVector<double>> toMatrix3x6(const QJsonValue& value)
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
    table->setRowCount(3);
    table->setColumnCount(6);
    table->setHorizontalHeaderLabels(
        {kVoigtHeaders[0], kVoigtHeaders[1], kVoigtHeaders[2], kVoigtHeaders[3],
         kVoigtHeaders[4], kVoigtHeaders[5]});
    table->setVerticalHeaderLabels({kRowHeaders[0], kRowHeaders[1], kRowHeaders[2]});
    for (int i = 0; i < 3 && i < matrix.size(); ++i)
        for (int j = 0; j < 6 && j < matrix[i].size(); ++j)
            table->setItem(i, j,
                           new QTableWidgetItem(QString::number(matrix[i][j], 'g', 4)));
    table->resizeColumnsToContents();
    table->horizontalHeader()->setStretchLastSection(true);
}

} // namespace

// --- PiezoelectricPointPlot -------------------------------------------------

PiezoelectricPointPlot::PiezoelectricPointPlot(QWidget* parent) : QWidget(parent)
{
    setMinimumHeight(220);
}

void PiezoelectricPointPlot::setSeries(std::vector<double> eps,
                                       std::vector<double> pAxis, double slope,
                                       double intercept, const QString& axisLabel,
                                       const QString& units)
{
    eps_ = std::move(eps);
    pAxis_ = std::move(pAxis);
    slope_ = slope;
    intercept_ = intercept;
    axisLabel_ = axisLabel;
    units_ = units;
    update();
}

void PiezoelectricPointPlot::paintEvent(QPaintEvent*)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.fillRect(rect(), PlotPalette::canvas);

    if (eps_.empty() || eps_.size() != pAxis_.size()) {
        painter.setPen(PlotPalette::placeholder);
        painter.drawText(rect(), Qt::AlignCenter,
                         tr("No strain series for this component."));
        return;
    }

    const double margin = 48.0;
    const QRectF plotRect(margin, 12.0, width() - margin - 16.0, height() - margin - 12.0);

    double xMin = *std::min_element(eps_.begin(), eps_.end());
    double xMax = *std::max_element(eps_.begin(), eps_.end());
    double yMin = *std::min_element(pAxis_.begin(), pAxis_.end());
    double yMax = *std::max_element(pAxis_.begin(), pAxis_.end());
    // Also bracket the fitted line's endpoints, so the line is never clipped
    // even when a point happens to sit exactly on it.
    yMin = std::min({yMin, slope_ * xMin + intercept_, slope_ * xMax + intercept_});
    yMax = std::max({yMax, slope_ * xMin + intercept_, slope_ * xMax + intercept_});
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

    // Fitted line — the SAME slope/intercept the tensor entry was assembled
    // from, so this plot and the table can never disagree.
    painter.setPen(QPen(PlotPalette::seriesAlt, 1.6));
    painter.drawLine(toPoint(xMin + xPad, slope_ * (xMin + xPad) + intercept_),
                     toPoint(xMax - xPad, slope_ * (xMax - xPad) + intercept_));

    painter.setPen(QPen(PlotPalette::series, 1.4));
    painter.setBrush(PlotPalette::series);
    for (std::size_t i = 0; i < eps_.size(); ++i) {
        const QPointF p = toPoint(eps_[i], pAxis_[i]);
        painter.drawEllipse(p, 4.0, 4.0);
    }

    painter.setPen(PlotPalette::text);
    painter.drawText(QRectF(plotRect.left(), 0, plotRect.width(), 14), Qt::AlignCenter,
                     tr("%1 vs. strain — slope (this Voigt column's e_ij) = %2 %3")
                         .arg(axisLabel_)
                         .arg(slope_, 0, 'g', 4)
                         .arg(units_));
}

// --- PiezoelectricViewer -----------------------------------------------------

PiezoelectricViewer::PiezoelectricViewer(QWidget* parent) : QDialog(parent)
{
    setWindowTitle(tr("Piezoelectric Tensor"));
    resize(880, 700);

    auto* layout = new QVBoxLayout(this);

    summaryLabel_ = new QLabel(this);
    summaryLabel_->setWordWrap(true);
    summaryLabel_->setTextFormat(Qt::RichText);
    layout->addWidget(summaryLabel_);

    auto* tensorRow = new QHBoxLayout();
    tensorRow->addWidget(new QLabel(tr("Show:"), this));
    tensorKindCombo_ = new QComboBox(this);
    for (int i = 0; i < 4; ++i)
        tensorKindCombo_->addItem(tr(kTensorKinds[i].label));
    // The 2D (C/m) entries are appended in loadResults() once it is known
    // whether this run detected a 2D structure — added here would show four
    // meaningless rows of NaN for every ordinary bulk result.
    tensorKindCombo_->setToolTip(
        tr("Proper vs. improper: Vanderbilt's correction for the "
           "volume-definition artefact a cell-strain finite difference picks "
           "up (arXiv:cond-mat/9903137, Eq. 15) — proper is the physical, "
           "surface-charge-response tensor.\n\n"
           "Symmetrized vs. raw: whether the point-group averaging that "
           "zeroes symmetry-forbidden components and cleans numerical noise "
           "has been applied.\n\n"
           "For a 2D structure, the C/m entries multiply the ordinary C/m^2 "
           "value by the vacuum axis's own cell length — the coefficient "
           "that no longer depends on how much vacuum padding the cell "
           "happens to carry, unlike C/m^2 divided by the full 3D volume."));
    tensorRow->addWidget(tensorKindCombo_);
    tensorRow->addStretch(1);
    layout->addLayout(tensorRow);

    tensorTable_ = new QTableWidget(this);
    tensorTable_->setSelectionBehavior(QAbstractItemView::SelectItems);
    tensorTable_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    layout->addWidget(tensorTable_);

    dTensorLabel_ = new QLabel(tr("<b>d_ij (pm/V)</b> — from the supplied elastic "
                                  "stiffness"),
                               this);
    layout->addWidget(dTensorLabel_);
    dTensorTable_ = new QTableWidget(this);
    dTensorTable_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    layout->addWidget(dTensorTable_);

    auto* plotRow = new QHBoxLayout();
    plotRow->addWidget(new QLabel(tr("P(eps) for component:"), this));
    componentCombo_ = new QComboBox(this);
    plotRow->addWidget(componentCombo_);
    plotRow->addWidget(new QLabel(tr("axis:"), this));
    axisCombo_ = new QComboBox(this);
    axisCombo_->addItems({tr("P_x"), tr("P_y"), tr("P_z")});
    plotRow->addWidget(axisCombo_);
    plotRow->addStretch(1);
    layout->addLayout(plotRow);
    connect(componentCombo_, &QComboBox::currentIndexChanged, this,
            &PiezoelectricViewer::refreshPlot);
    connect(axisCombo_, &QComboBox::currentIndexChanged, this,
            &PiezoelectricViewer::refreshPlot);

    plot_ = new PiezoelectricPointPlot(this);
    layout->addWidget(plot_);

    connect(tensorKindCombo_, &QComboBox::currentIndexChanged, this,
            &PiezoelectricViewer::refreshTensorTable);

    auto* buttonRow = new QHBoxLayout;
    auto* copyButton = new QPushButton(tr("Copy"), this);
    auto* csvButton = new QPushButton(tr("Export CSV…"), this);
    buttonRow->addWidget(copyButton);
    buttonRow->addWidget(csvButton);
    buttonRow->addStretch(1);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    buttonRow->addWidget(buttons);
    layout->addLayout(buttonRow);

    connect(copyButton, &QPushButton::clicked, this,
            &PiezoelectricViewer::copyToClipboard);
    connect(csvButton, &QPushButton::clicked, this,
            &PiezoelectricViewer::exportCsv);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
}

bool PiezoelectricViewer::loadResults(const QString& jsonPath)
{
    QFile file(jsonPath);
    if (!file.open(QIODevice::ReadOnly))
        return false;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll());
    if (!document.isObject())
        return false;
    data_ = document.object();
    sourcePath_ = jsonPath;

    if (!data_.contains(QStringLiteral("proper_tensor_Cm2")))
        return false;

    const QString pointGroup =
        data_.value(QStringLiteral("point_group")).toString(tr("(unknown — "
                                                                "spglib unavailable)"));
    const bool relaxIons = data_.value(QStringLiteral("relax_ions")).toBool();
    QString summary =
        tr("<b>Point group:</b> %1 &nbsp; <b>strain delta:</b> %2 &nbsp; "
           "<b>points/component:</b> %3 &nbsp; <b>ions:</b> %4")
            .arg(pointGroup)
            .arg(data_.value(QStringLiteral("strain_magnitude")).toDouble(), 0, 'g', 3)
            .arg(data_.value(QStringLiteral("points_per_component")).toInt())
            .arg(relaxIons ? tr("relaxed") : tr("clamped"));

    const bool is2d = data_.value(QStringLiteral("is_2d")).toBool();
    if (is2d) {
        static const char* const axisNames[3] = {"a", "b", "c"};
        const int axis = data_.value(QStringLiteral("vacuum_axis")).toInt(-1);
        summary += tr(" &nbsp; <b style='color:#2a7a2a;'>2D system</b> "
                      "(vacuum along %1) — e<sub>ij</sub> reported per "
                      "volume (C/m&sup2;) and per area (C/m)")
                       .arg(axis >= 0 && axis < 3
                                ? QString::fromLatin1(axisNames[axis])
                                : tr("?"));
    }
    const QJsonArray missing =
        data_.value(QStringLiteral("components_with_no_data")).toArray();
    if (!missing.isEmpty()) {
        QStringList names;
        for (const QJsonValue& v : missing) {
            const int voigt = v.toInt();
            names << (voigt >= 0 && voigt < 6
                          ? QString::fromLatin1(kVoigtHeaders[voigt])
                          : QString::number(voigt));
        }
        summary += tr(" &nbsp; <b style='color:#c0392b;'>%n component(s) "
                      "have no data</b> (every strain point for %1 "
                      "failed — see log.txt)",
                      nullptr, static_cast<int>(missing.size()))
                       .arg(names.join(QStringLiteral(", ")));
    }
    summaryLabel_->setText(summary);

    // The 2D (C/m) rows only make sense once it is known this run detected
    // a 2D structure — rebuilt every load rather than left to accumulate
    // duplicates across successive loadResults() calls on the same dialog.
    tensorKindCombo_->blockSignals(true);
    tensorKindCombo_->clear();
    for (int i = 0; i < (is2d ? 8 : 4); ++i)
        tensorKindCombo_->addItem(tr(kTensorKinds[i].label));
    tensorKindCombo_->blockSignals(false);

    refreshTensorTable();

    const QJsonObject perComponent =
        data_.value(QStringLiteral("per_component_P_of_eps")).toObject();
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
    refreshPlot();

    const bool hasD = data_.contains(QStringLiteral("d_tensor_pmV"))
        && !data_.value(QStringLiteral("d_tensor_pmV")).isNull();
    dTensorLabel_->setVisible(hasD);
    dTensorTable_->setVisible(hasD);
    if (hasD)
        fillTensorTable(dTensorTable_, toMatrix3x6(data_.value(QStringLiteral("d_tensor_pmV"))));

    return true;
}

void PiezoelectricViewer::refreshTensorTable()
{
    const int index = tensorKindCombo_->currentIndex();
    if (index < 0 || index >= 8)
        return;
    fillTensorTable(tensorTable_,
                    toMatrix3x6(data_.value(
                        QString::fromLatin1(kTensorKinds[index].jsonKey))));
}

void PiezoelectricViewer::refreshPlot()
{
    if (componentCombo_->count() == 0) {
        plot_->setSeries({}, {}, 0.0, 0.0, {});
        return;
    }
    const QString key = componentCombo_->currentData().toString();
    const QJsonObject entry = data_.value(QStringLiteral("per_component_P_of_eps"))
                                  .toObject()
                                  .value(key)
                                  .toObject();
    std::vector<double> eps;
    for (const QJsonValue& v : entry.value(QStringLiteral("eps")).toArray())
        eps.push_back(v.toDouble());

    const int axis = axisCombo_->currentIndex();
    std::vector<double> pAxis;
    for (const QJsonValue& row : entry.value(QStringLiteral("P_Cm2")).toArray()) {
        const QJsonArray triple = row.toArray();
        pAxis.push_back(axis < triple.size() ? triple.at(axis).toDouble() : 0.0);
    }

    const int voigt = key.toInt();
    const int tensorIndex = tensorKindCombo_->currentIndex();
    // per_component_P_of_eps only ever stores the C/m^2 (volume-normalized)
    // series — when a "2D (C/m)" tensor kind is selected, the PLOTTED
    // points must be rescaled the same way the tensor entry itself was
    // (multiplied by the vacuum axis length), or the line and the points it
    // is supposed to run through would silently be in different units.
    if (tensorIndex >= 4) {
        const double vacuumLengthM =
            data_.value(QStringLiteral("vacuum_axis_length_A")).toDouble() * 1e-10;
        for (double& p : pAxis)
            p *= vacuumLengthM;
    }

    // The same slope this component's tensor column was fit from — read
    // straight from the currently displayed tensor rather than re-fit here,
    // so the plot and the table can never disagree.
    double slope = 0.0;
    if (voigt >= 0 && voigt < 6 && tensorIndex >= 0 && tensorIndex < 8) {
        const auto matrix = toMatrix3x6(
            data_.value(QString::fromLatin1(kTensorKinds[tensorIndex].jsonKey)));
        if (axis < matrix.size() && voigt < matrix[axis].size())
            slope = matrix[axis][voigt];
    }
    // Intercept: least-squares through the plotted points at the fixed
    // slope above — purely a visual anchor for the line, not a second
    // physics computation.
    double meanEps = 0.0, meanP = 0.0;
    for (std::size_t i = 0; i < eps.size(); ++i) {
        meanEps += eps[i];
        meanP += pAxis[i];
    }
    if (!eps.empty()) { meanEps /= double(eps.size()); meanP /= double(eps.size()); }
    const double intercept = meanP - slope * meanEps;

    plot_->setSeries(eps, pAxis, slope, intercept,
                     QString::fromLatin1(kRowHeaders[std::clamp(axis, 0, 2)]),
                     tensorIndex >= 4 ? QStringLiteral("C/m")
                                      : QStringLiteral("C/m^2"));
}

void PiezoelectricViewer::copyToClipboard()
{
    QString text;
    QTextStream stream(&text);
    stream << "Piezoelectric tensor e_ij (C/m^2), proper & symmetrized\n";
    for (const auto& row : toMatrix3x6(data_.value(QStringLiteral("proper_tensor_symmetrized_Cm2")))) {
        for (double v : row)
            stream << QString::number(v, 'g', 4) << '\t';
        stream << '\n';
    }
    if (data_.value(QStringLiteral("is_2d")).toBool()) {
        stream << "\n2D piezoelectric coefficient e_ij (C/m), proper & "
                  "symmetrized\n";
        for (const auto& row : toMatrix3x6(
                 data_.value(QStringLiteral("proper_tensor_symmetrized_2d_Cm")))) {
            for (double v : row)
                stream << QString::number(v, 'g', 4) << '\t';
            stream << '\n';
        }
    }
    QApplication::clipboard()->setText(text);
}

void PiezoelectricViewer::exportCsv()
{
    const QString suggestion = QFileInfo(sourcePath_).dir().filePath(
        QStringLiteral("piezoelectric.csv"));
    const QString path = QFileDialog::getSaveFileName(
        this, tr("Export Piezoelectric Tensor"), suggestion,
        tr("CSV (*.csv);;All files (*)"));
    if (path.isEmpty())
        return;
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text))
        return;
    QTextStream stream(&file);
    stream << "row,eps1_xx,eps2_yy,eps3_zz,eps4_yz,eps5_xz,eps6_xy\n";
    const auto writeMatrix = [&](const char* label, const char* key) {
        stream << label << "\n";
        int i = 0;
        for (const auto& row : toMatrix3x6(data_.value(QString::fromLatin1(key)))) {
            stream << kRowHeaders[i++];
            for (double v : row)
                stream << ',' << v;
            stream << '\n';
        }
    };
    writeMatrix("proper_symmetrized", "proper_tensor_symmetrized_Cm2");
    writeMatrix("proper_raw", "proper_tensor_Cm2");
    writeMatrix("improper_symmetrized", "improper_tensor_symmetrized_Cm2");
    writeMatrix("improper_raw", "improper_tensor_Cm2");
    if (data_.value(QStringLiteral("is_2d")).toBool()) {
        writeMatrix("proper_symmetrized_2d_Cm_per_m",
                    "proper_tensor_symmetrized_2d_Cm");
        writeMatrix("proper_raw_2d_Cm_per_m", "proper_tensor_2d_Cm");
        writeMatrix("improper_symmetrized_2d_Cm_per_m",
                    "improper_tensor_symmetrized_2d_Cm");
        writeMatrix("improper_raw_2d_Cm_per_m", "improper_tensor_2d_Cm");
    }
    if (data_.contains(QStringLiteral("d_tensor_pmV"))
        && !data_.value(QStringLiteral("d_tensor_pmV")).isNull())
        writeMatrix("d_pmV", "d_tensor_pmV");
}

} // namespace calango::gui
