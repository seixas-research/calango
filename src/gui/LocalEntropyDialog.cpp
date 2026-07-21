#include "gui/LocalEntropyDialog.hpp"

#include "core/LocalEntropy.hpp"
#include "gui/LinePlotWidget.hpp"
#include "gui/ViewportWidget.hpp"

#include <QApplication>
#include <QCheckBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QLabel>
#include <QPushButton>
#include <QSpinBox>
#include <QVBoxLayout>

#include <algorithm>

namespace calango::gui {

LocalEntropyDialog::LocalEntropyDialog(std::shared_ptr<core::Structure> structure,
                                       ViewportWidget* viewport, QWidget* parent)
    : QDialog(parent)
    , structure_(std::move(structure))
    , viewport_(viewport)
{
    setWindowTitle(tr("Local Entropy Analysis"));
    resize(480, 420);

    auto* layout = new QVBoxLayout(this);
    auto* form = new QFormLayout;
    layout->addLayout(form);

    cutoffSpin_ = new QDoubleSpinBox(this);
    cutoffSpin_->setRange(2.0, 15.0);
    cutoffSpin_->setValue(5.0);
    cutoffSpin_->setSuffix(QStringLiteral(" Å"));
    cutoffSpin_->setToolTip(tr("Integration cutoff of the local g(r) — "
                               "3-4 coordination shells work well"));
    form->addRow(tr("Cutoff:"), cutoffSpin_);

    sigmaSpin_ = new QDoubleSpinBox(this);
    sigmaSpin_->setRange(0.01, 1.0);
    sigmaSpin_->setSingleStep(0.05);
    sigmaSpin_->setValue(0.15);
    sigmaSpin_->setSuffix(QStringLiteral(" Å"));
    sigmaSpin_->setToolTip(tr("Gaussian broadening of the neighbor peaks"));
    form->addRow(tr("Broadening σ:"), sigmaSpin_);

    gridSpin_ = new QSpinBox(this);
    gridSpin_->setRange(20, 1000);
    gridSpin_->setValue(100);
    form->addRow(tr("Radial grid points:"), gridSpin_);

    averageCheck_ = new QCheckBox(tr("Average over neighbors"), this);
    averageCheck_->setToolTip(tr("Smooth s_i over each atom's neighborhood — "
                                 "sharpens the ordered/disordered contrast"));
    form->addRow(averageCheck_);

    summaryLabel_ = new QLabel(tr("Compute to color atoms by s_S "
                                  "(stored as \"local_entropy\")."),
                               this);
    summaryLabel_->setWordWrap(true);
    layout->addWidget(summaryLabel_);

    histogram_ = new LinePlotWidget(this);
    histogram_->setAxisLabels(tr("s_S (k_B / atom)"), tr("atom count"));
    layout->addWidget(histogram_, 1);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    auto* computeButton =
        buttons->addButton(tr("Compute"), QDialogButtonBox::ActionRole);
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(computeButton, &QPushButton::clicked,
            this, &LocalEntropyDialog::compute);
}

void LocalEntropyDialog::compute()
{
    core::LocalEntropyOptions options;
    options.cutoff = cutoffSpin_->value();
    options.sigma = sigmaSpin_->value();
    options.gridPoints = gridSpin_->value();
    options.averageOverNeighbors = averageCheck_->isChecked();

    QApplication::setOverrideCursor(Qt::WaitCursor);
    const std::vector<double> entropy =
        core::computeLocalEntropy(*structure_, options);
    QApplication::restoreOverrideCursor();
    if (entropy.empty())
        return;

    structure_->setScalarField("local_entropy", entropy);
    Q_EMIT fieldStored();
    // Map the fresh field onto the atoms right away.
    viewport_->setColorMode(render::ColorMode::CustomScalar,
                            QStringLiteral("local_entropy"));

    const auto [minIt, maxIt] =
        std::minmax_element(entropy.begin(), entropy.end());
    double mean = 0.0;
    for (const double value : entropy)
        mean += value / static_cast<double>(entropy.size());
    summaryLabel_->setText(
        tr("s_S: min %1, mean %2, max %3 k_B — atoms colored by "
           "\"local_entropy\" (lower = more ordered/crystalline, "
           "higher = more disordered).")
            .arg(*minIt, 0, 'f', 3)
            .arg(mean, 0, 'f', 3)
            .arg(*maxIt, 0, 'f', 3));

    // Distribution histogram (40 bins across the value range).
    constexpr int kBins = 40;
    const double lo = *minIt;
    const double span = std::max(*maxIt - lo, 1e-9);
    std::vector<double> x(kBins), y(kBins, 0.0);
    for (int b = 0; b < kBins; ++b)
        x[static_cast<std::size_t>(b)] = lo + (b + 0.5) * span / kBins;
    for (const double value : entropy) {
        const auto bin = std::min<std::size_t>(
            kBins - 1,
            static_cast<std::size_t>((value - lo) / span * kBins));
        y[bin] += 1.0;
    }
    histogram_->setData(std::move(x), std::move(y));
}

} // namespace calango::gui
