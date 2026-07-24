#include "gui/BrillouinZoneWidget.hpp"

#include "gui/BrillouinZoneStyleDialog.hpp"
#include "gui/BrillouinZoneView.hpp"
#include "ui/IconManager.hpp"

#include <QCheckBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QSpinBox>
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

BrillouinZoneWidget::BrillouinZoneWidget(
    const core::BrillouinZoneData& zone,
    const pybridge::AseBridge::BandPathInfo& bandPath, bool compact,
    QWidget* parent)
    : QWidget(parent)
    , zone_(zone)
    , specialPoints_(bandPath.specialPoints)
    , suggestedPath_(QString::fromStdString(bandPath.suggestedPath))
    , view_(new BrillouinZoneView(this))
    , pathList_(new QListWidget(this))
    , divisionsSpin_(new QSpinBox(this))
{
    // Cartesian positions of the special points: frac · (b1, b2, b3).
    std::vector<BrillouinZoneView::LabeledPoint> viewPoints;
    for (const auto& point : specialPoints_) {
        const core::Vec3 cart = zone_.reciprocal[0] * point.fractional.x
            + zone_.reciprocal[1] * point.fractional.y
            + zone_.reciprocal[2] * point.fractional.z;
        viewPoints.push_back({displayLabel(point.label),
                              {static_cast<float>(cart.x),
                               static_cast<float>(cart.y),
                               static_cast<float>(cart.z)}});
    }
    view_->setZone(zone_, viewPoints);
    connect(view_, &BrillouinZoneView::pointPicked,
            this, &BrillouinZoneWidget::appendPoint);

    auto* side = new QVBoxLayout;
    // The embedded (compact) wizard stage keeps only the interactive controls;
    // the introductory hint text is shown solely in the standalone builder.
    if (!compact) {
        auto* hint = new QLabel(
            tr("Click high-symmetry points to build the k-path.\n"
               "Drag rotates · Shift+drag pans · wheel zooms."),
            this);
        hint->setWordWrap(true);
        side->addWidget(hint);
    }

    // Default to orthographic: symmetric zone geometry reads more clearly
    // without perspective foreshortening. In the embedded (compact) wizard the
    // toggle is suppressed entirely — orthographic stays on, no control shown.
    view_->setOrthographic(true);
    if (!compact) {
        auto* orthoCheck = new QCheckBox(tr("Orthographic projection"), this);
        orthoCheck->setToolTip(tr("Parallel projection — useful for reading "
                                  "symmetric zone geometry without perspective "
                                  "foreshortening"));
        orthoCheck->setChecked(true);
        side->addWidget(orthoCheck);
        connect(orthoCheck, &QCheckBox::toggled,
                view_, &BrillouinZoneView::setOrthographic);
    }

    // Appearance is a styling concern, not a path-building one — it is offered
    // only in the standalone Brillouin Zone Builder, not in the embedded wizard
    // stage, which stays focused on defining the path.
    if (!compact) {
        auto* styleButton = new QPushButton(tr("Customize Appearance…"), this);
        styleButton->setToolTip(tr("Colors, transparency, line thickness and "
                                   "label toggles for the zone and k-path"));
        side->addWidget(styleButton);
        connect(styleButton, &QPushButton::clicked, this, [this] {
            auto* dialog = new BrillouinZoneStyleDialog(view_->style(), this);
            connect(dialog, &BrillouinZoneStyleDialog::styleChanged, view_,
                    &BrillouinZoneView::setStyle);
            dialog->setAttribute(Qt::WA_DeleteOnClose);
            dialog->show();
        });
    }

    side->addWidget(new QLabel(tr("k-path sequence:"), this));
    if (compact)
        pathList_->setMaximumHeight(150);
    side->addWidget(pathList_, 1);

    // Icon-only action bar (Suggested · Break · Undo · Remove · Clear) with
    // hover tooltips — modern RemixIcon glyphs (theme-tinted via IconManager),
    // matching the rest of the app's icon buttons.
    auto* pathButtons = new QHBoxLayout;
    const auto iconButton = [this](const QString& iconName, const QString& tip) {
        auto* button = new QPushButton(this);
        button->setIcon(ui::IconManager::icon(iconName));
        button->setToolTip(tip);
        button->setFocusPolicy(Qt::NoFocus);
        return button;
    };
    auto* suggestedButton = iconButton(
        QStringLiteral("magic-line"),
        tr("Suggested — load ASE's suggested path: %1").arg(suggestedPath_));
    auto* breakButton = iconButton(
        QStringLiteral("scissors-cut-line"),
        tr("Break — start a new discontinuous section (e.g. Γ → X | M → R)"));
    auto* undoButton = iconButton(QStringLiteral("arrow-go-back-line"),
                                  tr("Undo — remove the last point in the path"));
    auto* removeButton = iconButton(
        QStringLiteral("close-line"),
        tr("Remove — delete the selected k-point / break from the path"));
    auto* clearButton = iconButton(QStringLiteral("delete-bin-line"),
                                   tr("Clear — remove the entire path"));
    pathButtons->addWidget(suggestedButton);
    pathButtons->addWidget(breakButton);
    pathButtons->addWidget(undoButton);
    pathButtons->addWidget(removeButton);
    pathButtons->addWidget(clearButton);
    side->addLayout(pathButtons);
    connect(suggestedButton, &QPushButton::clicked,
            this, &BrillouinZoneWidget::useSuggestedPath);
    connect(breakButton, &QPushButton::clicked, this, &BrillouinZoneWidget::addBreak);
    connect(undoButton, &QPushButton::clicked,
            this, &BrillouinZoneWidget::undoLastPoint);
    connect(removeButton, &QPushButton::clicked,
            this, &BrillouinZoneWidget::removeSelectedPoint);
    connect(clearButton, &QPushButton::clicked, this, &BrillouinZoneWidget::clearPath);

    auto* divisionsRow = new QHBoxLayout;
    divisionsRow->addWidget(new QLabel(tr("Points per segment:"), this));
    divisionsSpin_->setRange(5, 500);
    divisionsSpin_->setValue(40);
    divisionsRow->addWidget(divisionsSpin_, 1);
    side->addLayout(divisionsRow);
    connect(divisionsSpin_, &QSpinBox::valueChanged, this,
            [this] { Q_EMIT pathChanged(); });

    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(view_, 1);
    auto* sideWidget = new QWidget(this);
    sideWidget->setLayout(side);
    sideWidget->setFixedWidth(280);
    layout->addWidget(sideWidget);
}

int BrillouinZoneWidget::pointsPerSegment() const
{
    return divisionsSpin_->value();
}

void BrillouinZoneWidget::appendPoint(int index)
{
    if (index < 0 || index >= static_cast<int>(specialPoints_.size()))
        return;
    if (!path_.empty() && path_.back() == index)
        return; // ignore double-clicks on the same point
    path_.push_back(index);
    syncPathViews();
}

void BrillouinZoneWidget::addBreak()
{
    // A break needs a point before it and never repeats.
    if (path_.empty() || path_.back() < 0)
        return;
    path_.push_back(-1);
    syncPathViews();
}

void BrillouinZoneWidget::undoLastPoint()
{
    if (path_.empty())
        return;
    path_.pop_back();
    syncPathViews();
}

void BrillouinZoneWidget::removeSelectedPoint()
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

void BrillouinZoneWidget::clearPath()
{
    path_.clear();
    syncPathViews();
}

void BrillouinZoneWidget::useSuggestedPath()
{
    setPathString(suggestedPath_);
}

void BrillouinZoneWidget::setPathString(const QString& path)
{
    path_.clear();
    for (const QString& label : parsePathLabels(path)) {
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

void BrillouinZoneWidget::syncPathViews()
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
    // Every mutation funnels through here, so this is the single place the
    // embedding wizard needs to observe.
    Q_EMIT pathChanged();
}

QString BrillouinZoneWidget::pathString() const
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

core::KPathSegments BrillouinZoneWidget::segments() const
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

bool BrillouinZoneWidget::hasExportablePath() const
{
    for (const auto& section : segments())
        if (section.size() >= 2)
            return true;
    return false;
}

} // namespace calango::gui
