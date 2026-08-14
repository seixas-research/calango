#include "gui/PointOfViewDialog.hpp"

#include "gui/SettingsManager.hpp"
#include "gui/ViewportWidget.hpp"

#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QSettings>
#include <QVBoxLayout>

#include <cmath>

namespace calango::gui {

namespace {
const auto kGroup = QStringLiteral("pointOfView");
} // namespace

QString PointOfViewDialog::settingsGroup()
{
    return kGroup;
}

render::PointOfView PointOfViewDialog::defaultPointOfView()
{
    const QString encoded =
        QSettings()
            .value(QLatin1String(SettingsManager::kDefaultPointOfView))
            .toString();
    // decode() already returns an invalid POV for anything unparseable, so a
    // hand-edited settings.json cannot make Reset camera aim at nonsense — it
    // just falls back to framing the structure.
    return encoded.isEmpty() ? render::PointOfView{} : decode(encoded);
}

void PointOfViewDialog::setDefaultPointOfView(const render::PointOfView& pov)
{
    QSettings().setValue(QLatin1String(SettingsManager::kDefaultPointOfView),
                         pov.valid ? encode(pov) : QString());
    SettingsManager::save(); // mirror to ~/.calango/settings.json immediately
}

PointOfViewDialog::PointOfViewDialog(ViewportWidget* viewport, QWidget* parent)
    : QDialog(parent)
    , viewport_(viewport)
{
    setWindowTitle(tr("Set Point-of-View"));
    // Tall enough that the intro note, the three group boxes and the saved-
    // views list are all fully visible without clipping or scrolling; the
    // minimum keeps a stray drag from squeezing rows out of sight again.
    resize(560, 780);
    setMinimumSize(520, 700);

    auto* layout = new QVBoxLayout(this);

    auto* note = new QLabel(
        tr("The camera state — zoom, orientation and pan — as numbers rather "
           "than as a drag. Type them to reproduce a framing exactly across a "
           "series of structures, and save the ones worth keeping.<br><br>"
           "Each workspace tab remembers its own point-of-view, so switching "
           "away and back does not disturb the view you set up."),
        this);
    note->setWordWrap(true);
    note->setTextFormat(Qt::RichText);
    layout->addWidget(note);

    // -- Manual controls ----------------------------------------------------
    auto* manual = new QGroupBox(tr("Camera"), this);
    auto* form = new QFormLayout(manual);

    zoomSpin_ = new QDoubleSpinBox(manual);
    zoomSpin_->setRange(0.01, 100000.0);
    zoomSpin_->setDecimals(3);
    zoomSpin_->setSingleStep(0.5);
    zoomSpin_->setSuffix(tr(" Å"));
    zoomSpin_->setToolTip(
        tr("Distance from the camera to its target. Smaller is closer, i.e. "
           "more zoomed in — this is the dolly distance the scroll wheel "
           "drives, and it sets the extent in orthographic mode too."));
    form->addRow(tr("Zoom (distance):"), zoomSpin_);

    const auto addAngle = [this, form, manual](QDoubleSpinBox*& spin,
                                               const QString& label,
                                               const QString& tip) {
        spin = new QDoubleSpinBox(manual);
        spin->setRange(-360.0, 360.0);
        spin->setDecimals(1);
        spin->setSingleStep(5.0);
        spin->setSuffix(tr(" °"));
        spin->setWrapping(true);
        spin->setToolTip(tip);
        form->addRow(label, spin);
        connect(spin, &QDoubleSpinBox::valueChanged, this,
                &PointOfViewDialog::applyToViewport);
    };
    addAngle(yawSpin_, tr("Rotation — yaw:"),
             tr("Turntable rotation about the vertical axis."));
    addAngle(pitchSpin_, tr("Rotation — pitch:"),
             tr("Elevation above (positive) or below the horizontal."));
    addAngle(rollSpin_, tr("Rotation — roll:"),
             tr("Tilt about the viewing axis itself — the camera turning its "
                "head, so the picture rotates in the screen plane while the "
                "viewpoint and the structure stay put.\n\n"
                "Yaw and pitch cannot produce it, which is why it is separate. "
                "Distinct from the X/Y/Z toolbar buttons, which rotate the "
                "STRUCTURE about a world axis."));

    auto* panRow = new QHBoxLayout;
    const char* axes[3] = {"x", "y", "z"};
    for (int axis = 0; axis < 3; ++axis) {
        panRow->addWidget(new QLabel(QLatin1String(axes[axis]), manual));
        panSpin_[axis] = new QDoubleSpinBox(manual);
        panSpin_[axis]->setRange(-100000.0, 100000.0);
        panSpin_[axis]->setDecimals(3);
        panSpin_[axis]->setSingleStep(0.5);
        panSpin_[axis]->setToolTip(
            tr("The point the camera looks at, in world coordinates (Å). "
               "Panning moves this; typing it centers the view on a chosen "
               "site exactly."));
        panRow->addWidget(panSpin_[axis], 1);
        connect(panSpin_[axis], &QDoubleSpinBox::valueChanged, this,
                &PointOfViewDialog::applyToViewport);
    }
    form->addRow(tr("Pan (target, Å):"), panRow);
    layout->addWidget(manual);

    // -- Default point-of-view ----------------------------------------------
    // A saved view is recalled deliberately; THIS one is what the toolbar's
    // Reset camera button (and 'F') snaps back to, so a preferred orientation
    // becomes the resting state of every structure rather than something to
    // re-apply by hand after each stray drag.
    auto* defaultGroup = new QGroupBox(tr("Default Point-of-View"), this);
    auto* defaultLayout = new QVBoxLayout(defaultGroup);
    defaultLabel_ = new QLabel(defaultGroup);
    defaultLabel_->setWordWrap(true);
    defaultLayout->addWidget(defaultLabel_);
    auto* defaultButtons = new QHBoxLayout;
    auto* setDefaultButton =
        new QPushButton(tr("Set point-of-view as default"), defaultGroup);
    setDefaultButton->setToolTip(
        tr("Write the camera on screen to ~/.calango/settings.json as the "
           "default view. The toolbar's \"Reset camera\" button (and the F "
           "key) then restores exactly this framing instead of re-fitting the "
           "structure to the window.\n\n"
           "It is stored verbatim — zoom, rotation and pan — so it reproduces "
           "the same view across structures and across sessions."));
    clearDefaultButton_ = new QPushButton(tr("Clear"), defaultGroup);
    clearDefaultButton_->setToolTip(
        tr("Forget the stored default, so \"Reset camera\" goes back to "
           "centering and framing whatever structure is open."));
    defaultButtons->addWidget(setDefaultButton);
    defaultButtons->addWidget(clearDefaultButton_);
    defaultButtons->addStretch(1);
    defaultLayout->addLayout(defaultButtons);
    layout->addWidget(defaultGroup);

    connect(setDefaultButton, &QPushButton::clicked, this,
            &PointOfViewDialog::saveAsDefault);
    connect(clearDefaultButton_, &QPushButton::clicked, this,
            &PointOfViewDialog::clearDefault);

    // -- Saved views --------------------------------------------------------
    auto* saved = new QGroupBox(tr("Saved Points-of-View"), this);
    auto* savedLayout = new QVBoxLayout(saved);
    savedList_ = new QListWidget(saved);
    savedList_->setToolTip(
        tr("Persisted across sessions. Double-click to apply one — including "
           "to a different structure, which is how a set of figures is kept "
           "consistently framed."));
    savedLayout->addWidget(savedList_, 1);
    auto* savedButtons = new QHBoxLayout;
    auto* saveButton = new QPushButton(tr("Save Current…"), saved);
    auto* loadButton = new QPushButton(tr("Load"), saved);
    deleteButton_ = new QPushButton(tr("Delete"), saved);
    savedButtons->addWidget(saveButton);
    savedButtons->addWidget(loadButton);
    savedButtons->addWidget(deleteButton_);
    savedButtons->addStretch(1);
    savedLayout->addLayout(savedButtons);
    layout->addWidget(saved, 1);

    connect(saveButton, &QPushButton::clicked, this,
            &PointOfViewDialog::saveCurrent);
    connect(loadButton, &QPushButton::clicked, this,
            &PointOfViewDialog::loadSelected);
    connect(deleteButton_, &QPushButton::clicked, this,
            &PointOfViewDialog::deleteSelected);
    connect(savedList_, &QListWidget::itemActivated, this,
            [this] { loadSelected(); });
    connect(savedList_, &QListWidget::itemSelectionChanged, this, [this] {
        deleteButton_->setEnabled(savedList_->currentItem() != nullptr);
    });

    summaryLabel_ = new QLabel(this);
    summaryLabel_->setWordWrap(true);
    layout->addWidget(summaryLabel_);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    // The viewport changes under us whenever the user orbits with the mouse or
    // switches tab; mirror it rather than showing stale numbers.
    connect(viewport_, &ViewportWidget::cameraChanged, this,
            &PointOfViewDialog::syncFromViewport);

    refreshSavedList();
    refreshDefaultState();
    syncFromViewport();
    deleteButton_->setEnabled(false);
}

void PointOfViewDialog::refreshDefaultState()
{
    const render::PointOfView pov = defaultPointOfView();
    clearDefaultButton_->setEnabled(pov.valid);
    if (!pov.valid) {
        defaultLabel_->setText(
            tr("No default set — \"Reset camera\" centers and frames the "
               "structure."));
        return;
    }
    defaultLabel_->setText(
        tr("\"Reset camera\" restores: zoom %1 Å, yaw %2°, pitch %3°, "
           "roll %4°, target (%5, %6, %7) Å.")
            .arg(pov.distance, 0, 'f', 2)
            .arg(pov.yawDeg, 0, 'f', 1)
            .arg(pov.pitchDeg, 0, 'f', 1)
            .arg(pov.rollDeg, 0, 'f', 1)
            .arg(pov.target.x(), 0, 'f', 2)
            .arg(pov.target.y(), 0, 'f', 2)
            .arg(pov.target.z(), 0, 'f', 2));
}

void PointOfViewDialog::saveAsDefault()
{
    setDefaultPointOfView(viewport_->camera().pointOfView());
    refreshDefaultState();
}

void PointOfViewDialog::clearDefault()
{
    setDefaultPointOfView(render::PointOfView{}); // invalid == "no default"
    refreshDefaultState();
}

void PointOfViewDialog::syncFromViewport()
{
    const render::PointOfView pov = viewport_->camera().pointOfView();
    syncing_ = true;
    zoomSpin_->setValue(pov.distance);
    yawSpin_->setValue(pov.yawDeg);
    pitchSpin_->setValue(pov.pitchDeg);
    rollSpin_->setValue(pov.rollDeg);
    panSpin_[0]->setValue(pov.target.x());
    panSpin_[1]->setValue(pov.target.y());
    panSpin_[2]->setValue(pov.target.z());
    syncing_ = false;

    summaryLabel_->setText(
        tr("Projection: %1")
            .arg(pov.projection == render::CameraProjection::Orthographic
                     ? tr("orthographic")
                     : tr("perspective")));
}

void PointOfViewDialog::applyToViewport()
{
    if (syncing_)
        return;
    render::PointOfView pov = viewport_->camera().pointOfView();
    pov.distance = static_cast<float>(zoomSpin_->value());
    pov.yawDeg = static_cast<float>(yawSpin_->value());
    pov.pitchDeg = static_cast<float>(pitchSpin_->value());
    // The camera's own roll. This used to synthesize a SCENE rotation about
    // world z instead, which was wrong twice over: it turned the structure
    // rather than the camera, and because it assigned sceneRotation outright,
    // editing any field here silently discarded whatever rotation the X/Y/Z
    // toolbar buttons had accumulated. sceneRotation now rides along untouched
    // from pointOfView() above.
    pov.rollDeg = static_cast<float>(rollSpin_->value());
    pov.target = QVector3D(static_cast<float>(panSpin_[0]->value()),
                           static_cast<float>(panSpin_[1]->value()),
                           static_cast<float>(panSpin_[2]->value()));
    viewport_->setPointOfView(pov);
}

QString PointOfViewDialog::encode(const render::PointOfView& pov)
{
    // Roll is APPENDED as field 12 rather than inserted next to yaw/pitch,
    // so that named views saved by an earlier release still decode — they
    // simply carry no roll, which is what they meant.
    return QStringLiteral("%1,%2,%3,%4,%5,%6,%7,%8,%9,%10,%11,%12")
        .arg(pov.target.x()).arg(pov.target.y()).arg(pov.target.z())
        .arg(pov.distance).arg(pov.yawDeg).arg(pov.pitchDeg)
        .arg(pov.sceneRotation.scalar()).arg(pov.sceneRotation.x())
        .arg(pov.sceneRotation.y()).arg(pov.sceneRotation.z())
        .arg(static_cast<int>(pov.projection))
        .arg(pov.rollDeg);
}

render::PointOfView PointOfViewDialog::decode(const QString& text)
{
    const QStringList parts = text.split(QLatin1Char(','));
    render::PointOfView pov;
    // 11 fields is a view saved before roll existed; 12 includes it. Anything
    // else is not a point-of-view.
    if (parts.size() != 11 && parts.size() != 12)
        return pov; // stays invalid, so applying it is a no-op
    pov.target = QVector3D(parts[0].toFloat(), parts[1].toFloat(),
                           parts[2].toFloat());
    pov.distance = parts[3].toFloat();
    pov.yawDeg = parts[4].toFloat();
    pov.pitchDeg = parts[5].toFloat();
    pov.sceneRotation = QQuaternion(parts[6].toFloat(), parts[7].toFloat(),
                                    parts[8].toFloat(), parts[9].toFloat());
    pov.projection = parts[10].toInt() == 1
        ? render::CameraProjection::Orthographic
        : render::CameraProjection::Perspective;
    // A pre-roll view is an untilted one, not one carrying today's default.
    pov.rollDeg = parts.size() == 12 ? parts[11].toFloat() : 0.0f;
    pov.valid = true;
    return pov;
}

void PointOfViewDialog::refreshSavedList()
{
    savedList_->clear();
    QSettings settings;
    settings.beginGroup(kGroup);
    for (const QString& name : settings.childKeys()) {
        auto* item = new QListWidgetItem(name, savedList_);
        item->setData(Qt::UserRole, settings.value(name).toString());
    }
    settings.endGroup();
    if (savedList_->count() == 0) {
        auto* item = new QListWidgetItem(tr("(none saved yet)"), savedList_);
        item->setFlags(Qt::NoItemFlags);
    }
}

void PointOfViewDialog::saveCurrent()
{
    bool ok = false;
    const QString name = QInputDialog::getText(
        this, tr("Save Point-of-View"), tr("Name:"), QLineEdit::Normal,
        tr("View %1").arg(savedList_->count() + 1), &ok);
    if (!ok || name.trimmed().isEmpty())
        return;
    QSettings settings;
    settings.beginGroup(kGroup);
    settings.setValue(name.trimmed(),
                      encode(viewport_->camera().pointOfView()));
    settings.endGroup();
    refreshSavedList();
}

void PointOfViewDialog::loadSelected()
{
    const QListWidgetItem* item = savedList_->currentItem();
    if (!item)
        return;
    const QString encoded = item->data(Qt::UserRole).toString();
    if (encoded.isEmpty())
        return; // the "(none saved yet)" placeholder
    viewport_->setPointOfView(decode(encoded));
    syncFromViewport();
}

void PointOfViewDialog::deleteSelected()
{
    const QListWidgetItem* item = savedList_->currentItem();
    if (!item || item->data(Qt::UserRole).toString().isEmpty())
        return;
    QSettings settings;
    settings.beginGroup(kGroup);
    settings.remove(item->text());
    settings.endGroup();
    refreshSavedList();
}

} // namespace calango::gui
