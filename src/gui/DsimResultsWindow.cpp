#include "gui/DsimResultsWindow.hpp"

#include "gui/DsimTernaryPlotWidget.hpp"
#include "gui/EgqcaPlotWidget.hpp"
#include "gui/GuiUtils.hpp"

#include <QCheckBox>
#include <QFile>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QStackedWidget>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QVBoxLayout>

#include <cmath>

namespace calango::gui {

namespace {

std::vector<double> toDoubles(const QJsonArray& array)
{
    std::vector<double> out;
    out.reserve(static_cast<std::size_t>(array.size()));
    for (const QJsonValue& v : array)
        out.push_back(v.toDouble());
    return out;
}

QTableWidgetItem* numericItem(double value, int decimals = 6)
{
    auto* item = new QTableWidgetItem(QString::number(value, 'f', decimals));
    item->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
    return item;
}

/// N(N-1)/2 pairwise curves need N(N-1)/2 distinct colours, more than
/// PlotPalette's two named series colours cover — golden-angle hue
/// rotation, the same "as many visually distinct hues as needed" approach
/// GrainCasts uses for an unbounded count of grains.
QColor pairwiseCurveColor(int index)
{
    constexpr double kGoldenAngle = 137.508;
    const double hue = std::fmod(index * kGoldenAngle, 360.0);
    return QColor::fromHsv(static_cast<int>(hue), 200, 210);
}

} // namespace

DsimResultsWindow::DsimResultsWindow(QWidget* parent)
    : QDialog(parent)
    , curvePlot_(new EgqcaPlotWidget(this))
    , ternaryPlot_(new DsimTernaryPlotWidget(this))
    , table_(new QTableWidget(this))
{
    setWindowTitle(tr("DSIM Mixing Enthalpy"));
    resize(860, 680);

    auto* layout = new QVBoxLayout(this);
    summaryLabel_ = new QLabel(this);
    summaryLabel_->setWordWrap(true);
    layout->addWidget(summaryLabel_);

    plotStack_ = new QStackedWidget(this);
    plotStack_->addWidget(curvePlot_);   // page 0: binary curve or pairwise curves
    plotStack_->addWidget(ternaryPlot_); // page 1: ternary
    layout->addWidget(plotStack_, 2);

    table_->setColumnCount(6);
    table_->setHorizontalHeaderLabels(
        {tr("Supercell"), tr("Formula"), tr("Atoms"), tr("Energy (eV)"),
         tr("Energy/atom (eV)"), tr("Converged")});
    table_->horizontalHeader()->setStretchLastSection(true);
    table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table_->setSelectionMode(QAbstractItemView::NoSelection);
    disableTypeToEdit(table_);
    layout->addWidget(table_, 1);

    auto* row = new QHBoxLayout;
    tangentsCheck_ = new QCheckBox(tr("Show tangent lines"), this);
    tangentsCheck_->setChecked(style_.showTangents);
    row->addWidget(tangentsCheck_);
    auto* appearanceButton = new QPushButton(tr("Customize Appearance…"), this);
    row->addWidget(appearanceButton);
    row->addStretch(1);
    auto* imageButton = new QPushButton(tr("Export Image…"), this);
    auto* dataButton = new QPushButton(tr("Export Data…"), this);
    row->addWidget(imageButton);
    row->addWidget(dataButton);
    layout->addLayout(row);
    connect(tangentsCheck_, &QCheckBox::toggled, this, [this](bool on) {
        style_.showTangents = on;
        rebuildPlot();
    });
    connect(appearanceButton, &QPushButton::clicked, this, &DsimResultsWindow::customizeAppearance);
    connect(imageButton, &QPushButton::clicked, this, &DsimResultsWindow::exportImage);
    connect(dataButton, &QPushButton::clicked, this, &DsimResultsWindow::exportData);
}

bool DsimResultsWindow::loadResults(const QString& path)
{
    if (!QFile::exists(path))
        return false;
    root_ = readJsonObject(path);
    if (root_.isEmpty())
        return false;

    species_.clear();
    for (const QJsonValue& v : root_.value(QStringLiteral("species")).toArray())
        species_ << v.toString();
    const int natoms = root_.value(QStringLiteral("supercell_atom_count")).toInt();
    const double dilution = root_.value(QStringLiteral("dilution")).toDouble();
    const QJsonObject records = root_.value(QStringLiteral("records")).toObject();
    const QJsonObject failures = root_.value(QStringLiteral("failures")).toObject();
    const QJsonValue analysisValue = root_.value(QStringLiteral("analysis"));

    populateTable(records);

    if (analysisValue.isNull() || !analysisValue.isObject()) {
        summaryLabel_->setText(
            tr("<b>%1</b>: %2/%3 supercell(s) failed to relax (%4) — "
               "DeltaH_mix is not available.")
                .arg(species_.join(QStringLiteral("-")), QString::number(failures.size()),
                     QString::number(species_.size() * species_.size()),
                     QStringList(failures.keys()).join(QStringLiteral(", "))));
        curvePlot_->clear();
        ternaryPlot_->clear();
        return true;
    }

    const QJsonObject analysis = analysisValue.toObject();
    QString headline = tr("<b>%1</b> (%2 component(s), %3-atom supercells, x0 = 1/%3 = %4)")
                            .arg(species_.join(QStringLiteral("-")))
                            .arg(species_.size())
                            .arg(natoms)
                            .arg(QString::number(dilution, 'g', 4));
    if (const QJsonValue binaryValue = analysis.value(QStringLiteral("binary"));
        binaryValue.isObject()) {
        const QJsonObject binary = binaryValue.toObject();
        const double mBInA = binary.value(QStringLiteral("m_b_in_a_eV")).toDouble();
        const double mAInB = binary.value(QStringLiteral("m_a_in_b_eV")).toDouble();
        const double dHdx0 = binary.value(QStringLiteral("dHdx_at_0_eV")).toDouble();
        const double dHdx1 = binary.value(QStringLiteral("dHdx_at_1_eV")).toDouble();
        // M_i[j] (Eq. 9-10) is already the intensive, x0-normalized
        // quantity Eq. 7 needs — see Dsim.hpp's unit-convention note — so
        // its unit is plain eV, not eV/atom, despite being derived from
        // per-defect energetics.
        headline += tr("<br>M<sub>2[1]</sub> (%1 in %2) = %3 eV, "
                       "M<sub>1[2]</sub> (%2 in %1) = %4 eV &mdash; "
                       "dilute-limit slopes: %5 (x=0), %6 (x=1) eV")
                        .arg(species_.value(1), species_.value(0),
                             QString::number(mBInA, 'f', 6), QString::number(mAInB, 'f', 6),
                             QString::number(dHdx0, 'f', 6), QString::number(dHdx1, 'f', 6));
    }
    summaryLabel_->setText(headline);

    rebuildPlot();
    return true;
}

void DsimResultsWindow::rebuildPlot()
{
    const QJsonObject analysis = root_.value(QStringLiteral("analysis")).toObject();
    if (analysis.isEmpty()) {
        curvePlot_->clear();
        ternaryPlot_->clear();
        return;
    }

    const QString unitLabel = style_.useKilojoulesPerMole ? tr("kJ/mol") : tr("eV/atom");
    const QString enthalpyKey = style_.useKilojoulesPerMole ? QStringLiteral("enthalpy_kJ_per_mol")
                                                            : QStringLiteral("enthalpy_eV_per_atom");
    const double unitScale = style_.useKilojoulesPerMole ? 96.485332 : 1.0;

    const QJsonValue binaryValue = analysis.value(QStringLiteral("binary"));
    const QJsonValue ternaryValue = analysis.value(QStringLiteral("ternary"));

    if (binaryValue.isObject()) {
        // -- N=2: DeltaH_mix(x) with the two dilute-limit tangent lines ------
        plotStack_->setCurrentWidget(curvePlot_);
        if (tangentsCheck_)
            tangentsCheck_->setEnabled(true);
        const QJsonObject binary = binaryValue.toObject();
        const double dHdx0 = binary.value(QStringLiteral("dHdx_at_0_eV")).toDouble();
        const double dHdx1 = binary.value(QStringLiteral("dHdx_at_1_eV")).toDouble();
        const std::vector<double> x = toDoubles(binary.value(QStringLiteral("x_grid")).toArray());
        const std::vector<double> h = toDoubles(binary.value(enthalpyKey).toArray());

        std::vector<EgqcaPlotWidget::Series> series;
        EgqcaPlotWidget::Series curve;
        curve.label = tr("ΔH_mix(x)");
        curve.color = style_.curveColor;
        for (std::size_t i = 0; i < x.size() && i < h.size(); ++i)
            curve.points.push_back({x[i], h[i]});
        series.push_back(curve);

        if (style_.showTangents) {
            // Dilute-limit tangent lines (Eq. 8): a short segment through
            // each endpoint (x, DeltaH_mix)=(0,0)/(1,0) at that endpoint's
            // own slope. At x=1 the line runs BACKWARD from x=1 (delta
            // x=-kSpan), so its far-end height is -dHdx1*kSpan.
            constexpr double kSpan = 0.15;
            EgqcaPlotWidget::Series tangent0;
            tangent0.label = tr("tangent at x=0");
            tangent0.color = style_.tangentColor;
            tangent0.points = {{0.0, 0.0}, {kSpan, dHdx0 * kSpan * unitScale}};
            series.push_back(tangent0);

            EgqcaPlotWidget::Series tangent1;
            tangent1.label = tr("tangent at x=1");
            tangent1.color = style_.tangentColor;
            tangent1.points = {{1.0 - kSpan, -dHdx1 * kSpan * unitScale}, {1.0, 0.0}};
            series.push_back(tangent1);
        }
        // "_{mix}" renders as a real typographic subscript —
        // EgqcaPlotWidget's axis-title draw call is
        // GuiUtils::drawWithSubscripts, not plain text.
        curvePlot_->setSeries(std::move(series), tr("x"), tr("ΔH_{mix} (%1)").arg(unitLabel));
    } else if (ternaryValue.isObject()) {
        // -- N=3: DeltaH_mix over the composition triangle -------------------
        plotStack_->setCurrentWidget(ternaryPlot_);
        if (tangentsCheck_)
            tangentsCheck_->setEnabled(false); // no tangent-line concept here
        const QJsonObject ternary = ternaryValue.toObject();
        const int resolution = ternary.value(QStringLiteral("resolution")).toInt();
        const std::vector<double> xB = toDoubles(ternary.value(QStringLiteral("xB")).toArray());
        const std::vector<double> xC = toDoubles(ternary.value(QStringLiteral("xC")).toArray());
        const std::vector<double> h = toDoubles(ternary.value(enthalpyKey).toArray());
        std::vector<DsimTernaryPlotWidget::GridPoint> grid;
        grid.reserve(xB.size());
        for (std::size_t i = 0; i < xB.size() && i < xC.size() && i < h.size(); ++i)
            grid.push_back({xB[i], xC[i], h[i]});
        ternaryPlot_->setData(std::move(grid), resolution, species_, unitLabel);
    } else {
        // -- N>=4: the pairwise binary sub-curves, together on one plot ------
        plotStack_->setCurrentWidget(curvePlot_);
        if (tangentsCheck_)
            tangentsCheck_->setEnabled(false); // no single dilute-limit pair here
        std::vector<EgqcaPlotWidget::Series> series;
        const QJsonArray pairwise = analysis.value(QStringLiteral("pairwise")).toArray();
        int index = 0;
        for (const QJsonValue& pairValue : pairwise) {
            const QJsonObject pair = pairValue.toObject();
            EgqcaPlotWidget::Series curve;
            curve.label = tr("%1-%2")
                              .arg(pair.value(QStringLiteral("species_i")).toString(),
                                   pair.value(QStringLiteral("species_j")).toString());
            curve.color = pairwiseCurveColor(index++);
            const std::vector<double> x = toDoubles(pair.value(QStringLiteral("x_grid")).toArray());
            const std::vector<double> h = toDoubles(pair.value(enthalpyKey).toArray());
            for (std::size_t i = 0; i < x.size() && i < h.size(); ++i)
                curve.points.push_back({x[i], h[i]});
            series.push_back(curve);
        }
        curvePlot_->setSeries(std::move(series), tr("x (fraction of species j)"),
                              tr("ΔH_{mix} (%1)").arg(unitLabel));
    }
}

void DsimResultsWindow::customizeAppearance()
{
    auto* dialog = new DsimPlotStyleDialog(style_, this);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    connect(dialog, &DsimPlotStyleDialog::styleChanged, this, [this](const DsimPlotStyle& style) {
        style_ = style;
        const QSignalBlocker block(tangentsCheck_);
        tangentsCheck_->setChecked(style_.showTangents);
        rebuildPlot();
    });
    dialog->show();
}

void DsimResultsWindow::populateTable(const QJsonObject& records)
{
    // Row order matches the generated script's own LABELS: every pristine
    // species first, then every ordered impurity pair — not
    // records.keys()'s unspecified QJsonObject iteration order.
    QStringList labels;
    QStringList displayNames;
    for (const QString& sp : species_) {
        labels << QStringLiteral("pristine_") + sp;
        displayNames << tr("pristine %1").arg(sp);
    }
    for (const QString& si : species_)
        for (const QString& sj : species_)
            if (si != sj) {
                labels << si + QStringLiteral("_in_") + sj;
                displayNames << tr("%1 in %2").arg(si, sj);
            }

    table_->setRowCount(labels.size());
    for (int row = 0; row < labels.size(); ++row) {
        const QJsonObject rec = records.value(labels[row]).toObject();
        table_->setItem(row, 0, new QTableWidgetItem(displayNames[row]));
        table_->setItem(row, 1, new QTableWidgetItem(rec.value(QStringLiteral("formula")).toString()));
        table_->setItem(row, 2, numericItem(rec.value(QStringLiteral("natoms")).toInt(), 0));
        table_->setItem(row, 3, numericItem(rec.value(QStringLiteral("energy")).toDouble()));
        table_->setItem(row, 4, numericItem(rec.value(QStringLiteral("energy_per_atom")).toDouble()));
        auto* convergedItem = new QTableWidgetItem(
            rec.contains(QStringLiteral("converged"))
                ? (rec.value(QStringLiteral("converged")).toBool() ? tr("yes") : tr("no"))
                : tr("—"));
        table_->setItem(row, 5, convergedItem);
    }
}

void DsimResultsWindow::exportImage()
{
    const QString path = QFileDialog::getSaveFileName(this, tr("Export DSIM Plot"),
                                                       QStringLiteral("dsim.png"),
                                                       tr("PNG Image (*.png)"));
    if (path.isEmpty())
        return;
    QWidget* current = plotStack_->currentWidget();
    bool ok = false;
    if (current == curvePlot_)
        ok = curvePlot_->exportImage(path);
    else if (current == ternaryPlot_)
        ok = ternaryPlot_->exportImage(path);
    if (!ok)
        QMessageBox::warning(this, tr("Export DSIM Plot"), tr("Could not write %1.").arg(path));
}

void DsimResultsWindow::exportData()
{
    const QString path = QFileDialog::getSaveFileName(this, tr("Export DSIM Data"),
                                                       QStringLiteral("dsim.csv"),
                                                       tr("CSV (*.csv)"));
    if (path.isEmpty())
        return;

    const QJsonObject analysis = root_.value(QStringLiteral("analysis")).toObject();
    writeTextFile(this, path, [this, &analysis](QTextStream& out) {
        out << "# Calango DSIM (Dilute Solution Interpolation) results\n";
        out << "# supercell,formula,atoms,energy_eV,energy_per_atom_eV,converged\n";
        for (int row = 0; row < table_->rowCount(); ++row) {
            const auto cell = [this, row](int col) {
                return table_->item(row, col) ? table_->item(row, col)->text() : QString();
            };
            out << cell(0) << ',' << cell(1) << ',' << cell(2) << ',' << cell(3) << ',' << cell(4)
                << ',' << cell(5) << '\n';
        }
        if (const QJsonValue binaryValue = analysis.value(QStringLiteral("binary"));
            binaryValue.isObject()) {
            out << "#\n# x,DeltaH_mix_kJ_per_mol\n";
            const QJsonObject binary = binaryValue.toObject();
            const std::vector<double> x = toDoubles(binary.value(QStringLiteral("x_grid")).toArray());
            const std::vector<double> h =
                toDoubles(binary.value(QStringLiteral("enthalpy_kJ_per_mol")).toArray());
            for (std::size_t i = 0; i < x.size() && i < h.size(); ++i)
                out << x[i] << ',' << h[i] << '\n';
        } else if (const QJsonValue ternaryValue = analysis.value(QStringLiteral("ternary"));
                  ternaryValue.isObject()) {
            out << "#\n# xB,xC,DeltaH_mix_kJ_per_mol\n";
            const QJsonObject ternary = ternaryValue.toObject();
            const std::vector<double> xB = toDoubles(ternary.value(QStringLiteral("xB")).toArray());
            const std::vector<double> xC = toDoubles(ternary.value(QStringLiteral("xC")).toArray());
            const std::vector<double> h =
                toDoubles(ternary.value(QStringLiteral("enthalpy_kJ_per_mol")).toArray());
            for (std::size_t i = 0; i < xB.size() && i < xC.size() && i < h.size(); ++i)
                out << xB[i] << ',' << xC[i] << ',' << h[i] << '\n';
        } else {
            out << "#\n# species_i,species_j,x,DeltaH_mix_kJ_per_mol\n";
            for (const QJsonValue& pairValue : analysis.value(QStringLiteral("pairwise")).toArray()) {
                const QJsonObject pair = pairValue.toObject();
                const QString si = pair.value(QStringLiteral("species_i")).toString();
                const QString sj = pair.value(QStringLiteral("species_j")).toString();
                const std::vector<double> x = toDoubles(pair.value(QStringLiteral("x_grid")).toArray());
                const std::vector<double> h =
                    toDoubles(pair.value(QStringLiteral("enthalpy_kJ_per_mol")).toArray());
                for (std::size_t i = 0; i < x.size() && i < h.size(); ++i)
                    out << si << ',' << sj << ',' << x[i] << ',' << h[i] << '\n';
            }
        }
    });
}

} // namespace calango::gui
