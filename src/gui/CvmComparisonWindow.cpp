#include "gui/CvmComparisonWindow.hpp"

#include "gui/GuiUtils.hpp"
#include "gui/PlotPalette.hpp"

#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFile>
#include <QFileDialog>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QImage>
#include <QLabel>
#include <QMessageBox>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QSpinBox>
#include <QTableWidget>
#include <QTextStream>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>

namespace calango::gui {

namespace {

/// Ticks at 1, 2 or 5 times a power of ten — the spacings people read without
/// having to decode them.
double niceStep(double span, int target)
{
    if (span <= 0.0 || target <= 0)
        return 1.0;
    const double raw = span / target;
    const double magnitude = std::pow(10.0, std::floor(std::log10(raw)));
    const double normalized = raw / magnitude;
    double step = 10.0;
    if (normalized <= 1.0)
        step = 1.0;
    else if (normalized <= 2.0)
        step = 2.0;
    else if (normalized <= 5.0)
        step = 5.0;
    return step * magnitude;
}

} // namespace

// ---------------------------------------------------------------------------
// The canvas
// ---------------------------------------------------------------------------

CvmComparisonPlot::CvmComparisonPlot(QWidget* parent)
    : QWidget(parent)
{
    setMinimumSize(420, 300);
}

void CvmComparisonPlot::setCurves(std::vector<CvmCurve> curves,
                                  const QString& xLabel, const QString& yLabel)
{
    curves_ = std::move(curves);
    xLabel_ = xLabel;
    yLabel_ = yLabel;
    update();
}

void CvmComparisonPlot::setShadeGap(bool shade)
{
    shadeGap_ = shade;
    update();
}

void CvmComparisonPlot::setTransitionTemperature(double kelvin,
                                                 const QString& label)
{
    transitionK_ = kelvin;
    transitionLabel_ = label;
    update();
}

void CvmComparisonPlot::paintEvent(QPaintEvent*)
{
    QPainter painter(this);
    render(painter, QRectF(rect()));
}

void CvmComparisonPlot::render(QPainter& painter, const QRectF& bounds) const
{
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.fillRect(bounds, PlotPalette::canvas);

    // Margins scale with the figure so an exported PNG at 3x is not a
    // 3x-magnified plot with 1x text.
    const double scale = bounds.width() / 720.0;
    const QRectF plotArea = bounds.adjusted(78.0 * scale, 22.0 * scale,
                                            -22.0 * scale, -52.0 * scale);
    if (plotArea.width() <= 10.0 || plotArea.height() <= 10.0)
        return;

    QFont font = painter.font();
    font.setPointSizeF(std::max(6.0, 9.0 * scale));
    painter.setFont(font);

    if (curves_.empty()) {
        painter.setPen(PlotPalette::placeholder);
        painter.drawText(plotArea, Qt::AlignCenter,
                         tr("Set a composition and an interaction, then "
                            "Compute."));
        return;
    }

    // -- Ranges -------------------------------------------------------------
    double xMin = 0.0;
    double xMax = 0.0;
    double yMin = 0.0;
    double yMax = 0.0;
    bool first = true;
    for (const CvmCurve& curve : curves_) {
        for (std::size_t i = 0; i < curve.xs.size(); ++i) {
            if (first) {
                xMin = xMax = curve.xs[i];
                yMin = yMax = curve.ys[i];
                first = false;
            }
            xMin = std::min(xMin, curve.xs[i]);
            xMax = std::max(xMax, curve.xs[i]);
            yMin = std::min(yMin, curve.ys[i]);
            yMax = std::max(yMax, curve.ys[i]);
        }
        if (curve.horizontal) {
            if (first) {
                yMin = yMax = curve.constantValue;
                first = false;
            }
            yMin = std::min(yMin, curve.constantValue);
            yMax = std::max(yMax, curve.constantValue);
        }
    }
    if (first)
        return;
    // Entropy is bounded below by zero and the ideal value is the ceiling, so
    // anchoring the axis at zero keeps the SIZE of the correction honest — an
    // auto-scaled axis makes a 2% deviation look like a collapse.
    yMin = 0.0;
    yMax *= 1.08;
    if (xMax - xMin < 1e-12)
        xMax = xMin + 1.0;
    if (yMax - yMin < 1e-12)
        yMax = yMin + 1.0;

    const auto toScreen = [&](double x, double y) {
        return QPointF(plotArea.left()
                           + (x - xMin) / (xMax - xMin) * plotArea.width(),
                       plotArea.bottom()
                           - (y - yMin) / (yMax - yMin) * plotArea.height());
    };

    // -- Grid and ticks ------------------------------------------------------
    const double xStep = niceStep(xMax - xMin, 6);
    const double yStep = niceStep(yMax - yMin, 5);
    painter.setPen(QPen(PlotPalette::grid, 1.0));
    for (double x = std::ceil(xMin / xStep) * xStep; x <= xMax; x += xStep)
        painter.drawLine(toScreen(x, yMin), toScreen(x, yMax));
    for (double y = std::ceil(yMin / yStep) * yStep; y <= yMax; y += yStep)
        painter.drawLine(toScreen(xMin, y), toScreen(xMax, y));

    painter.setPen(QPen(PlotPalette::tickText, 1.0));
    for (double x = std::ceil(xMin / xStep) * xStep; x <= xMax; x += xStep) {
        const QPointF at = toScreen(x, yMin);
        painter.drawText(QRectF(at.x() - 40.0 * scale, at.y() + 4.0 * scale,
                                80.0 * scale, 18.0 * scale),
                         Qt::AlignHCenter | Qt::AlignTop,
                         QString::number(x, 'g', 4));
    }
    for (double y = std::ceil(yMin / yStep) * yStep; y <= yMax; y += yStep) {
        const QPointF at = toScreen(xMin, y);
        painter.drawText(QRectF(plotArea.left() - 72.0 * scale,
                                at.y() - 9.0 * scale, 66.0 * scale,
                                18.0 * scale),
                         Qt::AlignRight | Qt::AlignVCenter,
                         QString::number(y, 'f', 2));
    }

    // -- The gap between the ideal bound and the approximations -------------
    //
    // Shaded because the gap IS the result: it is how much of the entropy the
    // ideal formula overcounts. Drawn beneath the curves so it never obscures
    // them.
    const CvmCurve* ideal = nullptr;
    for (const CvmCurve& curve : curves_)
        if (curve.horizontal)
            ideal = &curve;
    if (shadeGap_ && ideal) {
        for (const CvmCurve& curve : curves_) {
            if (curve.horizontal || curve.xs.size() < 2)
                continue;
            QPainterPath path;
            path.moveTo(toScreen(curve.xs.front(), ideal->constantValue));
            for (std::size_t i = 0; i < curve.xs.size(); ++i)
                path.lineTo(toScreen(curve.xs[i], curve.ys[i]));
            path.lineTo(toScreen(curve.xs.back(), ideal->constantValue));
            path.closeSubpath();
            QColor fill = curve.colour;
            fill.setAlpha(38);
            painter.fillPath(path, fill);
        }
    }

    // -- Axis frame ----------------------------------------------------------
    painter.setPen(QPen(PlotPalette::spine, 1.4));
    painter.drawRect(plotArea);

    // -- Curves --------------------------------------------------------------
    for (const CvmCurve& curve : curves_) {
        painter.setPen(QPen(curve.colour, curve.horizontal ? 1.8 : 2.2,
                            curve.horizontal ? Qt::DashLine : Qt::SolidLine));
        if (curve.horizontal) {
            painter.drawLine(toScreen(xMin, curve.constantValue),
                             toScreen(xMax, curve.constantValue));
            continue;
        }
        if (curve.xs.size() < 2)
            continue;
        QPainterPath path;
        path.moveTo(toScreen(curve.xs.front(), curve.ys.front()));
        for (std::size_t i = 1; i < curve.xs.size(); ++i)
            path.lineTo(toScreen(curve.xs[i], curve.ys[i]));
        painter.drawPath(path);
    }

    // -- The transition ------------------------------------------------------
    // Drawn as a rule rather than a point because T_c is a property of the
    // whole curve, and at a FIRST-ORDER transition the entropy is
    // discontinuous there — a marker on one branch would imply the other does
    // not exist.
    if (transitionK_ > xMin && transitionK_ < xMax) {
        painter.setPen(QPen(PlotPalette::highlight, 1.6, Qt::DashDotLine));
        painter.drawLine(toScreen(transitionK_, yMin),
                         toScreen(transitionK_, yMax));
        painter.setPen(PlotPalette::highlight);
        const QPointF at = toScreen(transitionK_, yMax);
        painter.drawText(QRectF(at.x() + 4.0 * scale, at.y() + 2.0 * scale,
                                220.0 * scale, 18.0 * scale),
                         Qt::AlignLeft | Qt::AlignTop, transitionLabel_);
    }

    // -- Legend --------------------------------------------------------------
    {
        double y = plotArea.top() + 8.0 * scale;
        const double x = plotArea.left() + 10.0 * scale;
        double widest = 0.0;
        const QFontMetricsF metrics(painter.font());
        for (const CvmCurve& curve : curves_)
            widest = std::max(widest, metrics.horizontalAdvance(curve.label));
        const QRectF box(x - 4.0 * scale, y - 4.0 * scale,
                         widest + 34.0 * scale,
                         curves_.size() * 17.0 * scale + 8.0 * scale);
        painter.fillRect(box, PlotPalette::readoutFill);
        painter.setPen(QPen(PlotPalette::grid, 1.0));
        painter.drawRect(box);
        for (const CvmCurve& curve : curves_) {
            painter.setPen(QPen(curve.colour, 2.2,
                                curve.horizontal ? Qt::DashLine
                                                 : Qt::SolidLine));
            painter.drawLine(QPointF(x, y + 8.0 * scale),
                             QPointF(x + 22.0 * scale, y + 8.0 * scale));
            painter.setPen(PlotPalette::text);
            painter.drawText(QPointF(x + 28.0 * scale, y + 12.0 * scale),
                             curve.label);
            y += 17.0 * scale;
        }
    }

    // -- Axis labels ---------------------------------------------------------
    painter.setPen(PlotPalette::text);
    painter.drawText(QRectF(plotArea.left(), bounds.bottom() - 28.0 * scale,
                            plotArea.width(), 22.0 * scale),
                     Qt::AlignHCenter | Qt::AlignVCenter, xLabel_);
    painter.save();
    painter.translate(bounds.left() + 16.0 * scale, plotArea.center().y());
    painter.rotate(-90.0);
    painter.drawText(QRectF(-plotArea.height() / 2.0, -10.0 * scale,
                            plotArea.height(), 20.0 * scale),
                     Qt::AlignCenter, yLabel_);
    painter.restore();
}

bool CvmComparisonPlot::exportImage(const QString& path, double scale) const
{
    const QSize size(static_cast<int>(width() * scale),
                     static_cast<int>(height() * scale));
    if (size.isEmpty())
        return false;
    QImage image(size, QImage::Format_ARGB32_Premultiplied);
    image.fill(PlotPalette::canvas);
    QPainter painter(&image);
    // Through the SAME render(), so the exported file is the figure on
    // screen rather than a second drawing path that can drift from it.
    render(painter, QRectF(QPointF(0, 0), QSizeF(size)));
    painter.end();
    return image.save(path);
}

// ---------------------------------------------------------------------------
// The window
// ---------------------------------------------------------------------------

CvmComparisonWindow::CvmComparisonWindow(QWidget* parent)
    : QWidget(parent)
{
    setWindowTitle(tr("CVM / Alloy Thermodynamics"));

    auto* layout = new QHBoxLayout(this);
    auto* panel = new QWidget(this);
    panel->setMaximumWidth(320);
    buildControls(panel);
    layout->addWidget(panel);

    plot_ = new CvmComparisonPlot(this);
    layout->addWidget(plot_, 1);

    recompute();
}

void CvmComparisonWindow::buildControls(QWidget* panel)
{
    auto* outer = new QVBoxLayout(panel);

    auto* systemGroup = new QGroupBox(tr("System"), panel);
    auto* systemForm = new QFormLayout(systemGroup);

    latticeCombo_ = new QComboBox(systemGroup);
    latticeCombo_->addItem(tr("FCC (z = 12)"),
                           static_cast<int>(core::CvmLattice::Fcc));
    latticeCombo_->addItem(tr("BCC (z = 8)"),
                           static_cast<int>(core::CvmLattice::Bcc));
    latticeCombo_->addItem(tr("1D chain (z = 2) — pair is exact"),
                           static_cast<int>(core::CvmLattice::Chain));
    systemForm->addRow(tr("Lattice:"), latticeCombo_);

    speciesSpin_ = new QSpinBox(systemGroup);
    speciesSpin_->setRange(2, 5);
    speciesSpin_->setValue(2);
    speciesSpin_->setToolTip(
        tr("Number of species on the substitutional sublattice. Five gives "
           "the ln 5 = 1.61 k_B ideal entropy quoted for equiatomic "
           "high-entropy alloys."));
    systemForm->addRow(tr("Species:"), speciesSpin_);

    compositionTable_ = new QTableWidget(2, 2, systemGroup);
    compositionTable_->setHorizontalHeaderLabels(
        {tr("Species"), tr("Fraction")});
    compositionTable_->horizontalHeader()->setStretchLastSection(true);
    compositionTable_->verticalHeader()->setVisible(false);
    compositionTable_->setMaximumHeight(160);
    // Without this a dead key (acute, tilde, circumflex — routine on a
    // pt_BR keyboard) recurses through the item view's type-to-edit handling
    // until the stack dies. Enforced by the dialog-construction test.
    disableTypeToEdit(compositionTable_);
    systemForm->addRow(compositionTable_);

    outer->addWidget(systemGroup);

    auto* interactionGroup = new QGroupBox(tr("Interaction"), panel);
    auto* interactionForm = new QFormLayout(interactionGroup);
    interactionSpin_ = new QDoubleSpinBox(interactionGroup);
    interactionSpin_->setRange(-0.5, 0.5);
    interactionSpin_->setDecimals(4);
    interactionSpin_->setSingleStep(0.005);
    interactionSpin_->setValue(-0.02);
    interactionSpin_->setSuffix(tr(" eV"));
    interactionSpin_->setToolTip(
        tr("Nearest-neighbour unlike-pair energy e_AB (with e_AA = e_BB = 0)."
           "\n\nNEGATIVE favours unlike neighbours and the alloy ORDERS; "
           "positive favours like neighbours and it clusters or phase "
           "separates. Zero returns the ideal entropy at every temperature, "
           "which is the check that the solver is honest."));
    interactionForm->addRow(tr("e_AB (unlike-pair energy):"), interactionSpin_);

    // The conversion, stated in the window rather than left in a header. The
    // question "which parameter do I enter?" has a specific answer and the
    // UI is where it belongs.
    auto* convention = new QLabel(
        tr("<small>From a fitted nearest-neighbour pair ECI J<sub>2</sub> "
           "(±1 correlation basis): <b>e_AB = −J<sub>2</sub></b>, with "
           "e_AA = e_BB = +J<sub>2</sub>. So J<sub>2</sub> &gt; 0 orders the "
           "alloy.<br>Modules → Alloys → <i>Effective Cluster Interactions</i> "
           "fits J<sub>2</sub> from a run and can fill this in for you.</small>"),
        interactionGroup);
    convention->setWordWrap(true);
    interactionForm->addRow(convention);

    provenanceLabel_ = new QLabel(interactionGroup);
    provenanceLabel_->setWordWrap(true);
    provenanceLabel_->setVisible(false);
    interactionForm->addRow(provenanceLabel_);
    outer->addWidget(interactionGroup);

    longRangeCheck_ = new QCheckBox(
        tr("Allow long-range order (4 sublattices)"), interactionGroup);
    longRangeCheck_->setToolTip(
        tr("Split the FCC lattice into four sublattices that may differ.\n\n"
           "Without this the solver is HOMOGENEOUS: it describes short-range "
           "order in a disordered solid solution and cannot produce an "
           "ordered phase or a transition temperature, because one sublattice "
           "has no symmetry to break.\n\n"
           "With it, the ordered and disordered branches are both solved and "
           "the one with the lower free energy wins — which is how a "
           "first-order transition must be located, since the order parameter "
           "jumps rather than decaying to zero."));
    interactionForm->addRow(longRangeCheck_);

    orderCombo_ = new QComboBox(interactionGroup);
    orderCombo_->addItem(tr("L1_2 (Cu3Au-type, x = 1/4)"),
                         static_cast<int>(core::SublatticeOrder::L12));
    orderCombo_->addItem(tr("L1_0 (CuAu-type, x = 1/2)"),
                         static_cast<int>(core::SublatticeOrder::L10));
    orderCombo_->setEnabled(false);
    interactionForm->addRow(tr("Ordered structure:"), orderCombo_);
    connect(longRangeCheck_, &QCheckBox::toggled, orderCombo_,
            &QWidget::setEnabled);

    // Snap to a setup that can actually order. L1_2 is stoichiometric at
    // x = 1/4 and L1_0 at x = 1/2, and asking for L1_2 at x = 1/2 correctly
    // reports "no transition" — which reads as a broken feature rather than
    // as a mismatched request. The low-temperature end is also raised: below
    // roughly 0.35 T_c the ordered trial converges onto an equal mixture of
    // symmetry-related domains, a boundary fixed point of the natural
    // iteration that is discarded, so a range starting at 100 K finds nothing.
    const auto snapToStoichiometry = [this] {
        if (!longRangeCheck_->isChecked() || speciesSpin_->value() != 2)
            return;
        const auto order = static_cast<core::SublatticeOrder>(
            orderCombo_->currentData().toInt());
        const double x = order == core::SublatticeOrder::L12 ? 0.25 : 0.5;
        if (auto* a = compositionTable_->item(0, 1))
            a->setText(QString::number(1.0 - x, 'f', 4));
        if (auto* b = compositionTable_->item(1, 1))
            b->setText(QString::number(x, 'f', 4));
        if (minTemperatureSpin_->value() < 300.0)
            minTemperatureSpin_->setValue(300.0);
        recompute();
    };
    connect(longRangeCheck_, &QCheckBox::toggled, this,
            [snapToStoichiometry](bool on) { if (on) snapToStoichiometry(); });
    connect(orderCombo_, &QComboBox::currentIndexChanged, this,
            [snapToStoichiometry](int) { snapToStoichiometry(); });

    auto* rangeGroup = new QGroupBox(tr("Range"), panel);
    auto* rangeForm = new QFormLayout(rangeGroup);
    axisCombo_ = new QComboBox(rangeGroup);
    axisCombo_->addItem(tr("Temperature"), 0);
    axisCombo_->addItem(tr("Composition (binary)"), 1);
    rangeForm->addRow(tr("Plot against:"), axisCombo_);

    minTemperatureSpin_ = new QDoubleSpinBox(rangeGroup);
    minTemperatureSpin_->setRange(1.0, 10000.0);
    minTemperatureSpin_->setValue(100.0);
    minTemperatureSpin_->setSuffix(tr(" K"));
    rangeForm->addRow(tr("T min:"), minTemperatureSpin_);

    maxTemperatureSpin_ = new QDoubleSpinBox(rangeGroup);
    maxTemperatureSpin_->setRange(1.0, 20000.0);
    maxTemperatureSpin_->setValue(2000.0);
    maxTemperatureSpin_->setSuffix(tr(" K"));
    rangeForm->addRow(tr("T max:"), maxTemperatureSpin_);

    stepsSpin_ = new QSpinBox(rangeGroup);
    stepsSpin_->setRange(5, 500);
    stepsSpin_->setValue(120);
    rangeForm->addRow(tr("Points:"), stepsSpin_);
    outer->addWidget(rangeGroup);

    auto* buttons = new QHBoxLayout();
    auto* compute = new QPushButton(tr("Compute"), panel);
    connect(compute, &QPushButton::clicked, this,
            &CvmComparisonWindow::recompute);
    buttons->addWidget(compute);
    auto* image = new QPushButton(tr("Export Image…"), panel);
    connect(image, &QPushButton::clicked, this,
            &CvmComparisonWindow::exportImage);
    buttons->addWidget(image);
    auto* data = new QPushButton(tr("Export Data…"), panel);
    connect(data, &QPushButton::clicked, this,
            &CvmComparisonWindow::exportData);
    buttons->addWidget(data);
    outer->addLayout(buttons);

    summaryLabel_ = new QLabel(panel);
    summaryLabel_->setWordWrap(true);
    summaryLabel_->setTextFormat(Qt::RichText);
    outer->addWidget(summaryLabel_);
    outer->addStretch(1);

    // Default binary.
    const char* defaults[] = {"A", "B", "C", "D", "E"};
    for (int row = 0; row < 2; ++row) {
        compositionTable_->setItem(row, 0,
                                   new QTableWidgetItem(defaults[row]));
        compositionTable_->setItem(row, 1, new QTableWidgetItem("0.5"));
    }

    connect(speciesSpin_, &QSpinBox::valueChanged, this, [this, defaults](int n) {
        compositionTable_->setRowCount(n);
        for (int row = 0; row < n; ++row) {
            if (!compositionTable_->item(row, 0))
                compositionTable_->setItem(
                    row, 0, new QTableWidgetItem(defaults[row]));
            if (!compositionTable_->item(row, 1))
                compositionTable_->setItem(
                    row, 1,
                    new QTableWidgetItem(QString::number(1.0 / n, 'f', 4)));
        }
    });
}

void CvmComparisonWindow::setPairEci(double eci)
{
    bool ok = false;
    const auto energies = core::pairEnergiesFromEci(eci, &ok);
    if (ok && energies.size() == 4)
        interactionSpin_->setValue(energies[1]);
    // Provenance, shown rather than implied. A number that arrived from a fit
    // and one typed in by hand carry very different confidence, and the
    // entropy curves look identical either way.
    provenanceLabel_->setText(
        tr("<small style='color:#1f77b4'>From a fitted ECI "
           "J<sub>2</sub> = %1 eV → e_AB = %2 eV.</small>")
            .arg(eci, 0, 'g', 4)
            .arg(interactionSpin_->value(), 0, 'g', 4));
    provenanceLabel_->setVisible(true);
    recompute();
}

core::CvmInput
CvmComparisonWindow::currentInput(core::CvmApproximation approximation) const
{
    core::CvmInput input;
    input.lattice =
        static_cast<core::CvmLattice>(latticeCombo_->currentData().toInt());
    input.approximation = approximation;

    const int species = speciesSpin_->value();
    for (int row = 0; row < species; ++row) {
        const auto* nameItem = compositionTable_->item(row, 0);
        const auto* valueItem = compositionTable_->item(row, 1);
        input.species.push_back(nameItem ? nameItem->text().toStdString()
                                         : std::string(1, 'A' + row));
        // QString::toDouble is locale-INDEPENDENT (it always reads '.'),
        // unlike std::stod which follows LC_NUMERIC and truncates at the
        // point on this machine's pt_BR locale. Deliberate choice, not an
        // accident of which API came to hand.
        input.composition.push_back(valueItem ? valueItem->text().toDouble()
                                              : 1.0 / species);
    }

    const double e = interactionSpin_->value();
    input.pairEnergiesEv.assign(
        static_cast<std::size_t>(species) * species, 0.0);
    for (int i = 0; i < species; ++i)
        for (int j = 0; j < species; ++j)
            if (i != j)
                input.pairEnergiesEv[static_cast<std::size_t>(i) * species + j] =
                    e;

    input.minTemperatureK = minTemperatureSpin_->value();
    input.maxTemperatureK = maxTemperatureSpin_->value();
    input.temperatureSteps = stepsSpin_->value();
    return input;
}

void CvmComparisonWindow::recompute()
{
    const bool againstComposition = axisCombo_->currentData().toInt() == 1;
    const int species = speciesSpin_->value();

    std::vector<CvmCurve> curves;
    QString summary;

    if (againstComposition && species != 2) {
        summaryLabel_->setText(
            tr("<b>Composition axis needs exactly two species.</b><br>"
               "A composition sweep has to vary something one-dimensional; "
               "with three or more species there is a simplex, not an axis."));
        plot_->setCurves({}, QString(), QString());
        return;
    }

    const auto solveAt = [&](core::CvmApproximation approximation) {
        return core::solveClusterVariation(currentInput(approximation));
    };

    // -- Long-range order: the four-sublattice solver ------------------------
    //
    // A separate branch rather than a fourth curve on the same axes, because
    // it answers a different question. The homogeneous curves say how much
    // short-range order costs in entropy; this one says whether the alloy
    // ORDERS at all, and at what temperature. Below T_c the stable phase's
    // entropy is the ordered branch's, which is discontinuous there — so the
    // curve is drawn in two pieces and the transition marked between them.
    if (longRangeCheck_->isChecked() && !againstComposition) {
        core::SublatticeCvmInput in;
        const auto base = currentInput(core::CvmApproximation::Tetrahedron);
        in.species = base.species;
        in.composition = base.composition;
        in.pairEnergiesEv = base.pairEnergiesEv;
        in.minTemperatureK = base.minTemperatureK;
        in.maxTemperatureK = base.maxTemperatureK;
        in.temperatureSteps = base.temperatureSteps;
        in.trials = {static_cast<core::SublatticeOrder>(
            orderCombo_->currentData().toInt())};
        const auto lro = core::solveSublatticeClusterVariation(in);
        if (!lro.ok) {
            summaryLabel_->setText(
                tr("<b>The four-sublattice solver did not converge.</b><br>%1")
                    .arg(lro.warnings.empty()
                             ? tr("Try a narrower temperature range.")
                             : QString::fromStdString(lro.warnings.front())));
            plot_->setCurves({}, QString(), QString());
            plot_->setTransitionTemperature(0.0, QString());
            return;
        }

        CvmCurve ideal;
        ideal.label = tr("Ideal  −Σx ln x");
        ideal.colour = PlotPalette::reference;
        ideal.horizontal = true;
        ideal.constantValue = lro.idealEntropyKb;
        curves.push_back(ideal);

        CvmCurve disordered;
        disordered.label = tr("Disordered (A1)");
        disordered.colour = PlotPalette::series;
        for (const auto& point : lro.disorderedPoints) {
            disordered.xs.push_back(point.temperatureK);
            disordered.ys.push_back(point.entropyPerSiteKb);
        }
        curves.push_back(disordered);

        CvmCurve stable;
        stable.label = tr("Stable phase");
        stable.colour = PlotPalette::seriesAlt;
        for (const auto& point : lro.points) {
            stable.xs.push_back(point.temperatureK);
            stable.ys.push_back(point.entropyPerSiteKb);
        }
        curves.push_back(stable);

        plot_->setCurves(std::move(curves), tr("Temperature  (K)"),
                         tr("S_conf per site  (k_B)"));
        if (lro.transitionTemperatureK > 0.0)
            plot_->setTransitionTemperature(
                lro.transitionTemperatureK,
                tr("T_c = %1 K").arg(lro.transitionTemperatureK, 0, 'f', 1));
        else
            plot_->setTransitionTemperature(0.0, QString());

        QString text;
        if (lro.transitionTemperatureK > 0.0) {
            text = tr("<b>T<sub>c</sub> = %1 K</b> — %2 &rarr; A1<br>"
                      "Order parameter &eta;: %3 below, %4 above<br>"
                      "%5<br><br>"
                      "<small>S<sub>ideal</sub> = %6 k<sub>B</sub>. The "
                      "transition is located by the free energies CROSSING, "
                      "not by &eta; decaying to zero — which at a first-order "
                      "transition it never does.</small>")
                       .arg(lro.transitionTemperatureK, 0, 'f', 1)
                       .arg(QString::fromLatin1(
                           core::sublatticeOrderName(lro.orderedPhase)))
                       .arg(lro.orderParameterBelowTc, 0, 'f', 3)
                       .arg(lro.orderParameterAboveTc, 0, 'f', 3)
                       .arg(lro.firstOrder
                                ? tr("<b>First order</b> (&eta; jumps).")
                                : tr("Continuous."))
                       .arg(lro.idealEntropyKb, 0, 'f', 4);
        } else {
            const auto order = static_cast<core::SublatticeOrder>(
                orderCombo_->currentData().toInt());
            const double stoich =
                order == core::SublatticeOrder::L12 ? 0.25 : 0.5;
            const double x = in.composition.size() > 1 ? in.composition[1] : 0.0;
            if (std::abs(x - stoich) > 0.05)
                text = tr("<b>No ordering transition — and the composition "
                          "does not match the structure.</b><br>"
                          "%1 is stoichiometric at x = %2, but x = %3 was "
                          "requested. Away from stoichiometry the ordered "
                          "phase is only stable over part of the composition "
                          "range, and this solver works at FIXED composition "
                          "with no common-tangent construction, so it cannot "
                          "find the two-phase field.")
                           .arg(QString::fromLatin1(
                               core::sublatticeOrderName(order)))
                           .arg(stoich, 0, 'f', 2)
                           .arg(x, 0, 'f', 3);
            else
            text = tr("<b>No ordering transition in this range.</b><br>"
                      "<small>Either the interaction is too weak to order at "
                      "these temperatures, or T<sub>c</sub> lies outside the "
                      "scanned range.</small>");
        }
        for (const std::string& warning : lro.warnings)
            text += QStringLiteral("<br><small>• ")
                + QString::fromStdString(warning).toHtmlEscaped()
                + QStringLiteral("</small>");
        summaryLabel_->setText(text);
        return;
    }
    plot_->setTransitionTemperature(0.0, QString());

    if (!againstComposition) {
        const auto pair = solveAt(core::CvmApproximation::Pair);
        const auto tet = solveAt(core::CvmApproximation::Tetrahedron);
        if (!pair.ok) {
            summaryLabel_->setText(
                tr("<b>Could not solve.</b><br>%1")
                    .arg(pair.warnings.empty()
                             ? tr("Check the composition.")
                             : QString::fromStdString(pair.warnings.front())));
            plot_->setCurves({}, QString(), QString());
            return;
        }

        CvmCurve ideal;
        ideal.label = tr("Ideal  −Σx ln x");
        ideal.colour = PlotPalette::reference;
        ideal.horizontal = true;
        ideal.constantValue = pair.idealEntropyKb;
        curves.push_back(ideal);

        CvmCurve bpg;
        bpg.label = tr("BPG (pair)");
        bpg.colour = PlotPalette::series;
        for (const auto& point : pair.points) {
            bpg.xs.push_back(point.temperatureK);
            bpg.ys.push_back(point.entropyPerSiteKb);
        }
        curves.push_back(bpg);

        if (tet.ok) {
            CvmCurve tetrahedron;
            tetrahedron.label = tr("CVM (tetrahedron)");
            tetrahedron.colour = PlotPalette::seriesAlt;
            for (const auto& point : tet.points) {
                tetrahedron.xs.push_back(point.temperatureK);
                tetrahedron.ys.push_back(point.entropyPerSiteKb);
            }
            curves.push_back(tetrahedron);
        }

        const double coldestPair = pair.points.front().entropyPerSiteKb;
        summary =
            tr("<b>S<sub>ideal</sub> = %1 k<sub>B</sub></b><br>"
               "At %2 K: BPG %3, tetrahedron %4 k<sub>B</sub><br><br>"
               "<small>The spread between the curves IS the correlation "
               "correction. The pair approximation cannot see the frustration "
               "of the close-packed tetrahedron, so it overestimates ordering "
               "and sits below the tetrahedron result.</small>")
                .arg(pair.idealEntropyKb, 0, 'f', 4)
                .arg(pair.points.front().temperatureK, 0, 'f', 0)
                .arg(coldestPair, 0, 'f', 4)
                .arg(tet.ok ? QString::number(
                         tet.points.front().entropyPerSiteKb, 'f', 4)
                            : tr("n/a"));

        plot_->setCurves(std::move(curves), tr("Temperature  (K)"),
                         tr("S_conf per site  (k_B)"));
    } else {
        // Composition sweep at a single temperature: the midpoint of the
        // range, so the axis controls still mean something.
        const double t =
            0.5 * (minTemperatureSpin_->value() + maxTemperatureSpin_->value());
        CvmCurve ideal;
        ideal.label = tr("Ideal  −Σx ln x");
        ideal.colour = PlotPalette::reference;
        CvmCurve bpg;
        bpg.label = tr("BPG (pair)");
        bpg.colour = PlotPalette::series;
        CvmCurve tetrahedron;
        tetrahedron.label = tr("CVM (tetrahedron)");
        tetrahedron.colour = PlotPalette::seriesAlt;

        const int points = std::max(5, stepsSpin_->value());
        for (int i = 1; i < points; ++i) {
            const double x = static_cast<double>(i) / points;
            auto input = currentInput(core::CvmApproximation::Pair);
            input.composition = {x, 1.0 - x};
            input.minTemperatureK = input.maxTemperatureK = t;
            input.temperatureSteps = 1;
            const auto pair = core::solveClusterVariation(input);
            if (!pair.ok)
                continue;
            input.approximation = core::CvmApproximation::Tetrahedron;
            const auto tet = core::solveClusterVariation(input);

            ideal.xs.push_back(x);
            ideal.ys.push_back(pair.idealEntropyKb);
            bpg.xs.push_back(x);
            bpg.ys.push_back(pair.points.front().entropyPerSiteKb);
            if (tet.ok) {
                tetrahedron.xs.push_back(x);
                tetrahedron.ys.push_back(tet.points.front().entropyPerSiteKb);
            }
        }
        // Against composition the ideal entropy is a CURVE, not a line — it
        // is -x ln x - (1-x) ln(1-x), peaking at x = 0.5. Drawing it flat
        // here would be simply wrong, which is why `horizontal` is a property
        // of the curve rather than of the ideal approximation.
        curves.push_back(ideal);
        curves.push_back(bpg);
        if (!tetrahedron.xs.empty())
            curves.push_back(tetrahedron);
        summary = tr("<b>Composition sweep at %1 K.</b><br><small>The ideal "
                     "entropy is a curve here, not a baseline: it peaks at "
                     "x = 0.5 with ln 2 = 0.693 k<sub>B</sub>.</small>")
                      .arg(t, 0, 'f', 0);
        plot_->setCurves(std::move(curves), tr("Mole fraction x"),
                         tr("S_conf per site  (k_B)"));
    }

    summaryLabel_->setText(summary);
}

void CvmComparisonWindow::exportImage()
{
    const QString path = QFileDialog::getSaveFileName(
        this, tr("Export Image"), QString(), tr("PNG image (*.png)"));
    if (path.isEmpty())
        return;
    if (!plot_->exportImage(path, 3.0))
        QMessageBox::warning(this, tr("Export Image"),
                             tr("Could not write %1").arg(path));
}

void CvmComparisonWindow::exportData()
{
    const QString path = QFileDialog::getSaveFileName(
        this, tr("Export Data"), QString(), tr("CSV (*.csv)"));
    if (path.isEmpty())
        return;
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QMessageBox::warning(this, tr("Export Data"),
                             tr("Could not write %1").arg(path));
        return;
    }
    QTextStream out(&file);
    const bool againstComposition = axisCombo_->currentData().toInt() == 1;
    out << (againstComposition ? "x" : "temperature_K")
        << ",ideal_kB,bpg_kB,tetrahedron_kB\n";
    const auto pair = core::solveClusterVariation(
        currentInput(core::CvmApproximation::Pair));
    const auto tet = core::solveClusterVariation(
        currentInput(core::CvmApproximation::Tetrahedron));
    for (std::size_t i = 0; i < pair.points.size(); ++i) {
        out << pair.points[i].temperatureK << ',' << pair.idealEntropyKb << ','
            << pair.points[i].entropyPerSiteKb << ',';
        if (tet.ok && i < tet.points.size())
            out << tet.points[i].entropyPerSiteKb;
        out << '\n';
    }
}

} // namespace calango::gui
