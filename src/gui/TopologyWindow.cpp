#include "gui/TopologyWindow.hpp"

#include "gui/GuiUtils.hpp"
#include "gui/PlotPalette.hpp"

#include <QCheckBox>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QLabel>
#include <QMessageBox>
#include <QPainter>
#include <QPixmap>
#include <QPushButton>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>

namespace calango::gui {

namespace {
constexpr int kMargin = 54;
}

WccFlowWidget::WccFlowWidget(QWidget* parent) : QWidget(parent)
{
    setMinimumSize(420, 300);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
}

void WccFlowWidget::setFlow(std::vector<std::vector<double>> wcc,
                            std::vector<double> gapMid)
{
    wcc_ = std::move(wcc);
    gapMid_ = std::move(gapMid);
    update();
}

void WccFlowWidget::setShowGapMidpoint(bool on)
{
    showGap_ = on;
    update();
}

void WccFlowWidget::paintEvent(QPaintEvent*)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.fillRect(rect(), PlotPalette::canvas);
    if (wcc_.size() < 2) {
        painter.setPen(PlotPalette::placeholder);
        painter.drawText(rect(), Qt::AlignCenter, tr("No Wilson-loop flow"));
        return;
    }
    const QRectF box(kMargin, 14, std::max(10, width() - kMargin - 18),
                     std::max(10, height() - 14 - 44));
    const auto toX = [&](std::size_t k) {
        return box.left() + box.width() * static_cast<double>(k)
            / std::max<std::size_t>(1, wcc_.size() - 1);
    };
    const auto toY = [&](double v) { return box.bottom() - box.height() * v; };

    painter.setPen(QPen(PlotPalette::spine, 1.0));
    painter.drawRect(box);
    const QFontMetricsF metrics(painter.font());
    for (int i = 0; i <= 4; ++i) {
        const double v = i / 4.0;
        painter.setPen(QPen(PlotPalette::grid, 0.5, Qt::DotLine));
        painter.drawLine(QPointF(box.left(), toY(v)),
                         QPointF(box.right(), toY(v)));
        painter.setPen(PlotPalette::text);
        painter.drawText(QPointF(8.0, toY(v) + metrics.height() / 3.0),
                         QString::number(v, 'f', 2));
    }
    painter.setPen(PlotPalette::text);
    painter.save();
    painter.translate(14.0, box.center().y() + 60.0);
    painter.rotate(-90.0);
    painter.drawText(0, 0, tr("Wannier centre  x / a"));
    painter.restore();
    painter.drawText(QPointF(box.center().x() - 40.0, box.bottom() + 32.0),
                     tr("loop coordinate k"));

    // The largest-gap reference first, so the centres draw over it.
    if (showGap_ && gapMid_.size() > 1) {
        painter.setPen(QPen(QColor(217, 83, 79), 1.4));
        for (std::size_t k = 1; k < gapMid_.size(); ++k) {
            // Only join consecutive points when the reference did not wrap;
            // a line drawn across the wrap would read as a crossing that is
            // not there.
            if (std::abs(gapMid_[k] - gapMid_[k - 1]) > 0.5)
                continue;
            painter.drawLine(QPointF(toX(k - 1), toY(gapMid_[k - 1])),
                             QPointF(toX(k), toY(gapMid_[k])));
        }
    }

    painter.setPen(Qt::NoPen);
    painter.setBrush(PlotPalette::text);
    for (std::size_t k = 0; k < wcc_.size(); ++k)
        for (const double v : wcc_[k])
            painter.drawEllipse(QPointF(toX(k), toY(v)), 1.8, 1.8);
}

// ---------------------------------------------------------------------------

TopologyWindow::TopologyWindow(QWidget* parent) : QDialog(parent)
{
    setWindowTitle(tr("Topological Invariants"));
    resize(860, 640);

    auto* layout = new QVBoxLayout(this);
    invariants_ = new QLabel(this);
    invariants_->setTextFormat(Qt::RichText);
    invariants_->setWordWrap(true);
    invariants_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    layout->addWidget(invariants_);

    flow_ = new WccFlowWidget(this);
    layout->addWidget(flow_, 1);

    caveats_ = new QLabel(this);
    caveats_->setTextFormat(Qt::RichText);
    caveats_->setWordWrap(true);
    layout->addWidget(caveats_);

    auto* controls = new QHBoxLayout;
    gapCheck_ = new QCheckBox(tr("Show largest-gap reference"), this);
    gapCheck_->setChecked(true);
    gapCheck_->setToolTip(
        tr("The moving reference the Z₂ crossings are counted against. It "
           "follows the middle of the widest gap between centres — chosen "
           "precisely because no centre is ever near it, which is what makes "
           "the count robust where a fixed reference line is not."));
    connect(gapCheck_, &QCheckBox::toggled, flow_,
            &WccFlowWidget::setShowGapMidpoint);
    controls->addWidget(gapCheck_);
    controls->addStretch(1);
    auto* exportButton = new QPushButton(tr("Export Image…"), this);
    connect(exportButton, &QPushButton::clicked, this,
            &TopologyWindow::exportImage);
    controls->addWidget(exportButton);
    layout->addLayout(controls);
}

bool TopologyWindow::loadResults(const QString& jsonPath)
{
    data_ = readJsonObject(jsonPath);
    if (data_.isEmpty())
        return false;

    std::vector<std::vector<double>> wcc;
    for (const QJsonValue& row : data_.value(QStringLiteral("wcc")).toArray()) {
        std::vector<double> centres;
        for (const QJsonValue& v : row.toArray())
            centres.push_back(v.toDouble());
        wcc.push_back(std::move(centres));
    }
    if (wcc.size() < 2)
        return false;

    const QJsonObject z2 = data_.value(QStringLiteral("z2")).toObject();
    std::vector<double> gapMid;
    for (const QJsonValue& v : z2.value(QStringLiteral("gap_midpoint")).toArray())
        gapMid.push_back(v.toDouble());
    flow_->setFlow(std::move(wcc), std::move(gapMid));

    const QJsonObject chern = data_.value(QStringLiteral("chern")).toObject();
    QStringList parts;
    parts << tr("<b>%1</b> · %2 occupied bands · loop along "
                "k<sub>%3</sub> · %4 points")
                 .arg(data_.value(QStringLiteral("formula")).toString())
                 .arg(data_.value(QStringLiteral("occupied_bands")).toInt())
                 .arg(QStringLiteral("xyz")[data_.value(
                     QStringLiteral("direction")).toInt() % 3])
                 .arg(data_.value(QStringLiteral("loop_points")).toInt());
    if (!chern.isEmpty()) {
        parts << tr("Chern number <b>C = %1</b> "
                    "<span style='color:gray;'>(winding %2, residual %3)</span>")
                     .arg(chern.value(QStringLiteral("value")).toInt())
                     .arg(chern.value(QStringLiteral("winding")).toDouble(), 0,
                          'f', 4)
                     .arg(chern.value(QStringLiteral("residual")).toDouble(), 0,
                          'e', 2);
    }
    if (!z2.isEmpty()) {
        const int nu = z2.value(QStringLiteral("value")).toInt();
        parts << tr("Z<sub>2</sub> index <b>ν = %1</b> — %2")
                     .arg(nu)
                     .arg(nu ? tr("<b>non-trivial</b>: a topological insulator "
                                  "in this plane")
                             : tr("trivial"));
    }
    invariants_->setText(parts.join(QStringLiteral("<br>")));

    // The two ways to get a confident wrong integer, stated where they cannot
    // be missed.
    QStringList caveats;
    const double residual = chern.value(QStringLiteral("residual")).toDouble();
    if (!chern.isEmpty() && residual > 1e-2)
        caveats << tr("The winding is <b>%1 from the nearest integer</b>. A "
                      "clean calculation lands within a few 10<sup>−3</sup>; "
                      "this far off usually means the loop is under-sampled or "
                      "the manifold is not gapped, and the integer is a "
                      "rounding of noise.")
                       .arg(residual, 0, 'e', 2);
    if (!z2.isEmpty())
        caveats << tr("Z<sub>2</sub> assumes <b>time-reversal symmetry</b> and "
                      "is not defined without it. For a magnetic system the "
                      "Chern number is the invariant that applies.");
    if (!data_.value(QStringLiteral("spin_orbit")).toBool())
        caveats << tr("Spin-orbit coupling was <b>off</b>. For most candidate "
                      "materials SOC is what opens the inverted gap that makes "
                      "the phase non-trivial, so a trivial result here may say "
                      "more about the setting than about the material.");
    caveats_->setText(
        caveats.isEmpty()
            ? QString()
            : QStringLiteral("<span style='color:#d9534f;'>⚠ %1</span>")
                  .arg(caveats.join(QStringLiteral("<br>⚠ "))));
    caveats_->setVisible(!caveats.isEmpty());
    return true;
}

void TopologyWindow::exportImage()
{
    const QString path = QFileDialog::getSaveFileName(
        this, tr("Export Wilson-loop flow"), QStringLiteral("topology.png"),
        tr("PNG image (*.png)"));
    if (path.isEmpty())
        return;
    QPixmap pixmap(flow_->size());
    pixmap.fill(PlotPalette::canvas);
    flow_->render(&pixmap);
    if (!pixmap.save(path))
        QMessageBox::warning(this, windowTitle(),
                             tr("Could not write %1").arg(path));
}

} // namespace calango::gui
