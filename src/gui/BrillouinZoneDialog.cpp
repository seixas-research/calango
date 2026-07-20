#include "gui/BrillouinZoneDialog.hpp"

#include "gui/BrillouinZoneView.hpp"

#include <QDialogButtonBox>
#include <QFile>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QTextStream>
#include <QVBoxLayout>

#include <algorithm>

namespace calango::gui {

namespace {

QString displayLabel(const std::string& label)
{
    return label == "G" ? QStringLiteral("Γ") : QString::fromStdString(label);
}

/// Parse an ASE path string ("GXWKGLUWLK,UX") into labels; only the first
/// continuous segment (up to the first ',') is used. Labels are one letter
/// optionally followed by digits ("X1").
QStringList parsePathLabels(const QString& path)
{
    QStringList labels;
    for (const QChar c : path) {
        if (c == QLatin1Char(','))
            break;
        if (c.isDigit() && !labels.isEmpty())
            labels.last() += c;
        else
            labels << QString(c);
    }
    return labels;
}

} // namespace

BrillouinZoneDialog::BrillouinZoneDialog(const core::BrillouinZoneData& zone,
                                         const pybridge::AseBridge::BandPathInfo& bandPath,
                                         QWidget* parent)
    : QDialog(parent)
    , zone_(zone)
    , specialPoints_(bandPath.specialPoints)
    , suggestedPath_(QString::fromStdString(bandPath.suggestedPath))
    , view_(new BrillouinZoneView(this))
    , pathList_(new QListWidget(this))
    , divisionsSpin_(new QSpinBox(this))
{
    setWindowTitle(tr("Brillouin Zone & k-Path Builder"));
    resize(900, 560);

    // Cartesian positions of the special points: frac · (b1, b2, b3).
    std::vector<BrillouinZoneView::LabeledPoint> viewPoints;
    for (const auto& point : specialPoints_) {
        const core::Vec3 cart = zone_.reciprocal[0] * point.fractional.x
            + zone_.reciprocal[1] * point.fractional.y
            + zone_.reciprocal[2] * point.fractional.z;
        viewPoints.push_back({displayLabel(point.label),
                              {static_cast<float>(cart.x), static_cast<float>(cart.y),
                               static_cast<float>(cart.z)}});
    }
    view_->setZone(zone_, viewPoints);
    connect(view_, &BrillouinZoneView::pointPicked,
            this, &BrillouinZoneDialog::appendPoint);

    auto* side = new QVBoxLayout;
    auto* hint = new QLabel(
        tr("Click high-symmetry points to build the k-path.\n"
           "Drag rotates · Shift+drag pans · wheel zooms."),
        this);
    hint->setWordWrap(true);
    side->addWidget(hint);
    side->addWidget(new QLabel(tr("k-path sequence:"), this));
    side->addWidget(pathList_, 1);

    auto* pathButtons = new QHBoxLayout;
    auto* suggestedButton = new QPushButton(tr("Suggested"), this);
    suggestedButton->setToolTip(tr("Load ASE's suggested path: %1").arg(suggestedPath_));
    auto* undoButton = new QPushButton(tr("Undo"), this);
    auto* clearButton = new QPushButton(tr("Clear"), this);
    pathButtons->addWidget(suggestedButton);
    pathButtons->addWidget(undoButton);
    pathButtons->addWidget(clearButton);
    side->addLayout(pathButtons);
    connect(suggestedButton, &QPushButton::clicked,
            this, &BrillouinZoneDialog::useSuggestedPath);
    connect(undoButton, &QPushButton::clicked, this, &BrillouinZoneDialog::undoLastPoint);
    connect(clearButton, &QPushButton::clicked, this, &BrillouinZoneDialog::clearPath);

    auto* divisionsRow = new QHBoxLayout;
    divisionsRow->addWidget(new QLabel(tr("Points per segment:"), this));
    divisionsSpin_->setRange(5, 500);
    divisionsSpin_->setValue(40);
    divisionsRow->addWidget(divisionsSpin_, 1);
    side->addLayout(divisionsRow);

    auto* exportVaspButton = new QPushButton(tr("Export VASP KPOINTS…"), this);
    auto* exportQeButton = new QPushButton(tr("Export QE K_POINTS…"), this);
    side->addWidget(exportVaspButton);
    side->addWidget(exportQeButton);
    connect(exportVaspButton, &QPushButton::clicked,
            this, &BrillouinZoneDialog::exportVaspKpoints);
    connect(exportQeButton, &QPushButton::clicked,
            this, &BrillouinZoneDialog::exportQeKpoints);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    side->addWidget(buttons);

    auto* layout = new QHBoxLayout(this);
    layout->addWidget(view_, 1);
    auto* sideWidget = new QWidget(this);
    sideWidget->setLayout(side);
    sideWidget->setFixedWidth(280);
    layout->addWidget(sideWidget);
}

void BrillouinZoneDialog::appendPoint(int index)
{
    if (index < 0 || index >= static_cast<int>(specialPoints_.size()))
        return;
    if (!path_.empty() && path_.back() == index)
        return; // ignore double-clicks on the same point
    path_.push_back(index);
    syncPathViews();
}

void BrillouinZoneDialog::undoLastPoint()
{
    if (path_.empty())
        return;
    path_.pop_back();
    syncPathViews();
}

void BrillouinZoneDialog::clearPath()
{
    path_.clear();
    syncPathViews();
}

void BrillouinZoneDialog::useSuggestedPath()
{
    path_.clear();
    for (const QString& label : parsePathLabels(suggestedPath_)) {
        const auto it = std::find_if(
            specialPoints_.begin(), specialPoints_.end(),
            [&](const core::KPathPoint& p) {
                return QString::fromStdString(p.label) == label;
            });
        if (it != specialPoints_.end())
            path_.push_back(static_cast<int>(it - specialPoints_.begin()));
    }
    syncPathViews();
}

void BrillouinZoneDialog::syncPathViews()
{
    pathList_->clear();
    for (std::size_t i = 0; i < path_.size(); ++i) {
        const auto& point = specialPoints_[static_cast<std::size_t>(path_[i])];
        pathList_->addItem(QStringLiteral("%1.  %2   (%3, %4, %5)")
                               .arg(i + 1)
                               .arg(displayLabel(point.label))
                               .arg(point.fractional.x, 0, 'f', 3)
                               .arg(point.fractional.y, 0, 'f', 3)
                               .arg(point.fractional.z, 0, 'f', 3));
    }
    view_->setPath(path_);
}

std::vector<core::KPathPoint> BrillouinZoneDialog::pathPoints() const
{
    std::vector<core::KPathPoint> points;
    for (const int index : path_)
        points.push_back(specialPoints_[static_cast<std::size_t>(index)]);
    return points;
}

void BrillouinZoneDialog::saveTextFile(const QString& text, const QString& caption,
                                       const QString& defaultName)
{
    const QString path = QFileDialog::getSaveFileName(this, caption, defaultName);
    if (path.isEmpty())
        return;
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QMessageBox::critical(this, caption, tr("Could not write %1").arg(path));
        return;
    }
    QTextStream(&file) << text;
}

void BrillouinZoneDialog::exportVaspKpoints()
{
    if (path_.size() < 2) {
        QMessageBox::information(this, tr("Export KPOINTS"),
                                 tr("Pick at least two points first."));
        return;
    }
    saveTextFile(QString::fromStdString(
                     core::toVaspKpoints(pathPoints(), divisionsSpin_->value())),
                 tr("Export VASP KPOINTS"), QStringLiteral("KPOINTS"));
}

void BrillouinZoneDialog::exportQeKpoints()
{
    if (path_.size() < 2) {
        QMessageBox::information(this, tr("Export K_POINTS"),
                                 tr("Pick at least two points first."));
        return;
    }
    saveTextFile(QString::fromStdString(
                     core::toQeKpointsCard(pathPoints(), divisionsSpin_->value())),
                 tr("Export Quantum ESPRESSO K_POINTS"),
                 QStringLiteral("kpath_qe.in"));
}

} // namespace calango::gui
