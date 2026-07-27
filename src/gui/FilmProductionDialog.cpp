#include "gui/FilmProductionDialog.hpp"

#include "gui/PointOfViewDialog.hpp"
#include "gui/ViewportWidget.hpp"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QSettings>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QTableWidget>
#include <QVBoxLayout>

#include <algorithm>

namespace calango::gui {

namespace {

QString transitionName(render::FilmTransition transition)
{
    switch (transition) {
    case render::FilmTransition::HardCut:
        return FilmProductionDialog::tr("Hard cut");
    case render::FilmTransition::FadeInOut:
        return FilmProductionDialog::tr("Fade in / Fade out");
    case render::FilmTransition::Crossfade:
        return FilmProductionDialog::tr("Crossfade");
    case render::FilmTransition::Interpolation:
        break;
    }
    return FilmProductionDialog::tr("Interpolation");
}

/// Columns of the film timeline table.
enum ShotColumn { ShotColumnName = 0, ShotColumnSeconds, ShotColumnTransition,
                  ShotColumnCount };

/// A transition dropdown, built the same way for every row.
QComboBox* makeTransitionCombo(QWidget* parent)
{
    auto* combo = new QComboBox(parent);
    for (const render::FilmTransition transition :
         {render::FilmTransition::HardCut, render::FilmTransition::Interpolation,
          render::FilmTransition::Crossfade,
          render::FilmTransition::FadeInOut}) {
        combo->addItem(transitionName(transition), static_cast<int>(transition));
    }
    combo->setToolTip(FilmProductionDialog::tr(
        "How the film gets from this shot to the next one.\n\n"
        "Hard cut — the camera holds this shot, then the next one begins "
        "already in place. No motion.\n"
        "Interpolation — the camera flies between them, easing in and out. "
        "The only transition that shows the angles in between.\n"
        "Crossfade — both shots are rendered and dissolved into each other. "
        "The camera never occupies the angles in between, which is what makes "
        "it right for two views with nothing sensible between them — opposite "
        "faces of a slab, before and after a reaction — and unlike a fade the "
        "screen is never empty.\n"
        "Fade in / Fade out — the image dips to black and back, and the "
        "camera changes under cover of it."));
    return combo;
}

} // namespace

FilmProductionDialog::FilmProductionDialog(ViewportWidget* viewport,
                                           render::FilmScript script,
                                           QWidget* parent)
    : QDialog(parent)
    , viewport_(viewport)
    , script_(std::move(script))
{
    setWindowTitle(tr("Film Production"));
    resize(760, 660);

    auto* layout = new QVBoxLayout(this);

    auto* note = new QLabel(
        tr("A film is a list of <b>shots</b> — a saved point-of-view plus what "
           "the casts look like while the camera is there — and a duration. "
           "Each row of the film below carries its own time, so a slow orbit "
           "and a quick reposition need not run equally long; the total "
           "follows the sum, and editing the total re-times every shot in "
           "proportion.<br><br>"
           "Edits apply immediately. Scrub the timeline under the viewport to "
           "see any instant, or press <b>Preview</b> to play it."),
        this);
    note->setWordWrap(true);
    note->setTextFormat(Qt::RichText);
    layout->addWidget(note);

    // -- Shots ---------------------------------------------------------------
    auto* shotsGroup = new QGroupBox(tr("Shots"), this);
    auto* shotsLayout = new QHBoxLayout(shotsGroup);

    auto* savedColumn = new QVBoxLayout;
    savedColumn->addWidget(new QLabel(tr("Saved points-of-view:"), shotsGroup));
    savedList_ = new QListWidget(shotsGroup);
    savedList_->setToolTip(
        tr("The library from the \"Set point-of-view…\" dialog. Double-click "
           "one to append it as a shot.\n\n"
           "The camera state is COPIED into the film, so renaming or deleting "
           "a saved view later cannot silently change a film you already "
           "built."));
    savedColumn->addWidget(savedList_, 1);
    auto* addSavedButton = new QPushButton(tr("Add as Shot →"), shotsGroup);
    auto* addCurrentButton = new QPushButton(tr("Add Current View →"), shotsGroup);
    addCurrentButton->setToolTip(
        tr("Append the camera exactly as it is in the viewport right now, "
           "without saving it to the point-of-view library first."));
    savedColumn->addWidget(addSavedButton);
    savedColumn->addWidget(addCurrentButton);
    shotsLayout->addLayout(savedColumn, 1);

    auto* filmColumn = new QVBoxLayout;
    filmColumn->addWidget(new QLabel(tr("Film (in order):"), shotsGroup));
    shotTable_ = new QTableWidget(shotsGroup);
    shotTable_->setColumnCount(ShotColumnCount);
    shotTable_->setHorizontalHeaderLabels(
        {tr("Shot"), tr("Seconds"), tr("Transition to next")});
    shotTable_->horizontalHeader()->setStretchLastSection(true);
    shotTable_->verticalHeader()->setVisible(false);
    shotTable_->setSelectionBehavior(QAbstractItemView::SelectRows);
    shotTable_->setSelectionMode(QAbstractItemView::SingleSelection);
    shotTable_->setToolTip(
        tr("The shots in playback order. Type a time straight into the "
           "Seconds column — it is the length of the move FROM that shot to "
           "the next, which is why the last row has none."));
    filmColumn->addWidget(shotTable_, 1);
    auto* shotButtons = new QHBoxLayout;
    upButton_ = new QPushButton(tr("Up"), shotsGroup);
    downButton_ = new QPushButton(tr("Down"), shotsGroup);
    removeButton_ = new QPushButton(tr("Remove"), shotsGroup);
    shotButtons->addWidget(upButton_);
    shotButtons->addWidget(downButton_);
    shotButtons->addWidget(removeButton_);
    filmColumn->addLayout(shotButtons);
    shotsLayout->addLayout(filmColumn, 1);
    layout->addWidget(shotsGroup, 1);

    // -- Selected shot -------------------------------------------------------
    auto* shotGroup = new QGroupBox(tr("Selected Shot"), this);
    auto* shotForm = new QFormLayout(shotGroup);

    castTable_ = new QTableWidget(shotGroup);
    castTable_->setColumnCount(2);
    castTable_->setHorizontalHeaderLabels({tr("Cast"), tr("Opacity")});
    castTable_->horizontalHeader()->setStretchLastSection(true);
    castTable_->verticalHeader()->setVisible(false);
    castTable_->setMaximumHeight(140);
    castTable_->setToolTip(
        tr("What each cast looks like AT this shot; the film ramps between "
           "neighboring shots.\n\n"
           "Setting a cast to 0.2 on one shot fades it down to that and back "
           "out again — the usual way to reveal a molecule sitting inside a "
           "substrate without deleting the substrate. Set it on two "
           "consecutive shots to hold it there instead."));
    shotForm->addRow(tr("Cast opacity:"), castTable_);
    castNote_ = new QLabel(shotGroup);
    castNote_->setWordWrap(true);
    shotForm->addRow(QString(), castNote_);

    overlayOverrideCheck_ =
        new QCheckBox(tr("Set the overlays shown in this shot"), shotGroup);
    overlayOverrideCheck_->setToolTip(
        tr("Off, this shot shows whatever the Additional Overlays dock has "
           "ticked — which is what every film did before this existed.\n\n"
           "On, the shot shows exactly the overlays ticked below and nothing "
           "else, so a caption can appear over one view and be gone by the "
           "next. Ticking nothing is a valid choice: it clears the screen of "
           "annotations for that shot."));
    shotForm->addRow(QString(), overlayOverrideCheck_);

    overlayList_ = new QListWidget(shotGroup);
    overlayList_->setMaximumHeight(120);
    shotForm->addRow(tr("Overlays:"), overlayList_);
    overlayNote_ = new QLabel(shotGroup);
    overlayNote_->setWordWrap(true);
    shotForm->addRow(QString(), overlayNote_);
    layout->addWidget(shotGroup);

    // -- Timing --------------------------------------------------------------
    auto* timingGroup = new QGroupBox(tr("Timing"), this);
    auto* timingForm = new QFormLayout(timingGroup);

    durationSpin_ = new QDoubleSpinBox(timingGroup);
    durationSpin_->setRange(0.1, 3600.0);
    durationSpin_->setDecimals(2);
    durationSpin_->setSingleStep(1.0);
    durationSpin_->setSuffix(tr(" s"));
    durationSpin_->setValue(script_.duration);
    durationSpin_->setToolTip(
        tr("The whole film. Follows the sum of the per-shot times above; "
           "typing here instead re-times every shot in proportion, so the "
           "pacing survives a change of length."));
    timingForm->addRow(tr("Total duration:"), durationSpin_);

    fpsSpin_ = new QSpinBox(timingGroup);
    fpsSpin_->setRange(1, 120);
    fpsSpin_->setValue(script_.fps);
    fpsSpin_->setSuffix(tr(" fps"));
    fpsSpin_->setToolTip(
        tr("Frames per second. 24 is cinema, 30 is video, 60 is smooth "
           "on-screen motion — and the frame count below follows from it."));
    timingForm->addRow(tr("Frame rate:"), fpsSpin_);

    priorityCombo_ = new QComboBox(timingGroup);
    priorityCombo_->addItem(tr("Film production"),
                            static_cast<int>(render::FilmTimelinePriority::Film));
    priorityCombo_->addItem(
        tr("Trajectory"),
        static_cast<int>(render::FilmTimelinePriority::Trajectory));
    priorityCombo_->setToolTip(
        tr("Which timeline sets the length when this workspace holds both a "
           "film and a trajectory.\n\n"
           "Film production — the duration above wins, and the trajectory is "
           "stretched or compressed to play exactly once across it.\n"
           "Trajectory — the trajectory's own length wins, and the camera "
           "moves are re-timed to fit it."));
    timingForm->addRow(tr("Timeline priority:"), priorityCombo_);
    priorityNote_ = new QLabel(timingGroup);
    priorityNote_->setWordWrap(true);
    timingForm->addRow(QString(), priorityNote_);

    summaryLabel_ = new QLabel(timingGroup);
    summaryLabel_->setWordWrap(true);
    timingForm->addRow(summaryLabel_);
    layout->addWidget(timingGroup);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    auto* previewButton =
        buttons->addButton(tr("Preview"), QDialogButtonBox::ActionRole);
    previewButton->setToolTip(
        tr("Play the film from the start in the main 3D viewport."));
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(previewButton, &QPushButton::clicked, this,
            &FilmProductionDialog::previewRequested);

    connect(addSavedButton, &QPushButton::clicked, this,
            &FilmProductionDialog::addShotFromSaved);
    connect(savedList_, &QListWidget::itemActivated, this,
            [this] { addShotFromSaved(); });
    connect(addCurrentButton, &QPushButton::clicked, this,
            &FilmProductionDialog::addShotFromCurrentView);
    connect(removeButton_, &QPushButton::clicked, this,
            &FilmProductionDialog::removeShot);
    connect(upButton_, &QPushButton::clicked, this, [this] { moveShot(-1); });
    connect(downButton_, &QPushButton::clicked, this, [this] { moveShot(+1); });
    connect(shotTable_, &QTableWidget::currentCellChanged, this,
            [this](int, int, int, int) { shotSelectionChanged(); });
    connect(overlayOverrideCheck_, &QCheckBox::toggled, this,
            &FilmProductionDialog::applyShotEdits);
    connect(overlayList_, &QListWidget::itemChanged, this,
            [this](QListWidgetItem*) { applyShotEdits(); });
    connect(castTable_, &QTableWidget::cellChanged, this,
            [this](int, int) { applyShotEdits(); });
    connect(durationSpin_, &QDoubleSpinBox::valueChanged, this,
            &FilmProductionDialog::applyTiming);
    connect(fpsSpin_, &QSpinBox::valueChanged, this,
            &FilmProductionDialog::applyTiming);
    connect(priorityCombo_, &QComboBox::currentIndexChanged, this,
            &FilmProductionDialog::applyTiming);

    refreshSavedList();
    refreshShotList();
    setTrajectory(script_.trajectoryFrames, script_.trajectoryFps);
}

void FilmProductionDialog::setAvailableOverlays(
    std::vector<std::pair<int, QString>> overlays)
{
    availableOverlays_ = std::move(overlays);
    refreshOverlayList();
}

void FilmProductionDialog::setScript(const render::FilmScript& script)
{
    script_ = script;
    loading_ = true;
    durationSpin_->setValue(script_.duration);
    fpsSpin_->setValue(script_.fps);
    const int index = priorityCombo_->findData(static_cast<int>(script_.priority));
    if (index >= 0)
        priorityCombo_->setCurrentIndex(index);
    loading_ = false;
    refreshShotList();
    // No publish() here: this is the host telling the dialog what the film IS,
    // not the dialog telling the host it changed. Echoing it back would be a
    // write on every tab switch.
    updateSummary();
}

void FilmProductionDialog::setTrajectory(int frameCount, double fps)
{
    script_.trajectoryFrames = std::max(0, frameCount);
    script_.trajectoryFps = fps > 0.0 ? fps : 10.0;

    const bool has = script_.hasTrajectory();
    priorityCombo_->setEnabled(has);
    if (!has) {
        // Without a trajectory there is nothing to reconcile against, so the
        // dropdown would be a control with no effect. Say why rather than
        // leaving a greyed-out box unexplained.
        priorityNote_->setText(
            tr("<i>This workspace has no trajectory, so there is only one "
               "timeline to obey.</i>"));
    }
    updateSummary();
    publish();
}

void FilmProductionDialog::refreshSavedList()
{
    savedList_->clear();
    QSettings settings;
    settings.beginGroup(PointOfViewDialog::settingsGroup());
    for (const QString& name : settings.childKeys()) {
        auto* item = new QListWidgetItem(name, savedList_);
        item->setData(Qt::UserRole, settings.value(name).toString());
    }
    settings.endGroup();
    if (savedList_->count() == 0) {
        auto* item = new QListWidgetItem(
            tr("(none saved — use \"Set point-of-view…\")"), savedList_);
        item->setFlags(Qt::NoItemFlags);
    }
}

void FilmProductionDialog::refreshShotList()
{
    const int previous = shotTable_->currentRow();
    loading_ = true;
    shotTable_->setRowCount(0);
    const auto count = static_cast<int>(script_.shots.size());
    // Seed the seconds column from the timing actually in force, so a film
    // built before per-shot times existed opens showing its real even split
    // rather than a column of zeros the user would have to fill in.
    const std::vector<double> lengths = script_.segmentDurations();
    shotTable_->setRowCount(count);
    for (int i = 0; i < count; ++i) {
        const render::FilmShot& shot = script_.shots[static_cast<std::size_t>(i)];
        auto* name = new QTableWidgetItem(tr("%1. %2").arg(i + 1).arg(
            shot.povName.isEmpty() ? tr("(current view)") : shot.povName));
        name->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
        shotTable_->setItem(i, ShotColumnName, name);

        // The last shot has nothing to transition TO and no segment to time,
        // so it gets neither control — an editable 0 s there would suggest a
        // move that never runs.
        if (i + 1 >= count) {
            for (const int column : {ShotColumnSeconds, ShotColumnTransition}) {
                auto* blank = new QTableWidgetItem(QStringLiteral("—"));
                blank->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
                blank->setTextAlignment(Qt::AlignCenter);
                shotTable_->setItem(i, column, blank);
            }
            continue;
        }

        auto* seconds = new QDoubleSpinBox(shotTable_);
        seconds->setRange(0.05, 3600.0);
        seconds->setDecimals(2);
        seconds->setSingleStep(0.25);
        seconds->setSuffix(tr(" s"));
        seconds->setValue(lengths[static_cast<std::size_t>(i)]);
        connect(seconds, &QDoubleSpinBox::valueChanged, this,
                &FilmProductionDialog::applyShotTimeline);
        shotTable_->setCellWidget(i, ShotColumnSeconds, seconds);

        auto* transition = makeTransitionCombo(shotTable_);
        const int index =
            transition->findData(static_cast<int>(shot.transitionToNext));
        if (index >= 0)
            transition->setCurrentIndex(index);
        connect(transition, &QComboBox::currentIndexChanged, this,
                &FilmProductionDialog::applyShotTimeline);
        shotTable_->setCellWidget(i, ShotColumnTransition, transition);
    }
    shotTable_->resizeColumnsToContents();
    loading_ = false;
    if (count > 0)
        shotTable_->setCurrentCell(std::clamp(previous, 0, count - 1),
                                   ShotColumnName);
    shotSelectionChanged();
    updateSummary();
}

double FilmProductionDialog::shotDurationSum() const
{
    double sum = 0.0;
    for (int row = 0; row < shotTable_->rowCount(); ++row) {
        const auto* spin = qobject_cast<QDoubleSpinBox*>(
            shotTable_->cellWidget(row, ShotColumnSeconds));
        if (spin)
            sum += spin->value();
    }
    return sum;
}

void FilmProductionDialog::rescaleShotDurations(double seconds)
{
    const auto count = static_cast<int>(script_.shots.size());
    if (count < 2 || seconds <= 0.0)
        return;
    const std::vector<double> lengths = script_.segmentDurations();
    double sum = 0.0;
    for (const double length : lengths)
        sum += length;
    if (sum <= 0.0)
        return;
    const double scale = seconds / sum;
    for (int i = 0; i + 1 < count; ++i) {
        script_.shots[static_cast<std::size_t>(i)].segmentSeconds =
            lengths[static_cast<std::size_t>(i)] * scale;
    }
}

int FilmProductionDialog::selectedShot() const
{
    const int row = shotTable_->currentRow();
    return row >= 0 && row < static_cast<int>(script_.shots.size()) ? row : -1;
}

void FilmProductionDialog::appendShot(render::FilmShot shot)
{
    // Adding a shot now LENGTHENS the film rather than subdividing it. With
    // explicit per-shot times that is the only coherent behaviour: the times
    // already on screen are what the user asked for, and silently squeezing
    // them to make room would undo the pacing they just set.
    const std::vector<double> lengths = script_.segmentDurations();
    double seed = 2.0;
    if (!lengths.empty()) {
        double sum = 0.0;
        for (const double length : lengths)
            sum += length;
        seed = sum / static_cast<double>(lengths.size());
    }
    for (std::size_t i = 0; i + 1 < script_.shots.size(); ++i)
        script_.shots[i].segmentSeconds = lengths[i];
    if (!script_.shots.empty())
        script_.shots.back().segmentSeconds = seed;
    script_.shots.push_back(std::move(shot));

    double total = 0.0;
    for (std::size_t i = 0; i + 1 < script_.shots.size(); ++i)
        total += script_.shots[i].segmentSeconds;
    if (total > 0.0) {
        script_.duration = total;
        const QSignalBlocker block(durationSpin_);
        durationSpin_->setValue(total);
    }
}

void FilmProductionDialog::addShotFromSaved()
{
    const QListWidgetItem* item = savedList_->currentItem();
    if (!item)
        return;
    const QString encoded = item->data(Qt::UserRole).toString();
    if (encoded.isEmpty())
        return; // the "(none saved)" placeholder
    render::FilmShot shot;
    shot.povName = item->text();
    shot.pov = PointOfViewDialog::decode(encoded);
    appendShot(std::move(shot));
    refreshShotList();
    shotTable_->setCurrentCell(shotTable_->rowCount() - 1, ShotColumnName);
    publish();
}

void FilmProductionDialog::addShotFromCurrentView()
{
    render::FilmShot shot;
    shot.pov = viewport_->camera().pointOfView();
    appendShot(std::move(shot));
    refreshShotList();
    shotTable_->setCurrentCell(shotTable_->rowCount() - 1, ShotColumnName);
    publish();
}

void FilmProductionDialog::removeShot()
{
    const int row = selectedShot();
    if (row < 0)
        return;
    script_.shots.erase(script_.shots.begin() + row);
    // Removing a shot removes the segment leaving it, so the film gets
    // shorter by exactly that much — the mirror of appendShot().
    double total = 0.0;
    for (std::size_t i = 0; i + 1 < script_.shots.size(); ++i)
        total += script_.shots[i].segmentSeconds;
    if (total > 0.0) {
        script_.duration = total;
        const QSignalBlocker block(durationSpin_);
        durationSpin_->setValue(total);
    }
    refreshShotList();
    publish();
}

void FilmProductionDialog::moveShot(int delta)
{
    const int row = selectedShot();
    const int target = row + delta;
    if (row < 0 || target < 0 || target >= static_cast<int>(script_.shots.size()))
        return;
    std::swap(script_.shots[static_cast<std::size_t>(row)],
              script_.shots[static_cast<std::size_t>(target)]);
    refreshShotList();
    shotTable_->setCurrentCell(target, ShotColumnName);
    publish();
}

void FilmProductionDialog::shotSelectionChanged()
{
    const int row = selectedShot();
    const bool valid = row >= 0;
    removeButton_->setEnabled(valid);
    upButton_->setEnabled(valid && row > 0);
    downButton_->setEnabled(valid
                            && row + 1 < static_cast<int>(script_.shots.size()));
    castTable_->setEnabled(valid);
    overlayOverrideCheck_->setEnabled(valid);
    if (!valid) {
        loading_ = true;
        castTable_->setRowCount(0);
        overlayList_->clear();
        overlayOverrideCheck_->setChecked(false);
        loading_ = false;
        overlayList_->setEnabled(false);
        return;
    }

    refreshCastTable();
    refreshOverlayList();
}

void FilmProductionDialog::refreshOverlayList()
{
    const int row = selectedShot();
    loading_ = true;
    overlayList_->clear();
    if (row >= 0) {
        const render::FilmShot& shot =
            script_.shots[static_cast<std::size_t>(row)];
        overlayOverrideCheck_->setChecked(shot.overridesOverlays);
        for (const auto& [id, label] : availableOverlays_) {
            auto* item = new QListWidgetItem(label, overlayList_);
            item->setData(Qt::UserRole, id);
            item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
            const bool on = std::find(shot.overlayIds.begin(),
                                      shot.overlayIds.end(), id)
                != shot.overlayIds.end();
            item->setCheckState(on ? Qt::Checked : Qt::Unchecked);
        }
    }
    loading_ = false;
    overlayList_->setEnabled(row >= 0 && overlayOverrideCheck_->isChecked());
    overlayNote_->setText(
        availableOverlays_.empty()
            ? tr("<i>No overlays in the scene. Add them in the "
                 "<b>Additional Overlays</b> dock and they will be offered "
                 "here.</i>")
            : QString());
}

void FilmProductionDialog::refreshCastTable()
{
    const int row = selectedShot();
    if (row < 0)
        return;
    const render::FilmShot& shot = script_.shots[static_cast<std::size_t>(row)];
    const int castCount = viewport_->style().castCount();

    loading_ = true;
    castTable_->setRowCount(castCount);
    for (int cast = 0; cast < castCount; ++cast) {
        auto* label = new QTableWidgetItem(QString::number(cast));
        label->setFlags(Qt::ItemIsEnabled);
        castTable_->setItem(cast, 0, label);

        float opacity = 1.0f;
        for (const render::FilmCastOpacity& entry : shot.castOpacity)
            if (entry.cast == cast)
                opacity = entry.opacity;
        auto* spin = new QDoubleSpinBox(castTable_);
        spin->setRange(0.0, 1.0);
        spin->setDecimals(2);
        spin->setSingleStep(0.05);
        spin->setValue(opacity);
        connect(spin, &QDoubleSpinBox::valueChanged, this,
                &FilmProductionDialog::applyShotEdits);
        castTable_->setCellWidget(cast, 1, spin);
    }
    loading_ = false;
    castNote_->setText(
        castCount > 1
            ? QString()
            : tr("<i>This structure has a single cast. Split it in "
                 "Representation → Cast change… to fade parts separately.</i>"));
}

void FilmProductionDialog::applyShotEdits()
{
    if (loading_)
        return;
    const int row = selectedShot();
    if (row < 0)
        return;
    render::FilmShot& shot = script_.shots[static_cast<std::size_t>(row)];

    shot.overridesOverlays = overlayOverrideCheck_->isChecked();
    overlayList_->setEnabled(shot.overridesOverlays);
    shot.overlayIds.clear();
    if (shot.overridesOverlays) {
        for (int i = 0; i < overlayList_->count(); ++i) {
            const QListWidgetItem* item = overlayList_->item(i);
            if (item->checkState() == Qt::Checked)
                shot.overlayIds.push_back(item->data(Qt::UserRole).toInt());
        }
    }

    // Only casts actually moved off 1.0 are stored: a keyframe listing every
    // cast at full opacity would pin them there and defeat the ramping, and
    // would fight the Representation panel for casts the film never mentions.
    shot.castOpacity.clear();
    for (int cast = 0; cast < castTable_->rowCount(); ++cast) {
        const auto* spin =
            qobject_cast<QDoubleSpinBox*>(castTable_->cellWidget(cast, 1));
        if (!spin || spin->value() >= 0.999)
            continue;
        shot.castOpacity.push_back(
            {cast, static_cast<float>(spin->value())});
    }
    publish();
}

void FilmProductionDialog::applyShotTimeline()
{
    if (loading_)
        return;
    const auto count = static_cast<int>(script_.shots.size());
    for (int i = 0; i + 1 < count; ++i) {
        render::FilmShot& shot = script_.shots[static_cast<std::size_t>(i)];
        if (const auto* spin = qobject_cast<QDoubleSpinBox*>(
                shotTable_->cellWidget(i, ShotColumnSeconds)))
            shot.segmentSeconds = spin->value();
        if (const auto* combo = qobject_cast<QComboBox*>(
                shotTable_->cellWidget(i, ShotColumnTransition)))
            shot.transitionToNext = static_cast<render::FilmTransition>(
                combo->currentData().toInt());
    }
    // The total is the sum, not an independent number the user has to keep in
    // step by hand. Written back into the spin box without re-entering
    // applyTiming(), which would rescale the times just typed.
    const double sum = shotDurationSum();
    if (sum > 0.0) {
        script_.duration = sum;
        const QSignalBlocker block(durationSpin_);
        durationSpin_->setValue(sum);
    }
    updateSummary();
    publish();
}

void FilmProductionDialog::applyTiming()
{
    if (loading_)
        return;
    // Editing the total re-times the shots in proportion, so a film paced 1 s
    // / 4 s / 1 s stays paced that way when it is stretched to 30 s.
    rescaleShotDurations(durationSpin_->value());
    script_.duration = durationSpin_->value();
    script_.fps = fpsSpin_->value();
    script_.priority = static_cast<render::FilmTimelinePriority>(
        priorityCombo_->currentData().toInt());
    refreshShotList();
    updateSummary();
    publish();
}

void FilmProductionDialog::updateSummary()
{
    const int shots = static_cast<int>(script_.shots.size());
    if (shots == 0) {
        summaryLabel_->setText(
            tr("<b>No shots yet.</b> Add at least one point-of-view above."));
    } else {
        const std::vector<double> lengths = script_.segmentDurations();
        const auto [shortest, longest] =
            lengths.empty()
                ? std::pair<double, double>{0.0, 0.0}
                : std::pair<double, double>{
                      *std::min_element(lengths.begin(), lengths.end()),
                      *std::max_element(lengths.begin(), lengths.end())};
        summaryLabel_->setText(
            tr("<b>%1 shot(s), %2 s, %3 fps → %4 frames.</b> "
               "%5 transition(s), %6.")
                .arg(shots)
                .arg(script_.effectiveDuration(), 0, 'f', 2)
                .arg(script_.fps)
                .arg(script_.frameCount())
                .arg(std::max(0, shots - 1))
                .arg(lengths.empty()
                         ? tr("none to time")
                         : (std::abs(longest - shortest) < 1e-6
                                ? tr("each running %1 s")
                                      .arg(longest, 0, 'f', 2)
                                : tr("running %1 s to %2 s")
                                      .arg(shortest, 0, 'f', 2)
                                      .arg(longest, 0, 'f', 2))));
    }

    if (!script_.hasTrajectory())
        return;
    // Spell out what the priority rule DID, in the two numbers the user can
    // check: how long the trajectory would run on its own, and how long it
    // now runs. A rule stated as prose is a rule nobody verifies.
    const double natural =
        static_cast<double>(script_.trajectoryFrames) / script_.trajectoryFps;
    const double effective = script_.effectiveDuration();
    priorityNote_->setText(
        script_.priority == render::FilmTimelinePriority::Film
            ? tr("<i>Film wins: the %1-frame trajectory (%2 s on its own) is "
                 "re-timed to %3 s — %4× its natural speed.</i>")
                  .arg(script_.trajectoryFrames)
                  .arg(natural, 0, 'f', 2)
                  .arg(effective, 0, 'f', 2)
                  .arg(effective > 0.0 ? natural / effective : 1.0, 0, 'f', 2)
            : tr("<i>Trajectory wins: the film runs %1 s — the %2-frame "
                 "trajectory's own length at %3 fps — and the camera moves "
                 "are compressed to fit.</i>")
                  .arg(effective, 0, 'f', 2)
                  .arg(script_.trajectoryFrames)
                  .arg(script_.trajectoryFps, 0, 'f', 1));
}

void FilmProductionDialog::publish()
{
    updateSummary();
    Q_EMIT scriptChanged(script_);
}

} // namespace calango::gui
