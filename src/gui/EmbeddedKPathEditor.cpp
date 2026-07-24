#include "gui/EmbeddedKPathEditor.hpp"

#include "core/BrillouinZone.hpp"
#include "gui/BrillouinZoneWidget.hpp"
#include "python_bridge/AseBridge.hpp"

#include <QLabel>
#include <QLineEdit>
#include <QVBoxLayout>

#include <algorithm>
#include <exception>

namespace calango::gui {

EmbeddedKPathEditor::EmbeddedKPathEditor(
    std::shared_ptr<const core::Structure> structure, QWidget* parent)
    : QWidget(parent)
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);

    QString suggested;
    if (structure && structure->cell().isDefined()) {
        try {
            const auto zone = core::computeBrillouinZone(structure->cell());
            const auto bandPath = pybridge::AseBridge::bandPathInfo(*structure);
            suggested = QString::fromStdString(bandPath.suggestedPath);
            zoneWidget_ =
                new BrillouinZoneWidget(zone, bandPath, /*compact=*/true, this);
            zoneWidget_->setMinimumHeight(320);
            layout->addWidget(zoneWidget_, 1);
        } catch (const std::exception&) {
            // Lattices ASE cannot classify (or a missing spglib) leave the
            // text field as the only route — better than blocking the wizard.
            zoneWidget_ = nullptr;
        }
    }

    if (zoneWidget_) {
        // The interactive builder (its 3D canvas + segment list) is the single
        // source of truth for the path; the old text readout duplicated the
        // segment list, so it is gone. path() reads straight off the widget.
        // Seed the 3D view with the suggestion so the stage opens on a valid,
        // visible path rather than an empty zone.
        zoneWidget_->setPathString(suggested);
        connect(zoneWidget_, &BrillouinZoneWidget::pathChanged, this,
                [this] { Q_EMIT pathChanged(); });
    } else {
        // No Brillouin zone (non-periodic structure): fall back to a plain text
        // field, which is then the only route to enter a path.
        auto* note = new QLabel(
            tr("No Brillouin zone available for this structure (it needs a "
               "periodic unit cell) — enter the path manually."),
            this);
        note->setWordWrap(true);
        layout->addWidget(note);

        pathEdit_ = new QLineEdit(suggested, this);
        pathEdit_->setPlaceholderText(tr("empty = ASE suggestion (e.g. GXMG)"));
        pathEdit_->setToolTip(
            tr("High-symmetry path as concatenated labels; ',' separates "
               "discontinuous sections (GXWK,UX)."));
        auto* row = new QVBoxLayout;
        row->addWidget(new QLabel(tr("k-path:"), this));
        row->addWidget(pathEdit_);
        layout->addLayout(row);
        connect(pathEdit_, &QLineEdit::textEdited, this,
                [this] { Q_EMIT pathChanged(); });
    }
}

int EmbeddedKPathEditor::pointsPerSegment() const
{
    // 40 mirrors BrillouinZoneWidget's own default, so the manual-entry
    // fallback samples the path the same way the 3D builder would.
    return zoneWidget_ ? zoneWidget_->pointsPerSegment() : 40;
}

int EmbeddedKPathEditor::segmentCount() const
{
    // Derived from the text, so it stays right whether the path came from the
    // 3D builder or was typed in: labels minus one per continuous section.
    int segments = 0;
    for (const QString& section : path().split(QLatin1Char(','),
                                               Qt::SkipEmptyParts)) {
        // Labels are one letter plus optional digits ("X1"), so counting
        // letters counts points.
        int points = 0;
        for (const QChar c : section)
            if (c.isLetter())
                ++points;
        segments += std::max(0, points - 1);
    }
    return std::max(1, segments);
}

QString EmbeddedKPathEditor::path() const
{
    // The interactive builder is authoritative; the text field only exists in
    // the non-periodic fallback where there is no 3D builder.
    if (zoneWidget_)
        return zoneWidget_->pathString().trimmed();
    return pathEdit_ ? pathEdit_->text().trimmed() : QString();
}

} // namespace calango::gui
