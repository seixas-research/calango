#include "gui/CoordinationDialog.hpp"

#include "gui/ViewportWidget.hpp"

#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QVBoxLayout>
#include <QtConcurrent>

#include <algorithm>
#include <numeric>

namespace calango::gui {

CoordinationDialog::CoordinationDialog(std::shared_ptr<const core::Structure> structure,
                                       ViewportWidget* viewport, QWidget* parent)
    : QDialog(parent)
    , structure_(std::move(structure))
    , viewport_(viewport)
    , cutoffModeCombo_(new QComboBox(this))
    , toleranceSpin_(new QDoubleSpinBox(this))
    , cutoffSpin_(new QDoubleSpinBox(this))
    , bulkCnSpin_(new QDoubleSpinBox(this))
    , computeButton_(new QPushButton(tr("Compute"), this))
    , colorCnButton_(new QPushButton(tr("Color Viewport by CN"), this))
    , colorGcnButton_(new QPushButton(tr("Color Viewport by GCN"), this))
    , summaryLabel_(new QLabel(this))
    , table_(new QTableWidget(this))
{
    setWindowTitle(tr("Coordination Numbers (CN / GCN)"));
    resize(640, 560);

    cutoffModeCombo_->addItems({tr("Covalent radii × tolerance"),
                                tr("Fixed cutoff radius")});

    toleranceSpin_->setRange(0.5, 2.5);
    toleranceSpin_->setDecimals(2);
    toleranceSpin_->setSingleStep(0.05);
    toleranceSpin_->setValue(1.15);

    cutoffSpin_->setRange(0.5, 12.0);
    cutoffSpin_->setDecimals(2);
    cutoffSpin_->setSingleStep(0.1);
    cutoffSpin_->setValue(3.0);
    cutoffSpin_->setSuffix(tr(" Å"));
    cutoffSpin_->setEnabled(false);

    bulkCnSpin_->setRange(0.0, 24.0);
    bulkCnSpin_->setDecimals(0);
    bulkCnSpin_->setSpecialValueText(tr("Auto (max CN found)"));
    bulkCnSpin_->setToolTip(tr("cn_max normalizing the GCN: 12 for fcc/hcp,\n"
                               "8 for bcc, 4 for diamond. Auto uses the largest\n"
                               "CN present in the structure."));

    connect(cutoffModeCombo_, &QComboBox::currentIndexChanged, this, [this](int index) {
        toleranceSpin_->setEnabled(index == 0);
        cutoffSpin_->setEnabled(index == 1);
    });

    auto* form = new QFormLayout;
    form->addRow(tr("Neighbor cutoff:"), cutoffModeCombo_);
    form->addRow(tr("Tolerance:"), toleranceSpin_);
    form->addRow(tr("Cutoff radius:"), cutoffSpin_);
    form->addRow(tr("Bulk CN reference:"), bulkCnSpin_);
    form->addRow(computeButton_);

    table_->setColumnCount(4);
    table_->setHorizontalHeaderLabels({tr("Atom"), tr("Element"), tr("CN"), tr("GCN")});
    table_->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    table_->verticalHeader()->setVisible(false);
    table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table_->setSelectionBehavior(QAbstractItemView::SelectRows);

    summaryLabel_->setWordWrap(true);

    auto* colorRow = new QHBoxLayout;
    colorRow->addWidget(colorCnButton_);
    colorRow->addWidget(colorGcnButton_);
    colorRow->addStretch(1);
    connect(colorCnButton_, &QPushButton::clicked, this, [this] { colorViewport(false); });
    connect(colorGcnButton_, &QPushButton::clicked, this, [this] { colorViewport(true); });

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    auto* layout = new QVBoxLayout(this);
    layout->addLayout(form);
    layout->addWidget(table_, 1);
    layout->addWidget(summaryLabel_);
    layout->addLayout(colorRow);
    layout->addWidget(buttons);

    connect(computeButton_, &QPushButton::clicked, this, &CoordinationDialog::compute);
    connect(&watcher_, &QFutureWatcher<core::CoordinationResult>::finished,
            this, &CoordinationDialog::computeFinished);

    compute(); // show something immediately
}

core::CoordinationOptions CoordinationDialog::currentOptions() const
{
    core::CoordinationOptions options;
    options.cutoffMode = cutoffModeCombo_->currentIndex() == 0
        ? core::CoordinationOptions::CutoffMode::CovalentScaled
        : core::CoordinationOptions::CutoffMode::Fixed;
    options.tolerance = toleranceSpin_->value();
    options.fixedCutoff = cutoffSpin_->value();
    options.bulkCoordination = bulkCnSpin_->value();
    return options;
}

void CoordinationDialog::compute()
{
    computeButton_->setEnabled(false);
    computeButton_->setText(tr("Computing…"));

    // Copy the structure so the worker thread owns its data outright.
    watcher_.setFuture(QtConcurrent::run(
        [structure = *structure_, options = currentOptions()] {
            return core::computeCoordination(structure, options);
        }));
}

void CoordinationDialog::computeFinished()
{
    const core::CoordinationResult result = watcher_.result();
    const auto& atoms = structure_->atoms();
    const auto n = std::min(atoms.size(), result.cn.size());

    table_->setRowCount(static_cast<int>(n));
    for (std::size_t i = 0; i < n; ++i) {
        const auto row = static_cast<int>(i);
        table_->setItem(row, 0, new QTableWidgetItem(QString::number(i + 1)));
        table_->setItem(row, 1, new QTableWidgetItem(QLatin1String(atoms[i].symbol())));
        table_->setItem(row, 2, new QTableWidgetItem(QString::number(result.cn[i])));
        table_->setItem(row, 3,
                        new QTableWidgetItem(QString::number(result.gcn[i], 'f', 3)));
    }

    if (n > 0) {
        const double meanCn =
            std::accumulate(result.cn.begin(), result.cn.end(), 0.0)
            / static_cast<double>(result.cn.size());
        const double meanGcn =
            std::accumulate(result.gcn.begin(), result.gcn.end(), 0.0)
            / static_cast<double>(result.gcn.size());
        const auto [cnLo, cnHi] = std::minmax_element(result.cn.begin(), result.cn.end());
        const auto [gcnLo, gcnHi] =
            std::minmax_element(result.gcn.begin(), result.gcn.end());
        summaryLabel_->setText(
            tr("cn_max = %1 · CN: %2–%3 (mean %4) · GCN: %5–%6 (mean %7)")
                .arg(result.bulkCoordinationUsed)
                .arg(*cnLo)
                .arg(*cnHi)
                .arg(meanCn, 0, 'f', 2)
                .arg(*gcnLo, 0, 'f', 3)
                .arg(*gcnHi, 0, 'f', 3)
                .arg(meanGcn, 0, 'f', 3));
    } else {
        summaryLabel_->clear();
    }

    computeButton_->setEnabled(true);
    computeButton_->setText(tr("Compute"));
}

void CoordinationDialog::colorViewport(bool gcn)
{
    if (!viewport_)
        return;
    viewport_->setCoordinationOptions(currentOptions());
    viewport_->setColorMode(gcn ? render::ColorMode::GeneralizedCoordination
                                : render::ColorMode::CoordinationNumber);
}

} // namespace calango::gui
