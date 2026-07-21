#include "gui/BrillouinZoneDialog.hpp"

#include "gui/BrillouinZoneView.hpp"

#include <QDialogButtonBox>
#include <QFile>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QImage>
#include <QLabel>
#include <QMessageBox>
#include <QPainter>
#include <QPushButton>
#include <QSvgGenerator>
#include <QTextStream>
#include <QVBoxLayout>

#include <algorithm>

namespace calango::gui {

namespace {

QString displayLabel(const std::string& label)
{
    return label == "G" ? QStringLiteral("Γ") : QString::fromStdString(label);
}

/// Parse an ASE path string ("GXWKGLUWLK,UX") into labels; a "," marks a
/// section break and is kept as an empty entry. Labels are one letter
/// optionally followed by digits ("X1").
QStringList parsePathLabels(const QString& path)
{
    QStringList labels;
    for (const QChar c : path) {
        if (c == QLatin1Char(',')) {
            labels << QString(); // break marker
            continue;
        }
        if (c.isDigit() && !labels.isEmpty() && !labels.last().isEmpty())
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
    auto* breakButton = new QPushButton(tr("Break"), this);
    breakButton->setToolTip(tr("Start a new discontinuous section "
                               "(e.g. Γ → X | M → R)"));
    auto* undoButton = new QPushButton(tr("Undo"), this);
    auto* clearButton = new QPushButton(tr("Clear"), this);
    pathButtons->addWidget(suggestedButton);
    pathButtons->addWidget(breakButton);
    pathButtons->addWidget(undoButton);
    pathButtons->addWidget(clearButton);
    side->addLayout(pathButtons);
    connect(suggestedButton, &QPushButton::clicked,
            this, &BrillouinZoneDialog::useSuggestedPath);
    connect(breakButton, &QPushButton::clicked, this, &BrillouinZoneDialog::addBreak);
    connect(undoButton, &QPushButton::clicked, this, &BrillouinZoneDialog::undoLastPoint);
    connect(clearButton, &QPushButton::clicked, this, &BrillouinZoneDialog::clearPath);

    auto* divisionsRow = new QHBoxLayout;
    divisionsRow->addWidget(new QLabel(tr("Points per segment:"), this));
    divisionsSpin_->setRange(5, 500);
    divisionsSpin_->setValue(40);
    divisionsRow->addWidget(divisionsSpin_, 1);
    side->addLayout(divisionsRow);

    const auto addExportButton = [this, side](const QString& text, auto slot) {
        auto* button = new QPushButton(text, this);
        side->addWidget(button);
        connect(button, &QPushButton::clicked, this, slot);
    };
    addExportButton(tr("Export VASP KPOINTS…"),
                    &BrillouinZoneDialog::exportVaspKpoints);
    addExportButton(tr("Export QE K_POINTS…"),
                    &BrillouinZoneDialog::exportQeKpoints);
    addExportButton(tr("Export CASTEP Path…"),
                    &BrillouinZoneDialog::exportCastepPath);
    addExportButton(tr("Export SIESTA BandLines…"),
                    &BrillouinZoneDialog::exportSiestaBands);
    addExportButton(tr("Export ASE Script…"),
                    &BrillouinZoneDialog::exportAseScript);
    addExportButton(tr("Export Figure (PNG/SVG)…"),
                    &BrillouinZoneDialog::exportFigure);

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

void BrillouinZoneDialog::addBreak()
{
    // A break needs a point before it and never repeats.
    if (path_.empty() || path_.back() < 0)
        return;
    path_.push_back(-1);
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
        if (label.isEmpty()) { // "," in the ASE path string
            if (!path_.empty() && path_.back() >= 0)
                path_.push_back(-1);
            continue;
        }
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
    int order = 0;
    for (const int entry : path_) {
        if (entry < 0) {
            pathList_->addItem(QStringLiteral("      — break —"));
            continue;
        }
        const auto& point = specialPoints_[static_cast<std::size_t>(entry)];
        pathList_->addItem(QStringLiteral("%1.  %2   (%3, %4, %5)")
                               .arg(++order)
                               .arg(displayLabel(point.label))
                               .arg(point.fractional.x, 0, 'f', 3)
                               .arg(point.fractional.y, 0, 'f', 3)
                               .arg(point.fractional.z, 0, 'f', 3));
    }
    view_->setPath(path_);
}

core::KPathSegments BrillouinZoneDialog::segments() const
{
    core::KPathSegments sections;
    std::vector<core::KPathPoint> current;
    for (const int entry : path_) {
        if (entry < 0) {
            if (!current.empty())
                sections.push_back(std::move(current));
            current.clear();
            continue;
        }
        current.push_back(specialPoints_[static_cast<std::size_t>(entry)]);
    }
    if (!current.empty())
        sections.push_back(std::move(current));
    return sections;
}

bool BrillouinZoneDialog::hasExportablePath() const
{
    for (const auto& section : segments())
        if (section.size() >= 2)
            return true;
    return false;
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
    if (!hasExportablePath()) {
        QMessageBox::information(this, tr("Export KPOINTS"),
                                 tr("Pick at least two connected points first."));
        return;
    }
    saveTextFile(QString::fromStdString(
                     core::toVaspKpoints(segments(), divisionsSpin_->value())),
                 tr("Export VASP KPOINTS"), QStringLiteral("KPOINTS"));
}

void BrillouinZoneDialog::exportQeKpoints()
{
    if (!hasExportablePath()) {
        QMessageBox::information(this, tr("Export K_POINTS"),
                                 tr("Pick at least two connected points first."));
        return;
    }
    saveTextFile(QString::fromStdString(
                     core::toQeKpointsCard(segments(), divisionsSpin_->value())),
                 tr("Export Quantum ESPRESSO K_POINTS"),
                 QStringLiteral("kpath_qe.in"));
}

void BrillouinZoneDialog::exportCastepPath()
{
    if (!hasExportablePath()) {
        QMessageBox::information(this, tr("Export CASTEP Path"),
                                 tr("Pick at least two connected points first."));
        return;
    }
    saveTextFile(QString::fromStdString(core::toCastepPath(segments())),
                 tr("Export CASTEP Spectral k-Point Path"),
                 QStringLiteral("kpath_castep.cell"));
}

void BrillouinZoneDialog::exportSiestaBands()
{
    if (!hasExportablePath()) {
        QMessageBox::information(this, tr("Export SIESTA BandLines"),
                                 tr("Pick at least two connected points first."));
        return;
    }
    saveTextFile(QString::fromStdString(
                     core::toSiestaBandLines(segments(), divisionsSpin_->value())),
                 tr("Export SIESTA BandLines"), QStringLiteral("kpath_siesta.fdf"));
}

void BrillouinZoneDialog::exportAseScript()
{
    if (!hasExportablePath()) {
        QMessageBox::information(this, tr("Export ASE Script"),
                                 tr("Pick at least two connected points first."));
        return;
    }
    // Total sampling matched to per-segment divisions across all legs.
    int legs = 0;
    for (const auto& section : segments())
        if (section.size() >= 2)
            legs += static_cast<int>(section.size()) - 1;
    saveTextFile(QString::fromStdString(core::toAsePythonScript(
                     segments(), std::max(20, legs * divisionsSpin_->value()))),
                 tr("Export ASE Band-Path Script"), QStringLiteral("kpath_ase.py"));
}

void BrillouinZoneDialog::exportFigure()
{
    QString selectedFilter;
    const QString path = QFileDialog::getSaveFileName(
        this, tr("Export Brillouin Zone Figure"),
        QStringLiteral("brillouin_zone.png"),
        tr("PNG image (*.png);;SVG vector image (*.svg)"), &selectedFilter);
    if (path.isEmpty())
        return;

    const bool svg = path.endsWith(QStringLiteral(".svg"), Qt::CaseInsensitive);
    const QSize figureSize(1600, 1400); // high-resolution canvas

    if (svg) {
        QSvgGenerator generator;
        generator.setFileName(path);
        generator.setSize(figureSize);
        generator.setViewBox(QRect(QPoint(0, 0), figureSize));
        generator.setTitle(QStringLiteral("Brillouin zone — Calango"));
        generator.setDescription(
            QStringLiteral("First Brillouin zone with high-symmetry points "
                           "and k-path"));
        QPainter painter(&generator);
        view_->paintFigure(painter, figureSize);
        painter.end();
    } else {
        QImage image(figureSize, QImage::Format_ARGB32);
        image.fill(Qt::white);
        QPainter painter(&image);
        view_->paintFigure(painter, figureSize);
        painter.end();
        if (!image.save(path)) {
            QMessageBox::critical(this, tr("Export Figure"),
                                  tr("Could not write %1").arg(path));
            return;
        }
    }
}

} // namespace calango::gui
