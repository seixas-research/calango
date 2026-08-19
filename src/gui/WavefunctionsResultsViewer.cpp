#include "gui/WavefunctionsResultsViewer.hpp"

#include <QFile>
#include <QHeaderView>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QVBoxLayout>

namespace calango::gui {

WavefunctionsResultsViewer::WavefunctionsResultsViewer(QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle(tr("Wavefunctions"));
    resize(720, 420);

    auto* layout = new QVBoxLayout(this);

    summaryLabel_ = new QLabel(this);
    summaryLabel_->setWordWrap(true);
    layout->addWidget(summaryLabel_);

    table_ = new QTableWidget(this);
    table_->setColumnCount(7);
    table_->setHorizontalHeaderLabels(
        {tr("Band"), tr("k-point"), tr("Spin"), tr("Quantity"),
         tr("All-electron"), tr("Energy (eV)"), tr("Occupation")});
    table_->horizontalHeader()->setStretchLastSection(true);
    table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table_->setSelectionMode(QAbstractItemView::NoSelection);
    layout->addWidget(table_, 1);

    auto* hint = new QLabel(
        tr("Every cube above is listed in the Volumetric Data dock, "
           "unchecked — tick the ones you want to render."),
        this);
    hint->setWordWrap(true);
    hint->setStyleSheet(QStringLiteral("font-style: italic;"));
    layout->addWidget(hint);
}

bool WavefunctionsResultsViewer::loadResults(const QString& path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return false;
    const QJsonObject root = QJsonDocument::fromJson(file.readAll()).object();
    file.close();
    if (root.isEmpty())
        return false;

    const QJsonArray states = root.value(QStringLiteral("states")).toArray();
    const bool periodic = root.value(QStringLiteral("periodic")).toBool(false);

    if (summaryLabel_)
        summaryLabel_->setText(
            tr("%n wavefunction(s) written from %1 (%2 baseline).", nullptr,
               states.size())
                .arg(root.value(QStringLiteral("baseline_dir")).toString())
                .arg(periodic ? tr("periodic") : tr("non-periodic")));

    if (table_) {
        table_->setRowCount(states.size());
        for (int row = 0; row < states.size(); ++row) {
            const QJsonObject s = states.at(row).toObject();
            const int band = s.value(QStringLiteral("band")).toInt();
            const int kpt = s.value(QStringLiteral("kpt")).toInt();
            const int spin = s.value(QStringLiteral("spin")).toInt();
            const QString quantity = s.value(QStringLiteral("quantity")).toString();
            const bool allElectron =
                s.value(QStringLiteral("all_electron")).toBool(false);
            const double energy = s.value(QStringLiteral("energy_eV")).toDouble();
            const QJsonValue occ = s.value(QStringLiteral("occupation"));

            table_->setItem(row, 0, new QTableWidgetItem(QString::number(band)));
            table_->setItem(row, 1, new QTableWidgetItem(QString::number(kpt)));
            table_->setItem(
                row, 2,
                new QTableWidgetItem(spin == 0 ? tr("up") : tr("down")));
            table_->setItem(row, 3, new QTableWidgetItem(quantity));
            table_->setItem(row, 4,
                            new QTableWidgetItem(allElectron ? tr("yes")
                                                             : tr("no")));
            table_->setItem(
                row, 5, new QTableWidgetItem(QString::number(energy, 'f', 4)));
            table_->setItem(
                row, 6,
                new QTableWidgetItem(occ.isDouble()
                                         ? QString::number(occ.toDouble(), 'f', 3)
                                         : tr("n/a")));
        }
        table_->resizeColumnsToContents();
    }

    hasData_ = !states.isEmpty();
    return hasData_;
}

} // namespace calango::gui
