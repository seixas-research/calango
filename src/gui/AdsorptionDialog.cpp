#include "gui/AdsorptionDialog.hpp"

#include <QApplication>
#include <QSlider>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QRadioButton>
#include <QTabWidget>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QPushButton>
#include <QTableWidget>
#include <QVBoxLayout>

#include <cmath>
#include <limits>
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
    auto* tabs = new QTabWidget(this);
    layout->addWidget(tabs, 1);

    // ===== Tab 1 — Site Identification & Geometry Generation ==============
    auto* sitesTab = new QWidget(tabs);
    auto* sitesLayout = new QVBoxLayout(sitesTab);
    tabs->addTab(sitesTab, tr("Site Identification && Geometry Generation"));

    summaryLabel_ = new QLabel(this);
    summaryLabel_->setWordWrap(true);
    sitesLayout->addWidget(summaryLabel_);

    auto* filterRow = new QHBoxLayout;
    filterRow->addWidget(new QLabel(tr("Show sites:"), this));
    filterCombo_ = new QComboBox(this);
    filterCombo_->addItems({tr("All"), QStringLiteral("top"),
                            QStringLiteral("bridge"), QStringLiteral("fcc"),
                            QStringLiteral("hcp"), QStringLiteral("hollow")});
    filterRow->addWidget(filterCombo_, 1);
    sitesLayout->addLayout(filterRow);

    table_ = new QTableWidget(this);
    table_->setColumnCount(3);
    table_->setHorizontalHeaderLabels({tr("Site"), QStringLiteral("x (Å)"),
                                       QStringLiteral("y (Å)")});
    table_->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    table_->setSelectionMode(QAbstractItemView::ExtendedSelection);
    table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    sitesLayout->addWidget(table_, 1);

    auto* form = new QFormLayout;
    sitesLayout->addLayout(form);
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

    // How the generated geometries are surfaced. One tab per site is right
    // for comparing a handful of sites side by side; a single trajectory is
    // right for scrubbing through many, and keeps the tab bar usable.
    auto* outputGroup = new QGroupBox(tr("Output"), sitesTab);
    auto* outputLayout = new QVBoxLayout(outputGroup);
    individualTabsRadio_ =
        new QRadioButton(tr("Individual workspace tabs"), outputGroup);
    trajectoryRadio_ =
        new QRadioButton(tr("Single trajectory tab"), outputGroup);
    individualTabsRadio_->setChecked(true);
    individualTabsRadio_->setToolTip(
        tr("Each generated geometry opens in its own tab — best for comparing "
           "a few sites directly."));
    trajectoryRadio_->setToolTip(
        tr("All generated geometries become frames of one trajectory tab, "
           "scrubbable from the timeline — best for many sites at once."));
    outputLayout->addWidget(individualTabsRadio_);
    outputLayout->addWidget(trajectoryRadio_);
    sitesLayout->addWidget(outputGroup);

    auto* placeButton = new QPushButton(tr("Place on Selected Sites"), this);
    form->addRow(placeButton);
    connect(placeButton, &QPushButton::clicked,
            this, &AdsorptionDialog::placeOnSelection);

    // ===== Tab 2 — Coverage ================================================
    auto* coverageTab = new QWidget(tabs);
    auto* coverageTabLayout = new QVBoxLayout(coverageTab);
    tabs->addTab(coverageTab, tr("Coverage"));

    auto* coverageGroup = new QGroupBox(tr("Coverage series"), coverageTab);
    auto* coverageLayout = new QVBoxLayout(coverageGroup);
    auto* coverageForm = new QFormLayout;
    coverageSiteCombo_ = new QComboBox(coverageGroup);
    coverageSiteCombo_->addItems({QStringLiteral("top"), QStringLiteral("fcc"),
                                  QStringLiteral("hcp"),
                                  QStringLiteral("bridge"),
                                  QStringLiteral("hollow")});
    coverageForm->addRow(tr("Site family:"), coverageSiteCombo_);

    // Continuous coverage: a spin box and slider kept in sync, so the user can
    // dial in (or type) any fractional monolayer coverage from 0 to 1 ML.
    coverageSpin_ = new QDoubleSpinBox(coverageGroup);
    coverageSpin_->setRange(0.0, 1.0);
    coverageSpin_->setDecimals(2);
    coverageSpin_->setSingleStep(0.05);
    coverageSpin_->setValue(0.25);
    coverageSpin_->setSuffix(tr(" ML"));
    coverageSlider_ = new QSlider(Qt::Horizontal, coverageGroup);
    coverageSlider_->setRange(0, 100); // hundredths of a monolayer
    coverageSlider_->setValue(25);
    auto* coverageRow = new QHBoxLayout;
    coverageRow->addWidget(coverageSlider_, 1);
    coverageRow->addWidget(coverageSpin_);
    coverageForm->addRow(tr("Coverage:"), coverageRow);
    coverageLayout->addLayout(coverageForm);

    // Two-way binding (guarded against feedback loops by Qt's no-op-on-equal
    // value semantics; the slider is integer hundredths of the spin value).
    connect(coverageSlider_, &QSlider::valueChanged, this, [this](int v) {
        coverageSpin_->setValue(v / 100.0);
    });
    connect(coverageSpin_, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, [this](double v) {
                coverageSlider_->setValue(static_cast<int>(std::lround(v * 100.0)));
            });

    // Steric floor. Two adsorbates on neighbouring hollow sites can sit ~1.5 Å
    // apart, which is inside a bond length — a configuration no relaxation
    // recovers from. The placement below honours this and reports when the
    // requested coverage cannot be reached without violating it.
    minSeparationSpin_ = new QDoubleSpinBox(coverageGroup);
    minSeparationSpin_->setRange(0.0, 20.0);
    minSeparationSpin_->setDecimals(2);
    minSeparationSpin_->setSingleStep(0.25);
    minSeparationSpin_->setValue(3.0);
    minSeparationSpin_->setSuffix(QStringLiteral(" Å"));
    minSeparationSpin_->setToolTip(
        tr("Minimum centre-to-centre distance between occupied sites, "
           "evaluated under the minimum-image convention so neighbours across "
           "the periodic boundary count.\n"
           "0 disables the constraint. If the target coverage cannot be met at "
           "this separation, the run places as many as it can and says so — "
           "the two are often genuinely incompatible on a small cell."));
    coverageForm->addRow(tr("Minimum separation:"), minSeparationSpin_);

    auto* seriesButton = new QPushButton(tr("Generate Coverage"), coverageGroup);
    coverageLayout->addWidget(seriesButton);
    connect(seriesButton, &QPushButton::clicked,
            this, &AdsorptionDialog::generateCoverageSeries);
    coverageTabLayout->addWidget(coverageGroup);
    coverageTabLayout->addStretch(1);

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
    outputMode_ = trajectoryRadio_ && trajectoryRadio_->isChecked()
        ? OutputMode::SingleTrajectory
        : OutputMode::IndividualTabs;
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

std::vector<pybridge::SurfaceScience::AdsorptionSite>
AdsorptionDialog::spreadSites(
    const std::vector<pybridge::SurfaceScience::AdsorptionSite>& pool,
    std::size_t want, double minSeparation) const
{
    std::vector<pybridge::SurfaceScience::AdsorptionSite> chosen;
    if (pool.empty() || want == 0)
        return chosen;

    // Minimum-image separation in the slab plane. Two sites on opposite edges
    // of the cell are periodic neighbours, so a plain Cartesian distance would
    // call them far apart and let the placement pack them together.
    const auto& cell = slab_->cell().vectors();
    const double lx = cell[0].x;
    const double ly = cell[1].y;
    const double shear = cell[1].x;
    const auto separation = [&](const auto& a, const auto& b) {
        double dx = a.x - b.x;
        double dy = a.y - b.y;
        if (ly > 1e-9) {
            const double nj = std::round(dy / ly);
            dy -= nj * ly;
            dx -= nj * shear;
        }
        if (lx > 1e-9)
            dx -= std::round(dx / lx) * lx;
        return std::sqrt(dx * dx + dy * dy);
    };

    // Farthest-point selection: repeatedly take the candidate whose closest
    // approach to the already-chosen set is largest. That spreads the
    // adsorbates over the cell rather than clustering them, and makes the
    // minimum-separation test a cheap accept/reject on top.
    //
    // Seeded from the strided start so the result is deterministic for a
    // given pool ordering rather than depending on which site happened to be
    // detected first.
    chosen.push_back(pool.front());
    while (chosen.size() < want) {
        double bestDistance = -1.0;
        std::size_t best = pool.size();
        for (std::size_t i = 0; i < pool.size(); ++i) {
            double closest = std::numeric_limits<double>::infinity();
            bool already = false;
            for (const auto& taken : chosen) {
                const double d = separation(pool[i], taken);
                if (d < 1e-6) {
                    already = true;
                    break;
                }
                closest = std::min(closest, d);
            }
            if (already)
                continue;
            if (closest > bestDistance) {
                bestDistance = closest;
                best = i;
            }
        }
        // Out of candidates, or the best remaining one violates the floor.
        if (best == pool.size()
            || (minSeparation > 0.0 && bestDistance < minSeparation))
            break;
        chosen.push_back(pool[best]);
    }
    return chosen;
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

    const double coverage = coverageSpin_->value();
    if (coverage <= 0.0) {
        statusLabel_->setText(tr("Set a coverage greater than 0 ML."));
        return;
    }
    QApplication::setOverrideCursor(Qt::WaitCursor);
    try {
        const auto want = std::max<std::size_t>(
            1, static_cast<std::size_t>(std::llround(coverage * pool.size())));
        const double minSeparation = minSeparationSpin_->value();
        const auto subset = spreadSites(pool, want, minSeparation);
        if (subset.empty()) {
            QApplication::restoreOverrideCursor();
            statusLabel_->setText(
                tr("No sites could be placed at a %1 Å minimum separation.")
                    .arg(minSeparation, 0, 'f', 2));
            return;
        }
        if (subset.size() < want) {
            // Reported, not silently accepted: the structure carries a lower
            // coverage than the one asked for, and a paper that quotes the
            // requested figure would be quoting a number that is not in the
            // file.
            statusLabel_->setText(
                tr("Placed %1 of %2 adsorbates — a %3 Å minimum separation "
                   "cannot be maintained at %4 ML on this cell. The generated "
                   "structure is %5 ML.")
                    .arg(subset.size())
                    .arg(want)
                    .arg(minSeparation, 0, 'f', 2)
                    .arg(coverage, 0, 'f', 2)
                    .arg(static_cast<double>(subset.size()) / pool.size(),
                         0, 'f', 2));
        }
        auto structure = std::make_shared<core::Structure>(
            pybridge::SurfaceScience::placeAdsorbates(
                *slab_, subset, adsorbateName().toStdString(),
                heightSpin_->value()));
        outputs_.push_back(
            {tr("%1/%2 %3 ML").arg(adsorbateName(),
                                   QString::fromStdString(family))
                 .arg(static_cast<double>(subset.size()) / pool.size(),
                      0, 'f', 2),
             std::move(structure)});
        QApplication::restoreOverrideCursor();
        if (!outputs_.empty())
            accept();
    } catch (const std::exception& e) {
        QApplication::restoreOverrideCursor();
        statusLabel_->setText(QString::fromUtf8(e.what()));
    }
}

} // namespace calango::gui
