#include "gui/NonlinearOpticsResultsWindow.hpp"

#include "gui/GuiUtils.hpp"
#include "gui/SpectrumPlotWidget.hpp"

#include <QApplication>
#include <QClipboard>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QLabel>
#include <QPushButton>
#include <QTextStream>
#include <QVBoxLayout>

namespace calango::gui {

namespace {

std::vector<double> toDoubles(const QJsonArray& array)
{
    std::vector<double> values;
    values.reserve(static_cast<std::size_t>(array.size()));
    for (const QJsonValue& value : array)
        values.push_back(value.toDouble());
    return values;
}

/// Which series a given spectrum object should draw, as (json key, legend).
///
/// Driven by what the object actually contains rather than by the selection,
/// so a bulk run (no sheet columns) and a sheet run share one code path and
/// neither has to know what the other wrote.
QList<QPair<QString, QString>> seriesFor(const QJsonObject& spectrum)
{
    static const QList<QPair<QString, QString>> kCandidates = {
        {QStringLiteral("chi2_re_pm_V"),
         QCoreApplication::translate("NonlinearOptics", "Re χ⁽²⁾")},
        {QStringLiteral("chi2_im_pm_V"),
         QCoreApplication::translate("NonlinearOptics", "Im χ⁽²⁾")},
        {QStringLiteral("chi2_abs_pm_V"),
         QCoreApplication::translate("NonlinearOptics", "|χ⁽²⁾|")},
        {QStringLiteral("chi2_sheet_re_nm2_V"),
         QCoreApplication::translate("NonlinearOptics", "Re χ⁽²⁾ (sheet)")},
        {QStringLiteral("chi2_sheet_im_nm2_V"),
         QCoreApplication::translate("NonlinearOptics", "Im χ⁽²⁾ (sheet)")},
        {QStringLiteral("sigma_A_V2"),
         QCoreApplication::translate("NonlinearOptics", "σ (shift current)")},
        {QStringLiteral("sigma_sheet_A_nm_V2"),
         QCoreApplication::translate("NonlinearOptics", "σ (sheet)")},
    };
    QList<QPair<QString, QString>> present;
    for (const auto& [key, label] : kCandidates)
        if (spectrum.contains(key))
            present.append({key, label});
    return present;
}

/// The y-axis label a spectrum wants, chosen from the columns it carries: a
/// susceptibility and a conductivity share the energy axis and nothing else.
QString axisLabelFor(const QJsonObject& spectrum)
{
    if (spectrum.contains(QStringLiteral("chi2_sheet_re_nm2_V")))
        return QCoreApplication::translate(
            "NonlinearOptics", "χ⁽²⁾ (pm/V) — sheet columns in nm²/V");
    if (spectrum.contains(QStringLiteral("chi2_re_pm_V")))
        return QCoreApplication::translate("NonlinearOptics", "χ⁽²⁾ (pm/V)");
    if (spectrum.contains(QStringLiteral("sigma_sheet_A_nm_V2")))
        return QCoreApplication::translate(
            "NonlinearOptics", "σ (A/V²) — sheet column in A·nm/V²");
    if (spectrum.contains(QStringLiteral("sigma_A_V2")))
        return QCoreApplication::translate("NonlinearOptics", "σ (A/V²)");
    return QCoreApplication::translate("NonlinearOptics", "Response");
}

} // namespace

NonlinearOpticsResultsWindow::NonlinearOpticsResultsWindow(QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle(tr("Nonlinear Optics"));
    resize(940, 660);

    auto* layout = new QVBoxLayout(this);

    summaryLabel_ = new QLabel(this);
    summaryLabel_->setWordWrap(true);
    summaryLabel_->setTextFormat(Qt::RichText);
    layout->addWidget(summaryLabel_);

    auto* comboRow = new QHBoxLayout;
    comboRow->addWidget(new QLabel(tr("Spectrum:"), this));
    spectrumCombo_ = new QComboBox(this);
    comboRow->addWidget(spectrumCombo_, 1);
    layout->addLayout(comboRow);
    connect(spectrumCombo_, &QComboBox::currentIndexChanged, this,
            &NonlinearOpticsResultsWindow::showSelectedSpectrum);

    plot_ = new SpectrumPlotWidget(this);
    layout->addWidget(plot_, 1);

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
            &NonlinearOpticsResultsWindow::copyToClipboard);
    connect(csvButton, &QPushButton::clicked, this,
            &NonlinearOpticsResultsWindow::exportCsv);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
}

bool NonlinearOpticsResultsWindow::loadResults(const QString& directory)
{
    sourcePath_ = directory + QStringLiteral("/nlopt.json");
    data_ = readJsonObject(sourcePath_);
    if (data_.isEmpty())
        return false;

    populateSelector();
    if (spectrumCombo_->count() == 0)
        return false;

    const bool centro =
        data_.value(QStringLiteral("centrosymmetric")).toBool();
    QString summary =
        tr("<b>%1</b> — η = %2 eV, %3 gauge, %4 points from %5 to %6 eV.")
            .arg(data_.value(QStringLiteral("formula")).toString())
            .arg(data_.value(QStringLiteral("eta_eV")).toDouble(), 0, 'f', 3)
            .arg(data_.value(QStringLiteral("gauge")).toString())
            .arg(data_.value(QStringLiteral("energy_eV")).toArray().size())
            .arg(data_.value(QStringLiteral("energy_eV"))
                     .toArray()
                     .first()
                     .toDouble(),
                 0, 'f', 2)
            .arg(data_.value(QStringLiteral("energy_eV"))
                     .toArray()
                     .last()
                     .toDouble(),
                 0, 'f', 2);
    const double scissors =
        data_.value(QStringLiteral("eshift_eV")).toDouble();
    if (scissors != 0.0)
        // Stated up front, every time. A scissors-shifted spectrum compared
        // against an unshifted one is the commonest way two runs of the same
        // material end up disagreeing for a reason nobody can find.
        summary += tr(" A scissors shift of <b>%1 eV</b> was applied.")
                       .arg(scissors, 0, 'f', 2);
    if (centro)
        summary += tr("<br><b>This cell has an inversion centre.</b> χ⁽²⁾ and "
                      "the shift current vanish identically by symmetry, so "
                      "any structure below is the residue of an incomplete "
                      "cancellation over a finite k-mesh — not a spectrum.");
    summaryLabel_->setText(summary);

    showSelectedSpectrum();
    return true;
}

void NonlinearOpticsResultsWindow::populateSelector()
{
    spectrumCombo_->clear();
    const auto addGroup = [this](const QString& groupKey,
                                 const QString& prefix) {
        const QJsonObject group = data_.value(groupKey).toObject();
        for (auto it = group.begin(); it != group.end(); ++it)
            spectrumCombo_->addItem(prefix.arg(it.key()),
                                    groupKey + QLatin1Char('/') + it.key());
    };
    addGroup(QStringLiteral("shg"), tr("SHG χ⁽²⁾ %1"));
    addGroup(QStringLiteral("shift"), tr("Shift current σ %1"));
    if (data_.contains(QStringLiteral("linear")))
        spectrumCombo_->addItem(tr("Linear χ⁽¹⁾ (diagonal ε)"),
                                QStringLiteral("linear"));
}

QJsonObject NonlinearOpticsResultsWindow::currentSpectrum() const
{
    const QString path =
        spectrumCombo_ ? spectrumCombo_->currentData().toString() : QString();
    if (path.isEmpty())
        return {};
    const QStringList parts = path.split(QLatin1Char('/'));
    if (parts.size() == 1)
        return data_.value(parts.at(0)).toObject();
    return data_.value(parts.at(0)).toObject().value(parts.at(1)).toObject();
}

void NonlinearOpticsResultsWindow::showSelectedSpectrum()
{
    const QJsonObject spectrum = currentSpectrum();
    std::vector<double> x =
        toDoubles(spectrum.value(QStringLiteral("energy_eV")).toArray());
    std::vector<QPair<QString, std::vector<double>>> series;

    const bool linear =
        spectrumCombo_
        && spectrumCombo_->currentData().toString() == QLatin1String("linear");
    if (linear) {
        // Only the diagonal. The nine components of χ⁽¹⁾ on one axis is nine
        // curves of which six are usually zero, and the reason to look at this
        // panel at all is the absorption edge — which is the diagonal ε₂.
        for (const char* axis : {"xx", "yy", "zz"}) {
            const QString key =
                QStringLiteral("eps_%1_2").arg(QLatin1String(axis));
            if (!spectrum.contains(key))
                continue;
            series.push_back({tr("ε₂ %1").arg(QLatin1String(axis)),
                              toDoubles(spectrum.value(key).toArray())});
        }
        plot_->setSeries(x, series, tr("Photon energy (eV)"),
                         tr("ε₂ = Im χ⁽¹⁾"));
        return;
    }

    for (const auto& [key, label] : seriesFor(spectrum))
        series.push_back({label, toDoubles(spectrum.value(key).toArray())});
    plot_->setSeries(x, series, tr("Photon energy (eV)"),
                     axisLabelFor(spectrum));
}

QString NonlinearOpticsResultsWindow::currentAsCsv() const
{
    const QJsonObject spectrum = currentSpectrum();
    const std::vector<double> x =
        toDoubles(spectrum.value(QStringLiteral("energy_eV")).toArray());

    // Every numeric column the selected spectrum carries, not only the plotted
    // ones: the export is what a user takes into their own analysis, and
    // silently dropping |χ⁽²⁾| because the plot showed Re and Im would be a
    // export that quietly disagrees with the file it came from.
    QStringList keys;
    for (auto it = spectrum.begin(); it != spectrum.end(); ++it)
        if (it.key() != QLatin1String("energy_eV") && it.value().isArray())
            keys << it.key();
    keys.sort();

    QString text;
    QTextStream stream(&text);
    stream << "energy_eV";
    for (const QString& key : keys)
        stream << ',' << key;
    stream << '\n';
    std::vector<std::vector<double>> columns;
    columns.reserve(static_cast<std::size_t>(keys.size()));
    for (const QString& key : keys)
        columns.push_back(toDoubles(spectrum.value(key).toArray()));
    for (std::size_t row = 0; row < x.size(); ++row) {
        stream << x[row];
        for (const std::vector<double>& column : columns)
            stream << ',' << (row < column.size() ? column[row] : 0.0);
        stream << '\n';
    }
    return text;
}

void NonlinearOpticsResultsWindow::copyToClipboard()
{
    QApplication::clipboard()->setText(currentAsCsv());
}

void NonlinearOpticsResultsWindow::exportCsv()
{
    const QString suggestion =
        sourcePath_.isEmpty()
            ? QStringLiteral("nlopt.csv")
            : QFileInfo(sourcePath_).absolutePath()
                + QStringLiteral("/nlopt.csv");
    const QString path = QFileDialog::getSaveFileName(
        this, tr("Export Spectrum as CSV"), suggestion,
        tr("Comma-separated values (*.csv)"));
    if (path.isEmpty())
        return;
    writeTextFile(this, path, currentAsCsv());
}

} // namespace calango::gui
