#include "gui/BornChargesViewer.hpp"

#include <QApplication>
#include <QClipboard>
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
#include <QTableWidget>
#include <QTextStream>
#include <QVBoxLayout>

namespace calango::gui {

namespace {

enum Column {
    ColIndex = 0, ColSymbol, ColIsotropic, ColEigen, ColTensor, ColumnCount
};

/// "1.23, -0.45, 2.01" for a row of a tensor or an eigenvalue triple.
QString formatTriple(const QJsonArray& values)
{
    QStringList parts;
    for (const QJsonValue& value : values)
        parts << QString::number(value.toDouble(), 'f', 3);
    return parts.join(QStringLiteral(", "));
}

} // namespace

BornChargesViewer::BornChargesViewer(QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle(tr("Born Effective Charges"));
    resize(880, 560);

    auto* layout = new QVBoxLayout(this);

    summaryLabel_ = new QLabel(this);
    summaryLabel_->setWordWrap(true);
    summaryLabel_->setTextFormat(Qt::RichText);
    layout->addWidget(summaryLabel_);

    sumRuleLabel_ = new QLabel(this);
    sumRuleLabel_->setWordWrap(true);
    sumRuleLabel_->setTextFormat(Qt::RichText);
    layout->addWidget(sumRuleLabel_);

    table_ = new QTableWidget(this);
    table_->setColumnCount(ColumnCount);
    table_->setHorizontalHeaderLabels({tr("Atom"), tr("Species"),
                                       tr("Z*_iso (e)"), tr("Eigenvalues (e)"),
                                       tr("Tensor Z*_αβ (e)")});
    table_->horizontalHeader()->setStretchLastSection(true);
    table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table_->verticalHeader()->setVisible(false);
    layout->addWidget(table_, 1);

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
            &BornChargesViewer::copyToClipboard);
    connect(csvButton, &QPushButton::clicked, this,
            &BornChargesViewer::exportCsv);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
}

bool BornChargesViewer::loadResults(const QString& jsonPath)
{
    QFile file(jsonPath);
    if (!file.open(QIODevice::ReadOnly))
        return false;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll());
    if (!document.isObject())
        return false;
    data_ = document.object();
    sourcePath_ = jsonPath;

    const QJsonArray atoms = data_.value(QStringLiteral("atoms")).toArray();
    if (atoms.isEmpty())
        return false;

    summaryLabel_->setText(
        tr("<b>%1 atom(s)</b>, displacement δ = %2 Å, cell volume %3 Å³.<br>"
           "Z* is the derivative of the macroscopic polarization with respect "
           "to an atomic displacement — a tensor, in units of the elementary "
           "charge.")
            .arg(atoms.size())
            .arg(data_.value(QStringLiteral("displacement_A")).toDouble(), 0, 'f', 3)
            .arg(data_.value(QStringLiteral("volume_A3")).toDouble(), 0, 'f', 2));

    // The ASR residual is the convergence diagnostic. Σ_k Z*_k must be exactly
    // zero, so whatever is left is error — and a large residual means the
    // numbers below are not to be trusted, whether or not the correction was
    // applied on top of them.
    const double residual =
        data_.value(QStringLiteral("asr_residual_e")).toDouble();
    const bool imposed =
        data_.value(QStringLiteral("acoustic_sum_rule")).toBool();
    const bool large = residual > 0.1;
    sumRuleLabel_->setText(
        tr("Acoustic sum rule: %1 — residual max|Σ_k Z*_k| = "
           "<span style=\"color:%2;\">%3 e</span>. %4")
            .arg(imposed ? tr("imposed") : tr("not imposed"),
                 large ? QStringLiteral("#d9534f") : QStringLiteral("#5cb85c"))
            .arg(residual, 0, 'f', 4)
            .arg(large
                     ? tr("That is large: Σ_k Z*_k is exactly zero for a "
                          "converged calculation, so treat these tensors as "
                          "under-converged (denser k-mesh, tighter SCF, or a "
                          "different δ).")
                     : tr("Small, as it should be for a converged run.")));

    table_->setRowCount(atoms.size());
    for (int row = 0; row < atoms.size(); ++row) {
        const QJsonObject entry = atoms.at(row).toObject();
        const auto set = [this, row](int column, const QString& text) {
            table_->setItem(row, column, new QTableWidgetItem(text));
        };
        set(ColIndex, QString::number(entry.value(QStringLiteral("index")).toInt()));
        set(ColSymbol, entry.value(QStringLiteral("symbol")).toString());
        set(ColIsotropic,
            QString::number(entry.value(QStringLiteral("isotropic")).toDouble(),
                            'f', 3));
        set(ColEigen,
            formatTriple(entry.value(QStringLiteral("eigenvalues")).toArray()));

        QStringList rows;
        for (const QJsonValue& tensorRow :
             entry.value(QStringLiteral("tensor")).toArray())
            rows << QStringLiteral("[%1]").arg(formatTriple(tensorRow.toArray()));
        set(ColTensor, rows.join(QStringLiteral(" ")));
    }
    table_->resizeColumnsToContents();
    table_->horizontalHeader()->setStretchLastSection(true);
    return true;
}

QString BornChargesViewer::plainTextSummary() const
{
    QString text;
    QTextStream stream(&text);
    stream << "Born effective charges Z* (e)\n"
           << "displacement delta = "
           << data_.value(QStringLiteral("displacement_A")).toDouble() << " A\n"
           << "acoustic sum rule: "
           << (data_.value(QStringLiteral("acoustic_sum_rule")).toBool()
                   ? "imposed"
                   : "not imposed")
           << ", residual = "
           << data_.value(QStringLiteral("asr_residual_e")).toDouble() << " e\n\n";
    for (const QJsonValue& value :
         data_.value(QStringLiteral("atoms")).toArray()) {
        const QJsonObject entry = value.toObject();
        stream << entry.value(QStringLiteral("index")).toInt() << "  "
               << entry.value(QStringLiteral("symbol")).toString()
               << "  Z*_iso = "
               << QString::number(
                      entry.value(QStringLiteral("isotropic")).toDouble(), 'f', 4)
               << "\n";
        for (const QJsonValue& tensorRow :
             entry.value(QStringLiteral("tensor")).toArray())
            stream << "      " << formatTriple(tensorRow.toArray()) << "\n";
    }
    return text;
}

void BornChargesViewer::copyToClipboard()
{
    QApplication::clipboard()->setText(plainTextSummary());
}

void BornChargesViewer::exportCsv()
{
    const QString suggestion =
        QFileInfo(sourcePath_).dir().filePath(QStringLiteral("born_charges.csv"));
    const QString path = QFileDialog::getSaveFileName(
        this, tr("Export Born Charges"), suggestion,
        tr("CSV (*.csv);;All files (*)"));
    if (path.isEmpty())
        return;
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text))
        return;
    QTextStream stream(&file);
    // One row per atom with the tensor flattened in row-major order — the shape
    // a spreadsheet or a plotting script can actually consume.
    stream << "index,symbol,z_iso,"
              "zxx,zxy,zxz,zyx,zyy,zyz,zzx,zzy,zzz\n";
    for (const QJsonValue& value :
         data_.value(QStringLiteral("atoms")).toArray()) {
        const QJsonObject entry = value.toObject();
        stream << entry.value(QStringLiteral("index")).toInt() << ','
               << entry.value(QStringLiteral("symbol")).toString() << ','
               << entry.value(QStringLiteral("isotropic")).toDouble();
        for (const QJsonValue& tensorRow :
             entry.value(QStringLiteral("tensor")).toArray())
            for (const QJsonValue& component : tensorRow.toArray())
                stream << ',' << component.toDouble();
        stream << '\n';
    }
}

} // namespace calango::gui
