#include "gui/VibrationalAnalysisDialog.hpp"

#include "gui/GuiUtils.hpp"
#include "gui/ViewportWidget.hpp"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonObject>
#include <QLabel>
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
           "zone centre, where all cells move in phase; at a zone-boundary q "
           "neighbouring cells move in antiphase."));
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
        modeCombo_->addItem(tr("#%1 — %2 cm⁻¹%3")
                                .arg(branch + 1)
                                .arg(frequency, 0, 'f', 2)
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
    modeLabel_->setText(tr("%1 cm⁻¹   ·   %2 meV   ·   %3 THz")
                            .arg(cm, 0, 'f', 2)
                            .arg(cm * kCmToMev, 0, 'f', 3)
                            .arg(cm * 0.0299792458, 0, 'f', 3));
}

void VibrationalAnalysisDialog::applyDisplacement()
{
    if (!viewport_ || !reference_ || !hasEigenvectors_)
        return;
    const int q = qpointCombo_->currentIndex();
    const int branch = modeCombo_->currentIndex();
    if (q < 0 || q >= static_cast<int>(qpoints_.size()) || branch < 0)
        return;
    const QPointModes& qpoint = qpoints_[static_cast<std::size_t>(q)];
    if (branch >= static_cast<int>(qpoint.eigenvectorsReal.size()))
        return;
    const auto& real = qpoint.eigenvectorsReal[static_cast<std::size_t>(branch)];
    const auto& imag = qpoint.eigenvectorsImag[static_cast<std::size_t>(branch)];

    auto displaced = std::make_shared<core::Structure>(*reference_);
    auto& atoms = displaced->atoms();
    const double amplitude = amplitudeSlider_->value() / 100.0; // Å
    const bool periodic = displaced->cell().isDefined();

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
        const double angle = qr - phase_;
        const double c = std::cos(angle);
        const double s = std::sin(angle);
        const core::Vec3& re = real[i];
        const core::Vec3& im = i < imag.size() ? imag[i] : core::Vec3{};
        // Re[(re + i·im)(c + i·s)] = re·c − im·s
        atoms[i].position = atoms[i].position
            + core::Vec3{re.x * c - im.x * s, re.y * c - im.y * s,
                         re.z * c - im.z * s}
                * amplitude;
    }
    // frameCamera=false: re-framing every tick would make the structure jitter
    // in place instead of showing the motion.
    viewport_->setStructure(displaced, false);
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
