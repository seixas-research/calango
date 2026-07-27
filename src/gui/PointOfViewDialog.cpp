#include "gui/PointOfViewDialog.hpp"

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
#include <QSignalBlocker>
#include <QVBoxLayout>

#include <cmath>

namespace calango::gui {

namespace {
const auto kGroup = QStringLiteral("pointOfView");
} // namespace

PointOfViewDialog::PointOfViewDialog(ViewportWidget* viewport, QWidget* parent)
    : QDialog(parent)
    , viewport_(viewport)
{
    setWindowTitle(tr("Set Point-of-View"));
    resize(440, 560);

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
             tr("Rotation about the viewing axis itself — the scene rotation "
                "the X/Y/Z toolbar buttons apply. Yaw and pitch cannot "
                "produce it, which is why it is separate."));

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
               "Panning moves this; typing it centres the view on a chosen "
               "site exactly."));
        panRow->addWidget(panSpin_[axis], 1);
        connect(panSpin_[axis], &QDoubleSpinBox::valueChanged, this,
                &PointOfViewDialog::applyToViewport);
    }
    form->addRow(tr("Pan (target, Å):"), panRow);
    layout->addWidget(manual);

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
    syncFromViewport();
    deleteButton_->setEnabled(false);
}

void PointOfViewDialog::syncFromViewport()
{
    const render::PointOfView pov = viewport_->camera().pointOfView();
    syncing_ = true;
    zoomSpin_->setValue(pov.distance);
    yawSpin_->setValue(pov.yawDeg);
    pitchSpin_->setValue(pov.pitchDeg);
    // The scene rotation is a quaternion; report it as the angle about its own
    // axis, which is what the roll control edits.
    QVector3D axis;
    float angle = 0.0f;
    pov.sceneRotation.getAxisAndAngle(&axis, &angle);
    rollSpin_->setValue(angle);
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
    // Roll is applied about the camera's own viewing axis so the control does
    // what its name says regardless of where the turntable currently points.
    pov.sceneRotation = QQuaternion::fromAxisAndAngle(
        QVector3D(0.0f, 0.0f, 1.0f), static_cast<float>(rollSpin_->value()));
    pov.target = QVector3D(static_cast<float>(panSpin_[0]->value()),
                           static_cast<float>(panSpin_[1]->value()),
                           static_cast<float>(panSpin_[2]->value()));
    viewport_->setPointOfView(pov);
}

QString PointOfViewDialog::encode(const render::PointOfView& pov)
{
    return QStringLiteral("%1,%2,%3,%4,%5,%6,%7,%8,%9,%10,%11")
        .arg(pov.target.x()).arg(pov.target.y()).arg(pov.target.z())
        .arg(pov.distance).arg(pov.yawDeg).arg(pov.pitchDeg)
        .arg(pov.sceneRotation.scalar()).arg(pov.sceneRotation.x())
        .arg(pov.sceneRotation.y()).arg(pov.sceneRotation.z())
        .arg(static_cast<int>(pov.projection));
}

render::PointOfView PointOfViewDialog::decode(const QString& text)
{
    const QStringList parts = text.split(QLatin1Char(','));
    render::PointOfView pov;
    if (parts.size() != 11)
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
