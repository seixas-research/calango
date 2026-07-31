#include "gui/VibrationalAnalysisDialog.hpp"

#include "core/Element.hpp"

#include "gui/GuiUtils.hpp"
#include "gui/ViewportWidget.hpp"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonObject>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QSlider>
#include <QTimer>
#include <QVBoxLayout>

#include <cmath>

namespace calango::gui {

namespace {

/// Animation tick. 20 ms is smooth without spending the GUI thread on geometry
/// uploads; the speed slider changes the phase step, not this interval, so the
/// frame rate stays constant while the motion slows or quickens.
constexpr int kTickMs = 20;
/// cm⁻¹ → meV, for the second unit in the mode label.
constexpr double kCmToMev = 0.1239841984;
/// cm⁻¹ → Hz (c in cm/s), for the angular frequency the restoring force needs.
constexpr double kCmToHz = 2.99792458e10;
/// amu·Å·(rad/s)² → eV/Å. 1 u = 1.66053907e-27 kg, 1 Å = 1e-10 m,
/// 1 eV = 1.602176634e-19 J, so the factor is m_u·Å²/eV = 1.0360e-28... in
/// practice: F[eV/Å] = M[u]·ω²[s⁻²]·u[Å] × (1.66053907e-27 × 1e-20 / 1.602176634e-19).
constexpr double kForceUnit = 1.66053907e-27 * 1e-20 / 1.602176634e-19;
/// Å/s → ASE's velocity unit, which is Å per ASE time unit.
///
/// ASE works in eV, Å and u, which fixes its time unit at Å·sqrt(u/eV) =
/// 1.0180505671e-14 s (≈ 10.18 fs) — so a velocity in Å/s is multiplied by
/// that many seconds to express it per ASE time unit. Written out rather than
/// left in Å/fs because ase.Atoms.set_velocities() is what the export goes
/// through, and a velocity in the wrong unit is not an error anywhere: it
/// round-trips, plots, and integrates — as a trajectory ten times too fast.
constexpr double kVelocityUnit = 1.0180505671156725e-14;
/// Frames per vibrational period in the exported trajectory. 32 is smooth at
/// the timeline's playback rate without inflating a project file.
constexpr int kTrajectoryFrames = 32;

core::Vec3 vec3From(const QJsonArray& array)
{
    return {array.size() > 0 ? array.at(0).toDouble() : 0.0,
            array.size() > 1 ? array.at(1).toDouble() : 0.0,
            array.size() > 2 ? array.at(2).toDouble() : 0.0};
}

} // namespace

VibrationalAnalysisDialog::VibrationalAnalysisDialog(
    const QString& directory, std::shared_ptr<const core::Structure> structure,
    ViewportWidget* viewport, QWidget* parent)
    : QDialog(parent), reference_(std::move(structure)), viewport_(viewport)
{
    setWindowTitle(tr("Vibrational Analysis"));
    resize(440, 340);

    auto* layout = new QVBoxLayout(this);
    auto* form = new QFormLayout;
    layout->addLayout(form);

    qpointCombo_ = new QComboBox(this);
    qpointCombo_->setToolTip(
        tr("Reciprocal-space point q at which the mode is evaluated. Γ is the "
           "zone center, where all cells move in phase; at a zone-boundary q "
           "neighboring cells move in antiphase."));
    form->addRow(tr("q-point:"), qpointCombo_);

    modeCombo_ = new QComboBox(this);
    modeCombo_->setToolTip(
        tr("Phonon branch at the selected q, listed by frequency. The three "
           "lowest at Γ are the acoustic modes (rigid translations)."));
    form->addRow(tr("Mode ω:"), modeCombo_);

    modeLabel_ = new QLabel(this);
    modeLabel_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    form->addRow(tr("Frequency:"), modeLabel_);

    amplitudeSlider_ = new QSlider(Qt::Horizontal, this);
    amplitudeSlider_->setRange(1, 100);
    amplitudeSlider_->setValue(30);
    amplitudeSlider_->setToolTip(
        tr("Peak displacement in hundredths of an Å per unit eigenvector "
           "component. Purely a visualization scale — the harmonic eigenvector "
           "has no intrinsic amplitude, so this is chosen for legibility, not "
           "physical accuracy."));
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

    connect(qpointCombo_, &QComboBox::currentIndexChanged, this,
            &VibrationalAnalysisDialog::onQPointChanged);
    connect(modeCombo_, &QComboBox::currentIndexChanged, this,
            &VibrationalAnalysisDialog::onModeChanged);
    // Amplitude edits must be visible while paused, so re-apply immediately
    // rather than waiting for the next tick.
    connect(amplitudeSlider_, &QSlider::valueChanged, this,
            [this] { applyDisplacement(); });

    load(directory);
}

VibrationalAnalysisDialog::~VibrationalAnalysisDialog()
{
    restoreStructure();
}

void VibrationalAnalysisDialog::load(const QString& directory)
{
    // phonon_modes.json is the only file that carries eigenvectors;
    // phonon_band.json has frequencies alone.
    const QJsonObject modes =
        readJsonObject(directory + QStringLiteral("/phonon_modes.json"));
    if (!modes.isEmpty()) {
        for (const auto& entry : modes.value(QStringLiteral("qpoints")).toArray()) {
            const QJsonObject object = entry.toObject();
            QPointModes qpoint;
            qpoint.label = object.value(QStringLiteral("label")).toString();
            const QJsonArray q = object.value(QStringLiteral("q")).toArray();
            for (int i = 0; i < 3 && i < q.size(); ++i)
                qpoint.q[i] = q.at(i).toDouble();
            if (qpoint.label.isEmpty())
                qpoint.label = QStringLiteral("(%1 %2 %3)")
                                   .arg(qpoint.q[0], 0, 'g', 3)
                                   .arg(qpoint.q[1], 0, 'g', 3)
                                   .arg(qpoint.q[2], 0, 'g', 3);
            qpoint.frequenciesCm =
                toDoubleVector(object.value(QStringLiteral("frequencies")).toArray());
            // Γ-only irrep labels; runs predating the export simply lack the
            // key and the combo shows plain frequencies.
            for (const auto& irrep :
                 object.value(QStringLiteral("irreps")).toArray())
                qpoint.irreps.push_back(irrep.toString());
            for (const auto& branch :
                 object.value(QStringLiteral("eigenvectors")).toArray()) {
                std::vector<core::Vec3> real;
                std::vector<core::Vec3> imag;
                for (const auto& atom : branch.toArray()) {
                    const QJsonObject displacement = atom.toObject();
                    if (displacement.contains(QStringLiteral("re"))) {
                        real.push_back(vec3From(
                            displacement.value(QStringLiteral("re")).toArray()));
                        imag.push_back(vec3From(
                            displacement.value(QStringLiteral("im")).toArray()));
                    } else {
                        // A real-valued eigenvector (Γ, or a run that exported
                        // only the real part) is stored as a bare [x, y, z].
                        real.push_back(vec3From(atom.toArray()));
                        imag.emplace_back();
                    }
                }
                qpoint.eigenvectorsReal.push_back(std::move(real));
                qpoint.eigenvectorsImag.push_back(std::move(imag));
            }
            qpoints_.push_back(std::move(qpoint));
        }
    }
    hasEigenvectors_ =
        !qpoints_.empty() && !qpoints_.front().eigenvectorsReal.empty();

    if (!hasEigenvectors_) {
        // Frequencies alone still let the user browse what the run found, but
        // there is nothing to animate. Say exactly that — inventing a
        // displacement pattern would look convincing and be meaningless.
        const QJsonObject band =
            readJsonObject(directory + QStringLiteral("/phonon_band.json"));
        const QJsonArray frequencies =
            band.value(QStringLiteral("frequencies")).toArray();
        if (!frequencies.isEmpty()) {
            QPointModes gamma;
            gamma.label = QStringLiteral("Γ");
            gamma.frequenciesCm = toDoubleVector(frequencies.first().toArray());
            qpoints_.push_back(std::move(gamma));
        }
        noticeLabel_->setText(
            tr("⚠ This run exported no eigenvectors (phonon_modes.json), so "
               "the modes can be listed but not animated. Re-run the phonon "
               "calculation with mode export enabled to animate them."));
        noticeLabel_->setStyleSheet(QStringLiteral("color: #d9534f;"));
        playButton_->setEnabled(false);
        trajectoryButton_->setEnabled(false);
        amplitudeSlider_->setEnabled(false);
        speedSlider_->setEnabled(false);
    } else {
        noticeLabel_->setText(
            tr("Displacements are shown on the main 3D viewport. Closing this "
               "dialog restores the undisplaced structure."));
    }

    const QSignalBlocker blocker(qpointCombo_);
    for (const QPointModes& qpoint : qpoints_)
        qpointCombo_->addItem(qpoint.label);
    if (qpoints_.empty()) {
        noticeLabel_->setText(tr("No phonon modes were found in this run."));
        playButton_->setEnabled(false);
        trajectoryButton_->setEnabled(false);
        return;
    }
    onQPointChanged(0);
}

void VibrationalAnalysisDialog::onQPointChanged(int index)
{
    if (index < 0 || index >= static_cast<int>(qpoints_.size()))
        return;
    const QPointModes& qpoint = qpoints_[static_cast<std::size_t>(index)];
    const QSignalBlocker blocker(modeCombo_);
    modeCombo_->clear();
    for (std::size_t branch = 0; branch < qpoint.frequenciesCm.size(); ++branch) {
        const double frequency = qpoint.frequenciesCm[branch];
        // At Γ each branch carries its irreducible representation, when the
        // run could assign one — the mode's symmetry name, next to its
        // frequency.
        const QString irrep = branch < qpoint.irreps.size()
            ? qpoint.irreps[branch]
            : QString();
        modeCombo_->addItem(tr("#%1 — %2 cm⁻¹%3%4")
                                .arg(branch + 1)
                                .arg(frequency, 0, 'f', 2)
                                .arg(irrep.isEmpty()
                                         ? QString()
                                         : QStringLiteral("  [%1]").arg(irrep))
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
    const int q = qpointCombo_->currentIndex();
    const int branch = modeCombo_->currentIndex();
    if (q < 0 || q >= static_cast<int>(qpoints_.size()) || branch < 0) {
        modeLabel_->setText(QStringLiteral("—"));
        return;
    }
    const auto& frequencies = qpoints_[static_cast<std::size_t>(q)].frequenciesCm;
    if (branch >= static_cast<int>(frequencies.size())) {
        modeLabel_->setText(QStringLiteral("—"));
        return;
    }
    const double cm = frequencies[static_cast<std::size_t>(branch)];
    const auto& irreps = qpoints_[static_cast<std::size_t>(q)].irreps;
    const QString irrep =
        static_cast<std::size_t>(branch) < irreps.size()
        ? irreps[static_cast<std::size_t>(branch)]
        : QString();
    modeLabel_->setText(tr("%1 cm⁻¹   ·   %2 meV   ·   %3 THz%4")
                            .arg(cm, 0, 'f', 2)
                            .arg(cm * kCmToMev, 0, 'f', 3)
                            .arg(cm * 0.0299792458, 0, 'f', 3)
                            .arg(irrep.isEmpty()
                                     ? QString()
                                     : tr("   ·   irrep %1").arg(irrep)));
}

std::shared_ptr<core::Structure> VibrationalAnalysisDialog::displacedAt(
    double phase, bool withDynamics) const
{
    if (!reference_ || !hasEigenvectors_)
        return nullptr;
    const int q = qpointCombo_->currentIndex();
    const int branch = modeCombo_->currentIndex();
    if (q < 0 || q >= static_cast<int>(qpoints_.size()) || branch < 0)
        return nullptr;
    const QPointModes& qpoint = qpoints_[static_cast<std::size_t>(q)];
    if (branch >= static_cast<int>(qpoint.eigenvectorsReal.size())
        || branch >= static_cast<int>(qpoint.frequenciesCm.size()))
        return nullptr;
    const auto& real = qpoint.eigenvectorsReal[static_cast<std::size_t>(branch)];
    const auto& imag = qpoint.eigenvectorsImag[static_cast<std::size_t>(branch)];

    auto displaced = std::make_shared<core::Structure>(*reference_);
    auto& atoms = displaced->atoms();
    const double amplitude = amplitudeSlider_->value() / 100.0; // Å
    const bool periodic = displaced->cell().isDefined();

    // ω in rad/s from the frequency in cm⁻¹, for the restoring forces.
    const double frequencyCm = qpoint.frequenciesCm[static_cast<std::size_t>(branch)];
    const double omega = 2.0 * M_PI * std::abs(frequencyCm) * kCmToHz;

    std::vector<core::Vec3> displacements(atoms.size());
    std::vector<core::Vec3> velocities(atoms.size());
    for (std::size_t i = 0; i < atoms.size() && i < real.size(); ++i) {
        // u_α(t) = Re[ e_α(q) · exp(i(q·R_α − ωt)) ]. The q·R phase is what
        // makes a zone-boundary mode show neighbouring cells in antiphase
        // instead of every cell moving identically; dropping it would render
        // every q as if it were Γ.
        double qr = 0.0;
        if (periodic) {
            const core::Vec3 fractional =
                displaced->cell().cartesianToFractional(atoms[i].position);
            qr = 2.0 * M_PI
                * (qpoint.q[0] * fractional.x + qpoint.q[1] * fractional.y
                   + qpoint.q[2] * fractional.z);
        }
        const double angle = qr - phase;
        const double c = std::cos(angle);
        const double sn = std::sin(angle);
        const core::Vec3& re = real[i];
        const core::Vec3& im = i < imag.size() ? imag[i] : core::Vec3{};
        // Re[(re + i·im)(c + i·s)] = re·c − im·s
        const core::Vec3 u{(re.x * c - im.x * sn) * amplitude,
                           (re.y * c - im.y * sn) * amplitude,
                           (re.z * c - im.z * sn) * amplitude};
        displacements[i] = u;
        atoms[i].position = atoms[i].position + u;

        // v = du/dt, differentiated in closed form rather than by differencing
        // consecutive frames. With angle = q·R − ωt,
        //     du/dt = du/dangle · (−ω) = A·ω·(re·sin(angle) + im·cos(angle)),
        // which is exact at every phase; a finite difference over the 32
        // sampled frames would be wrong by ~cos(π/32) and would have no value
        // at all for the last frame.
        //
        // Note the quarter-period offset this puts between u and v: the atoms
        // are fastest as they pass through their equilibrium positions and
        // momentarily at rest at the turning points. That is the check to make
        // if these ever look wrong — velocities in phase with the displacement
        // mean a sign or a sin/cos has been swapped.
        const double speed = amplitude * omega * kVelocityUnit;
        velocities[i] = {(re.x * sn + im.x * c) * speed,
                         (re.y * sn + im.y * c) * speed,
                         (re.z * sn + im.z * c) * speed};
    }

    if (withDynamics) {
        // Velocities travel as their own field. AseBridge routes a field named
        // "velocities" through ase.Atoms.set_velocities(), which stores it as
        // momenta — so the extended-XYZ writer emits a momenta column that a
        // reader converts back to the same velocities, rather than an opaque
        // extra column nothing interprets.
        displaced->setVectorField("velocities", velocities);

        // Harmonic restoring force F_α = −M_α ω² u_α. Converted from
        // u·amu·rad²/s² to eV/Å so it lands in the same units as every other
        // force in the app — the Vector Overlay scale is calibrated for those,
        // and a raw SI magnitude would render as an invisible or absurd arrow.
        std::vector<core::Vec3> forces(atoms.size());
        for (std::size_t i = 0; i < atoms.size(); ++i) {
            const double mass =
                core::Elements::atomicMass(atoms[i].atomicNumber);
            const double factor = -mass * omega * omega * kForceUnit;
            forces[i] = {displacements[i].x * factor,
                         displacements[i].y * factor,
                         displacements[i].z * factor};
        }
        displaced->setVectorField("forces", std::move(forces));
    }
    return displaced;
}

void VibrationalAnalysisDialog::applyDisplacement()
{
    if (!viewport_)
        return;
    // No forces on the live animation: the arrows would be redrawn 50x a
    // second and the overlay is off by default anyway. The exported trajectory
    // carries them, which is where they are actually inspected.
    if (auto displaced = displacedAt(phase_, /*withDynamics=*/false)) {
        // frameCamera=false: re-framing every tick would make the structure
        // jitter in place instead of showing the motion.
        viewport_->setStructure(displaced, false);
    }
}

void VibrationalAnalysisDialog::createModeTrajectory()
{
    const int q = qpointCombo_->currentIndex();
    const int branch = modeCombo_->currentIndex();
    if (!hasEigenvectors_ || q < 0 || branch < 0) {
        QMessageBox::information(
            this, tr("Create Mode Trajectory Tab"),
            tr("This run exported no eigenvectors, so there is no displacement "
               "pattern to animate."));
        return;
    }
    const QPointModes& qpoint = qpoints_[static_cast<std::size_t>(q)];
    const double frequencyCm =
        branch < static_cast<int>(qpoint.frequenciesCm.size())
        ? qpoint.frequenciesCm[static_cast<std::size_t>(branch)]
        : 0.0;

    // One full period sampled uniformly in phase. The last frame is omitted:
    // phase 2π is the same configuration as phase 0, and a duplicated frame
    // makes a looped playback stutter.
    std::vector<std::shared_ptr<core::Structure>> frames;
    frames.reserve(kTrajectoryFrames);
    for (int i = 0; i < kTrajectoryFrames; ++i) {
        const double phase =
            2.0 * M_PI * static_cast<double>(i) / kTrajectoryFrames;
        if (auto frame = displacedAt(phase, /*withDynamics=*/true))
            frames.push_back(std::move(frame));
    }
    if (frames.empty()) {
        QMessageBox::information(this, tr("Create Mode Trajectory Tab"),
                                 tr("Could not build the mode trajectory."));
        return;
    }

    Q_EMIT modeTrajectoryRequested(
        frames, tr("Mode %1 @ %2 — %3 cm⁻¹")
                    .arg(branch + 1)
                    .arg(qpoint.label)
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
    if (viewport_ && reference_)
        viewport_->setStructure(reference_, false);
}

} // namespace calango::gui
