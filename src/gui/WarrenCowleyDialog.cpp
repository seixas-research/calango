#include "gui/WarrenCowleyDialog.hpp"

#include "gui/GuiUtils.hpp"

#include "core/Element.hpp"

#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QSaveFile>
#include <QTableWidget>
#include <QTextStream>
#include <QVBoxLayout>

#include <cmath>

namespace calango::gui {

WarrenCowleyDialog::WarrenCowleyDialog(
    std::shared_ptr<const core::Structure> structure, QWidget* parent)
    : QDialog(parent)
    , structure_(std::move(structure))
{
    setWindowTitle(tr("Warren-Cowley analysis"));
    resize(460, 420);

    auto* layout = new QVBoxLayout(this);
    auto* form = new QFormLayout;
    layout->addLayout(form);

    shell1Spin_ = new QDoubleSpinBox(this);
    shell1Spin_->setRange(1.0, 20.0);
    shell1Spin_->setValue(3.2);
    shell1Spin_->setSuffix(QStringLiteral(" Å"));
    form->addRow(tr("First shell cutoff:"), shell1Spin_);

    shell2Spin_ = new QDoubleSpinBox(this);
    shell2Spin_->setRange(0.0, 25.0);
    shell2Spin_->setValue(4.8);
    shell2Spin_->setSuffix(QStringLiteral(" Å"));
    shell2Spin_->setSpecialValueText(tr("off"));
    form->addRow(tr("Second shell cutoff:"), shell2Spin_);

    infoLabel_ = new QLabel(this);
    infoLabel_->setWordWrap(true);
    layout->addWidget(infoLabel_);

    table_ = new QTableWidget(this);
    table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table_->horizontalHeader()->setStretchLastSection(true);
    layout->addWidget(table_, 1);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    auto* computeButton =
        buttons->addButton(tr("Compute"), QDialogButtonBox::ActionRole);
    auto* exportButton =
        buttons->addButton(tr("Export CSV…"), QDialogButtonBox::ActionRole);
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(computeButton, &QPushButton::clicked,
            this, &WarrenCowleyDialog::compute);
    connect(exportButton, &QPushButton::clicked,
            this, &WarrenCowleyDialog::exportData);

    compute();
}

void WarrenCowleyDialog::compute()
{
    core::WarrenCowleyOptions options;
    options.shellCutoffs = {shell1Spin_->value()};
    if (shell2Spin_->value() > shell1Spin_->value())
        options.shellCutoffs.push_back(shell2Spin_->value());

    result_ = core::computeWarrenCowley(*structure_, options);

    const auto speciesCount = result_.species.size();
    if (speciesCount < 2) {
        infoLabel_->setText(tr("The structure has a single species — "
                               "short-range order is undefined."));
        table_->clear();
        table_->setRowCount(0);
        table_->setColumnCount(0);
        return;
    }

    QStringList concentrations;
    for (std::size_t s = 0; s < speciesCount; ++s)
        concentrations << QStringLiteral("c(%1) = %2")
                              .arg(QString::fromUtf8(core::Elements::data(
                                       result_.species[s]).symbol))
                              .arg(result_.concentrations[s], 0, 'f', 3);
    QStringList shellNotes;
    for (const auto& shell : result_.shells)
        shellNotes << tr("%1–%2 Å: ⟨N⟩ = %3")
                          .arg(shell.rMin, 0, 'f', 2)
                          .arg(shell.rMax, 0, 'f', 2)
                          .arg(shell.meanNeighbors, 0, 'f', 1);
    infoLabel_->setText(concentrations.join(QStringLiteral("   "))
                        + QStringLiteral("\n") + tr("Shells — ")
                        + shellNotes.join(QStringLiteral("   ")));

    // Rows: ordered pairs i→j; columns: one α per shell.
    const auto pairCount = static_cast<int>(speciesCount * speciesCount);
    table_->setRowCount(pairCount);
    table_->setColumnCount(static_cast<int>(result_.shells.size()));
    QStringList headers;
    for (std::size_t k = 0; k < result_.shells.size(); ++k)
        headers << tr("α shell %1").arg(k + 1);
    table_->setHorizontalHeaderLabels(headers);

    int row = 0;
    for (std::size_t si = 0; si < speciesCount; ++si) {
        for (std::size_t sj = 0; sj < speciesCount; ++sj, ++row) {
            table_->setVerticalHeaderItem(
                row, new QTableWidgetItem(QStringLiteral("%1 – %2").arg(
                         QString::fromUtf8(
                             core::Elements::data(result_.species[si]).symbol),
                         QString::fromUtf8(
                             core::Elements::data(result_.species[sj]).symbol))));
            for (std::size_t k = 0; k < result_.shells.size(); ++k) {
                const double alpha = result_.shells[k].alpha[si][sj];
                table_->setItem(row, static_cast<int>(k),
                                new QTableWidgetItem(
                                    std::isnan(alpha)
                                        ? QStringLiteral("—")
                                        : QString::number(alpha, 'f', 4)));
            }
        }
    }
}

void WarrenCowleyDialog::exportData()
{
    if (result_.shells.empty())
        return;
    const QString path = QFileDialog::getSaveFileName(
        this, tr("Export Warren-Cowley Parameters"),
        QStringLiteral("warren_cowley.csv"), tr("CSV (*.csv)"));
    if (path.isEmpty())
        return;

    writeTextFile(this, path, [&](QTextStream& out) {
        out << "pair";
        for (std::size_t k = 0; k < result_.shells.size(); ++k)
            out << QStringLiteral(",alpha_shell%1_%2A_%3A")
                       .arg(k + 1)
                       .arg(result_.shells[k].rMin, 0, 'f', 2)
                       .arg(result_.shells[k].rMax, 0, 'f', 2);
        out << "\n";
        const auto speciesCount = result_.species.size();
        for (std::size_t si = 0; si < speciesCount; ++si) {
            for (std::size_t sj = 0; sj < speciesCount; ++sj) {
                out << core::Elements::data(result_.species[si]).symbol << "-"
                    << core::Elements::data(result_.species[sj]).symbol;
                for (const auto& shell : result_.shells)
                    out << "," << QString::number(shell.alpha[si][sj], 'g', 6);
                out << "\n";
            }
        }
    });
}

} // namespace calango::gui
