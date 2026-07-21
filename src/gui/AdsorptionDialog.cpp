#include "gui/AdsorptionDialog.hpp"

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QPushButton>
#include <QTableWidget>
#include <QVBoxLayout>

#include <cmath>
#include <map>

namespace calango::gui {

AdsorptionDialog::AdsorptionDialog(std::shared_ptr<const core::Structure> slab,
                                   QWidget* parent)
    : QDialog(parent)
    , slab_(std::move(slab))
{
    setWindowTitle(tr("Adsorption & Catalysis"));
    resize(560, 620);

    auto* layout = new QVBoxLayout(this);
    summaryLabel_ = new QLabel(this);
    summaryLabel_->setWordWrap(true);
    layout->addWidget(summaryLabel_);

    auto* filterRow = new QHBoxLayout;
    filterRow->addWidget(new QLabel(tr("Show sites:"), this));
    filterCombo_ = new QComboBox(this);
    filterCombo_->addItems({tr("All"), QStringLiteral("top"),
                            QStringLiteral("bridge"), QStringLiteral("fcc"),
                            QStringLiteral("hcp"), QStringLiteral("hollow")});
    filterRow->addWidget(filterCombo_, 1);
    layout->addLayout(filterRow);

    table_ = new QTableWidget(this);
    table_->setColumnCount(3);
    table_->setHorizontalHeaderLabels({tr("Site"), QStringLiteral("x (Å)"),
                                       QStringLiteral("y (Å)")});
    table_->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    table_->setSelectionMode(QAbstractItemView::ExtendedSelection);
    table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    layout->addWidget(table_, 1);

    auto* form = new QFormLayout;
    layout->addLayout(form);
    adsorbateCombo_ = new QComboBox(this);
    adsorbateCombo_->setEditable(true);
    adsorbateCombo_->addItems({QStringLiteral("OH"), QStringLiteral("O"),
                               QStringLiteral("H"), QStringLiteral("CO"),
                               QStringLiteral("CHO"), QStringLiteral("H2O"),
                               QStringLiteral("N2"), QStringLiteral("O2"),
                               QStringLiteral("NH3"), QStringLiteral("CH4")});
    adsorbateCombo_->setToolTip(tr("ase.build.molecule name or a chemical "
                                   "formula for custom species"));
    form->addRow(tr("Adsorbate:"), adsorbateCombo_);
    heightSpin_ = new QDoubleSpinBox(this);
    heightSpin_->setRange(0.5, 10.0);
    heightSpin_->setValue(2.0);
    heightSpin_->setSuffix(QStringLiteral(" Å"));
    heightSpin_->setToolTip(tr("Anchor-atom height above the surface layer"));
    form->addRow(tr("Height:"), heightSpin_);

    auto* placeButton = new QPushButton(tr("Place on Selected Sites"), this);
    form->addRow(placeButton);
    connect(placeButton, &QPushButton::clicked,
            this, &AdsorptionDialog::placeOnSelection);

    // --- Coverage series ---------------------------------------------------
    auto* coverageGroup = new QGroupBox(tr("Coverage series"), this);
    auto* coverageLayout = new QVBoxLayout(coverageGroup);
    auto* coverageForm = new QFormLayout;
    coverageSiteCombo_ = new QComboBox(coverageGroup);
    coverageSiteCombo_->addItems({QStringLiteral("top"), QStringLiteral("fcc"),
                                  QStringLiteral("hcp"),
                                  QStringLiteral("bridge"),
                                  QStringLiteral("hollow")});
    coverageForm->addRow(tr("Site family:"), coverageSiteCombo_);
    coverageLayout->addLayout(coverageForm);
    auto* checksRow = new QHBoxLayout;
    const char* coverageNames[4] = {"0.25 ML", "0.50 ML", "0.75 ML", "1.00 ML"};
    for (int i = 0; i < 4; ++i) {
        coverageChecks_[i] = new QCheckBox(QLatin1String(coverageNames[i]),
                                           coverageGroup);
        coverageChecks_[i]->setChecked(true);
        checksRow->addWidget(coverageChecks_[i]);
    }
    coverageLayout->addLayout(checksRow);
    auto* seriesButton = new QPushButton(tr("Generate Coverage Series"),
                                         coverageGroup);
    coverageLayout->addWidget(seriesButton);
    connect(seriesButton, &QPushButton::clicked,
            this, &AdsorptionDialog::generateCoverageSeries);
    layout->addWidget(coverageGroup);

    statusLabel_ = new QLabel(this);
    statusLabel_->setWordWrap(true);
    layout->addWidget(statusLabel_);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);

    connect(filterCombo_, &QComboBox::currentIndexChanged, this,
            [this] { refreshTable(); });

    // Detect immediately — the dialog is only opened on slab-like input.
    QApplication::setOverrideCursor(Qt::WaitCursor);
    try {
        sites_ = pybridge::SurfaceScience::detectSites(*slab_);
        QApplication::restoreOverrideCursor();
    } catch (const std::exception& e) {
        QApplication::restoreOverrideCursor();
        summaryLabel_->setText(QString::fromUtf8(e.what()));
    }

    std::map<std::string, int> counts;
    for (const auto& site : sites_)
        ++counts[site.type];
    QStringList parts;
    for (const auto& [type, count] : counts)
        parts << QStringLiteral("%1 %2").arg(count).arg(
            QString::fromStdString(type));
    if (!sites_.empty())
        summaryLabel_->setText(tr("Detected %n adsorption site(s): ", nullptr,
                                  static_cast<int>(sites_.size()))
                               + parts.join(QStringLiteral(", ")));
    refreshTable();
}

void AdsorptionDialog::refreshTable()
{
    const std::string filter = filterCombo_->currentIndex() == 0
        ? std::string()
        : filterCombo_->currentText().toStdString();
    table_->setRowCount(0);
    for (const auto& site : sites_) {
        if (!filter.empty() && site.type != filter)
            continue;
        const int row = table_->rowCount();
        table_->insertRow(row);
        table_->setItem(row, 0,
                        new QTableWidgetItem(QString::fromStdString(site.type)));
        table_->setItem(row, 1,
                        new QTableWidgetItem(QString::number(site.x, 'f', 3)));
        table_->setItem(row, 2,
                        new QTableWidgetItem(QString::number(site.y, 'f', 3)));
    }
}

std::vector<pybridge::SurfaceScience::AdsorptionSite>
AdsorptionDialog::sitesOfType(const std::string& type) const
{
    std::vector<pybridge::SurfaceScience::AdsorptionSite> subset;
    for (const auto& site : sites_)
        if (site.type == type)
            subset.push_back(site);
    return subset;
}

QString AdsorptionDialog::adsorbateName() const
{
    return adsorbateCombo_->currentText().trimmed();
}

void AdsorptionDialog::placeOnSelection()
{
    // Map selected (filtered) rows back to site entries.
    const std::string filter = filterCombo_->currentIndex() == 0
        ? std::string()
        : filterCombo_->currentText().toStdString();
    std::vector<pybridge::SurfaceScience::AdsorptionSite> visible;
    for (const auto& site : sites_)
        if (filter.empty() || site.type == filter)
            visible.push_back(site);

    std::vector<pybridge::SurfaceScience::AdsorptionSite> chosen;
    const auto ranges = table_->selectionModel()->selectedRows();
    for (const auto& index : ranges)
        if (index.row() >= 0
            && static_cast<std::size_t>(index.row()) < visible.size())
            chosen.push_back(visible[static_cast<std::size_t>(index.row())]);
    if (chosen.empty()) {
        statusLabel_->setText(tr("Select one or more sites in the table first."));
        return;
    }

    QApplication::setOverrideCursor(Qt::WaitCursor);
    try {
        auto structure = std::make_shared<core::Structure>(
            pybridge::SurfaceScience::placeAdsorbates(
                *slab_, chosen, adsorbateName().toStdString(),
                heightSpin_->value()));
        QApplication::restoreOverrideCursor();
        outputs_.push_back({tr("%1 on %n site(s)", nullptr,
                               static_cast<int>(chosen.size()))
                                .arg(adsorbateName()),
                            std::move(structure)});
        accept();
    } catch (const std::exception& e) {
        QApplication::restoreOverrideCursor();
        statusLabel_->setText(QString::fromUtf8(e.what()));
    }
}

void AdsorptionDialog::generateCoverageSeries()
{
    const std::string family = coverageSiteCombo_->currentText().toStdString();
    const auto pool = sitesOfType(family);
    if (pool.empty()) {
        statusLabel_->setText(tr("No %1 sites detected on this slab.")
                                  .arg(coverageSiteCombo_->currentText()));
        return;
    }

    static constexpr double kCoverages[4] = {0.25, 0.50, 0.75, 1.00};
    QApplication::setOverrideCursor(Qt::WaitCursor);
    try {
        for (int i = 0; i < 4; ++i) {
            if (!coverageChecks_[i]->isChecked())
                continue;
            const auto want = std::max<std::size_t>(
                1, static_cast<std::size_t>(
                       std::llround(kCoverages[i] * pool.size())));
            // Evenly strided subset spreads the adsorbates over the cell.
            std::vector<pybridge::SurfaceScience::AdsorptionSite> subset;
            for (std::size_t k = 0; k < want; ++k)
                subset.push_back(pool[k * pool.size() / want]);
            auto structure = std::make_shared<core::Structure>(
                pybridge::SurfaceScience::placeAdsorbates(
                    *slab_, subset, adsorbateName().toStdString(),
                    heightSpin_->value()));
            outputs_.push_back(
                {tr("%1/%2 %3 ML").arg(adsorbateName(),
                                       QString::fromStdString(family))
                     .arg(kCoverages[i], 0, 'f', 2),
                 std::move(structure)});
        }
        QApplication::restoreOverrideCursor();
        if (!outputs_.empty())
            accept();
        else
            statusLabel_->setText(tr("Check at least one coverage."));
    } catch (const std::exception& e) {
        QApplication::restoreOverrideCursor();
        statusLabel_->setText(QString::fromUtf8(e.what()));
    }
}

} // namespace calango::gui
