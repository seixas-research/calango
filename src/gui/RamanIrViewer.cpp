#include "gui/RamanIrViewer.hpp"

#include "gui/LinePlotWidget.hpp"

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
#include <QPushButton>
#include <QSplitter>
#include <QTableWidget>
#include <QTextStream>
#include <QVBoxLayout>

namespace calango::gui {

namespace {

enum Column {
    ColIndex = 0, ColFrequency, ColIr, ColRamanActivity, ColRamanIntensity,
    ColumnCount
};

/// A numeric, right-aligned, non-editable table cell that SORTS numerically.
/// Storing the value in Qt::EditRole is what makes the sort ordinal rather than
/// lexicographic — "1000" before "200" in a spectroscopy table is worse than
/// useless.
QTableWidgetItem* numberCell(double value, int decimals)
{
    auto* item = new QTableWidgetItem;
    item->setData(Qt::EditRole, value);
    item->setText(QString::number(value, 'f', decimals));
    item->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
    item->setFlags(item->flags() & ~Qt::ItemIsEditable);
    return item;
}

std::vector<double> toDoubles(const QJsonArray& array)
{
    std::vector<double> values;
    values.reserve(static_cast<std::size_t>(array.size()));
    for (const QJsonValue& value : array)
        values.push_back(value.toDouble());
    return values;
}

} // namespace

RamanIrViewer::RamanIrViewer(QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle(tr("Raman and IR Spectroscopy"));
    resize(960, 680);

    auto* layout = new QVBoxLayout(this);

    summaryLabel_ = new QLabel(this);
    summaryLabel_->setWordWrap(true);
    summaryLabel_->setTextFormat(Qt::RichText);
    layout->addWidget(summaryLabel_);

    spectrumCombo_ = new QComboBox(this);
    spectrumCombo_->addItem(tr("Infrared absorption"), QStringLiteral("ir"));
    spectrumCombo_->addItem(tr("Raman scattering"), QStringLiteral("raman"));
    auto* comboRow = new QHBoxLayout;
    comboRow->addWidget(new QLabel(tr("Spectrum:"), this));
    comboRow->addWidget(spectrumCombo_);
    comboRow->addStretch(1);
    layout->addLayout(comboRow);
    connect(spectrumCombo_, &QComboBox::currentIndexChanged, this,
            &RamanIrViewer::showSelectedSpectrum);

    // Splitter rather than a fixed split: which half matters depends on the
    // question. Reading off a band position wants the plot; assigning it to a
    // mode wants the table.
    auto* splitter = new QSplitter(Qt::Vertical, this);
    plot_ = new LinePlotWidget(splitter);
    plot_->setAxisLabels(tr("Wavenumber (cm⁻¹)"), tr("Intensity (arb. units)"));
    splitter->addWidget(plot_);

    table_ = new QTableWidget(splitter);
    table_->setColumnCount(ColumnCount);
    table_->setHorizontalHeaderLabels(
        {tr("Mode"), tr("ν̃ (cm⁻¹)"), tr("IR ((D/Å)²/amu)"),
         tr("Raman activity (Å⁴/amu)"), tr("Raman intensity (arb.)")});
    table_->horizontalHeader()->setStretchLastSection(true);
    table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table_->setSortingEnabled(true);
    table_->verticalHeader()->setVisible(false);
    splitter->addWidget(table_);
    splitter->setStretchFactor(0, 3);
    splitter->setStretchFactor(1, 2);
    layout->addWidget(splitter, 1);

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
            &RamanIrViewer::copyToClipboard);
    connect(csvButton, &QPushButton::clicked, this, &RamanIrViewer::exportCsv);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
}

bool RamanIrViewer::loadResults(const QString& jsonPath)
{
    QFile file(jsonPath);
    if (!file.open(QIODevice::ReadOnly))
        return false;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll());
    if (!document.isObject())
        return false;
    data_ = document.object();
    sourcePath_ = jsonPath;

    const QJsonArray modes = data_.value(QStringLiteral("modes")).toArray();
    if (modes.isEmpty())
        return false;

    const bool ramanComputed = data_.value(QStringLiteral("raman"))
                                   .toObject()
                                   .value(QStringLiteral("computed"))
                                   .toBool();
    // Say plainly when the Raman half was skipped. A flat Raman curve that is
    // flat because nothing computed it looks exactly like one that is flat
    // because every mode is inactive, and the two mean opposite things.
    QString summary =
        tr("<b>%1</b> — %2 atoms, %3 Γ-point modes at δ = %4 Å.<br>"
           "IR intensities from the inherited Born effective charges; ")
            .arg(data_.value(QStringLiteral("formula")).toString())
            .arg(data_.value(QStringLiteral("atoms")).toInt())
            .arg(modes.size())
            .arg(data_.value(QStringLiteral("displacement_A")).toDouble(), 0,
                 'f', 3);
    summary += ramanComputed
        ? tr("Raman activities from ∂α/∂Q at %1 nm, %2 K.")
              .arg(data_.value(QStringLiteral("laser_nm")).toDouble(), 0, 'f', 0)
              .arg(data_.value(QStringLiteral("temperature_K")).toDouble(), 0,
                   'f', 0)
        : tr("<b>the Raman spectrum was not computed</b> in this run — the "
             "Raman columns are zero because nothing evaluated them, not "
             "because the modes are inactive.");
    summaryLabel_->setText(summary);
    spectrumCombo_->setEnabled(ramanComputed);
    if (!ramanComputed)
        spectrumCombo_->setCurrentIndex(0);

    table_->setSortingEnabled(false);
    table_->setRowCount(modes.size());
    for (int row = 0; row < modes.size(); ++row) {
        const QJsonObject mode = modes.at(row).toObject();
        table_->setItem(row, ColIndex,
                        numberCell(mode.value(QStringLiteral("index")).toInt(), 0));
        table_->setItem(
            row, ColFrequency,
            numberCell(mode.value(QStringLiteral("frequency_cm")).toDouble(), 2));
        table_->setItem(
            row, ColIr,
            numberCell(
                mode.value(QStringLiteral("ir_intensity_D2_A2_amu")).toDouble(),
                4));
        table_->setItem(
            row, ColRamanActivity,
            numberCell(
                mode.value(QStringLiteral("raman_activity_A4_amu")).toDouble(),
                4));
        table_->setItem(
            row, ColRamanIntensity,
            numberCell(mode.value(QStringLiteral("raman_intensity")).toDouble(),
                       4));
    }
    table_->setSortingEnabled(true);
    table_->sortByColumn(ColFrequency, Qt::AscendingOrder);

    showSelectedSpectrum();
    return true;
}

void RamanIrViewer::showSelectedSpectrum()
{
    const QJsonObject spectrum =
        data_.value(QStringLiteral("spectrum")).toObject();
    const QString key = spectrumCombo_->currentData().toString();
    std::vector<double> x =
        toDoubles(spectrum.value(QStringLiteral("frequency_cm")).toArray());
    std::vector<double> y = toDoubles(spectrum.value(key).toArray());
    if (x.size() != y.size() || x.empty()) {
        plot_->clear();
        return;
    }
    plot_->setAxisLabels(tr("Wavenumber (cm⁻¹)"),
                         key == QStringLiteral("ir")
                             ? tr("IR absorption (arb. units)")
                             : tr("Raman intensity (arb. units)"));
    plot_->setData(std::move(x), std::move(y));
}

QString RamanIrViewer::plainTextSummary() const
{
    QString text;
    QTextStream stream(&text);
    stream << "index,frequency_cm,ir_D2_A2_amu,raman_activity_A4_amu,"
              "raman_intensity\n";
    for (const QJsonValue& value : data_.value(QStringLiteral("modes")).toArray()) {
        const QJsonObject mode = value.toObject();
        stream << mode.value(QStringLiteral("index")).toInt() << ','
               << mode.value(QStringLiteral("frequency_cm")).toDouble() << ','
               << mode.value(QStringLiteral("ir_intensity_D2_A2_amu")).toDouble()
               << ','
               << mode.value(QStringLiteral("raman_activity_A4_amu")).toDouble()
               << ','
               << mode.value(QStringLiteral("raman_intensity")).toDouble()
               << '\n';
    }
    return text;
}

void RamanIrViewer::copyToClipboard()
{
    QApplication::clipboard()->setText(plainTextSummary());
}

void RamanIrViewer::exportCsv()
{
    const QString suggestion =
        sourcePath_.isEmpty()
            ? QStringLiteral("raman_ir.csv")
            : QFileInfo(sourcePath_).absolutePath()
                + QStringLiteral("/raman_ir.csv");
    const QString path = QFileDialog::getSaveFileName(
        this, tr("Export Modes as CSV"), suggestion,
        tr("Comma-separated values (*.csv)"));
    if (path.isEmpty())
        return;
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text))
        return;
    QTextStream(&file) << plainTextSummary();
}

} // namespace calango::gui
