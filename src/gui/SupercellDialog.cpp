#include "gui/SupercellDialog.hpp"

#include <QColor>
#include <QDialogButtonBox>
#include <QFont>
#include <QFontMetrics>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QPainterPath>
#include <QPen>
#include <QPointF>
#include <QPushButton>
#include <QSpinBox>
#include <QVBoxLayout>

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

namespace calango::gui {

namespace {

// Colours shared by the preview: original cell in blue, supercell in orange.
const QColor kOriginalColor(60, 120, 220);
const QColor kSuperColor(230, 120, 30);

// Combine the base cell rows into the i-th transformed lattice vector:
// new_i = Σ_j P[i][j] · cell[j].
core::Vec3 transformedVector(const std::array<core::Vec3, 3>& cell,
                             const int p[3][3], int i)
{
    return cell[0] * static_cast<double>(p[i][0])
         + cell[1] * static_cast<double>(p[i][1])
         + cell[2] * static_cast<double>(p[i][2]);
}

} // namespace

// ---------------------------------------------------------------------------
// LatticePreviewWidget
// ---------------------------------------------------------------------------

LatticePreviewWidget::LatticePreviewWidget(QWidget* parent) : QWidget(parent)
{
    setMinimumSize(220, 220);
}

void LatticePreviewWidget::setCell(const std::array<core::Vec3, 3>& cell, bool defined)
{
    cell_ = cell;
    defined_ = defined;
    update();
}

void LatticePreviewWidget::setMatrix(const int p[3][3])
{
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 3; ++j)
            p_[i][j] = p[i][j];
    update();
}

QPointF LatticePreviewWidget::project(const core::Vec3& v)
{
    // Classic isometric axes: x → right-down, y → left-down, z → up.
    // cos30 ≈ 0.866, sin30 = 0.5. Screen y grows downward, so a larger z
    // (subtracted) moves a point up on screen.
    constexpr double c30 = 0.8660254037844387;
    const double px = (v.x - v.y) * c30;
    const double py = (v.x + v.y) * 0.5 - v.z;
    return {px, py};
}

void LatticePreviewWidget::paintEvent(QPaintEvent* /*event*/)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.fillRect(rect(), palette().base());

    if (!defined_) {
        painter.setPen(palette().color(QPalette::Disabled, QPalette::Text));
        painter.drawText(rect(), Qt::AlignCenter,
                         tr("No unit cell defined —\nthe preview needs a periodic cell."));
        return;
    }

    // Gather every point we intend to draw so the view auto-fits: the origin,
    // the three original vectors, and the three transformed vectors.
    std::array<core::Vec3, 3> superVecs{};
    for (int i = 0; i < 3; ++i)
        superVecs[i] = transformedVector(cell_, p_, i);

    std::array<QPointF, 7> pts{project({0, 0, 0})};
    for (int i = 0; i < 3; ++i)
        pts[1 + i] = project(cell_[i]);
    for (int i = 0; i < 3; ++i)
        pts[4 + i] = project(superVecs[i]);

    double minX = std::numeric_limits<double>::max();
    double minY = std::numeric_limits<double>::max();
    double maxX = std::numeric_limits<double>::lowest();
    double maxY = std::numeric_limits<double>::lowest();
    for (const QPointF& p : pts) {
        minX = std::min(minX, p.x());
        minY = std::min(minY, p.y());
        maxX = std::max(maxX, p.x());
        maxY = std::max(maxY, p.y());
    }

    // Reserve room for the outward label pills and the two-line legend so
    // nothing is clipped or drawn on top of the vectors.
    QFont labelFont = font();
    labelFont.setBold(true);
    const QFontMetrics fm(labelFont);
    const int legendHeight = 2 * fm.height() + 10;
    const double margin = fm.height() + 26.0;
    const double spanX = std::max(maxX - minX, 1e-6);
    const double spanY = std::max(maxY - minY, 1e-6);
    const double scale =
        std::min((width() - 2 * margin) / spanX,
                 (height() - 2 * margin - legendHeight) / spanY);
    const QPointF center((minX + maxX) / 2.0, (minY + maxY) / 2.0);
    // Bias the drawing upward so the legend band at the bottom stays clear.
    const QPointF screenCenter(width() / 2.0,
                               (height() - legendHeight) / 2.0);

    auto toScreen = [&](const core::Vec3& v) {
        const QPointF q = project(v);
        return QPointF(screenCenter.x() + (q.x() - center.x()) * scale,
                       screenCenter.y() + (q.y() - center.y()) * scale);
    };

    const QPointF origin = toScreen({0, 0, 0});
    const QColor pillBg = palette().color(QPalette::Base);

    // Draws a text pill centered at `at`, with a filled rounded background so
    // labels stay legible where they cross a vector or another label.
    auto drawPill = [&](const QPointF& at, const QColor& color, const QString& text) {
        const QRectF textRect = fm.boundingRect(text);
        QRectF pill(0, 0, textRect.width() + 10, fm.height() + 4);
        pill.moveCenter(at);
        // Keep the pill fully inside the canvas.
        const double x = std::clamp(pill.left(), 2.0, width() - pill.width() - 2.0);
        const double y = std::clamp(pill.top(), 2.0,
                                    height() - legendHeight - pill.height() - 2.0);
        pill.moveTopLeft({x, y});
        painter.setBrush(pillBg);
        painter.setPen(QPen(color, 1.0));
        painter.drawRoundedRect(pill, 4, 4);
        painter.setFont(labelFont);
        painter.setPen(color);
        painter.drawText(pill, Qt::AlignCenter, text);
    };

    auto drawArrow = [&](const core::Vec3& v, const QColor& color, const QString& label,
                         bool dashed) {
        const QPointF tip = toScreen(v);
        QPen pen(color, dashed ? 1.6 : 2.4);
        if (dashed)
            pen.setStyle(Qt::DashLine);
        painter.setPen(pen);
        painter.setBrush(Qt::NoBrush);
        painter.drawLine(origin, tip);

        const double dx = tip.x() - origin.x();
        const double dy = tip.y() - origin.y();
        const double len = std::hypot(dx, dy);
        if (len <= 4.0)
            return; // near-zero vector: direction (and arrowhead/label) undefined
        const double ang = std::atan2(dy, dx);
        const double head = 9.0;
        const double spread = 0.42;
        QPainterPath path;
        path.moveTo(tip);
        path.lineTo(tip.x() - head * std::cos(ang - spread),
                    tip.y() - head * std::sin(ang - spread));
        path.lineTo(tip.x() - head * std::cos(ang + spread),
                    tip.y() - head * std::sin(ang + spread));
        path.closeSubpath();
        painter.fillPath(path, color);

        // Label sits just beyond the tip, along the vector direction, so the
        // six labels fan outward instead of piling onto the arrow tips.
        const QPointF labelPos(tip.x() + std::cos(ang) * (head + 12.0),
                               tip.y() + std::sin(ang) * (head + 12.0));
        drawPill(labelPos, color, label);
    };

    // Supercell underneath (solid), original on top (dashed) so both read.
    drawArrow(superVecs[0], kSuperColor, QStringLiteral("A₁"), false);
    drawArrow(superVecs[1], kSuperColor, QStringLiteral("A₂"), false);
    drawArrow(superVecs[2], kSuperColor, QStringLiteral("A₃"), false);
    drawArrow(cell_[0], kOriginalColor, QStringLiteral("a₁"), true);
    drawArrow(cell_[1], kOriginalColor, QStringLiteral("a₂"), true);
    drawArrow(cell_[2], kOriginalColor, QStringLiteral("a₃"), true);

    // Two-line legend in the reserved bottom band.
    painter.setFont(font());
    const QFontMetrics lfm(font());
    const int line2 = height() - 6;
    const int line1 = line2 - lfm.height();
    painter.setPen(kOriginalColor);
    painter.drawText(10, line1, tr("— — original cell (a₁, a₂, a₃)"));
    painter.setPen(kSuperColor);
    painter.drawText(10, line2, tr("——— supercell (A₁, A₂, A₃)"));
}

// ---------------------------------------------------------------------------
// SupercellDialog
// ---------------------------------------------------------------------------

SupercellDialog::SupercellDialog(std::shared_ptr<const core::Structure> structure,
                                 QWidget* parent)
    : QDialog(parent), structure_(std::move(structure))
{
    setWindowTitle(tr("Supercell — Transformation Matrix"));

    auto* rootLayout = new QVBoxLayout(this);

    auto* intro = new QLabel(
        tr("Build a supercell from an integer transformation matrix P. Each "
           "row is a new lattice vector Aᵢ expressed in the original vectors "
           "(a₁, a₂, a₃): Aᵢ = Pᵢ₁·a₁ + Pᵢ₂·a₂ + Pᵢ₃·a₃. Off-diagonal terms "
           "give sheared, non-diagonal supercells."),
        this);
    intro->setWordWrap(true);
    rootLayout->addWidget(intro);

    auto* midLayout = new QHBoxLayout;

    // --- Left: 3×3 integer matrix grid -------------------------------------
    // Just the input fields inside clean matrix brackets — no row/column text
    // labels (they cluttered and overlapped the compact spin boxes). The intro
    // above explains P; the volume readout and lattice preview stay below.
    auto* matrixBox = new QGroupBox(tr("Transformation matrix P"), this);
    auto* grid = new QGridLayout(matrixBox);
    const auto makeBracket = [matrixBox](const QString& glyph) {
        auto* bracket = new QLabel(glyph, matrixBox);
        QFont f = bracket->font();
        f.setPointSizeF(f.pointSizeF() * 3.2);
        bracket->setFont(f);
        bracket->setAlignment(Qt::AlignCenter);
        return bracket;
    };
    grid->addWidget(makeBracket(QStringLiteral("[")), 0, 0, 3, 1);
    for (int i = 0; i < 3; ++i) {
        for (int j = 0; j < 3; ++j) {
            auto* spin = new QSpinBox(matrixBox);
            spin->setRange(-20, 20);
            spin->setValue(i == j ? 1 : 0);
            spin->setAlignment(Qt::AlignCenter);
            spin->setFixedWidth(56);
            connect(spin, &QSpinBox::valueChanged, this, &SupercellDialog::onMatrixChanged);
            spins_[i][j] = spin;
            grid->addWidget(spin, i, j + 1);
        }
    }
    grid->addWidget(makeBracket(QStringLiteral("]")), 0, 4, 3, 1);

    auto* resetButton = new QPushButton(tr("Reset to identity"), matrixBox);
    connect(resetButton, &QPushButton::clicked, this, &SupercellDialog::resetToIdentity);
    grid->addWidget(resetButton, 3, 0, 1, 5);

    detLabel_ = new QLabel(matrixBox);
    detLabel_->setWordWrap(true);
    grid->addWidget(detLabel_, 4, 0, 1, 5);

    auto* matrixColumn = new QVBoxLayout;
    matrixColumn->addWidget(matrixBox);
    matrixColumn->addStretch();
    midLayout->addLayout(matrixColumn);

    // --- Right: live lattice preview ---------------------------------------
    auto* previewBox = new QGroupBox(tr("Lattice preview"), this);
    auto* previewLayout = new QVBoxLayout(previewBox);
    preview_ = new LatticePreviewWidget(previewBox);
    previewLayout->addWidget(preview_);
    midLayout->addWidget(previewBox, 1);

    rootLayout->addLayout(midLayout);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel,
                                         this);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    rootLayout->addWidget(buttons);

    // Seed the preview with the base cell.
    const bool defined = structure_ && structure_->cell().isDefined();
    preview_->setCell(defined ? structure_->cell().vectors()
                              : std::array<core::Vec3, 3>{},
                      defined);
    onMatrixChanged();
}

void SupercellDialog::matrix(int out[3][3]) const
{
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 3; ++j)
            out[i][j] = spins_[i][j]->value();
}

void SupercellDialog::onMatrixChanged()
{
    int p[3][3];
    matrix(p);
    preview_->setMatrix(p);

    const long det =
        static_cast<long>(p[0][0]) * (static_cast<long>(p[1][1]) * p[2][2]
                                      - static_cast<long>(p[1][2]) * p[2][1])
        - static_cast<long>(p[0][1]) * (static_cast<long>(p[1][0]) * p[2][2]
                                        - static_cast<long>(p[1][2]) * p[2][0])
        + static_cast<long>(p[0][2]) * (static_cast<long>(p[1][0]) * p[2][1]
                                        - static_cast<long>(p[1][1]) * p[2][0]);

    auto* ok = findChild<QDialogButtonBox*>();
    QPushButton* okButton = ok ? ok->button(QDialogButtonBox::Ok) : nullptr;

    const int baseAtoms = structure_ ? static_cast<int>(structure_->size()) : 0;
    if (det == 0) {
        detLabel_->setText(tr("det P = 0 — the three new vectors are coplanar; "
                              "not a valid supercell."));
        detLabel_->setStyleSheet(QStringLiteral("color:#c0392b;"));
        if (okButton)
            okButton->setEnabled(false);
    } else {
        const long cells = std::labs(det);
        detLabel_->setText(tr("det P = %1  →  %2× the cell = %3 atoms")
                               .arg(det)
                               .arg(cells)
                               .arg(baseAtoms * cells));
        detLabel_->setStyleSheet(QString());
        if (okButton)
            okButton->setEnabled(true);
    }
}

void SupercellDialog::resetToIdentity()
{
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 3; ++j) {
            QSignalBlocker blocker(spins_[i][j]);
            spins_[i][j]->setValue(i == j ? 1 : 0);
        }
    onMatrixChanged();
}

} // namespace calango::gui
