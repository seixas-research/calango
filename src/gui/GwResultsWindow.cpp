#include "gui/GwResultsWindow.hpp"

#include "gui/GuiUtils.hpp"

#include <QApplication>
#include <QClipboard>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonValue>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QTableWidget>
#include <QTextStream>
#include <QVBoxLayout>

#include <cmath>

namespace calango::gui {

namespace {

/// A JSON number that the script may legitimately write as null (an edge it
/// could not identify — e.g. a metal, where there is no gap to report).
std::optional<double> optionalNumber(const QJsonObject& object, const char* key)
{
    const QJsonValue value = object.value(QLatin1String(key));
    if (!value.isDouble())
        return std::nullopt;
    const double number = value.toDouble();
    if (!std::isfinite(number))
        return std::nullopt;
    return number;
}

QString formatEv(const std::optional<double>& value, int precision = 4)
{
    return value ? QStringLiteral("%1 eV").arg(*value, 0, 'f', precision)
                 : QObject::tr("—");
}

/// Rows of eigenvalues as written by the generator: a list of lists, already
/// flattened to (state-block × band).
std::vector<std::vector<double>> readMatrix(const QJsonObject& object,
                                            const char* key)
{
    std::vector<std::vector<double>> rows;
    const QJsonArray outer = object.value(QLatin1String(key)).toArray();
    rows.reserve(outer.size());
    for (const QJsonValue& rowValue : outer) {
        const QJsonArray inner = rowValue.toArray();
        std::vector<double> row;
        row.reserve(inner.size());
        for (const QJsonValue& item : inner)
            row.push_back(item.toDouble());
        rows.push_back(std::move(row));
    }
    return rows;
}

} // namespace

GwResultsWindow::GwResultsWindow(QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle(tr("GW Viewer"));
    resize(720, 620);

    auto* layout = new QVBoxLayout(this);

    auto* summaryGroup = new QGroupBox(tr("Quasiparticle Summary"), this);
    auto* summaryLayout = new QVBoxLayout(summaryGroup);

    engineLabel_ = new QLabel(summaryGroup);
    engineLabel_->setWordWrap(true);
    summaryLayout->addWidget(engineLabel_);

    dftGapLabel_ = new QLabel(summaryGroup);
    gwGapLabel_ = new QLabel(summaryGroup);
    renormLabel_ = new QLabel(summaryGroup);
    // The headline number carries the interpretation, so give it weight.
    QFont headline = renormLabel_->font();
    headline.setBold(true);
    headline.setPointSizeF(headline.pointSizeF() + 2.0);
    renormLabel_->setFont(headline);
    summaryLayout->addWidget(dftGapLabel_);
    summaryLayout->addWidget(gwGapLabel_);
    summaryLayout->addWidget(renormLabel_);

    edgesLabel_ = new QLabel(summaryGroup);
    edgesLabel_->setWordWrap(true);
    summaryLayout->addWidget(edgesLabel_);

    warningLabel_ = new QLabel(summaryGroup);
    warningLabel_->setWordWrap(true);
    warningLabel_->setStyleSheet(QStringLiteral("color: #d9534f;"));
    warningLabel_->hide();
    summaryLayout->addWidget(warningLabel_);

    layout->addWidget(summaryGroup);

    auto* statesGroup = new QGroupBox(tr("Per-State Corrections"), this);
    auto* statesLayout = new QVBoxLayout(statesGroup);
    auto* statesNote = new QLabel(
        tr("Σ − V_xc shifts each state individually. The corrections should "
           "vary smoothly across bands; erratic jumps between neighboring "
           "states point at an unconverged screening cutoff or too few empty "
           "bands, not at physics."),
        statesGroup);
    statesNote->setWordWrap(true);
    statesLayout->addWidget(statesNote);

    statesTable_ = new QTableWidget(statesGroup);
    statesTable_->setColumnCount(4);
    statesTable_->setHorizontalHeaderLabels(
        {tr("k-point"), tr("Band"), tr("ε_DFT (eV)"), tr("E_qp (eV)")});
    statesTable_->horizontalHeader()->setStretchLastSection(true);
    statesTable_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    statesTable_->setSelectionBehavior(QAbstractItemView::SelectRows);
    statesLayout->addWidget(statesTable_);
    layout->addWidget(statesGroup, 1);

    auto* buttons = new QHBoxLayout;
    auto* copyButton = new QPushButton(tr("Copy Summary"), this);
    auto* csvButton = new QPushButton(tr("Export CSV…"), this);
    auto* closeButton = new QPushButton(tr("Close"), this);
    buttons->addWidget(copyButton);
    buttons->addWidget(csvButton);
    buttons->addStretch(1);
    buttons->addWidget(closeButton);
    layout->addLayout(buttons);

    connect(copyButton, &QPushButton::clicked, this,
            &GwResultsWindow::copyToClipboard);
    connect(csvButton, &QPushButton::clicked, this,
            &GwResultsWindow::exportCsv);
    connect(closeButton, &QPushButton::clicked, this, &QDialog::accept);
}

bool GwResultsWindow::loadResults(const QString& jsonPath)
{
    QFile file(jsonPath);
    if (!file.open(QIODevice::ReadOnly))
        return false;
    const QByteArray raw = file.readAll();
    file.close();

    QJsonParseError parseError{};
    const QJsonDocument doc = QJsonDocument::fromJson(raw, &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject())
        return false;
    data_ = doc.object();
    sourcePath_ = jsonPath;

    const QString engine =
        data_.value(QStringLiteral("engine")).toString(tr("unknown"));
    const QString treatment =
        data_.value(QStringLiteral("frequency_treatment")).toString();
    const double cutoff =
        data_.value(QStringLiteral("screening_cutoff_eV")).toDouble();
    engineLabel_->setText(
        tr("<b>%1</b> · %2 screening · cutoff %3 eV")
            .arg(engine.toHtmlEscaped(), treatment.toHtmlEscaped())
            .arg(cutoff, 0, 'f', 1));

    const auto dftGap = optionalNumber(data_, "dft_gap_eV");
    const auto gwGap = optionalNumber(data_, "gw_gap_eV");
    const auto renorm = optionalNumber(data_, "gap_renormalization_eV");

    dftGapLabel_->setText(tr("DFT gap: %1").arg(formatEv(dftGap)));
    gwGapLabel_->setText(tr("G₀W₀ gap: %1").arg(formatEv(gwGap)));
    if (renorm) {
        renormLabel_->setText(tr("Gap renormalization: %1%2 eV")
                                  .arg(*renorm >= 0.0 ? QStringLiteral("+")
                                                      : QString())
                                  .arg(*renorm, 0, 'f', 4));
    } else {
        renormLabel_->setText(tr("Gap renormalization: —"));
    }

    const auto dftVbm = optionalNumber(data_, "dft_vbm_eV");
    const auto dftCbm = optionalNumber(data_, "dft_cbm_eV");
    const auto gwVbm = optionalNumber(data_, "gw_vbm_eV");
    const auto gwCbm = optionalNumber(data_, "gw_cbm_eV");
    edgesLabel_->setText(tr("VBM %1 → %2    ·    CBM %3 → %4")
                             .arg(formatEv(dftVbm, 3), formatEv(gwVbm, 3),
                                  formatEv(dftCbm, 3), formatEv(gwCbm, 3)));

    // A negative or vanishing renormalization is the classic signature of an
    // unconverged G0W0 run. Saying so here is the difference between a number
    // the user trusts and one they check.
    if (renorm && *renorm <= 0.05) {
        warningLabel_->setText(
            tr("⚠ G₀W₀ almost always opens a semiconductor gap by 0.5–2 eV. A "
               "renormalization of %1 eV is more likely an unconverged "
               "calculation — raise the screening cutoff and the number of "
               "empty bands until the gap stops moving — than a physical "
               "result. (For a metal or a semimetal the gap numbers are not "
               "meaningful in the first place.)")
                .arg(*renorm, 0, 'f', 4));
        warningLabel_->show();
    } else {
        warningLabel_->hide();
    }

    // -- Per-state table ----------------------------------------------------
    const auto dft = readMatrix(data_, "dft_eigenvalues_eV");
    const auto qp = readMatrix(data_, "qp_eigenvalues_eV");
    const std::size_t rows = std::min(dft.size(), qp.size());
    std::size_t total = 0;
    for (std::size_t k = 0; k < rows; ++k)
        total += std::min(dft[k].size(), qp[k].size());

    statesTable_->setRowCount(static_cast<int>(total));
    int row = 0;
    for (std::size_t k = 0; k < rows; ++k) {
        const std::size_t bands = std::min(dft[k].size(), qp[k].size());
        for (std::size_t b = 0; b < bands; ++b) {
            const double e0 = dft[k][b];
            const double e1 = qp[k][b];
            statesTable_->setItem(
                row, 0, new QTableWidgetItem(QString::number(k + 1)));
            statesTable_->setItem(
                row, 1, new QTableWidgetItem(QString::number(b + 1)));
            statesTable_->setItem(
                row, 2,
                new QTableWidgetItem(QString::number(e0, 'f', 4)));
            // The correction itself is what the table is read for, so it is
            // shown next to the value rather than left to be computed by eye.
            auto* qpItem = new QTableWidgetItem(
                tr("%1  (%2%3)")
                    .arg(QString::number(e1, 'f', 4),
                         e1 - e0 >= 0.0 ? QStringLiteral("+") : QString())
                    .arg(e1 - e0, 0, 'f', 4));
            statesTable_->setItem(row, 3, qpItem);
            ++row;
        }
    }
    statesTable_->resizeColumnsToContents();
    return true;
}

QString GwResultsWindow::plainTextSummary() const
{
    QString text;
    QTextStream out(&text);
    out << "G0W0 quasiparticle summary\n"
        << "  engine:              "
        << data_.value(QStringLiteral("engine")).toString() << '\n'
        << "  frequency treatment: "
        << data_.value(QStringLiteral("frequency_treatment")).toString() << '\n'
        << "  screening cutoff:    "
        << data_.value(QStringLiteral("screening_cutoff_eV")).toDouble()
        << " eV\n";
    const auto emit_ = [&out](const char* label, const std::optional<double>& v) {
        out << "  " << label
            << (v ? QString::number(*v, 'f', 4) : QStringLiteral("n/a"))
            << " eV\n";
    };
    emit_("DFT gap:             ", optionalNumber(data_, "dft_gap_eV"));
    emit_("GW gap:              ", optionalNumber(data_, "gw_gap_eV"));
    emit_("renormalization:     ",
          optionalNumber(data_, "gap_renormalization_eV"));
    return text;
}

void GwResultsWindow::copyToClipboard()
{
    QApplication::clipboard()->setText(plainTextSummary());
}

void GwResultsWindow::exportCsv()
{
    if (statesTable_->rowCount() == 0) {
        QMessageBox::information(
            this, tr("Export CSV"),
            tr("This job reported no per-state quasiparticle energies."));
        return;
    }
    const QString path = QFileDialog::getSaveFileName(
        this, tr("Export Quasiparticle Energies"), QStringLiteral("gw.csv"),
        tr("CSV (*.csv)"));
    if (path.isEmpty())
        return;

    writeTextFile(this, path, [&](QTextStream& out) {
        // A CSV starts with its header row — the run summary lives in the
        // window and in gw.json, not as '#' lines a CSV reader trips over.
        out << "kpoint,band,eps_dft_eV,e_qp_eV,correction_eV\n";

        const auto dft = readMatrix(data_, "dft_eigenvalues_eV");
        const auto qp = readMatrix(data_, "qp_eigenvalues_eV");
        const std::size_t rows = std::min(dft.size(), qp.size());
        for (std::size_t k = 0; k < rows; ++k) {
            const std::size_t bands = std::min(dft[k].size(), qp[k].size());
            for (std::size_t b = 0; b < bands; ++b) {
                out << (k + 1) << ',' << (b + 1) << ','
                    << QString::number(dft[k][b], 'g', 8) << ','
                    << QString::number(qp[k][b], 'g', 8) << ','
                    << QString::number(qp[k][b] - dft[k][b], 'g', 8) << '\n';
            }
        }
    });
}

} // namespace calango::gui
