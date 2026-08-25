#include "gui/TwoDRipplesWizard.hpp"

#include "gui/GuiUtils.hpp"

#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QSpinBox>
#include <QStringList>
#include <QVBoxLayout>

#include <algorithm>
#include <array>
#include <utility>

namespace calango::gui {

namespace {

/// The two in-plane axis indices for an out-of-plane axis, in index order —
/// the same pairing core::applyRipples uses, and the reason the wizard's two
/// period controls are labelled "first"/"second" rather than "x"/"y": which
/// Cartesian direction they are depends on which axis carries the vacuum.
std::array<int, 2> inPlaneAxes(int normalAxis)
{
    switch (normalAxis) {
    case 0:
        return {1, 2};
    case 1:
        return {0, 2};
    default:
        return {0, 1};
    }
}

const char* axisName(int axis)
{
    return axis == 0 ? "a" : (axis == 1 ? "b" : "c");
}

} // namespace

TwoDRipplesWizard::TwoDRipplesWizard(
    std::shared_ptr<const core::Structure> sheet, QWidget* parent)
    : QDialog(parent)
    , sheet_(std::move(sheet))
{
    setWindowTitle(tr("2D Ripples"));
    auto* layout = new QVBoxLayout(this);

    auto* note = new QLabel(
        tr("Displace a monolayer supercell out of plane on a <b>sinusoidal "
           "profile</b>, and contract the cell so the sheet's own "
           "(arc) length is unchanged — a ripple stores extra path, and "
           "leaving the footprint alone would be stretching the membrane "
           "instead of corrugating it."),
        this);
    note->setWordWrap(true);
    note->setTextFormat(Qt::RichText);
    layout->addWidget(note);

    auto* profileGroup = new QGroupBox(tr("Profile"), this);
    auto* form = new QFormLayout(profileGroup);

    axisCombo_ = new QComboBox(profileGroup);
    axisCombo_->setObjectName(QStringLiteral("rippleNormalAxis"));
    axisCombo_->addItem(tr("a (first cell vector)"), 0);
    axisCombo_->addItem(tr("b (second cell vector)"), 1);
    axisCombo_->addItem(tr("c (third cell vector)"), 2);
    // Seeded, never decided: guessVacuumAxis is explicitly a seed, and an
    // out-of-plane axis chosen wrongly does not make a slightly wrong sheet,
    // it ripples the structure sideways.
    const int guessed = guessVacuumAxis(sheet_.get());
    axisCombo_->setCurrentIndex(guessed >= 0 ? guessed : 2);
    axisCombo_->setToolTip(
        tr("Which cell axis carries the vacuum — i.e. which direction is "
           "OUT of the sheet. The other two are the in-plane pair, in index "
           "order.\n\n"
           "Seeded from the geometry (the axis whose atoms span far less "
           "than the cell) and left editable, because a thick slab in a "
           "modest cell and a thin one in a huge cell are not reliably "
           "distinguishable from coordinates. Getting it wrong corrugates "
           "the structure sideways rather than out of plane."));
    form->addRow(tr("Out-of-plane axis:"), axisCombo_);

    directionCombo_ = new QComboBox(profileGroup);
    directionCombo_->setObjectName(QStringLiteral("rippleDirection"));
    // Order matches core::RippleOptions::Direction.
    directionCombo_->addItem(tr("Both in-plane directions (xy)"));
    directionCombo_->addItem(tr("First in-plane axis only (x)"));
    directionCombo_->addItem(tr("Second in-plane axis only (y)"));
    directionCombo_->setToolTip(
        tr("Which in-plane direction(s) the height varies along.\n\n"
           "• Both — h = A·sin(2πn₁f₁)·sin(2πn₂f₂), an egg-box corrugation "
           "with four extrema per cell. The isotropic choice.\n"
           "• One only — h = A·sin(2πn f), a plain corrugation with straight "
           "crests running across the sheet. This is the one to use when the "
           "question is about a direction (a ripple perpendicular to a "
           "transport channel, an armchair-vs-zigzag comparison).\n\n"
           "Only the direction(s) the profile varies along are contracted: "
           "nothing is stored along an axis the sheet is straight in."));
    form->addRow(tr("Direction:"), directionCombo_);

    amplitudeSpin_ = new QDoubleSpinBox(profileGroup);
    amplitudeSpin_->setObjectName(QStringLiteral("rippleAmplitude"));
    amplitudeSpin_->setRange(0.0, 20.0);
    amplitudeSpin_->setDecimals(3);
    amplitudeSpin_->setSingleStep(0.05);
    amplitudeSpin_->setValue(0.5);
    amplitudeSpin_->setSuffix(tr(" Å"));
    amplitudeSpin_->setToolTip(
        tr("Peak height A, so the sheet spans 2A between its lowest and "
           "highest points.\n\n"
           "Suspended graphene's own intrinsic ripples are a few tenths of "
           "an angstrom over a few nanometres; a supercell of tens of "
           "angstroms therefore wants amplitudes in that range for a "
           "physically comparable curvature. Larger values are a strain "
           "study, not a thermal one."));
    form->addRow(tr("Amplitude A:"), amplitudeSpin_);

    periodsFirstSpin_ = new QSpinBox(profileGroup);
    periodsFirstSpin_->setObjectName(QStringLiteral("ripplePeriodsFirst"));
    periodsFirstSpin_->setRange(1, 32);
    periodsFirstSpin_->setValue(1);
    periodsFirstSpin_->setToolTip(
        tr("Whole periods of the sine across the cell, along the first "
           "in-plane vector.\n\n"
           "A WHOLE number, and the control has no other setting: the "
           "displacement at fractional 0 and at fractional 1 belong to the "
           "same periodic image, so a fractional period count is a seam — a "
           "step in the sheet at the cell boundary, which no amount of "
           "relaxation repairs.\n\n"
           "One period per cell makes the wavelength the supercell size, "
           "which is the usual way of asking \"what does a ripple of THIS "
           "wavelength do?\"."));
    form->addRow(tr("Periods per cell — first axis:"), periodsFirstSpin_);

    periodsSecondSpin_ = new QSpinBox(profileGroup);
    periodsSecondSpin_->setObjectName(QStringLiteral("ripplePeriodsSecond"));
    periodsSecondSpin_->setRange(1, 32);
    periodsSecondSpin_->setValue(1);
    periodsSecondSpin_->setToolTip(
        tr("The same, along the second in-plane vector. Only read when the "
           "profile varies along it."));
    form->addRow(tr("Periods per cell — second axis:"), periodsSecondSpin_);

    contractCheck_ =
        new QCheckBox(tr("Contract the cell to preserve arc length"),
                      profileGroup);
    contractCheck_->setObjectName(QStringLiteral("rippleContract"));
    contractCheck_->setChecked(true);
    contractCheck_->setToolTip(
        tr("On (the default): each rippled cell vector is shortened to the "
           "footprint whose sinusoid is exactly as long as the flat cell "
           "was, solved from the arc-length integral — so the atoms sit at "
           "the same intrinsic separations and the sheet is corrugated, not "
           "stretched.\n\n"
           "Off: the cell keeps its flat footprint, which applies a tensile "
           "strain equal to the whole arc-length excess. That is a "
           "different physical system — rippled AND strained — and "
           "occasionally the one wanted; it is never the one you get by "
           "accident with this on."));
    form->addRow(QString(), contractCheck_);

    cellSummary_ = new QLabel(profileGroup);
    cellSummary_->setWordWrap(true);
    cellSummary_->setTextFormat(Qt::RichText);
    form->addRow(cellSummary_);
    layout->addWidget(profileGroup);

    // -- The amplitude series ---------------------------------------------
    //
    // Same contract as Random Noise Setup's ensemble: a trajectory the
    // Structure Container node fans out, one calculation per frame. The
    // point of the module, really — a single rippled sheet answers nothing;
    // E(A) does.
    seriesGroup_ = new QGroupBox(tr("Amplitude series"), this);
    seriesGroup_->setObjectName(QStringLiteral("rippleSeriesGroup"));
    seriesGroup_->setCheckable(true);
    seriesGroup_->setChecked(false);
    seriesGroup_->setToolTip(
        tr("Generate a whole ramp of amplitudes as one trajectory, instead "
           "of a single structure. Save it (File → Save Trajectory As…) and "
           "load it into an Orchestration <b>Structure Container</b> node to "
           "fan a calculation out over the series — which is how the "
           "amplitude dependence of anything (energy, gap, work function) "
           "gets measured.\n\n"
           "Every frame is a full build at its own amplitude, contraction "
           "included: the contraction goes as A², so an interpolated series "
           "would be right only at its two ends."));
    auto* seriesForm = new QFormLayout(seriesGroup_);

    minAmplitudeSpin_ = new QDoubleSpinBox(seriesGroup_);
    minAmplitudeSpin_->setObjectName(QStringLiteral("rippleMinAmplitude"));
    minAmplitudeSpin_->setRange(0.0, 20.0);
    minAmplitudeSpin_->setDecimals(3);
    minAmplitudeSpin_->setSingleStep(0.05);
    minAmplitudeSpin_->setValue(0.0);
    minAmplitudeSpin_->setSuffix(tr(" Å"));
    minAmplitudeSpin_->setToolTip(
        tr("The first frame's amplitude. Zero is the flat sheet exactly — "
           "the reference every other frame is a departure from, and worth "
           "keeping for that reason."));
    seriesForm->addRow(tr("A minimum:"), minAmplitudeSpin_);

    maxAmplitudeSpin_ = new QDoubleSpinBox(seriesGroup_);
    maxAmplitudeSpin_->setObjectName(QStringLiteral("rippleMaxAmplitude"));
    maxAmplitudeSpin_->setRange(0.0, 20.0);
    maxAmplitudeSpin_->setDecimals(3);
    maxAmplitudeSpin_->setSingleStep(0.05);
    maxAmplitudeSpin_->setValue(1.0);
    maxAmplitudeSpin_->setSuffix(tr(" Å"));
    seriesForm->addRow(tr("A maximum:"), maxAmplitudeSpin_);

    seriesCountSpin_ = new QSpinBox(seriesGroup_);
    seriesCountSpin_->setObjectName(QStringLiteral("rippleSeriesCount"));
    seriesCountSpin_->setRange(1, 200);
    seriesCountSpin_->setValue(11);
    seriesCountSpin_->setToolTip(
        tr("Frames in the ramp, endpoints included. Eleven puts a frame "
           "every tenth of the range, which is enough to see a curvature "
           "and cheap enough to run."));
    seriesForm->addRow(tr("Frames:"), seriesCountSpin_);
    layout->addWidget(seriesGroup_);

    auto* actionRow = new QWidget(this);
    auto* actionLayout = new QHBoxLayout(actionRow);
    actionLayout->setContentsMargins(0, 0, 0, 0);
    generateButton_ = new QPushButton(tr("Generate"), actionRow);
    generateButton_->setToolTip(
        tr("Build now and open the result in the viewport — one structure, "
           "or the whole series as a scrubbable trajectory. Press again "
           "after changing anything above to rebuild it."));
    actionLayout->addStretch(1);
    auto* close = new QPushButton(tr("Close"), actionRow);
    connect(close, &QPushButton::clicked, this, &QDialog::accept);
    actionLayout->addWidget(close);
    actionLayout->addWidget(generateButton_);
    layout->addWidget(actionRow);

    generationStatus_ = new QLabel(this);
    generationStatus_->setWordWrap(true);
    layout->addWidget(generationStatus_);

    connect(generateButton_, &QPushButton::clicked, this,
            &TwoDRipplesWizard::generateStructures);
    for (QComboBox* combo : {axisCombo_, directionCombo_})
        connect(combo, &QComboBox::currentIndexChanged, this, [this] {
            syncControls();
            refreshSummary();
        });
    for (QDoubleSpinBox* spin :
         {amplitudeSpin_, minAmplitudeSpin_, maxAmplitudeSpin_})
        connect(spin, &QDoubleSpinBox::valueChanged, this,
                [this] { refreshSummary(); });
    for (QSpinBox* spin : {periodsFirstSpin_, periodsSecondSpin_,
                           seriesCountSpin_})
        connect(spin, &QSpinBox::valueChanged, this,
                [this] { refreshSummary(); });
    connect(contractCheck_, &QCheckBox::toggled, this,
            [this] { refreshSummary(); });
    connect(seriesGroup_, &QGroupBox::toggled, this, [this] {
        syncControls();
        refreshSummary();
    });

    layout->addStretch(1);
    syncControls();
    refreshSummary();
    resize(520, 620);
}

core::RippleOptions TwoDRipplesWizard::rippleOptions() const
{
    core::RippleOptions options;
    options.direction = static_cast<core::RippleOptions::Direction>(
        directionCombo_->currentIndex());
    options.amplitude = amplitudeSpin_->value();
    options.periodsFirst = periodsFirstSpin_->value();
    options.periodsSecond = periodsSecondSpin_->value();
    options.normalAxis = axisCombo_->currentData().toInt();
    options.contractInPlane = contractCheck_->isChecked();
    return options;
}

void TwoDRipplesWizard::syncControls()
{
    const auto direction = static_cast<core::RippleOptions::Direction>(
        directionCombo_->currentIndex());
    // A period count for a direction the profile is constant along is not a
    // setting this run has — disabled rather than hidden, because the two
    // rows are a pair and hiding one of them makes the other read as "the"
    // period count rather than as one of two.
    periodsFirstSpin_->setEnabled(
        direction != core::RippleOptions::Direction::Y);
    periodsSecondSpin_->setEnabled(
        direction != core::RippleOptions::Direction::X);
    // The single amplitude is what the SERIES ramps between when the series
    // is on, so it stops being the amplitude that gets built.
    amplitudeSpin_->setEnabled(!seriesGroup_->isChecked());
    generateButton_->setEnabled(sheet_ && !sheet_->empty());
}

void TwoDRipplesWizard::refreshSummary()
{
    if (!cellSummary_)
        return;
    if (!sheet_ || !sheet_->cell().isDefined()) {
        cellSummary_->setText(
            tr("<i>This structure has no unit cell, so it has no fractional "
               "coordinates for the profile to be periodic in.</i>"));
        return;
    }
    const core::RippleOptions options = rippleOptions();
    // The amplitude the read-out is ABOUT: the series' largest, when there
    // is a series, because that is the frame most likely to be refused and
    // the one whose contraction is largest.
    const double amplitude = seriesGroup_->isChecked()
        ? std::max(minAmplitudeSpin_->value(), maxAmplitudeSpin_->value())
        : options.amplitude;

    const std::array<int, 2> axes = inPlaneAxes(options.normalAxis);
    const int periods[2] = {options.periodsFirst, options.periodsSecond};
    QStringList parts;
    bool impossible = false;
    for (int slot = 0; slot < 2; ++slot) {
        const bool rippled =
            options.direction == core::RippleOptions::Direction::XY
            || (slot == 0
                && options.direction == core::RippleOptions::Direction::X)
            || (slot == 1
                && options.direction == core::RippleOptions::Direction::Y);
        if (!rippled)
            continue;
        const double flat =
            sheet_->cell().vectors()[static_cast<std::size_t>(axes[slot])]
                .norm();
        // The SAME solver the build uses. A read-out computed from a
        // closed-form approximation would disagree with the structure the
        // button produces, which is worse than no read-out.
        const double contracted =
            core::rippleContractedLength(amplitude, flat, periods[slot]);
        // "Too large for this cell" is a statement about the CONTRACTION,
        // and only the contraction refuses it: an uncontracted build has no
        // footprint to solve for and takes any amplitude. Reporting the
        // refusal with the box unticked would refuse something the button
        // goes on to build.
        if (contracted <= 0.0 && options.contractInPlane) {
            impossible = true;
            continue;
        }
        if (!options.contractInPlane) {
            parts << tr("%1: %2 Å (unchanged; arc length %3 Å, so the sheet "
                        "is stretched by %4 %)")
                         .arg(QString::fromLatin1(axisName(axes[slot])))
                         .arg(flat, 0, 'f', 4)
                         .arg(core::rippleArcLength(amplitude, flat,
                                                    periods[slot]),
                              0, 'f', 4)
                         .arg(100.0
                                  * (core::rippleArcLength(amplitude, flat,
                                                           periods[slot])
                                     - flat)
                                  / flat,
                              0, 'f', 3);
        } else {
            parts << tr("%1: %2 → %3 Å (−%4 %)")
                         .arg(QString::fromLatin1(axisName(axes[slot])))
                         .arg(flat, 0, 'f', 4)
                         .arg(contracted, 0, 'f', 4)
                         .arg(100.0 * (flat - contracted) / flat, 0, 'f', 3);
        }
    }
    if (impossible) {
        cellSummary_->setText(
            tr("<b>A = %1 Å does not fit this cell.</b> A sinusoid of "
               "amplitude A with n periods is at least 4nA long whatever "
               "footprint it occupies, and that already exceeds the cell "
               "vector it would have to fit into. Use a smaller amplitude, "
               "fewer periods, or a larger supercell.")
                .arg(amplitude, 0, 'f', 3));
        return;
    }
    cellSummary_->setText(
        tr("<i>%1</i>").arg(parts.join(QStringLiteral(" &nbsp;·&nbsp; "))));
}

void TwoDRipplesWizard::generateStructures()
{
    frames_.clear();
    if (!sheet_ || sheet_->empty()) {
        generationStatus_->setText(tr("<b>No structure to ripple.</b>"));
        return;
    }
    const core::RippleOptions options = rippleOptions();
    std::string error;
    if (seriesGroup_->isChecked()) {
        frames_ = core::buildRippleSeries(
            *sheet_, options, minAmplitudeSpin_->value(),
            maxAmplitudeSpin_->value(), seriesCountSpin_->value(), &error);
    } else {
        core::Structure built = core::applyRipples(*sheet_, options, &error);
        if (error.empty())
            frames_.push_back(std::make_shared<core::Structure>(
                std::move(built)));
    }
    if (frames_.empty()) {
        generationStatus_->setText(
            tr("<b>Nothing was built.</b> %1")
                .arg(QString::fromStdString(error).toHtmlEscaped()));
        return;
    }
    if (!error.empty()) {
        // Some frames built and some did not — the series drops what it
        // cannot build rather than substituting a flat sheet, and the count
        // below is what actually came back.
        generationStatus_->setText(
            tr("<b>%1 of %2 frames built.</b> %3")
                .arg(frames_.size())
                .arg(seriesCountSpin_->value())
                .arg(QString::fromStdString(error).toHtmlEscaped()));
    } else if (frames_.size() == 1) {
        generationStatus_->setText(
            tr("<b>One rippled structure ready.</b>"));
    } else {
        generationStatus_->setText(
            tr("<b>%1 structures ready</b> — amplitude ramped linearly from "
               "%2 to %3 Å, each frame carrying its own amplitude as the "
               "\"ripple_amplitude\" field.")
                .arg(frames_.size())
                .arg(minAmplitudeSpin_->value(), 0, 'f', 3)
                .arg(maxAmplitudeSpin_->value(), 0, 'f', 3));
    }
    Q_EMIT structuresGenerated(frames_);
}

} // namespace calango::gui
