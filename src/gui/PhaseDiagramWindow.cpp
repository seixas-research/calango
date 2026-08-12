#include "gui/PhaseDiagramWindow.hpp"

#include "core/CalphadModel.hpp"
#include "core/TdbExpression.hpp"
#include "gui/PlotPalette.hpp"

#include <QCheckBox>
#include <QColorDialog>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFile>
#include <QFileDialog>
#include <QFontMetricsF>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QImage>
#include <QSpinBox>
#include <QSvgGenerator>
#include <QTabWidget>
#include <QTextStream>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>

namespace calango::gui {

namespace {

constexpr double kMarginLeft = 64.0;
constexpr double kMarginRight = 18.0;
constexpr double kMarginTop = 18.0;
constexpr double kMarginBottom = 48.0;

/// "Nice" tick spacing for a range: 1, 2 or 5 times a power of ten.
double tickStep(double span, int target)
{
    if (span <= 0.0 || target <= 0)
        return 1.0;
    const double raw = span / target;
    const double magnitude = std::pow(10.0, std::floor(std::log10(raw)));
    const double normalized = raw / magnitude;
    if (normalized < 1.5)
        return magnitude;
    if (normalized < 3.5)
        return 2.0 * magnitude;
    if (normalized < 7.5)
        return 5.0 * magnitude;
    return 10.0 * magnitude;
}

/// Render `widget`'s drawing to PNG or SVG, chosen by the file extension.
///
/// Shared by both plots. PNG is written at `scale`x the on-screen size because
/// a screen-resolution raster of a line plot is unusable in print — the axis
/// numbers alias into grey mush at any sensible figure width — while SVG
/// carries no resolution at all and is what a figure should be if the
/// destination accepts it.
template <typename Renderer>
bool renderToFile(const QString& path, const QSizeF& logical, double scale,
                  const Renderer& draw)
{
    if (logical.width() <= 0.0 || logical.height() <= 0.0)
        return false;
    if (path.endsWith(QLatin1String(".svg"), Qt::CaseInsensitive)) {
        QSvgGenerator generator;
        generator.setFileName(path);
        generator.setSize(logical.toSize());
        generator.setViewBox(QRectF(QPointF(0.0, 0.0), logical));
        QPainter painter(&generator);
        if (!painter.isActive())
            return false;
        draw(painter, QRectF(QPointF(0.0, 0.0), logical));
        return true;
    }
    const double factor = std::max(1.0, scale);
    QImage image(static_cast<int>(logical.width() * factor),
                 static_cast<int>(logical.height() * factor),
                 QImage::Format_ARGB32_Premultiplied);
    if (image.isNull())
        return false;
    image.fill(Qt::transparent);
    QPainter painter(&image);
    painter.scale(factor, factor);
    draw(painter, QRectF(QPointF(0.0, 0.0), logical));
    painter.end();
    return image.save(path);
}

} // namespace

// ---------------------------------------------------------------------------
// PhaseDiagramStyle
// ---------------------------------------------------------------------------

PhaseDiagramStyle::PhaseDiagramStyle()
    : canvas(PlotPalette::canvas), spine(PlotPalette::spine),
      grid(PlotPalette::grid), text(PlotPalette::text)
{
    // The matplotlib tab10 family, for the same reason PlotPalette::series is:
    // a Calango figure and a matplotlib one made from the same data should look
    // like the same plot. Order is fixed so a phase keeps its colour between
    // the binary diagram and the ternary section of the same system.
    phaseColors = {QColor(0x1f, 0x77, 0xb4), QColor(0xff, 0x7f, 0x0e),
                   QColor(0x2c, 0xa0, 0x2c), QColor(0xd6, 0x27, 0x28),
                   QColor(0x94, 0x67, 0xbd), QColor(0x8c, 0x56, 0x4b),
                   QColor(0xe3, 0x77, 0xc2), QColor(0x7f, 0x7f, 0x7f),
                   QColor(0xbc, 0xbd, 0x22), QColor(0x17, 0xbe, 0xcf)};
}

QColor PhaseDiagramStyle::phaseColor(int index) const
{
    if (phaseColors.isEmpty())
        return PlotPalette::series;
    return phaseColors[std::max(0, index) % phaseColors.size()];
}

// ---------------------------------------------------------------------------
// PhaseDiagramStyleDialog
// ---------------------------------------------------------------------------

QPushButton* PhaseDiagramStyleDialog::colorButton(QColor* target)
{
    auto* button = new QPushButton(this);
    button->setAutoDefault(false);
    button->setFixedWidth(64);
    const auto paint = [button, target] {
        button->setStyleSheet(
            QStringLiteral("background-color: %1; border: 1px solid #888;")
                .arg(target->name()));
    };
    paint();
    connect(button, &QPushButton::clicked, this, [this, target, paint] {
        const QColor chosen =
            QColorDialog::getColor(*target, this, tr("Choose Colour"));
        if (!chosen.isValid())
            return;
        *target = chosen;
        paint();
        Q_EMIT styleChanged(style_);
    });
    return button;
}

PhaseDiagramStyleDialog::PhaseDiagramStyleDialog(const PhaseDiagramStyle& style,
                                                 const QStringList& phaseNames,
                                                 QWidget* parent)
    : QDialog(parent), style_(style)
{
    setWindowTitle(tr("Customize Appearance"));

    auto* layout = new QVBoxLayout(this);

    auto* colors = new QGroupBox(tr("Colours"), this);
    auto* colorForm = new QFormLayout(colors);
    colorForm->addRow(tr("Canvas:"), colorButton(&style_.canvas));
    colorForm->addRow(tr("Axes:"), colorButton(&style_.spine));
    colorForm->addRow(tr("Grid:"), colorButton(&style_.grid));
    colorForm->addRow(tr("Text:"), colorButton(&style_.text));
    layout->addWidget(colors);

    // One row per phase, NAMED. A palette of anonymous swatches is unusable on
    // a six-phase diagram: the whole point of changing a colour is to change a
    // particular phase's colour.
    auto* phases = new QGroupBox(tr("Phases"), this);
    auto* phaseForm = new QFormLayout(phases);
    while (style_.phaseColors.size() < phaseNames.size())
        style_.phaseColors.append(
            style_.phaseColor(static_cast<int>(style_.phaseColors.size())));
    for (int i = 0; i < phaseNames.size(); ++i)
        phaseForm->addRow(phaseNames[i], colorButton(&style_.phaseColors[i]));
    if (phaseNames.isEmpty())
        phaseForm->addRow(new QLabel(tr("<i>No phases yet.</i>"), phases));
    layout->addWidget(phases);

    auto* elements = new QGroupBox(tr("Elements"), this);
    auto* elementForm = new QFormLayout(elements);
    const auto toggle = [this, elementForm](const QString& label, bool* target) {
        auto* box = new QCheckBox(label, this);
        box->setChecked(*target);
        connect(box, &QCheckBox::toggled, this, [this, target](bool on) {
            *target = on;
            Q_EMIT styleChanged(style_);
        });
        elementForm->addRow(QString(), box);
    };
    toggle(tr("Grid lines"), &style_.showGrid);
    toggle(tr("Tie-lines (two-phase shading)"), &style_.showTieLines);
    toggle(tr("Phase-boundary points"), &style_.showBoundaryPoints);
    toggle(tr("Legend"), &style_.showLegend);

    auto* alpha = new QSpinBox(elements);
    alpha->setRange(10, 255);
    alpha->setValue(style_.tieLineAlpha);
    alpha->setToolTip(tr("Opacity of a single tie-line. Hundreds of them stack "
                         "into the two-phase field, so a dense temperature "
                         "sweep wants a lower value than a sparse one."));
    connect(alpha, &QSpinBox::valueChanged, this, [this](int value) {
        style_.tieLineAlpha = value;
        Q_EMIT styleChanged(style_);
    });
    elementForm->addRow(tr("Tie-line opacity:"), alpha);

    auto* width = new QDoubleSpinBox(elements);
    width->setRange(0.2, 6.0);
    width->setSingleStep(0.2);
    width->setValue(style_.tieLineWidth);
    connect(width, &QDoubleSpinBox::valueChanged, this, [this](double value) {
        style_.tieLineWidth = value;
        Q_EMIT styleChanged(style_);
    });
    elementForm->addRow(tr("Tie-line width:"), width);

    auto* radius = new QDoubleSpinBox(elements);
    radius->setRange(0.0, 8.0);
    radius->setSingleStep(0.2);
    radius->setValue(style_.boundaryPointRadius);
    connect(radius, &QDoubleSpinBox::valueChanged, this, [this](double value) {
        style_.boundaryPointRadius = value;
        Q_EMIT styleChanged(style_);
    });
    elementForm->addRow(tr("Boundary point size:"), radius);

    auto* font = new QSpinBox(elements);
    font->setRange(0, 32);
    font->setValue(style_.fontPointSize);
    font->setSpecialValueText(tr("default"));
    font->setToolTip(tr("Axis numbers and labels. Worth raising for a figure "
                        "that will be shrunk into a column."));
    connect(font, &QSpinBox::valueChanged, this, [this](int value) {
        style_.fontPointSize = value;
        Q_EMIT styleChanged(style_);
    });
    elementForm->addRow(tr("Font size:"), font);
    layout->addWidget(elements);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    auto* reset =
        buttons->addButton(tr("Reset"), QDialogButtonBox::ResetRole);
    connect(reset, &QPushButton::clicked, this, [this] {
        style_ = PhaseDiagramStyle{};
        Q_EMIT styleChanged(style_);
        // The controls would now be lying about the state, and rebuilding them
        // in place is more machinery than this dialog deserves — so it closes,
        // and reopening shows the defaults it just applied.
        accept();
    });
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    for (QPushButton* button : buttons->findChildren<QPushButton*>())
        button->setAutoDefault(false);
    layout->addWidget(buttons);
}

// ---------------------------------------------------------------------------
// BinaryPhaseDiagramWidget
// ---------------------------------------------------------------------------

BinaryPhaseDiagramWidget::BinaryPhaseDiagramWidget(QWidget* parent)
    : QWidget(parent)
{
    setMinimumSize(420, 320);
    setMouseTracking(true);
}

void BinaryPhaseDiagramWidget::setDiagram(core::BinaryPhaseDiagram diagram,
                                          const QString& elementA,
                                          const QString& elementB)
{
    diagram_ = std::move(diagram);
    elementA_ = elementA;
    elementB_ = elementB;
    minTemperature_ = 0.0;
    maxTemperature_ = 1.0;
    if (!diagram_.sections.empty()) {
        minTemperature_ = diagram_.sections.front().temperatureK;
        maxTemperature_ = diagram_.sections.back().temperatureK;
        if (maxTemperature_ <= minTemperature_)
            maxTemperature_ = minTemperature_ + 1.0;
    }
    update();
}

void BinaryPhaseDiagramWidget::clear()
{
    diagram_ = {};
    update();
}

void BinaryPhaseDiagramWidget::setStyle(const PhaseDiagramStyle& style)
{
    style_ = style;
    update();
}

QString BinaryPhaseDiagramWidget::toCsv() const
{
    QString out;
    QTextStream stream(&out);
    // QTextStream defaults to QLocale::c(), so this writes '.' decimals
    // whatever the user's locale — the same rule the .tdb writer follows, and
    // for the same reason: a data file is not prose.
    stream << "# Calango CALPHAD binary phase diagram\n";
    stream << "# system: " << elementA_ << "-" << elementB_ << "\n";
    stream << "# x is the mole fraction of " << elementB_ << "\n";
    stream << "# one row per two-phase field per temperature; the two "
              "compositions are the ends of the tie-line\n";
    stream << "temperature_K,x_left,x_right,phase_left,phase_right\n";
    const auto name = [this](int index) {
        return index >= 0 && index < static_cast<int>(diagram_.phaseNames.size())
            ? QString::fromStdString(
                  diagram_.phaseNames[static_cast<std::size_t>(index)])
            : QStringLiteral("?");
    };
    for (const core::BinarySection& section : diagram_.sections) {
        for (const core::BinaryTieLine& tie : section.tieLines) {
            stream << section.temperatureK << ',' << tie.xLeft << ','
                   << tie.xRight << ',' << name(tie.leftPhase) << ','
                   << name(tie.rightPhase) << '\n';
        }
    }
    return out;
}

bool BinaryPhaseDiagramWidget::exportImage(const QString& path, double scale)
{
    if (diagram_.sections.empty())
        return false;
    return renderToFile(path, QSizeF(width(), height()), scale,
                        [this](QPainter& painter, const QRectF& bounds) {
                            // interactive = false: no hover read-out in a file.
                            render(painter, bounds, false);
                        });
}

void BinaryPhaseDiagramWidget::paintEvent(QPaintEvent*)
{
    QPainter painter(this);
    render(painter, QRectF(rect()), true);
}

void BinaryPhaseDiagramWidget::render(QPainter& painter, const QRectF& bounds,
                                      bool interactive) const
{
    painter.setRenderHint(QPainter::Antialiasing);
    painter.fillRect(bounds, style_.canvas);
    if (style_.fontPointSize > 0) {
        QFont font = painter.font();
        font.setPointSize(style_.fontPointSize);
        painter.setFont(font);
    }

    plotRect_ = QRectF(bounds.left() + kMarginLeft, bounds.top() + kMarginTop,
                       std::max(10.0, bounds.width() - kMarginLeft - kMarginRight),
                       std::max(10.0, bounds.height() - kMarginTop - kMarginBottom));

    if (diagram_.sections.empty()) {
        painter.setPen(PlotPalette::placeholder);
        painter.drawText(bounds, Qt::AlignCenter,
                         tr("Choose a system and press Compute\n"
                            "to build a T–x phase diagram."));
        return;
    }

    const auto toScreen = [this](double x, double t) {
        const double fx = std::clamp(x, 0.0, 1.0);
        const double ft = (t - minTemperature_)
            / (maxTemperature_ - minTemperature_);
        return QPointF(plotRect_.left() + fx * plotRect_.width(),
                       plotRect_.bottom() - ft * plotRect_.height());
    };

    // --- Grid ------------------------------------------------------------
    const double step = tickStep(maxTemperature_ - minTemperature_, 6);
    if (style_.showGrid) {
        painter.setPen(QPen(style_.grid, 1.0));
        for (int i = 0; i <= 10; ++i) {
            const double x = i / 10.0;
            painter.drawLine(toScreen(x, minTemperature_),
                             toScreen(x, maxTemperature_));
        }
        for (double t = std::ceil(minTemperature_ / step) * step;
             t <= maxTemperature_; t += step) {
            painter.drawLine(toScreen(0.0, t), toScreen(1.0, t));
        }
    }

    // --- Two-phase fields, one horizontal tie-line per section ------------
    // Drawn at low opacity: several hundred of them stack into a shaded field
    // whose density is uniform, while any single one stays readable when the
    // user zooms the window.
    for (const core::BinarySection& section :
         style_.showTieLines ? diagram_.sections
                             : std::vector<core::BinarySection>{}) {
        for (const core::BinaryTieLine& tie : section.tieLines) {
            QColor color = style_.phaseColor(tie.leftPhase);
            if (tie.leftPhase != tie.rightPhase) {
                // A field between two different phases is shaded with the
                // blend of both, so the eye can tell it from a miscibility gap
                // (one phase, one colour) without reading a legend.
                const QColor right = style_.phaseColor(tie.rightPhase);
                color = QColor((color.red() + right.red()) / 2,
                               (color.green() + right.green()) / 2,
                               (color.blue() + right.blue()) / 2);
            }
            color.setAlpha(style_.tieLineAlpha);
            painter.setPen(QPen(color, style_.tieLineWidth));
            painter.drawLine(toScreen(tie.xLeft, section.temperatureK),
                             toScreen(tie.xRight, section.temperatureK));
        }
    }

    // --- Phase boundaries: the tie-line ends, as points --------------------
    painter.setPen(Qt::NoPen);
    if (style_.showBoundaryPoints && style_.boundaryPointRadius > 0.0) {
        const double r = style_.boundaryPointRadius;
        for (const core::BinarySection& section : diagram_.sections) {
            for (const core::BinaryTieLine& tie : section.tieLines) {
                painter.setBrush(style_.phaseColor(tie.leftPhase));
                painter.drawEllipse(toScreen(tie.xLeft, section.temperatureK),
                                    r, r);
                painter.setBrush(style_.phaseColor(tie.rightPhase));
                painter.drawEllipse(toScreen(tie.xRight, section.temperatureK),
                                    r, r);
            }
        }
    }

    // --- Frame and labels --------------------------------------------------
    painter.setBrush(Qt::NoBrush);
    painter.setPen(QPen(style_.spine, 1.4));
    painter.drawRect(plotRect_);

    painter.setPen(style_.text);
    const QFontMetricsF metrics(painter.font());
    for (int i = 0; i <= 10; ++i) {
        const double x = i / 10.0;
        const QString label = QString::number(x, 'g', 2);
        const QPointF at = toScreen(x, minTemperature_);
        painter.drawText(QPointF(at.x() - metrics.horizontalAdvance(label) / 2.0,
                                 at.y() + metrics.height() + 2.0),
                         label);
    }
    for (double t = std::ceil(minTemperature_ / step) * step;
         t <= maxTemperature_; t += step) {
        const QString label = QString::number(t, 'f', 0);
        const QPointF at = toScreen(0.0, t);
        painter.drawText(
            QPointF(at.x() - metrics.horizontalAdvance(label) - 6.0,
                    at.y() + metrics.height() / 3.0),
            label);
    }

    painter.setPen(style_.text);
    const QString xLabel = tr("mole fraction %1").arg(elementB_);
    painter.drawText(
        QPointF(plotRect_.center().x() - metrics.horizontalAdvance(xLabel) / 2.0,
                bounds.bottom() - 6.0),
        xLabel);
    painter.save();
    painter.translate(bounds.left() + 14.0, plotRect_.center().y());
    painter.rotate(-90.0);
    const QString yLabel = tr("temperature (K)");
    painter.drawText(QPointF(-metrics.horizontalAdvance(yLabel) / 2.0, 0.0),
                     yLabel);
    painter.restore();

    // --- Legend ------------------------------------------------------------
    if (style_.showLegend) {
        // Width from the longest name, not a fixed 130 px: a database with
        // phases called LIQUID_RENB overflows any constant chosen against
        // LIQUID.
        double legendWidth = 0.0;
        for (const std::string& phaseName : diagram_.phaseNames)
            legendWidth = std::max(
                legendWidth,
                metrics.horizontalAdvance(QString::fromStdString(phaseName)));
        double legendY = plotRect_.top() + 6.0;
        const double legendX = plotRect_.right() - legendWidth - 24.0;
        for (std::size_t i = 0; i < diagram_.phaseNames.size(); ++i) {
            painter.setPen(Qt::NoPen);
            painter.setBrush(style_.phaseColor(static_cast<int>(i)));
            painter.drawRect(QRectF(legendX, legendY, 10.0, 10.0));
            painter.setPen(style_.text);
            painter.drawText(QPointF(legendX + 15.0, legendY + 9.0),
                             QString::fromStdString(diagram_.phaseNames[i]));
            legendY += metrics.height() + 2.0;
        }
    }

    // --- Hover read-out ----------------------------------------------------
    if (interactive && plotRect_.contains(hover_)) {
        const double x = (hover_.x() - plotRect_.left()) / plotRect_.width();
        const double t = minTemperature_
            + (plotRect_.bottom() - hover_.y()) / plotRect_.height()
                * (maxTemperature_ - minTemperature_);
        // Nearest computed section, because the assemblage is only known on
        // the temperatures that were actually solved.
        const core::BinarySection* nearest = nullptr;
        double bestDistance = std::numeric_limits<double>::max();
        for (const core::BinarySection& section : diagram_.sections) {
            const double distance = std::fabs(section.temperatureK - t);
            if (distance < bestDistance) {
                bestDistance = distance;
                nearest = &section;
            }
        }
        QStringList names;
        if (nearest) {
            for (const int index : core::binaryAssemblageAt(*nearest, x)) {
                if (index >= 0
                    && index < static_cast<int>(diagram_.phaseNames.size()))
                    names << QString::fromStdString(
                        diagram_.phaseNames[static_cast<std::size_t>(index)]);
            }
        }
        const QString text = tr("x = %1   T = %2 K\n%3")
                                 .arg(x, 0, 'f', 3)
                                 .arg(t, 0, 'f', 0)
                                 .arg(names.isEmpty()
                                          ? tr("(no stable phase)")
                                          : names.join(QStringLiteral(" + ")));
        const QRectF box =
            metrics.boundingRect(QRectF(0, 0, 260, 60), Qt::AlignLeft, text)
                .adjusted(-5, -3, 5, 3)
                .translated(hover_ + QPointF(12.0, 12.0));
        painter.setPen(Qt::NoPen);
        painter.setBrush(PlotPalette::readoutFill);
        painter.drawRect(box);
        painter.setPen(style_.text);
        painter.drawText(box.adjusted(5, 3, -5, -3), Qt::AlignLeft, text);
    }
}

void BinaryPhaseDiagramWidget::mouseMoveEvent(QMouseEvent* event)
{
    hover_ = event->position();
    update();
}

void BinaryPhaseDiagramWidget::leaveEvent(QEvent*)
{
    hover_ = QPointF(-1.0, -1.0);
    update();
}

// ---------------------------------------------------------------------------
// TernarySectionWidget
// ---------------------------------------------------------------------------

TernarySectionWidget::TernarySectionWidget(QWidget* parent)
    : QWidget(parent)
{
    setMinimumSize(420, 380);
}

void TernarySectionWidget::setSection(core::TernaryIsothermalSection section,
                                      const QStringList& elements)
{
    section_ = std::move(section);
    elements_ = elements;
    update();
}

void TernarySectionWidget::clear()
{
    section_ = {};
    update();
}

QPointF TernarySectionWidget::project(double xB, double xC) const
{
    // Apex = pure A, bottom-right = pure B, bottom-left = pure C.
    const QPointF apex(plotRect_.center().x(), plotRect_.top());
    const QPointF right(plotRect_.right(), plotRect_.bottom());
    const QPointF left(plotRect_.left(), plotRect_.bottom());
    const double xA = 1.0 - xB - xC;
    return QPointF(xA * apex.x() + xB * right.x() + xC * left.x(),
                   xA * apex.y() + xB * right.y() + xC * left.y());
}

void TernarySectionWidget::setStyle(const PhaseDiagramStyle& style)
{
    style_ = style;
    update();
}

QString TernarySectionWidget::toCsv() const
{
    QString out;
    QTextStream stream(&out);
    stream << "# Calango CALPHAD ternary isothermal section\n";
    stream << "# system: " << elements_.join(QLatin1Char('-')) << "\n";
    stream << "# temperature_K: " << section_.temperatureK << "\n";
    stream << "# one row per lower-hull triangle; the number of DISTINCT "
              "phases among its\n";
    stream << "# three corners is the number in equilibrium there: 1 "
              "single-phase, 2 tie-line, 3 three-phase\n";
    const QString a = elements_.value(0, QStringLiteral("A"));
    const QString b = elements_.value(1, QStringLiteral("B"));
    const QString c = elements_.value(2, QStringLiteral("C"));
    stream << "x_" << b << "_1,x_" << c << "_1,phase_1,"
           << "x_" << b << "_2,x_" << c << "_2,phase_2,"
           << "x_" << b << "_3,x_" << c << "_3,phase_3,n_phases\n";
    for (const core::TernaryFacet& facet : section_.facets) {
        QStringList distinct;
        QString row;
        QTextStream cells(&row);
        bool usable = true;
        for (const int vertex : facet.vertex) {
            if (vertex < 0 || vertex >= static_cast<int>(section_.points.size())) {
                usable = false;
                break;
            }
            const core::TernaryPoint& point =
                section_.points[static_cast<std::size_t>(vertex)];
            const QString phaseName =
                point.phase >= 0
                    && point.phase < static_cast<int>(section_.phaseNames.size())
                ? QString::fromStdString(section_.phaseNames[
                      static_cast<std::size_t>(point.phase)])
                : QStringLiteral("?");
            cells << point.xB << ',' << point.xC << ',' << phaseName << ',';
            if (!distinct.contains(phaseName))
                distinct << phaseName;
        }
        if (!usable)
            continue;
        stream << row << distinct.size() << '\n';
    }
    // The A corner is implied by x_A = 1 - x_B - x_C and is not written: a
    // redundant column in a data file is one more thing that can disagree
    // with the others after somebody edits it.
    (void)a;
    return out;
}

QStringList TernarySectionWidget::phaseNames() const
{
    QStringList names;
    for (const std::string& phaseName : section_.phaseNames)
        names << QString::fromStdString(phaseName);
    return names;
}

bool TernarySectionWidget::exportImage(const QString& path, double scale)
{
    if (!section_.ok)
        return false;
    return renderToFile(path, QSizeF(width(), height()), scale,
                        [this](QPainter& painter, const QRectF& bounds) {
                            render(painter, bounds);
                        });
}

void TernarySectionWidget::paintEvent(QPaintEvent*)
{
    QPainter painter(this);
    render(painter, QRectF(rect()));
}

void TernarySectionWidget::render(QPainter& painter, const QRectF& bounds) const
{
    painter.setRenderHint(QPainter::Antialiasing);
    painter.fillRect(bounds, style_.canvas);
    if (style_.fontPointSize > 0) {
        QFont font = painter.font();
        font.setPointSize(style_.fontPointSize);
        painter.setFont(font);
    }

    const double side = std::min(bounds.width() - 60.0, bounds.height() - 60.0);
    plotRect_ = QRectF(bounds.left() + (bounds.width() - side) / 2.0,
                       bounds.top() + 24.0, std::max(10.0, side),
                       std::max(10.0, side * 0.866));

    if (!section_.ok) {
        painter.setPen(PlotPalette::placeholder);
        painter.drawText(bounds, Qt::AlignCenter,
                         section_.note.empty()
                             ? tr("Select three elements and press Compute\n"
                                  "for an isothermal section.")
                             : QString::fromStdString(section_.note));
        return;
    }

    // --- Facets, shaded by the number of phases at their corners -----------
    for (const core::TernaryFacet& facet : section_.facets) {
        std::vector<int> distinct;
        QPolygonF triangle;
        for (const int vertex : facet.vertex) {
            if (vertex < 0
                || vertex >= static_cast<int>(section_.points.size()))
                continue;
            const core::TernaryPoint& point =
                section_.points[static_cast<std::size_t>(vertex)];
            triangle << project(point.xB, point.xC);
            if (std::find(distinct.begin(), distinct.end(), point.phase)
                == distinct.end())
                distinct.push_back(point.phase);
        }
        if (triangle.size() != 3)
            continue;
        QColor fill = style_.phaseColor(distinct.empty() ? 0 : distinct.front());
        // One phase: solid-ish. Two: paler, because a tie-line region is not a
        // field of its own substance. Three: the darkest, since a three-phase
        // triangle is a single fixed assemblage and the strongest statement on
        // the diagram.
        fill.setAlpha(distinct.size() == 1 ? 70
                                           : (distinct.size() == 2 ? 40 : 150));
        painter.setPen(Qt::NoPen);
        painter.setBrush(fill);
        painter.drawPolygon(triangle);

        if (distinct.size() == 2 && style_.showTieLines) {
            // The tie-line: the edge joining the two DIFFERENT phases. Drawing
            // all three edges would draw two lines inside one phase as well.
            painter.setPen(QPen(style_.spine, 0.6));
            for (int i = 0; i < 3; ++i) {
                const int a = facet.vertex[i];
                const int b = facet.vertex[(i + 1) % 3];
                if (a < 0 || b < 0)
                    continue;
                const core::TernaryPoint& pa =
                    section_.points[static_cast<std::size_t>(a)];
                const core::TernaryPoint& pb =
                    section_.points[static_cast<std::size_t>(b)];
                if (pa.phase == pb.phase)
                    continue;
                painter.drawLine(project(pa.xB, pa.xC), project(pb.xB, pb.xC));
            }
        }
    }

    // --- The triangle itself ------------------------------------------------
    painter.setBrush(Qt::NoBrush);
    painter.setPen(QPen(style_.spine, 1.4));
    QPolygonF frame;
    frame << project(0.0, 0.0) << project(1.0, 0.0) << project(0.0, 1.0);
    painter.drawPolygon(frame);

    painter.setPen(style_.text);
    const QFontMetricsF metrics(painter.font());
    const QString a = elements_.value(0, QStringLiteral("A"));
    const QString b = elements_.value(1, QStringLiteral("B"));
    const QString c = elements_.value(2, QStringLiteral("C"));
    const QPointF apex = project(0.0, 0.0);
    painter.drawText(
        QPointF(apex.x() - metrics.horizontalAdvance(a) / 2.0, apex.y() - 6.0),
        a);
    const QPointF right = project(1.0, 0.0);
    painter.drawText(QPointF(right.x() - metrics.horizontalAdvance(b) / 2.0,
                             right.y() + metrics.height() + 2.0),
                     b);
    const QPointF left = project(0.0, 1.0);
    painter.drawText(QPointF(left.x() - metrics.horizontalAdvance(c) / 2.0,
                             left.y() + metrics.height() + 2.0),
                     c);
    painter.drawText(
        QPointF(plotRect_.left(), plotRect_.bottom() + 2.4 * metrics.height()),
        tr("isothermal section at %1 K").arg(section_.temperatureK, 0, 'f', 0));
}

// ---------------------------------------------------------------------------
// PhaseDiagramWindow
// ---------------------------------------------------------------------------

PhaseDiagramWindow::PhaseDiagramWindow(const core::TdbDatabase& database,
                                       const QStringList& elements,
                                       const QStringList& phases,
                                       QWidget* parent)
    : QDialog(parent), database_(database), elements_(elements), phases_(phases)
{
    setWindowTitle(tr("CALPHAD — Phase Diagram (%1)")
                       .arg(elements.join(QStringLiteral("-"))));
    resize(820, 680);

    auto* layout = new QVBoxLayout(this);

    auto* controls = new QGroupBox(tr("Range"), this);
    auto* controlsLayout = new QHBoxLayout(controls);
    auto* binaryForm = new QFormLayout;
    minTemperature_ = new QDoubleSpinBox(controls);
    minTemperature_->setRange(1.0, 10000.0);
    minTemperature_->setValue(300.0);
    minTemperature_->setSuffix(tr(" K"));
    binaryForm->addRow(tr("Lowest temperature:"), minTemperature_);
    maxTemperature_ = new QDoubleSpinBox(controls);
    maxTemperature_->setRange(1.0, 10000.0);
    maxTemperature_->setValue(2000.0);
    maxTemperature_->setSuffix(tr(" K"));
    binaryForm->addRow(tr("Highest temperature:"), maxTemperature_);
    temperatureSteps_ = new QSpinBox(controls);
    temperatureSteps_->setRange(5, 1000);
    temperatureSteps_->setValue(120);
    temperatureSteps_->setToolTip(
        tr("One equilibrium per step, so this is both the vertical resolution "
           "and the cost.\n\n"
           "It is the limit that bites first: a solidus/liquidus lens only a "
           "few tens of kelvin thick — which is what a nearly degenerate pair "
           "of phases gives, and the Nb-Re bcc/liquid pair is one — is sampled "
           "at one or two temperatures and reads as a congruent transition. "
           "Raise this if a phase boundary looks like isolated dots."));
    binaryForm->addRow(tr("Temperature steps:"), temperatureSteps_);
    compositionSteps_ = new QSpinBox(controls);
    compositionSteps_->setRange(21, 4001);
    compositionSteps_->setValue(401);
    compositionSteps_->setToolTip(
        tr("Every phase boundary is located to one composition step: the "
           "construction is a convex hull over sampled points, not a root "
           "find on the tangency condition."));
    binaryForm->addRow(tr("Composition steps:"), compositionSteps_);
    controlsLayout->addLayout(binaryForm);

    auto* ternaryForm = new QFormLayout;
    sectionTemperature_ = new QDoubleSpinBox(controls);
    sectionTemperature_->setRange(1.0, 10000.0);
    sectionTemperature_->setValue(1000.0);
    sectionTemperature_->setSuffix(tr(" K"));
    ternaryForm->addRow(tr("Section temperature:"), sectionTemperature_);
    ternarySteps_ = new QSpinBox(controls);
    ternarySteps_->setRange(6, 120);
    ternarySteps_->setValue(36);
    ternaryForm->addRow(tr("Triangle grid:"), ternarySteps_);
    controlsLayout->addLayout(ternaryForm);
    controlsLayout->addStretch(1);

    auto* compute = new QPushButton(tr("Compute"), controls);
    connect(compute, &QPushButton::clicked, this,
            &PhaseDiagramWindow::recompute);
    controlsLayout->addWidget(compute);
    layout->addWidget(controls);

    tabs_ = new QTabWidget(this);
    binary_ = new BinaryPhaseDiagramWidget(tabs_);
    tabs_->addTab(binary_, tr("Binary T–x"));
    ternary_ = new TernarySectionWidget(tabs_);
    tabs_->addTab(ternary_, tr("Ternary section"));
    layout->addWidget(tabs_, 1);

    status_ = new QLabel(this);
    status_->setWordWrap(true);
    status_->setTextFormat(Qt::RichText);
    layout->addWidget(status_);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    styleButton_ = buttons->addButton(tr("Customize Appearance…"),
                                      QDialogButtonBox::ActionRole);
    connect(styleButton_, &QPushButton::clicked, this, [this] {
        // The names of the phases actually MODELLED, not the ones ticked in
        // the database dialog: a colour row for a phase the evaluator refused
        // would do nothing, and the user would reasonably conclude the control
        // was broken rather than the phase absent.
        QStringList names;
        for (const std::string& phaseName : binary_->diagram().phaseNames)
            names << QString::fromStdString(phaseName);
        if (names.isEmpty())
            names = ternary_->phaseNames();
        auto* dialog = new PhaseDiagramStyleDialog(style_, names, this);
        dialog->setAttribute(Qt::WA_DeleteOnClose);
        // Live: the plot behind the dialog follows every edit, because
        // choosing a colour against a static preview is choosing blind.
        connect(dialog, &PhaseDiagramStyleDialog::styleChanged, this,
                &PhaseDiagramWindow::setStyle);
        dialog->show();
    });

    exportButton_ = buttons->addButton(tr("Export Data…"),
                                       QDialogButtonBox::ActionRole);
    exportButton_->setToolTip(
        tr("The tie-lines of the T–x diagram, or the lower-hull triangles of "
           "the ternary section — the data the picture is drawn from."));
    connect(exportButton_, &QPushButton::clicked, this, [this] {
        const bool isTernary = tabs_->currentWidget() == ternary_;
        const QString suggestion =
            QStringLiteral("%1_%2.csv")
                .arg(elements_.join(QLatin1Char('-')).toLower(),
                     isTernary ? QStringLiteral("section")
                               : QStringLiteral("phase_diagram"));
        const QString path = QFileDialog::getSaveFileName(
            this, tr("Export Phase Diagram Data"), suggestion,
            tr("CSV files (*.csv);;All files (*)"));
        if (path.isEmpty())
            return;
        if (!exportCsv(path))
            QMessageBox::warning(this, tr("Export Data"),
                                 tr("There is nothing to export yet — press "
                                    "Compute first."));
    });

    imageButton_ = buttons->addButton(tr("Export Image…"),
                                      QDialogButtonBox::ActionRole);
    imageButton_->setToolTip(
        tr("PNG at 3x for print, or SVG vector art for a figure."));
    connect(imageButton_, &QPushButton::clicked, this, [this] {
        const bool isTernary = tabs_->currentWidget() == ternary_;
        const QString suggestion =
            QStringLiteral("%1_%2.png")
                .arg(elements_.join(QLatin1Char('-')).toLower(),
                     isTernary ? QStringLiteral("section")
                               : QStringLiteral("phase_diagram"));
        const QString path = QFileDialog::getSaveFileName(
            this, tr("Export Plot Image"), suggestion,
            tr("PNG image (*.png);;SVG vector (*.svg)"));
        if (path.isEmpty())
            return;
        if (!exportImage(path))
            QMessageBox::warning(this, tr("Export Image"),
                                 tr("There is nothing to export yet — press "
                                    "Compute first."));
    });

    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    for (QPushButton* button : buttons->findChildren<QPushButton*>())
        button->setAutoDefault(false);
    layout->addWidget(buttons);

    // The tabs are built before this runs, which is the ordering that matters:
    // recompute() writes into both widgets, and a wizard in this project once
    // crashed by refreshing a preview from a constructor before the page it
    // drew into existed.
    recompute();
}

QString PhaseDiagramWindow::statusText() const
{
    return status_text_;
}

const core::BinaryPhaseDiagram& PhaseDiagramWindow::binaryDiagram() const
{
    return binary_->diagram();
}

void PhaseDiagramWindow::setTemperatureRange(double minimumK, double maximumK)
{
    // The spin boxes are the single source of truth for the range, so this
    // writes through them rather than beside them — otherwise the controls
    // would show one window and the plot another.
    minTemperature_->setValue(std::min(minimumK, maximumK));
    maxTemperature_->setValue(std::max(minimumK, maximumK));
    recompute();
}

void PhaseDiagramWindow::setStyle(const PhaseDiagramStyle& style)
{
    style_ = style;
    // Both plots, always — a phase keeps its colour between the T-x diagram
    // and the isothermal section of the same system, and it would not if the
    // two carried separate styles.
    binary_->setStyle(style);
    ternary_->setStyle(style);
}

bool PhaseDiagramWindow::exportCsv(const QString& path) const
{
    const bool isTernary = tabs_->currentWidget() == ternary_;
    if (isTernary ? !ternary_->hasData() : !binary_->hasData())
        return false;
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text))
        return false;
    QTextStream(&file) << (isTernary ? ternary_->toCsv() : binary_->toCsv());
    return true;
}

bool PhaseDiagramWindow::exportImage(const QString& path, double scale) const
{
    return tabs_->currentWidget() == ternary_
        ? ternary_->exportImage(path, scale)
        : binary_->exportImage(path, scale);
}

std::vector<core::GibbsPhase>
PhaseDiagramWindow::buildBinaryPhases(QStringList* skipped) const
{
    std::vector<core::GibbsPhase> out;
    if (elements_.size() != 2)
        return out;
    const std::string a = elements_[0].toStdString();
    const std::string b = elements_[1].toStdString();
    std::vector<std::string> selection{a, b};

    for (const QString& phaseName : phases_) {
        const std::string name = phaseName.toStdString();
        // Read at one temperature first, only to decide whether the phase is
        // modellable at all. The model itself is rebuilt at every temperature
        // inside the closure, because a TDB parameter is a function of T and
        // caching one temperature's coefficients would draw a diagram of a
        // system that does not exist.
        const core::TdbSubstitutionalPhase probe = core::tdbSubstitutionalPhase(
            database_, name, selection, 1000.0);
        if (!probe.ok) {
            if (skipped)
                *skipped << QString::fromStdString(probe.reason);
            continue;
        }
        if (probe.magneticIgnored && skipped) {
            *skipped << tr("%1 carries magnetic (TC/BMAGN) parameters, which "
                           "this evaluator does not include; its boundaries "
                           "are shifted by the magnetic contribution.")
                            .arg(phaseName);
        }

        core::GibbsPhase phase;
        phase.name = name;
        // THE DIAGRAM'S AXIS AND THE PHASE'S OWN ORDER ARE NOT THE SAME THING.
        // The diagram plots the mole fraction of the user's SECOND selected
        // element, which follows the database's declaration order; a phase's
        // constituents are sorted ALPHABETICALLY, because that is the order
        // TDB writes its interaction parameters in. For Fe-Cr the two
        // disagree, and assuming they agree mirrors the phase's Gibbs curve
        // about x = 1/2 — a plausible-looking diagram that is the wrong way
        // round.
        const std::string axisElement = elements_[1].toStdString();
        const bool reversed = probe.constituents.size() > 1
            && probe.constituents[1] != axisElement;
        // The composition range comes from the phase's own sublattice model,
        // not from an assumption that every phase spans the axis. Sigma in
        // Nb-Re is (Re)10(Nb)4(Re,Nb)16 and reaches only x_Nb in [2/15, 2/3];
        // drawing it across the whole diagram would invent solubility the
        // database does not describe.
        phase.minMoleFraction = reversed ? 1.0 - probe.maxMoleFraction
                                         : probe.minMoleFraction;
        phase.maxMoleFraction = reversed ? 1.0 - probe.minMoleFraction
                                         : probe.maxMoleFraction;
        const core::TdbDatabase* database = &database_;
        // One model per TEMPERATURE, cached. Rebuilding it per sampled point
        // would re-evaluate every PARAMETER expression in the database 400
        // times per isotherm — on a full SGTE file that is tens of millions of
        // expression evaluations for one diagram. The composition sweep runs
        // inside a fixed temperature, so a single-entry cache keyed on T hits
        // on all but the first sample of each isotherm.
        struct ModelCache {
            double temperature = std::numeric_limits<double>::quiet_NaN();
            core::TdbSubstitutionalPhase model;
        };
        auto cache = std::make_shared<ModelCache>();
        phase.gibbs = [database, name, selection, reversed,
                       cache](double x, double t) -> double {
            if (!(cache->temperature == t)) {
                cache->model =
                    core::tdbSubstitutionalPhase(*database, name, selection, t);
                cache->temperature = t;
            }
            const core::TdbSubstitutionalPhase& model = cache->model;
            if (!model.ok)
                return std::numeric_limits<double>::quiet_NaN();
            // The model does the rest: the site-fraction map, the site-ratio-
            // weighted ideal entropy and its own composition limits. All this
            // has to supply is the composition in the model's own direction.
            return model.gibbsAtMoleFraction(reversed ? 1.0 - x : x);
        };
        out.push_back(std::move(phase));
    }
    return out;
}

std::vector<core::TernaryGibbsPhase>
PhaseDiagramWindow::buildTernaryPhases(double temperatureK,
                                       QStringList* skipped) const
{
    std::vector<core::TernaryGibbsPhase> out;
    if (elements_.size() != 3)
        return out;
    std::vector<std::string> selection;
    for (const QString& element : elements_)
        selection.push_back(element.toStdString());

    for (const QString& phaseName : phases_) {
        const std::string name = phaseName.toStdString();
        const core::TdbSubstitutionalPhase model = core::tdbSubstitutionalPhase(
            database_, name, selection, temperatureK);
        if (!model.ok) {
            if (skipped)
                *skipped << QString::fromStdString(model.reason);
            continue;
        }
        core::TernaryGibbsPhase phase;
        phase.name = name;
        const std::vector<std::string> constituents = model.constituents;
        const std::vector<double> endmembers = model.endmemberJPerMol;
        const std::vector<std::vector<std::vector<double>>> interaction =
            model.interaction;
        // Fraction of each of the user's three elements inside this phase's
        // own constituent list, or -1 when the phase does not contain it.
        std::vector<int> slot;
        for (const QString& element : elements_) {
            const auto it = std::find(constituents.begin(), constituents.end(),
                                      element.toStdString());
            slot.push_back(it == constituents.end()
                               ? -1
                               : static_cast<int>(it - constituents.begin()));
        }
        phase.gibbs = [constituents, endmembers, interaction,
                       slot](double xB, double xC, double t) -> double {
            const double xA = 1.0 - xB - xC;
            if (xA < -1e-12)
                return std::numeric_limits<double>::quiet_NaN();
            std::vector<double> fraction(constituents.size(), 0.0);
            const double supplied[3] = {std::max(0.0, xA), xB, xC};
            double inside = 0.0;
            for (int i = 0; i < 3; ++i) {
                if (slot[static_cast<std::size_t>(i)] < 0) {
                    // The phase cannot dissolve this element at all, so it
                    // does not exist at compositions containing it.
                    if (supplied[i] > 1e-12)
                        return std::numeric_limits<double>::quiet_NaN();
                    continue;
                }
                fraction[static_cast<std::size_t>(
                    slot[static_cast<std::size_t>(i)])] = supplied[i];
                inside += supplied[i];
            }
            if (inside <= 0.0)
                return std::numeric_limits<double>::quiet_NaN();

            double value = 0.0;
            for (std::size_t i = 0; i < fraction.size(); ++i) {
                value += fraction[i] * endmembers[i];
                if (fraction[i] > 0.0)
                    value += core::kGasConstantJPerMolK * t * fraction[i]
                        * std::log(fraction[i]);
            }
            // Muggianu-free sub-binary sum: for a substitutional ternary the
            // excess energy is the sum of the binary Redlich-Kister terms
            // evaluated on the ternary site fractions. That is the standard
            // extrapolation and it is exact when no ternary interaction
            // parameter is assessed — which is the case for every database
            // this evaluator accepts, since it reads only binary L terms.
            for (std::size_t i = 0; i < fraction.size(); ++i) {
                for (std::size_t j = i + 1; j < fraction.size(); ++j) {
                    const std::vector<double>& series = interaction[i][j];
                    double power = 1.0;
                    double sum = 0.0;
                    for (const double coefficient : series) {
                        sum += coefficient * power;
                        power *= fraction[i] - fraction[j];
                    }
                    value += fraction[i] * fraction[j] * sum;
                }
            }
            return value;
        };
        out.push_back(std::move(phase));
    }
    return out;
}

void PhaseDiagramWindow::recompute()
{
    QStringList notes;
    if (elements_.size() == 2) {
        core::BinaryPhaseDiagramOptions options;
        options.minTemperatureK = minTemperature_->value();
        options.maxTemperatureK = std::max(minTemperature_->value() + 1.0,
                                           maxTemperature_->value());
        options.temperatureSteps = temperatureSteps_->value();
        options.compositionSteps = compositionSteps_->value();
        const std::vector<core::GibbsPhase> phases = buildBinaryPhases(&notes);
        if (phases.empty()) {
            binary_->clear();
        } else {
            binary_->setDiagram(
                core::computeBinaryPhaseDiagram(phases, options), elements_[0],
                elements_[1]);
        }
        ternary_->clear();
        notes.prepend(tr("<b>%1 phase(s)</b> modelled as substitutional "
                         "solutions out of %2 selected.")
                          .arg(phases.size())
                          .arg(phases_.size()));
    } else if (elements_.size() == 3) {
        core::TernarySectionOptions options;
        options.temperatureK = sectionTemperature_->value();
        options.gridSteps = ternarySteps_->value();
        const std::vector<core::TernaryGibbsPhase> phases =
            buildTernaryPhases(options.temperatureK, &notes);
        binary_->clear();
        ternary_->setSection(
            core::computeTernaryIsothermalSection(phases, options), elements_);
        tabs_->setCurrentWidget(ternary_);
        notes.prepend(tr("<b>%1 phase(s)</b> modelled as substitutional "
                         "solutions out of %2 selected.")
                          .arg(phases.size())
                          .arg(phases_.size()));
    } else {
        binary_->clear();
        ternary_->clear();
        notes << tr("A phase diagram needs exactly two elements (a T–x "
                    "diagram) or three (an isothermal section). %n element(s) "
                    "are selected.",
                    nullptr, static_cast<int>(elements_.size()));
    }
    status_text_ = notes.join(QStringLiteral("<br>"));
    status_->setText(status_text_);
}

} // namespace calango::gui
