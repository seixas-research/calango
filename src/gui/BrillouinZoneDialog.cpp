#include "gui/BrillouinZoneDialog.hpp"

#include "gui/BrillouinZoneStyleDialog.hpp"
#include "gui/BrillouinZoneView.hpp"
#include "gui/BrillouinZoneWidget.hpp"

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


BrillouinZoneDialog::BrillouinZoneDialog(const core::BrillouinZoneData& zone,
                                         const pybridge::AseBridge::BandPathInfo& bandPath,
                                         QWidget* parent)
    : QDialog(parent)
    , builder_(new BrillouinZoneWidget(zone, bandPath, /*compact=*/false, this))
{
    setWindowTitle(tr("Brillouin Zone Builder"));
    resize(900, 600);

    // The dialog contributes only what a wizard does not want: the file
    // exporters. Everything interactive belongs to the widget.
    auto* exportPathButton = new QPushButton(tr("Export k-Path…"), this);
    exportPathButton->setToolTip(tr("VASP KPOINTS · QE K_POINTS · CASTEP · "
                                    "SIESTA BandLines · ASE/Python script"));
    auto* exportFigureButton = new QPushButton(tr("Export Figure (PNG/SVG)…"), this);
    connect(exportPathButton, &QPushButton::clicked,
            this, &BrillouinZoneDialog::exportKPath);
    connect(exportFigureButton, &QPushButton::clicked,
            this, &BrillouinZoneDialog::exportFigure);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    auto* actionRow = new QHBoxLayout;
    actionRow->addWidget(exportPathButton);
    actionRow->addWidget(exportFigureButton);
    actionRow->addStretch(1);
    actionRow->addWidget(buttons);

    auto* layout = new QVBoxLayout(this);
    layout->addWidget(builder_, 1);
    layout->addLayout(actionRow);
}

QString BrillouinZoneDialog::asePathString() const
{
    return builder_->pathString();
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
    if (!builder_->hasExportablePath()) {
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
        return builder_->zone().reciprocal[0] * f.x + builder_->zone().reciprocal[1] * f.y
            + builder_->zone().reciprocal[2] * f.z;
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

    const core::KPathSegments sections = builder_->segments();

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
    root.insert(QStringLiteral("divisions"), builder_->pointsPerSegment());
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
                     core::toVaspKpoints(builder_->segments(), builder_->pointsPerSegment())),
                 tr("Export VASP KPOINTS"), QStringLiteral("KPOINTS"));
}

void BrillouinZoneDialog::exportQeKpoints()
{
    saveTextFile(QString::fromStdString(
                     core::toQeKpointsCard(builder_->segments(), builder_->pointsPerSegment())),
                 tr("Export Quantum ESPRESSO K_POINTS"),
                 QStringLiteral("kbuilder_->path()qe.in"));
}

void BrillouinZoneDialog::exportCastepPath()
{
    saveTextFile(QString::fromStdString(core::toCastepPath(builder_->segments())),
                 tr("Export CASTEP Spectral k-Point Path"),
                 QStringLiteral("kbuilder_->path()castep.cell"));
}

void BrillouinZoneDialog::exportSiestaBands()
{
    saveTextFile(QString::fromStdString(
                     core::toSiestaBandLines(builder_->segments(), builder_->pointsPerSegment())),
                 tr("Export SIESTA BandLines"), QStringLiteral("kbuilder_->path()siesta.fdf"));
}

void BrillouinZoneDialog::exportAseScript()
{
    // Total sampling matched to per-segment divisions across all legs.
    int legs = 0;
    for (const auto& section : builder_->segments())
        if (section.size() >= 2)
            legs += static_cast<int>(section.size()) - 1;
    saveTextFile(QString::fromStdString(core::toAsePythonScript(
                     builder_->segments(), std::max(20, legs * builder_->pointsPerSegment()))),
                 tr("Export ASE Band-Path Script"), QStringLiteral("kbuilder_->path()ase.py"));
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
        builder_->view()->paintFigure(painter, figureSize);
        painter.end();
    } else {
        QImage image(figureSize, QImage::Format_ARGB32);
        image.fill(Qt::white);
        QPainter painter(&image);
        builder_->view()->paintFigure(painter, figureSize);
        painter.end();
        if (!image.save(path)) {
            QMessageBox::critical(this, tr("Export Figure"),
                                  tr("Could not write %1").arg(path));
            return;
        }
    }
}

} // namespace calango::gui
