#include "gui/DefectDiagramWindow.hpp"

#include "gui/GuiUtils.hpp"
#include "gui/PlotPalette.hpp"
#include "render/ColorMap.hpp"

#include <QCheckBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QJsonArray>
#include <QLabel>
#include <QMessageBox>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QSplitter>
#include <QTableWidget>
#include <QTextStream>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>

namespace calango::gui {

namespace {
constexpr int kMarginLeft = 68;
constexpr int kMarginRight = 18;
constexpr int kMarginTop = 16;
constexpr int kMarginBottom = 46;
} // namespace

DefectDiagramWidget::DefectDiagramWidget(QWidget* parent) : QWidget(parent)
{
    setMinimumSize(420, 300);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
}

void DefectDiagramWidget::setData(std::vector<double> fermiLevels,
                                  std::vector<Line> lines,
                                  std::vector<double> envelope,
                                  std::vector<Transition> transitions,
                                  double gapEv)
{
    fermi_ = std::move(fermiLevels);
    lines_ = std::move(lines);
    envelope_ = std::move(envelope);
    transitions_ = std::move(transitions);
    gapEv_ = gapEv;
    update();
}

void DefectDiagramWidget::setEnvelopeOnly(bool on)
{
    envelopeOnly_ = on;
    update();
}

void DefectDiagramWidget::setShowTransitions(bool on)
{
    showTransitions_ = on;
    update();
}

void DefectDiagramWidget::paintEvent(QPaintEvent*)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.fillRect(rect(), PlotPalette::canvas);

    const QRectF box(kMarginLeft, kMarginTop,
                     std::max(10, width() - kMarginLeft - kMarginRight),
                     std::max(10, height() - kMarginTop - kMarginBottom));

    if (fermi_.size() < 2 || lines_.empty()) {
        painter.setPen(PlotPalette::placeholder);
        painter.drawText(rect(), Qt::AlignCenter,
                         tr("No formation-energy data"));
        return;
    }

    // Vertical range over what is actually drawn, so hiding the individual
    // lines rescales onto the envelope instead of leaving it in a corner.
    double lo = envelope_.empty() ? 0.0 : *std::min_element(envelope_.begin(),
                                                            envelope_.end());
    double hi = lo;
    const auto span = [&lo, &hi](const std::vector<double>& values) {
        for (const double v : values) {
            lo = std::min(lo, v);
            hi = std::max(hi, v);
        }
    };
    span(envelope_);
    if (!envelopeOnly_)
        for (const Line& line : lines_)
            span(line.energies);
    const double pad = std::max(0.05 * (hi - lo), 0.05);
    lo -= pad;
    hi += pad;
    const double range = std::max(hi - lo, 1e-9);
    const double xMax = std::max(gapEv_, 1e-9);

    const auto toX = [&](double e) {
        return box.left() + box.width() * std::clamp(e / xMax, 0.0, 1.0);
    };
    const auto toY = [&](double e) {
        return box.bottom() - box.height() * ((e - lo) / range);
    };

    // --- Axes and gridlines ------------------------------------------------
    painter.setPen(QPen(PlotPalette::spine, 1.0));
    painter.drawRect(box);
    const QFontMetricsF metrics(painter.font());
    for (int i = 0; i <= 4; ++i) {
        const double e = lo + range * i / 4.0;
        const double y = toY(e);
        painter.setPen(QPen(PlotPalette::grid, 0.5, Qt::DotLine));
        painter.drawLine(QPointF(box.left(), y), QPointF(box.right(), y));
        painter.setPen(PlotPalette::text);
        painter.drawText(QPointF(6.0, y + metrics.height() / 3.0),
                         QString::number(e, 'f', 2));
    }
    for (int i = 0; i <= 4; ++i) {
        const double e = xMax * i / 4.0;
        const double x = toX(e);
        painter.setPen(QPen(PlotPalette::grid, 0.5, Qt::DotLine));
        painter.drawLine(QPointF(x, box.top()), QPointF(x, box.bottom()));
        painter.setPen(PlotPalette::text);
        painter.drawText(QPointF(x - 12.0, box.bottom() + metrics.height() + 3.0),
                         QString::number(e, 'f', 2));
    }
    painter.setPen(PlotPalette::text);
    painter.drawText(QPointF(box.center().x() - 70.0,
                             box.bottom() + 2.2 * metrics.height() + 2.0),
                     tr("E_F − E_VBM  (eV)"));
    painter.save();
    painter.translate(14.0, box.center().y() + 60.0);
    painter.rotate(-90.0);
    painter.drawText(0, 0, tr("Formation energy (eV)"));
    painter.restore();

    // The band edges ARE the plot's boundaries, and saying so is what makes a
    // transition level "0.7 eV above the VBM" readable as a position.
    painter.setPen(QPen(QColor(0x1f, 0x77, 0xb4), 1.2, Qt::DashLine));
    painter.drawLine(QPointF(toX(0.0), box.top()),
                     QPointF(toX(0.0), box.bottom()));
    painter.drawLine(QPointF(toX(xMax), box.top()),
                     QPointF(toX(xMax), box.bottom()));
    painter.setPen(QColor(0x1f, 0x77, 0xb4));
    painter.drawText(QPointF(toX(0.0) + 3.0, box.top() + metrics.height()),
                     tr("VBM"));
    painter.drawText(QPointF(toX(xMax) - metrics.horizontalAdvance(tr("CBM"))
                                 - 3.0,
                             box.top() + metrics.height()),
                     tr("CBM"));

    const auto polyline = [&](const std::vector<double>& values) {
        QPainterPath path;
        for (std::size_t i = 0; i < values.size() && i < fermi_.size(); ++i) {
            const QPointF p(toX(fermi_[i]), toY(values[i]));
            if (i == 0)
                path.moveTo(p);
            else
                path.lineTo(p);
        }
        return path;
    };

    if (!envelopeOnly_) {
        for (const Line& line : lines_) {
            painter.setPen(QPen(line.color, 1.2));
            painter.drawPath(polyline(line.energies));
            // Charge labels at the left edge, where the lines are furthest
            // apart (they all converge toward the CBM side as q·E_F grows).
            if (!line.energies.empty()) {
                painter.drawText(
                    QPointF(toX(0.0) + 26.0, toY(line.energies.front()) - 3.0),
                    QStringLiteral("q = %1")
                        .arg(line.charge, 0, 10, QLatin1Char(' '))
                        .trimmed()
                        .replace(QStringLiteral("q = "),
                                 line.charge > 0 ? QStringLiteral("q = +")
                                                 : QStringLiteral("q = ")));
            }
        }
    }

    // The envelope last and heaviest: it is what is actually observed.
    painter.setPen(QPen(PlotPalette::text, 2.4));
    painter.drawPath(polyline(envelope_));

    if (showTransitions_) {
        for (const Transition& t : transitions_) {
            const QPointF p(toX(t.levelEv), toY(t.formationEv));
            painter.setPen(QPen(QColor(217, 83, 79), 1.2, Qt::DashLine));
            painter.drawLine(QPointF(p.x(), box.bottom()), p);
            painter.setBrush(QColor(217, 83, 79));
            painter.setPen(Qt::NoPen);
            painter.drawEllipse(p, 4.0, 4.0);
            painter.setPen(QColor(217, 83, 79));
            painter.drawText(
                p + QPointF(6.0, -6.0),
                QStringLiteral("(%1/%2)")
                    .arg(t.fromCharge > 0 ? QStringLiteral("+%1").arg(t.fromCharge)
                                          : QString::number(t.fromCharge))
                    .arg(t.toCharge > 0 ? QStringLiteral("+%1").arg(t.toCharge)
                                        : QString::number(t.toCharge)));
        }
    }
}

// ---------------------------------------------------------------------------
// DefectDiagramWindow
// ---------------------------------------------------------------------------

DefectDiagramWindow::DefectDiagramWindow(QWidget* parent) : QDialog(parent)
{
    setWindowTitle(tr("Charged Defect Diagram"));
    resize(980, 720);

    auto* layout = new QVBoxLayout(this);

    summary_ = new QLabel(this);
    summary_->setWordWrap(true);
    summary_->setTextFormat(Qt::RichText);
    layout->addWidget(summary_);

    warning_ = new QLabel(this);
    warning_->setWordWrap(true);
    warning_->setTextFormat(Qt::RichText);
    layout->addWidget(warning_);

    auto* splitter = new QSplitter(Qt::Vertical, this);
    plot_ = new DefectDiagramWidget(splitter);
    splitter->addWidget(plot_);

    auto* tables = new QWidget(splitter);
    auto* tablesLayout = new QHBoxLayout(tables);
    tablesLayout->setContentsMargins(0, 0, 0, 0);

    chargeTable_ = new QTableWidget(0, 6, tables);
    chargeTable_->setHorizontalHeaderLabels(
        {tr("q"), tr("E_tot (eV)"), tr("E_corr (eV)"), tr("isolated"),
         tr("periodic"), tr("alignment")});
    chargeTable_->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    chargeTable_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    chargeTable_->setToolTip(
        tr("The FNV breakdown, shown rather than folded away: E_corr enters "
           "every formation energy directly and is the largest single "
           "approximation in the method.\n\n"
           "E_corr = −(periodic − isolated) + q·alignment. A correction that "
           "dwarfs the energy differences being measured usually means ε was "
           "left at 1, or the supercell is too small for the averaging region "
           "to reach bulk-like potential."));
    tablesLayout->addWidget(chargeTable_, 3);

    transitionTable_ = new QTableWidget(0, 3, tables);
    transitionTable_->setHorizontalHeaderLabels(
        {tr("transition"), tr("E_F above VBM (eV)"), tr("below CBM (eV)")});
    transitionTable_->horizontalHeader()->setSectionResizeMode(
        QHeaderView::Stretch);
    transitionTable_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    transitionTable_->setToolTip(
        tr("Thermodynamic transition levels: the Fermi energies at which the "
           "lowest-energy charge state changes. These are the quantities a "
           "deep-level measurement reports."));
    tablesLayout->addWidget(transitionTable_, 2);
    splitter->addWidget(tables);
    splitter->setStretchFactor(0, 3);
    splitter->setStretchFactor(1, 2);
    layout->addWidget(splitter, 1);

    auto* controls = new QHBoxLayout;
    envelopeCheck_ = new QCheckBox(tr("Lower envelope only"), this);
    envelopeCheck_->setToolTip(
        tr("Hide the individual charge-state lines. The envelope is the "
           "physically observable object — the lines above it describe states "
           "that are never the ground state at that Fermi level."));
    connect(envelopeCheck_, &QCheckBox::toggled, plot_,
            &DefectDiagramWidget::setEnvelopeOnly);
    controls->addWidget(envelopeCheck_);

    transitionsCheck_ = new QCheckBox(tr("Mark transition levels"), this);
    transitionsCheck_->setChecked(true);
    connect(transitionsCheck_, &QCheckBox::toggled, plot_,
            &DefectDiagramWidget::setShowTransitions);
    controls->addWidget(transitionsCheck_);

    controls->addStretch(1);
    auto* exportImageButton = new QPushButton(tr("Export Image…"), this);
    connect(exportImageButton, &QPushButton::clicked, this,
            &DefectDiagramWindow::exportImage);
    controls->addWidget(exportImageButton);
    auto* exportDataButton = new QPushButton(tr("Export Data…"), this);
    connect(exportDataButton, &QPushButton::clicked, this,
            &DefectDiagramWindow::exportData);
    controls->addWidget(exportDataButton);
    layout->addLayout(controls);
}

bool DefectDiagramWindow::loadResults(const QString& jsonPath)
{
    data_ = readJsonObject(jsonPath);
    if (data_.isEmpty())
        return false;
    sourcePath_ = jsonPath;

    const QJsonObject host = data_.value(QStringLiteral("host")).toObject();
    const double gap = host.value(QStringLiteral("E_gap_eV")).toDouble();

    std::vector<double> fermi;
    for (const QJsonValue& v : data_.value(QStringLiteral("fermi_level_eV")).toArray())
        fermi.push_back(v.toDouble());
    std::vector<double> envelope;
    for (const QJsonValue& v : data_.value(QStringLiteral("envelope_eV")).toArray())
        envelope.push_back(v.toDouble());

    const QJsonArray charges = data_.value(QStringLiteral("charges")).toArray();
    std::vector<DefectDiagramWidget::Line> lines;
    for (int i = 0; i < charges.size(); ++i) {
        const QJsonObject entry = charges.at(i).toObject();
        DefectDiagramWidget::Line line;
        line.charge = entry.value(QStringLiteral("charge")).toInt();
        for (const QJsonValue& v :
             entry.value(QStringLiteral("formation_energy_eV")).toArray())
            line.energies.push_back(v.toDouble());
        line.color = render::ColorMap::sample(
            render::ColorGradient::Turbo,
            charges.size() > 1 ? static_cast<float>(i) / (charges.size() - 1)
                               : 0.5f);
        lines.push_back(std::move(line));
    }

    std::vector<DefectDiagramWidget::Transition> transitions;
    for (const QJsonValue& v :
         data_.value(QStringLiteral("transitions")).toArray()) {
        const QJsonObject t = v.toObject();
        transitions.push_back(
            {t.value(QStringLiteral("from_charge")).toInt(),
             t.value(QStringLiteral("to_charge")).toInt(),
             t.value(QStringLiteral("level_eV_above_VBM")).toDouble(),
             t.value(QStringLiteral("formation_energy_eV")).toDouble()});
    }

    if (fermi.size() < 2 || lines.empty())
        return false;

    plot_->setData(std::move(fermi), std::move(lines), std::move(envelope),
                   std::move(transitions), gap);

    const QJsonObject defect = data_.value(QStringLiteral("defect")).toObject();
    const QJsonObject fnv = data_.value(QStringLiteral("fnv")).toObject();
    summary_->setText(
        tr("<b>%1</b> (%2 atoms) in a %3-atom host · E<sub>gap</sub> = "
           "%4 eV · %5 charge state(s) · %6 transition level(s)")
            .arg(defect.value(QStringLiteral("formula")).toString())
            .arg(defect.value(QStringLiteral("natoms")).toInt())
            .arg(host.value(QStringLiteral("natoms")).toInt())
            .arg(gap, 0, 'f', 3)
            .arg(charges.size())
            .arg(data_.value(QStringLiteral("transitions")).toArray().size()));

    // The two failure modes that produce a plausible-looking diagram made of
    // wrong numbers, called out where they cannot be missed.
    QStringList warnings;
    if (!fnv.value(QStringLiteral("applied")).toBool())
        warnings << tr("The FNV correction was <b>not applied</b> — these "
                       "formation energies still carry the spurious "
                       "periodic-image interaction.");
    else if (std::abs(fnv.value(QStringLiteral("epsilon")).toDouble() - 1.0)
             < 1e-9)
        warnings << tr("The dielectric constant is <b>ε = 1</b>, which treats "
                       "the host as vacuum and overcorrects badly for any real "
                       "material.");
    if (data_.value(QStringLiteral("species")).toArray().isEmpty())
        warnings << tr("No exchanged species were declared, so the chemical-"
                       "potential term is zero and every formation energy is "
                       "shifted by a constant.");
    warning_->setText(
        warnings.isEmpty()
            ? QString()
            : QStringLiteral("<span style='color:#d9534f;'>⚠ %1</span>")
                  .arg(warnings.join(QStringLiteral("<br>⚠ "))));
    warning_->setVisible(!warnings.isEmpty());

    populateTables();
    return true;
}

void DefectDiagramWindow::populateTables()
{
    const QJsonArray charges = data_.value(QStringLiteral("charges")).toArray();
    chargeTable_->setRowCount(charges.size());
    for (int row = 0; row < charges.size(); ++row) {
        const QJsonObject entry = charges.at(row).toObject();
        const QJsonObject terms =
            entry.value(QStringLiteral("correction_terms")).toObject();
        const int q = entry.value(QStringLiteral("charge")).toInt();
        const auto cell = [&](int column, const QString& text) {
            chargeTable_->setItem(row, column, new QTableWidgetItem(text));
        };
        cell(0, q > 0 ? QStringLiteral("+%1").arg(q) : QString::number(q));
        cell(1, QString::number(
                    entry.value(QStringLiteral("E_tot_eV")).toDouble(), 'f', 5));
        cell(2, QString::number(
                    entry.value(QStringLiteral("correction_eV")).toDouble(),
                    'f', 5));
        const auto term = [&](const char* key) {
            const QJsonValue v = terms.value(QLatin1String(key));
            return v.isDouble() ? QString::number(v.toDouble(), 'f', 5)
                                : QStringLiteral("—");
        };
        cell(3, term("isolated_eV"));
        cell(4, term("periodic_eV"));
        cell(5, term("alignment_eV"));
    }

    const QJsonArray transitions =
        data_.value(QStringLiteral("transitions")).toArray();
    transitionTable_->setRowCount(transitions.size());
    for (int row = 0; row < transitions.size(); ++row) {
        const QJsonObject t = transitions.at(row).toObject();
        const auto sign = [](int q) {
            return q > 0 ? QStringLiteral("+%1").arg(q) : QString::number(q);
        };
        transitionTable_->setItem(
            row, 0,
            new QTableWidgetItem(
                QStringLiteral("ε(%1/%2)")
                    .arg(sign(t.value(QStringLiteral("from_charge")).toInt()))
                    .arg(sign(t.value(QStringLiteral("to_charge")).toInt()))));
        transitionTable_->setItem(
            row, 1,
            new QTableWidgetItem(QString::number(
                t.value(QStringLiteral("level_eV_above_VBM")).toDouble(), 'f',
                4)));
        transitionTable_->setItem(
            row, 2,
            new QTableWidgetItem(QString::number(
                t.value(QStringLiteral("level_eV_below_CBM")).toDouble(), 'f',
                4)));
    }
}

void DefectDiagramWindow::exportImage()
{
    const QString path = QFileDialog::getSaveFileName(
        this, tr("Export defect diagram"),
        QStringLiteral("charged_defects.png"), tr("PNG image (*.png)"));
    if (path.isEmpty())
        return;
    QPixmap pixmap(plot_->size());
    pixmap.fill(PlotPalette::canvas);
    plot_->render(&pixmap);
    if (!pixmap.save(path))
        QMessageBox::warning(this, windowTitle(),
                             tr("Could not write %1").arg(path));
}

void DefectDiagramWindow::exportData()
{
    const QString path = QFileDialog::getSaveFileName(
        this, tr("Export formation energies"),
        QStringLiteral("charged_defects.csv"), tr("CSV (*.csv)"));
    if (path.isEmpty())
        return;
    writeTextFile(this, path, [this](QTextStream& out) {
        const QJsonArray fermi =
            data_.value(QStringLiteral("fermi_level_eV")).toArray();
        const QJsonArray charges =
            data_.value(QStringLiteral("charges")).toArray();
        const QJsonArray envelope =
            data_.value(QStringLiteral("envelope_eV")).toArray();
        out << "E_F_minus_VBM_eV";
        for (const QJsonValue& v : charges)
            out << ",E_f_q" << v.toObject().value(QStringLiteral("charge")).toInt();
        out << ",envelope_eV,stable_charge\n";
        const QJsonArray stable =
            data_.value(QStringLiteral("stable_charge")).toArray();
        for (int i = 0; i < fermi.size(); ++i) {
            out << QString::number(fermi.at(i).toDouble(), 'g', 8);
            for (const QJsonValue& v : charges) {
                const QJsonArray line =
                    v.toObject().value(QStringLiteral("formation_energy_eV"))
                        .toArray();
                out << ','
                    << (i < line.size()
                            ? QString::number(line.at(i).toDouble(), 'g', 8)
                            : QString());
            }
            out << ','
                << (i < envelope.size()
                        ? QString::number(envelope.at(i).toDouble(), 'g', 8)
                        : QString())
                << ',' << (i < stable.size() ? stable.at(i).toInt() : 0) << '\n';
        }
    });
}

} // namespace calango::gui
