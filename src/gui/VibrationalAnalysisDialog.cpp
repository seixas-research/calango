#include "gui/VibrationalAnalysisDialog.hpp"

#include "gui/GuiUtils.hpp"

#include "python_bridge/AseBridge.hpp"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonObject>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSlider>
#include <QTimer>
#include <QVBoxLayout>

#include <cmath>
#include <cstdint>

namespace calango::gui {

namespace {

/// Animation tick. 20 ms is smooth without spending the GUI thread on geometry
/// uploads; the speed slider changes the phase step, not this interval, so the
/// frame rate stays constant while the motion slows or quickens.
constexpr int kTickMs = 20;
/// Frames per vibrational period in the exported trajectory. 32 is smooth at
/// the timeline's playback rate without inflating a project file.
constexpr int kTrajectoryFrames = 32;
/// An eigenbasis is orthonormal to machine precision; anything above this is a
/// broken or rescaled export rather than rounding.
constexpr double kOverlapWarnThreshold = 1e-3;

core::Vec3 vec3From(const QJsonArray& array)
{
    return {array.size() > 0 ? array.at(0).toDouble() : 0.0,
            array.size() > 1 ? array.at(1).toDouble() : 0.0,
            array.size() > 2 ? array.at(2).toDouble() : 0.0};
}

} // namespace

VibrationalAnalysisDialog::VibrationalAnalysisDialog(
    const QList<QPair<QString, QString>>& candidates, const QString& preselected,
    std::shared_ptr<const core::Structure> fallbackStructure, QWidget* parent)
    : QDialog(parent), fallback_(std::move(fallbackStructure))
{
    setWindowTitle(tr("Vibrational Mode Analysis"));
    resize(520, 400);

    auto* layout = new QVBoxLayout(this);
    auto* form = new QFormLayout;
    layout->addLayout(form);

    // -- Source phonon run --------------------------------------------------
    // Same two ways in as every other inheriting module (see
    // MlwfSourceSelector): the combo covers runs from this session, Browse…
    // covers a job from an earlier one or copied back from a cluster.
    auto* sourceRow = new QHBoxLayout;
    sourceCombo_ = new QComboBox(this);
    sourceCombo_->setSizeAdjustPolicy(
        QComboBox::AdjustToMinimumContentsLengthWithIcon);
    sourceCombo_->setToolTip(
        tr("A completed phonon run. Its phonon_modes.json supplies the "
           "eigenvectors and its structure.extxyz the geometry they are "
           "indexed by — both come from the run, not from the active tab."));
    for (const auto& [label, directory] : candidates)
        sourceCombo_->addItem(label, directory);
    sourceRow->addWidget(sourceCombo_, 1);
    auto* browseButton = new QPushButton(tr("Browse…"), this);
    browseButton->setToolTip(
        tr("Pick a phonon job directory that is not in the list — the one "
           "holding phonon_band.json."));
    sourceRow->addWidget(browseButton);
    form->addRow(tr("Phonon run:"), sourceRow);

    sourceStatus_ = new QLabel(this);
    sourceStatus_->setWordWrap(true);
    sourceStatus_->setTextFormat(Qt::RichText);
    form->addRow(QString(), sourceStatus_);

    qpointCombo_ = new QComboBox(this);
    qpointCombo_->setToolTip(
        tr("Reciprocal-space point q at which the mode is evaluated. Γ is the "
           "zone center, where all cells move in phase; at a zone-boundary q "
           "neighboring cells move in antiphase."));
    form->addRow(tr("q-point:"), qpointCombo_);

    modeCombo_ = new QComboBox(this);
    modeCombo_->setToolTip(
        tr("Phonon branch at the selected q, listed by frequency. The three "
           "lowest at Γ are the acoustic modes (rigid translations); they are "
           "labelled as such from the acoustic sum rule rather than guessed "
           "from their position in the list."));
    form->addRow(tr("Mode ω:"), modeCombo_);

    modeLabel_ = new QLabel(this);
    modeLabel_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    modeLabel_->setWordWrap(true);
    form->addRow(tr("Frequency:"), modeLabel_);

    amplitudeSlider_ = new QSlider(Qt::Horizontal, this);
    amplitudeSlider_->setRange(1, 100);
    amplitudeSlider_->setValue(30);
    amplitudeSlider_->setToolTip(
        tr("Peak displacement of the largest-moving atom, in hundredths of an "
           "Å. Purely a visualization scale — a harmonic eigenvector has no "
           "intrinsic amplitude — but anchoring it to the resulting MOTION "
           "rather than to the eigenvector's components means the same "
           "physical mode animates at the same size whichever driver wrote "
           "the file."));
    form->addRow(tr("Amplitude:"), amplitudeSlider_);

    speedSlider_ = new QSlider(Qt::Horizontal, this);
    speedSlider_->setRange(1, 100);
    speedSlider_->setValue(35);
    speedSlider_->setToolTip(
        tr("Animation rate. Real phonon periods are ~10⁻¹⁴ s, so this is not "
           "(and cannot be) real time — it is set for watchability."));
    form->addRow(tr("Speed:"), speedSlider_);

    noticeLabel_ = new QLabel(this);
    noticeLabel_->setWordWrap(true);
    layout->addWidget(noticeLabel_);
    layout->addStretch(1);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    playButton_ = buttons->addButton(tr("Play"), QDialogButtonBox::ActionRole);
    connect(playButton_, &QPushButton::clicked, this,
            &VibrationalAnalysisDialog::togglePlay);
    trajectoryButton_ = buttons->addButton(tr("Create Mode Trajectory Tab"),
                                           QDialogButtonBox::ActionRole);
    trajectoryButton_->setToolTip(
        tr("Open one full vibrational period of this mode as a new workspace "
           "tab, scrubbable on the timeline.\n"
           "Each frame carries the instantaneous harmonic restoring forces "
           "F = −Mω²u, so the Representation panel's Vector Overlay can show "
           "them in 3D."));
    connect(trajectoryButton_, &QPushButton::clicked, this,
            &VibrationalAnalysisDialog::createModeTrajectory);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);

    timer_ = new QTimer(this);
    timer_->setInterval(kTickMs);
    connect(timer_, &QTimer::timeout, this, &VibrationalAnalysisDialog::advance);

    connect(browseButton, &QPushButton::clicked, this,
            &VibrationalAnalysisDialog::browseForSource);
    connect(sourceCombo_, &QComboBox::currentIndexChanged, this,
            [this] { loadSource(); });
    connect(qpointCombo_, &QComboBox::currentIndexChanged, this,
            &VibrationalAnalysisDialog::onQPointChanged);
    connect(modeCombo_, &QComboBox::currentIndexChanged, this,
            &VibrationalAnalysisDialog::onModeChanged);
    // Amplitude edits must be visible while paused, so re-apply immediately
    // rather than waiting for the next tick.
    connect(amplitudeSlider_, &QSlider::valueChanged, this,
            [this] { applyDisplacement(); });

    // The preselected run is added rather than assumed present: the phonon
    // viewer can be showing a directory the Processes panel never tracked (a
    // project reopened, a job browsed to), and dropping it here would silently
    // switch the user to a different run than the one they came from.
    if (!preselected.isEmpty()) {
        const QString absolute = QDir(preselected).absolutePath();
        int index = -1;
        for (int i = 0; i < sourceCombo_->count(); ++i)
            if (QDir(sourceCombo_->itemData(i).toString()).absolutePath()
                == absolute)
                index = i;
        if (index < 0) {
            sourceCombo_->addItem(QDir(absolute).dirName(), absolute);
            index = sourceCombo_->count() - 1;
        }
        const QSignalBlocker blocker(sourceCombo_);
        sourceCombo_->setCurrentIndex(index);
    } else if (sourceCombo_->count() > 0) {
        // Newest last in the process list, and the newest run is nearly always
        // the one being followed up.
        const QSignalBlocker blocker(sourceCombo_);
        sourceCombo_->setCurrentIndex(sourceCombo_->count() - 1);
    }
    loadSource();
}

VibrationalAnalysisDialog::~VibrationalAnalysisDialog()
{
    restoreStructure();
}

void VibrationalAnalysisDialog::browseForSource()
{
    const QString chosen = QFileDialog::getExistingDirectory(
        this, tr("Select Phonon Job Directory"),
        sourceCombo_->currentData().toString());
    if (chosen.isEmpty())
        return;
    // Added rather than swapped in, so a browse that turns out to be the wrong
    // folder does not lose the tracked runs the user could pick instead.
    const QString absolute = QDir(chosen).absolutePath();
    for (int i = 0; i < sourceCombo_->count(); ++i) {
        if (QDir(sourceCombo_->itemData(i).toString()).absolutePath()
            == absolute) {
            sourceCombo_->setCurrentIndex(i);
            return;
        }
    }
    sourceCombo_->addItem(QDir(absolute).dirName(), absolute);
    sourceCombo_->setCurrentIndex(sourceCombo_->count() - 1);
}

void VibrationalAnalysisDialog::loadSource()
{
    // Whatever was on the viewport belonged to the PREVIOUS run's geometry;
    // put it back before the reference changes underneath it, or a source
    // switch leaves the old run's displaced structure on screen forever.
    restoreStructure();
    modes_ = core::VibrationalModeSet{};
    diagnostics_.clear();
    reference_.reset();
    {
        const QSignalBlocker qBlocker(qpointCombo_);
        const QSignalBlocker modeBlocker(modeCombo_);
        qpointCombo_->clear();
        modeCombo_->clear();
    }
    modeLabel_->setText(QStringLiteral("—"));
    noticeLabel_->clear();
    noticeLabel_->setStyleSheet(QString());

    const QString directory = sourceCombo_->currentIndex() < 0
        ? QString()
        : sourceCombo_->currentData().toString();
    if (directory.isEmpty()) {
        sourceStatus_->setText(
            tr("<b>No phonon run selected.</b> This module reads a completed "
               "phonon calculation — run Simulation → Phonon… first, or use "
               "Browse… to point at a finished job directory."));
        setAnimationEnabled(false);
        return;
    }

    const QString refusal = readModes(directory);
    if (!refusal.isEmpty()) {
        sourceStatus_->setText(
            QStringLiteral("<b style=\"color:#e06c5a\">%1</b>").arg(refusal));
        setAnimationEnabled(false);
        return;
    }

    resolveStructure(directory);

    // The one mismatch that produces a convincing lie: eigenvectors indexed by
    // a different number of atoms than the structure they are drawn on. Every
    // atom past the shorter list simply would not move, which reads as a
    // localized mode. Refuse instead.
    std::size_t patternAtoms = 0;
    for (const core::VibrationalQPoint& qpoint : modes_.qpoints)
        if (!qpoint.eigenvectorsReal.empty())
            patternAtoms = qpoint.eigenvectorsReal.front().size();
    if (patternAtoms > 0 && reference_
        && patternAtoms != reference_->size()) {
        sourceStatus_->setText(
            tr("<b style=\"color:#e06c5a\">Geometry mismatch</b> — the "
               "eigenvectors cover %1 atom(s) but the structure has %2. The "
               "eigenvector components are indexed by the run's own atom "
               "order, so they cannot be drawn on a different system.")
                .arg(patternAtoms)
                .arg(reference_->size()));
        setAnimationEnabled(false);
        modes_ = core::VibrationalModeSet{};
        diagnostics_.clear();
        return;
    }

    const bool animatable = modes_.hasEigenvectors() && reference_ != nullptr;
    setAnimationEnabled(animatable);

    if (!modes_.hasEigenvectors()) {
        // Frequencies alone still let the user browse what the run found, but
        // there is nothing to animate. Say exactly that — inventing a
        // displacement pattern would look convincing and be meaningless.
        noticeLabel_->setText(
            tr("⚠ This run exported no eigenvectors (phonon_modes.json), so "
               "the modes can be listed but not animated. Re-run the phonon "
               "calculation with mode export enabled to animate them."));
        noticeLabel_->setStyleSheet(QStringLiteral("color: #d9534f;"));
    } else if (!reference_) {
        noticeLabel_->setText(
            tr("⚠ No geometry: this run kept no structure.extxyz and no "
               "structure is open. Open the system the phonons were computed "
               "for in a tab, then reopen this module."));
        noticeLabel_->setStyleSheet(QStringLiteral("color: #d9534f;"));
    } else {
        noticeLabel_->setText(
            tr("Displacements are shown on the main 3D viewport. Closing this "
               "dialog restores the undisplaced structure."));
    }

    // Diagnostics per q-point: computed once, read by the mode list (which
    // branches are acoustic) and by the health line below the source combo.
    double worstOverlap = 0.0;
    diagnostics_.reserve(modes_.qpoints.size());
    for (const core::VibrationalQPoint& qpoint : modes_.qpoints) {
        diagnostics_.push_back(
            reference_ ? core::analyzeModes(*reference_, qpoint, modes_.convention)
                       : std::vector<core::ModeDiagnostics>{});
        for (const core::ModeDiagnostics& mode : diagnostics_.back())
            worstOverlap = std::max(worstOverlap, mode.maxOverlap);
    }

    // The convention is only meaningful when there ARE eigenvectors; reporting
    // the enum's default value on a frequencies-only run would state a fact
    // about a file that was never read.
    QString status =
        modes_.hasEigenvectors()
        ? tr("%1 q-point(s) from <code>%2</code>; eigenvectors stored as %3.")
              .arg(modes_.qpoints.size())
              .arg(QFileInfo(directory).fileName().toHtmlEscaped())
              .arg(modes_.convention == core::EigenvectorConvention::MassWeighted
                       ? tr("mass-weighted (√M·u)")
                       : tr("displacement (u)"))
        : tr("Frequencies only, from <code>%1</code>.")
              .arg(QFileInfo(directory).fileName().toHtmlEscaped());
    // An eigenbasis is orthonormal by construction. If the file's is not, the
    // animation is of something other than a normal mode, and no amount of
    // watching it would reveal that.
    if (worstOverlap > kOverlapWarnThreshold)
        status += tr(" <span style=\"color:#e06c5a\">Warning: branches are "
                     "not orthogonal (max overlap %1) — this export is not an "
                     "eigenbasis.</span>")
                      .arg(worstOverlap, 0, 'g', 3);
    sourceStatus_->setText(status);

    const QSignalBlocker blocker(qpointCombo_);
    for (const core::VibrationalQPoint& qpoint : modes_.qpoints)
        qpointCombo_->addItem(QString::fromStdString(qpoint.label));
    if (modes_.qpoints.empty())
        return;
    qpointCombo_->setCurrentIndex(0);
    onQPointChanged(0);
}

QString VibrationalAnalysisDialog::readModes(const QString& directory)
{
    const QDir dir(directory);
    if (!dir.exists())
        return tr("%1 does not exist.").arg(directory);

    // phonon_modes.json is the only file that carries eigenvectors;
    // phonon_band.json has frequencies alone.
    const QJsonObject modes =
        readJsonObject(dir.filePath(QStringLiteral("phonon_modes.json")));
    const QJsonObject band =
        readJsonObject(dir.filePath(QStringLiteral("phonon_band.json")));
    if (modes.isEmpty() && band.isEmpty())
        return tr("%1 holds no phonon_band.json, so it is not a completed "
                  "phonon run. That file is written when the dispersion has "
                  "been evaluated.")
            .arg(directory);

    // Which normalization the eigenvector components carry decides whether the
    // animation is right — see core::EigenvectorConvention. Runs from before
    // the key was written are inferred from the ENCODING, which happens to be
    // a reliable tell: the phonopy driver writes {"re":…,"im":…} objects and
    // the ASE driver writes bare [x,y,z] triples. Fragile as a contract, which
    // is exactly why the explicit key now exists.
    const QString declared =
        modes.value(QStringLiteral("eigenvector_convention")).toString();
    bool conventionKnown = false;
    if (declared == QLatin1String("displacement")) {
        modes_.convention = core::EigenvectorConvention::Displacement;
        conventionKnown = true;
    } else if (declared == QLatin1String("mass-weighted")) {
        modes_.convention = core::EigenvectorConvention::MassWeighted;
        conventionKnown = true;
    }

    for (const auto& entry : modes.value(QStringLiteral("qpoints")).toArray()) {
        const QJsonObject object = entry.toObject();
        core::VibrationalQPoint qpoint;
        qpoint.label =
            object.value(QStringLiteral("label")).toString().toStdString();
        const QJsonArray q = object.value(QStringLiteral("q")).toArray();
        for (int i = 0; i < 3 && i < q.size(); ++i)
            qpoint.q[i] = q.at(i).toDouble();
        if (qpoint.label.empty())
            qpoint.label = QStringLiteral("(%1 %2 %3)")
                               .arg(qpoint.q[0], 0, 'g', 3)
                               .arg(qpoint.q[1], 0, 'g', 3)
                               .arg(qpoint.q[2], 0, 'g', 3)
                               .toStdString();
        qpoint.frequenciesCm =
            toDoubleVector(object.value(QStringLiteral("frequencies")).toArray());
        // Γ-only irrep labels; runs predating the export simply lack the key
        // and the combo shows plain frequencies.
        for (const auto& irrep : object.value(QStringLiteral("irreps")).toArray())
            qpoint.irreps.push_back(irrep.toString().toStdString());
        for (const auto& branch :
             object.value(QStringLiteral("eigenvectors")).toArray()) {
            std::vector<core::Vec3> real;
            std::vector<core::Vec3> imag;
            for (const auto& atom : branch.toArray()) {
                const QJsonObject displacement = atom.toObject();
                if (displacement.contains(QStringLiteral("re"))) {
                    real.push_back(
                        vec3From(displacement.value(QStringLiteral("re")).toArray()));
                    imag.push_back(
                        vec3From(displacement.value(QStringLiteral("im")).toArray()));
                    if (!conventionKnown)
                        modes_.convention =
                            core::EigenvectorConvention::MassWeighted;
                } else {
                    // A real-valued eigenvector (Γ, or a run that exported only
                    // the real part) is stored as a bare [x, y, z].
                    real.push_back(vec3From(atom.toArray()));
                    imag.emplace_back();
                    if (!conventionKnown)
                        modes_.convention =
                            core::EigenvectorConvention::Displacement;
                }
            }
            qpoint.eigenvectorsReal.push_back(std::move(real));
            qpoint.eigenvectorsImag.push_back(std::move(imag));
        }
        modes_.qpoints.push_back(std::move(qpoint));
    }

    if (!modes_.hasEigenvectors()) {
        modes_.qpoints.clear();
        const QJsonArray frequencies =
            band.value(QStringLiteral("frequencies")).toArray();
        if (frequencies.isEmpty())
            return tr("%1 has a phonon_band.json with no frequencies in it.")
                .arg(directory);
        core::VibrationalQPoint gamma;
        gamma.label = "Γ";
        gamma.frequenciesCm = toDoubleVector(frequencies.first().toArray());
        modes_.qpoints.push_back(std::move(gamma));
    }
    return {};
}

void VibrationalAnalysisDialog::resolveStructure(const QString& directory)
{
    reference_ = fallback_;
    const QString path =
        QDir(directory).filePath(QStringLiteral("structure.extxyz"));
    // Guarded on existence rather than on the exception: readStructure needs a
    // live interpreter, and asking Python to open a file that is not there is a
    // slow way to learn what QFileInfo already knows.
    if (!QFileInfo::exists(path))
        return;
    try {
        reference_ = std::make_shared<const core::Structure>(
            pybridge::AseBridge::readStructure(path.toStdString()));
    } catch (const std::exception&) {
        // Readable job, unreadable geometry: fall back to whatever the host
        // handed over. The atom-count check above is what stops that fallback
        // from being drawn with the wrong eigenvectors.
    }
}

bool VibrationalAnalysisDialog::currentSelection(std::size_t& qIndex,
                                                 std::size_t& branch) const
{
    const int q = qpointCombo_->currentIndex();
    const int mode = modeCombo_->currentIndex();
    if (q < 0 || mode < 0 || static_cast<std::size_t>(q) >= modes_.qpoints.size())
        return false;
    qIndex = static_cast<std::size_t>(q);
    branch = static_cast<std::size_t>(mode);
    return true;
}

void VibrationalAnalysisDialog::setAnimationEnabled(bool enabled)
{
    if (!enabled)
        timer_->stop();
    playButton_->setText(tr("Play"));
    playButton_->setEnabled(enabled);
    trajectoryButton_->setEnabled(enabled);
    amplitudeSlider_->setEnabled(enabled);
    speedSlider_->setEnabled(enabled);
}

void VibrationalAnalysisDialog::onQPointChanged(int index)
{
    if (index < 0 || static_cast<std::size_t>(index) >= modes_.qpoints.size())
        return;
    const core::VibrationalQPoint& qpoint =
        modes_.qpoints[static_cast<std::size_t>(index)];
    static const std::vector<core::ModeDiagnostics> kNoDiagnostics;
    const std::vector<core::ModeDiagnostics>& diagnostics =
        static_cast<std::size_t>(index) < diagnostics_.size()
        ? diagnostics_[static_cast<std::size_t>(index)]
        : kNoDiagnostics;
    const QSignalBlocker blocker(modeCombo_);
    modeCombo_->clear();
    for (std::size_t branch = 0; branch < qpoint.frequenciesCm.size(); ++branch) {
        const double frequency = qpoint.frequenciesCm[branch];
        // At Γ each branch carries its irreducible representation, when the
        // run could assign one — the mode's symmetry name, next to its
        // frequency.
        const QString irrep = branch < qpoint.irreps.size()
            ? QString::fromStdString(qpoint.irreps[branch])
            : QString();
        // "acoustic" is read off the acoustic sum rule, not off the branch
        // index: at a general q the three lowest branches are NOT rigid
        // translations, and at Γ a soft optical mode can sit below one.
        const bool acoustic =
            branch < diagnostics.size() && diagnostics[branch].rigidTranslation;
        modeCombo_->addItem(tr("#%1 — %2 cm⁻¹%3%4%5")
                                .arg(branch + 1)
                                .arg(frequency, 0, 'f', 2)
                                .arg(irrep.isEmpty()
                                         ? QString()
                                         : QStringLiteral("  [%1]").arg(irrep))
                                .arg(acoustic ? tr("  (acoustic)") : QString())
                                .arg(frequency < 0.0 ? tr("  (imaginary)")
                                                     : QString()));
    }
    modeCombo_->setCurrentIndex(0);
    onModeChanged(0);
}

void VibrationalAnalysisDialog::onModeChanged(int)
{
    phase_ = 0.0;
    updateModeLabel();
    applyDisplacement();
}

void VibrationalAnalysisDialog::updateModeLabel()
{
    std::size_t q = 0;
    std::size_t branch = 0;
    if (!currentSelection(q, branch)) {
        modeLabel_->setText(QStringLiteral("—"));
        return;
    }
    const core::VibrationalQPoint& qpoint = modes_.qpoints[q];
    if (branch >= qpoint.frequenciesCm.size()) {
        modeLabel_->setText(QStringLiteral("—"));
        return;
    }
    const double cm = qpoint.frequenciesCm[branch];
    QString text = tr("%1 cm⁻¹   ·   %2 meV   ·   %3 THz")
                       .arg(cm, 0, 'f', 2)
                       .arg(core::wavenumberToMev(cm), 0, 'f', 3)
                       .arg(core::wavenumberToThz(cm), 0, 'f', 3);
    if (branch < qpoint.irreps.size() && !qpoint.irreps[branch].empty())
        text += tr("   ·   irrep %1")
                    .arg(QString::fromStdString(qpoint.irreps[branch]));
    if (q < diagnostics_.size() && branch < diagnostics_[q].size()) {
        const core::ModeDiagnostics& mode = diagnostics_[q][branch];
        if (mode.degeneracy > 1)
            text += tr("   ·   %1-fold degenerate").arg(mode.degeneracy);
        if (mode.rigidTranslation)
            text += tr("   ·   acoustic (rigid translation)");
    }
    modeLabel_->setText(text);
}

void VibrationalAnalysisDialog::applyDisplacement()
{
    std::size_t q = 0;
    std::size_t branch = 0;
    if (!reference_ || !currentSelection(q, branch))
        return;
    core::ModeDisplacement options;
    options.amplitudeAng = amplitudeSlider_->value() / 100.0;
    options.phase = phase_;
    // No forces on the live animation: the arrows would be redrawn 50x a
    // second and the overlay is off by default anyway. The exported trajectory
    // carries them, which is where they are actually inspected.
    options.withDynamics = false;
    // The host re-frames nothing: re-framing the camera every tick would make
    // the structure jitter in place instead of showing the motion.
    if (auto displaced =
            core::displaceByMode(*reference_, modes_, q, branch, options))
        Q_EMIT previewStructureRequested(std::move(displaced));
}

void VibrationalAnalysisDialog::createModeTrajectory()
{
    std::size_t q = 0;
    std::size_t branch = 0;
    if (!reference_ || !currentSelection(q, branch)) {
        QMessageBox::information(
            this, tr("Create Mode Trajectory Tab"),
            tr("No mode is selected, or this run exported no eigenvectors — "
               "either way there is no displacement pattern to animate."));
        return;
    }
    core::ModeDisplacement options;
    options.amplitudeAng = amplitudeSlider_->value() / 100.0;
    const std::vector<std::shared_ptr<core::Structure>> frames =
        core::modeTrajectory(*reference_, modes_, q, branch, options,
                             kTrajectoryFrames);
    if (frames.empty()) {
        QMessageBox::information(this, tr("Create Mode Trajectory Tab"),
                                 tr("Could not build the mode trajectory."));
        return;
    }

    const core::VibrationalQPoint& qpoint = modes_.qpoints[q];
    const double frequencyCm = branch < qpoint.frequenciesCm.size()
        ? qpoint.frequenciesCm[branch]
        : 0.0;
    Q_EMIT modeTrajectoryRequested(
        frames, tr("Mode %1 @ %2 — %3 cm⁻¹")
                    .arg(branch + 1)
                    .arg(QString::fromStdString(qpoint.label))
                    .arg(frequencyCm, 0, 'f', 1));
}

void VibrationalAnalysisDialog::advance()
{
    // Phase step scales with the speed slider; the tick interval stays fixed
    // so the frame rate does not change with the speed.
    phase_ += 0.01 * speedSlider_->value();
    if (phase_ > 2.0 * M_PI)
        phase_ -= 2.0 * M_PI;
    applyDisplacement();
}

void VibrationalAnalysisDialog::togglePlay()
{
    if (timer_->isActive()) {
        timer_->stop();
        playButton_->setText(tr("Play"));
    } else {
        timer_->start();
        playButton_->setText(tr("Pause"));
    }
}

void VibrationalAnalysisDialog::restoreStructure()
{
    timer_->stop();
    if (reference_)
        Q_EMIT previewStructureRequested(reference_);
}

} // namespace calango::gui
