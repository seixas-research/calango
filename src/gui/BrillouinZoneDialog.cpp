#include "gui/BrillouinZoneDialog.hpp"

#include "gui/BrillouinZoneStyleDialog.hpp"
#include "gui/BrillouinZoneView.hpp"

#include <QCheckBox>
#include <QDialogButtonBox>
#include <QFile>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QImage>
#include <QInputDialog>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QMessageBox>
#include <QPainter>
#include <QPushButton>
#include <QStyle>
#include <QSvgGenerator>
#include <QTextStream>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>

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
    setWindowTitle(tr("Brillouin Zone Builder"));
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

    auto* orthoCheck = new QCheckBox(tr("Orthographic projection"), this);
    orthoCheck->setToolTip(tr("Parallel projection — useful for reading "
                              "symmetric zone geometry without perspective "
                              "foreshortening"));
    side->addWidget(orthoCheck);
    connect(orthoCheck, &QCheckBox::toggled,
            view_, &BrillouinZoneView::setOrthographic);
    // Default to orthographic: symmetric zone geometry reads more clearly
    // without perspective foreshortening.
    orthoCheck->setChecked(true);
    view_->setOrthographic(true);

    auto* styleButton = new QPushButton(tr("Customize Appearance…"), this);
    styleButton->setToolTip(tr("Colors, transparency, line thickness and label "
                               "toggles for the zone and k-path"));
    side->addWidget(styleButton);
    connect(styleButton, &QPushButton::clicked, this, [this] {
        auto* dialog = new BrillouinZoneStyleDialog(view_->style(), this);
        connect(dialog, &BrillouinZoneStyleDialog::styleChanged, view_,
                &BrillouinZoneView::setStyle);
        dialog->setAttribute(Qt::WA_DeleteOnClose);
        dialog->show();
    });

    side->addWidget(new QLabel(tr("k-path sequence:"), this));
    side->addWidget(pathList_, 1);

    // Icon-only action bar (Suggested · Break · Undo · Remove · Clear) with
    // hover tooltips — glyphs come from the active style, matching the rest of
    // the app's icon buttons.
    auto* pathButtons = new QHBoxLayout;
    const auto iconButton = [this](QStyle::StandardPixmap sp, const QString& tip) {
        auto* button = new QPushButton(this);
        button->setIcon(style()->standardIcon(sp));
        button->setToolTip(tip);
        button->setFocusPolicy(Qt::NoFocus);
        return button;
    };
    auto* suggestedButton = iconButton(
        QStyle::SP_BrowserReload,
        tr("Suggested — load ASE's suggested path: %1").arg(suggestedPath_));
    auto* breakButton = iconButton(
        QStyle::SP_MediaSkipForward,
        tr("Break — start a new discontinuous section (e.g. Γ → X | M → R)"));
    auto* undoButton = iconButton(QStyle::SP_ArrowBack,
                                  tr("Undo — remove the last point in the path"));
    auto* removeButton = iconButton(
        QStyle::SP_LineEditClearButton,
        tr("Remove — delete the selected k-point / break from the path"));
    auto* clearButton = iconButton(QStyle::SP_TrashIcon,
                                   tr("Clear — remove the entire path"));
    pathButtons->addWidget(suggestedButton);
    pathButtons->addWidget(breakButton);
    pathButtons->addWidget(undoButton);
    pathButtons->addWidget(removeButton);
    pathButtons->addWidget(clearButton);
    side->addLayout(pathButtons);
    connect(suggestedButton, &QPushButton::clicked,
            this, &BrillouinZoneDialog::useSuggestedPath);
    connect(breakButton, &QPushButton::clicked, this, &BrillouinZoneDialog::addBreak);
    connect(undoButton, &QPushButton::clicked, this, &BrillouinZoneDialog::undoLastPoint);
    connect(removeButton, &QPushButton::clicked,
            this, &BrillouinZoneDialog::removeSelectedPoint);
    connect(clearButton, &QPushButton::clicked, this, &BrillouinZoneDialog::clearPath);

    auto* divisionsRow = new QHBoxLayout;
    divisionsRow->addWidget(new QLabel(tr("Points per segment:"), this));
    divisionsSpin_->setRange(5, 500);
    divisionsSpin_->setValue(40);
    divisionsRow->addWidget(divisionsSpin_, 1);
    side->addLayout(divisionsRow);

    // One consolidated k-path exporter (format picked in a sub-dialog)
    // plus the direct-action figure capture.
    auto* exportPathButton = new QPushButton(tr("Export k-Path…"), this);
    exportPathButton->setToolTip(tr("VASP KPOINTS · QE K_POINTS · CASTEP · "
                                    "SIESTA BandLines · ASE/Python script"));
    side->addWidget(exportPathButton);
    connect(exportPathButton, &QPushButton::clicked,
            this, &BrillouinZoneDialog::exportKPath);

    auto* exportFigureButton = new QPushButton(tr("Export Figure (PNG/SVG)…"), this);
    side->addWidget(exportFigureButton);
    connect(exportFigureButton, &QPushButton::clicked,
            this, &BrillouinZoneDialog::exportFigure);

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

void BrillouinZoneDialog::removeSelectedPoint()
{
    // List rows are 1:1 with path_ (breaks included), so currentRow() indexes
    // path_ directly.
    const int row = pathList_->currentRow();
    if (row < 0 || row >= static_cast<int>(path_.size()))
        return;
    path_.erase(path_.begin() + row);
    // Drop a break left dangling at the front, or a break now doubled/at the
    // end, so the path stays well-formed.
    while (!path_.empty() && path_.front() < 0)
        path_.erase(path_.begin());
    for (std::size_t i = path_.size(); i-- > 0;) {
        const bool danglingEnd = path_[i] < 0 && i + 1 == path_.size();
        const bool doubledBreak = path_[i] < 0 && i > 0 && path_[i - 1] < 0;
        if (danglingEnd || doubledBreak)
            path_.erase(path_.begin() + static_cast<std::ptrdiff_t>(i));
    }
    syncPathViews();
    if (!path_.empty())
        pathList_->setCurrentRow(
            std::min(row, static_cast<int>(path_.size()) - 1));
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

QString BrillouinZoneDialog::asePathString() const
{
    QString result;
    for (const int entry : path_) {
        if (entry < 0) {
            // Only a real break between two labelled points.
            if (!result.isEmpty() && !result.endsWith(QLatin1Char(',')))
                result += QLatin1Char(',');
            continue;
        }
        result += QString::fromStdString(
            specialPoints_[static_cast<std::size_t>(entry)].label);
    }
    if (result.endsWith(QLatin1Char(',')))
        result.chop(1);
    return result;
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

void BrillouinZoneDialog::exportKPath()
{
    if (!hasExportablePath()) {
        QMessageBox::information(this, tr("Export k-Path"),
                                 tr("Pick at least two connected points first."));
        return;
    }

    // JSON (kpath.json) is the primary/default format; the code-specific
    // formats follow.
    const QStringList formats = {tr("Calango k-path (kpath.json)"),
                                 tr("VASP KPOINTS (line mode)"),
                                 tr("Quantum ESPRESSO K_POINTS (crystal_b)"),
                                 tr("CASTEP SPECTRAL_KPOINT_PATH"),
                                 tr("SIESTA BandLines"),
                                 tr("ASE / Python script")};
    bool ok = false;
    const QString chosen = QInputDialog::getItem(
        this, tr("Export k-Path"), tr("Format:"), formats, 0,
        /*editable=*/false, &ok);
    if (!ok)
        return;
    switch (formats.indexOf(chosen)) {
    case 0: exportKpathJson(); break;
    case 1: exportVaspKpoints(); break;
    case 2: exportQeKpoints(); break;
    case 3: exportCastepPath(); break;
    case 4: exportSiestaBands(); break;
    case 5: exportAseScript(); break;
    default: break;
    }
}

void BrillouinZoneDialog::exportKpathJson()
{
    // Cartesian (Å⁻¹) position of a fractional reciprocal-space coordinate.
    const auto toCartesian = [this](const core::Vec3& f) {
        return zone_.reciprocal[0] * f.x + zone_.reciprocal[1] * f.y
            + zone_.reciprocal[2] * f.z;
    };
    // JSON-friendly label ("G" → "Gamma", others unchanged).
    const auto jsonLabel = [](const std::string& label) {
        return label == "G" ? QStringLiteral("Gamma")
                            : QString::fromStdString(label);
    };
    const auto jsonVec = [](const core::Vec3& v) {
        QJsonArray a;
        a << v.x << v.y << v.z;
        return a;
    };

    const core::KPathSegments sections = segments();

    // High-symmetry point coordinates (unique labels used in the path).
    QJsonObject points;
    for (const auto& section : sections)
        for (const auto& point : section)
            points.insert(jsonLabel(point.label), jsonVec(point.fractional));

    // Explicit sequential segments: labels + fractional coords + cumulative
    // reciprocal-space distance (Å⁻¹) from the start of each segment.
    QJsonArray segmentsJson;
    for (const auto& section : sections) {
        QJsonArray labels;
        QJsonArray pointArray;
        double distance = 0.0;
        core::Vec3 prevCart{};
        bool first = true;
        for (const auto& point : section) {
            const core::Vec3 cart = toCartesian(point.fractional);
            if (!first) {
                const core::Vec3 d{cart.x - prevCart.x, cart.y - prevCart.y,
                                   cart.z - prevCart.z};
                distance += std::sqrt(d.x * d.x + d.y * d.y + d.z * d.z);
            }
            first = false;
            prevCart = cart;

            labels.append(jsonLabel(point.label));
            QJsonObject entry;
            entry.insert(QStringLiteral("label"), jsonLabel(point.label));
            entry.insert(QStringLiteral("fractional"), jsonVec(point.fractional));
            entry.insert(QStringLiteral("distance"), distance);
            pointArray.append(entry);
        }
        QJsonObject segment;
        segment.insert(QStringLiteral("labels"), labels);
        segment.insert(QStringLiteral("points"), pointArray);
        segmentsJson.append(segment);
    }

    QJsonObject root;
    root.insert(QStringLiteral("format"), QStringLiteral("calango-kpath"));
    root.insert(QStringLiteral("version"), 1);
    // ASE path string (letters, ',' between sections) for direct round-trip.
    root.insert(QStringLiteral("path"), asePathString());
    root.insert(QStringLiteral("divisions"), divisionsSpin_->value());
    root.insert(QStringLiteral("high_symmetry_points"), points);
    root.insert(QStringLiteral("segments"), segmentsJson);

    const QString path = QFileDialog::getSaveFileName(
        this, tr("Export k-Path (JSON)"), QStringLiteral("kpath.json"),
        tr("k-path JSON (*.json);;All files (*)"));
    if (path.isEmpty())
        return;
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QMessageBox::critical(this, tr("Export k-Path"),
                              tr("Could not write %1").arg(path));
        return;
    }
    file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
}

void BrillouinZoneDialog::exportVaspKpoints()
{
    saveTextFile(QString::fromStdString(
                     core::toVaspKpoints(segments(), divisionsSpin_->value())),
                 tr("Export VASP KPOINTS"), QStringLiteral("KPOINTS"));
}

void BrillouinZoneDialog::exportQeKpoints()
{
    saveTextFile(QString::fromStdString(
                     core::toQeKpointsCard(segments(), divisionsSpin_->value())),
                 tr("Export Quantum ESPRESSO K_POINTS"),
                 QStringLiteral("kpath_qe.in"));
}

void BrillouinZoneDialog::exportCastepPath()
{
    saveTextFile(QString::fromStdString(core::toCastepPath(segments())),
                 tr("Export CASTEP Spectral k-Point Path"),
                 QStringLiteral("kpath_castep.cell"));
}

void BrillouinZoneDialog::exportSiestaBands()
{
    saveTextFile(QString::fromStdString(
                     core::toSiestaBandLines(segments(), divisionsSpin_->value())),
                 tr("Export SIESTA BandLines"), QStringLiteral("kpath_siesta.fdf"));
}

void BrillouinZoneDialog::exportAseScript()
{
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
