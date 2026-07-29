#include "gui/PhononWizard.hpp"

#include "core/PhononScriptGenerator.hpp"
#include "gui/EmbeddedKPathEditor.hpp"

#include <QComboBox>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QMessageBox>
#include <QPushButton>
#include <QGroupBox>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QCheckBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QSpinBox>
#include <QVBoxLayout>
#include <QWidget>

namespace calango::gui {

PhononWizard::PhononWizard(bool periodic,
                           std::shared_ptr<const core::Structure> structure,
                           QWidget* parent)
    : SimulationWizardBase(parent)
    , periodic_(periodic)
    , structure_(std::move(structure))
{
    buildUi();
}

QString PhononWizard::wizardTitle() const
{
    return tr("Phonon Setup");
}

QString PhononWizard::settingsHeader() const
{
    return tr("q-Path Definition");
}

QString PhononWizard::secondSettingsHeader() const
{
    return tr("Phonon Settings");
}

QWidget* PhononWizard::buildSecondSettingsPage()
{
    // Stage 2 — how the force constants are sampled. This is its own stage
    // rather than a group appended to Calculator Settings: the engine page is
    // already dense, and the supercell / δ / symmetry choices are what a user
    // revisits when a dispersion comes out wrong.
    auto* page = new QWidget(this);
    auto* pageLayout = new QVBoxLayout(page);
    auto* group = new QGroupBox(tr("Finite Displacements"), page);
    auto* form = new QFormLayout(group);
    pageLayout->addWidget(group);

    if (!periodic_)
        form->addRow(new QLabel(
            tr("Molecular system: Γ-point normal modes (no supercell / mesh)."),
            group));

    deltaSpin_ = new QDoubleSpinBox(group);
    deltaSpin_->setRange(0.001, 0.2);
    deltaSpin_->setDecimals(3);
    deltaSpin_->setSingleStep(0.005);
    deltaSpin_->setValue(0.01);
    deltaSpin_->setSuffix(tr(" Å"));
    deltaSpin_->setToolTip(tr("Finite-displacement step amplitude (± per atom)."));
    form->addRow(tr("Displacement δ:"), deltaSpin_);

    auto* superRow = new QHBoxLayout;
    for (auto*& spin : supercellSpins_) {
        spin = new QSpinBox(group);
        spin->setRange(1, 10);
        spin->setValue(2);
        spin->setEnabled(periodic_);
        superRow->addWidget(spin);
    }
    form->addRow(tr("Supercell (nx·ny·nz):"), superRow);

    symmetryCheck_ = new QCheckBox(tr("Symmetry-reduced displacements"), group);
    symmetryCheck_->setToolTip(
        tr("Displace only along the symmetry-irreducible directions and rebuild "
           "the full force-constant matrix by symmetry.\n"
           "The naive scheme costs 6N force evaluations (±δ along x, y, z for "
           "every atom); most of those are related by the crystal's space "
           "group. Spglib (through phonopy) finds the space group and the "
           "site-symmetry irreps, which for a high-symmetry cell can cut the "
           "count by an order of magnitude.\n"
           "Needs phonopy in the job environment — the script falls back to "
           "the full 6N set if it is missing."));
    symmetryCheck_->setEnabled(periodic_);
    form->addRow(symmetryCheck_);

    residualCheck_ = new QCheckBox(tr("Remove residual forces"), group);
    residualCheck_->setToolTip(
        tr("Compute the forces on the un-displaced geometry and subtract them "
           "from every displaced configuration.\n"
           "A relaxation stops at a finite fmax, so the reference geometry "
           "still carries small forces; left in, they add a spurious linear "
           "term to the force constants that shows up as non-zero acoustic "
           "frequencies at Γ. Costs one extra force evaluation."));
    form->addRow(residualCheck_);

    acousticCheck_ = new QCheckBox(tr("Enforce acoustic sum rule"), group);
    acousticCheck_->setChecked(true);
    acousticCheck_->setToolTip(
        tr("Make the 3 acoustic branches vanish at Γ (recommended)."));
    acousticCheck_->setEnabled(periodic_);
    form->addRow(acousticCheck_);

    meshSpin_ = new QSpinBox(group);
    // A smooth PhDOS needs a dense q-mesh: the DOS is a histogram over
    // sampled frequencies, so 20x20x20 leaves visible sampling noise on the
    // van Hove features. Interpolating force constants onto a mesh is cheap
    // compared with the force evaluations already done, so the cap is high.
    meshSpin_->setRange(2, 200);
    meshSpin_->setValue(30);
    meshSpin_->setSingleStep(5);
    meshSpin_->setToolTip(
        tr("Monkhorst-Pack n×n×n q-mesh for the phonon DOS.\n"
           "20 is adequate for a first look; 30–50 gives a smooth spectrum; "
           "beyond that the cost is memory rather than force evaluations "
           "(the force constants are already computed, the mesh only "
           "interpolates them).\n"
           "Raise the mesh before lowering the Gaussian σ below — a sharp σ on "
           "a coarse mesh produces spikes, not resolution."));
    meshSpin_->setEnabled(periodic_);
    form->addRow(tr("Mesh density (DOS):"), meshSpin_);

    dosWidthSpin_ = new QDoubleSpinBox(group);
    dosWidthSpin_->setRange(0.1, 200.0);
    dosWidthSpin_->setDecimals(2);
    dosWidthSpin_->setSingleStep(0.5);
    dosWidthSpin_->setValue(2.0);
    dosWidthSpin_->setSuffix(tr(" cm⁻¹"));
    dosWidthSpin_->setEnabled(periodic_);
    dosWidthSpin_->setToolTip(
        tr("Gaussian broadening σ used when sampling the PhDOS.\n"
           "A finite k-mesh gives a comb of delta peaks; σ smooths it into a "
           "continuous DOS. Too small leaves spikes, too large erases van "
           "Hove features — raise the mesh density before lowering σ."));
    form->addRow(tr("Gaussian smearing σ:"), dosWidthSpin_);

    for (QDoubleSpinBox* spin : {deltaSpin_, dosWidthSpin_})
        connect(spin, &QDoubleSpinBox::valueChanged, this,
                [this] { refreshPreview(); });
    for (QSpinBox* spin : {meshSpin_, supercellSpins_[0], supercellSpins_[1],
                           supercellSpins_[2]})
        connect(spin, &QSpinBox::valueChanged, this,
                [this] { refreshPreview(); });
    for (QCheckBox* check : {acousticCheck_, symmetryCheck_, residualCheck_})
        connect(check, &QCheckBox::toggled, this, [this] { refreshPreview(); });

    // -- LO-TO splitting ---------------------------------------------------
    //
    // In a polar crystal the long-wavelength LO mode is stiffened by the
    // macroscopic electric field its own displacement pattern creates. A
    // finite-displacement supercell is charge-neutral and cannot host that
    // field, so LO and TO come out degenerate at Γ — for MgO that is a 300
    // cm⁻¹ error, not a subtlety. The correction is added analytically, and
    // needs the two quantities below.
    loToGroup_ = new QGroupBox(tr("LO-TO Splitting"), page);
    loToGroup_->setEnabled(periodic_);
    auto* loToForm = new QFormLayout(loToGroup_);
    pageLayout->addWidget(loToGroup_);

    bornCombo_ = new QComboBox(loToGroup_);
    bornCombo_->addItem(tr("None — no LO-TO splitting"), QString());
    bornCombo_->setToolTip(
        tr("A completed Born Effective Charges run on THIS structure.\n\n"
           "Z* is how much dipole each atom creates when it moves, which is "
           "exactly\nwhat a finite-displacement supercell cannot see. Leave "
           "this at None for a\nnon-polar crystal (Si, diamond, a metal): "
           "there is no splitting to add."));
    // The combo lists completed runs in THIS workspace. A Z* file is a plain
    // JSON table, though, and a perfectly ordinary thing to have from another
    // session, another machine, or a cluster — so it must also be possible to
    // point at one directly. Without this, a user who has the physics in hand
    // is told to spend 6 SCF runs per atom recomputing it.
    auto* bornRow = new QHBoxLayout;
    bornRow->setSpacing(4);
    bornRow->addWidget(bornCombo_, 1);
    auto* bornLoadButton = new QPushButton(tr("Load…"), loToGroup_);
    bornLoadButton->setToolTip(
        tr("Load a born_charges.json written by a Born Effective Charges run "
           "— this session\'s or any other.\n\n"
           "It must be Z* for THIS structure: the correction indexes the "
           "tensors by atom, so a file from a different cell or a different "
           "atom order produces a dispersion that is wrong without being "
           "obviously wrong."));
    connect(bornLoadButton, &QPushButton::clicked, this,
            &PhononWizard::loadBornChargesFile);
    bornRow->addWidget(bornLoadButton);
    loToForm->addRow(tr("Born charges:"), bornRow);

    opticsCombo_ = new QComboBox(loToGroup_);
    opticsCombo_->addItem(tr("Enter manually"), QString());
    opticsCombo_->setToolTip(
        tr("Optionally take ε∞ from a completed Optics run, using the "
           "zero-frequency\nlimit of ε₁ along each axis. Otherwise type it — "
           "a literature or\nexperimental value is perfectly legitimate here, "
           "and is what most\npublished phonon dispersions use."));
    loToForm->addRow(tr("ε∞ source:"), opticsCombo_);

    auto* dielectricRow = new QHBoxLayout;
    const char* axisLabel[3] = {"xx", "yy", "zz"};
    for (int axis = 0; axis < 3; ++axis) {
        dielectricRow->addWidget(new QLabel(QLatin1String(axisLabel[axis]),
                                            loToGroup_));
        dielectricSpin_[axis] = new QDoubleSpinBox(loToGroup_);
        dielectricSpin_[axis]->setRange(1.0, 1000.0);
        dielectricSpin_[axis]->setDecimals(3);
        dielectricSpin_[axis]->setSingleStep(0.1);
        dielectricSpin_[axis]->setValue(1.0);
        dielectricSpin_[axis]->setToolTip(
            tr("The ELECTRONIC (clamped-ion, high-frequency) dielectric "
               "constant —\nε∞, not the static ε₀. It is what screens the "
               "field the LO mode creates:\nthe larger it is, the smaller the "
               "splitting.\n\n"
               "Typical values: MgO 2.96, NaCl 2.34, cubic BN 4.5, GaAs 10.9, "
               "Si 11.9\n(non-polar, so no splitting regardless)."));
        dielectricRow->addWidget(dielectricSpin_[axis], 1);
        connect(dielectricSpin_[axis], &QDoubleSpinBox::valueChanged, this,
                [this] { updateLoToState(); refreshPreview(); });
    }
    loToForm->addRow(tr("ε∞ (diagonal):"), dielectricRow);

    loToNote_ = new QLabel(loToGroup_);
    loToNote_->setWordWrap(true);
    loToForm->addRow(loToNote_);

    connect(bornCombo_, &QComboBox::currentIndexChanged, this, [this] {
        updateLoToState();
        refreshPreview();
    });
    connect(opticsCombo_, &QComboBox::currentIndexChanged, this, [this] {
        const QString file = opticsCombo_->currentData().toString();
        if (!file.isEmpty())
            loadDielectricFromOptics(file);
        updateLoToState();
        refreshPreview();
    });
    updateLoToState();

    pageLayout->addStretch(1);
    return page;
}

void PhononWizard::setBornChargeProcesses(
    const QList<QPair<QString, QString>>& processes)
{
    if (!bornCombo_)
        return;
    const QString previous = bornCombo_->currentData().toString();
    bornCombo_->clear();
    bornCombo_->addItem(tr("None — no LO-TO splitting"), QString());
    for (const auto& [label, file] : processes)
        bornCombo_->addItem(label, file);
    // Re-added after the workspace entries: this refills from the process list
    // whenever a run finishes, and a file the user loaded by hand must survive
    // that or it disappears mid-setup for no reason they can see.
    for (const auto& [label, file] : loadedBornFiles_)
        bornCombo_->addItem(label, file);
    const int restored = bornCombo_->findData(previous);
    bornCombo_->setCurrentIndex(restored >= 0 ? restored : 0);
    updateLoToState();
}

void PhononWizard::setOpticsProcesses(
    const QList<QPair<QString, QString>>& processes)
{
    if (!opticsCombo_)
        return;
    opticsCombo_->clear();
    opticsCombo_->addItem(tr("Enter manually"), QString());
    for (const auto& [label, file] : processes)
        opticsCombo_->addItem(label, file);
    updateLoToState();
}

bool PhononWizard::loadDielectricFromOptics(const QString& file)
{
    QFile handle(file);
    if (!handle.open(QIODevice::ReadOnly))
        return false;
    const QJsonObject root =
        QJsonDocument::fromJson(handle.readAll()).object();

    // optics.json stores eps_1(omega) per Cartesian axis. The high-frequency
    // dielectric constant the correction wants is the ELECTRONIC screening at
    // frequencies far above the phonons but below the electronic gap — which
    // on this grid is the omega -> 0 limit of the electronic response.
    const char* keys[3] = {"eps_x", "eps_y", "eps_z"};
    bool any = false;
    for (int axis = 0; axis < 3; ++axis) {
        const QJsonObject entry = root.value(QLatin1String(keys[axis])).toObject();
        const QJsonArray eps1 = entry.value(QStringLiteral("eps1")).toArray();
        if (eps1.isEmpty())
            continue;
        const double value = eps1.first().toDouble();
        if (value < 1.0)
            continue; // below vacuum is not a dielectric constant
        dielectricSpin_[axis]->setValue(value);
        any = true;
    }
    return any;
}

void PhononWizard::loadBornChargesFile()
{
    const QString path = QFileDialog::getOpenFileName(
        this, tr("Load Born Effective Charges"), QString(),
        tr("Born charges (born_charges.json *.json);;All files (*)"));
    if (path.isEmpty())
        return;

    // Validated before it is offered, not when the script runs: a bad path
    // discovered by the generated Python fails hours into a supercell job,
    // where a dialog here costs the user one click.
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        QMessageBox::warning(this, tr("Load Born Effective Charges"),
                             tr("Could not open %1.").arg(path));
        return;
    }
    QJsonParseError error{};
    const QJsonDocument document =
        QJsonDocument::fromJson(file.readAll(), &error);
    if (error.error != QJsonParseError::NoError || !document.isObject()
        || document.object().value(QStringLiteral("atoms")).toArray().isEmpty()) {
        QMessageBox::warning(
            this, tr("Load Born Effective Charges"),
            tr("%1 is not a Born Effective Charges result.\n\n"
               "Expected a JSON object with a non-empty \"atoms\" array, as "
               "written by Electronics → Born Effective Charges…")
                .arg(QFileInfo(path).fileName()));
        return;
    }

    const int atoms =
        document.object().value(QStringLiteral("atoms")).toArray().size();
    // The atom count is in the label because it is the one thing that makes a
    // mismatched file obvious at a glance.
    const QString label = tr("%1 (loaded, %n atom(s))", nullptr, atoms)
                              .arg(QFileInfo(path).fileName());
    const int existing = bornCombo_->findData(path);
    if (existing >= 0) {
        bornCombo_->setCurrentIndex(existing);
        return;
    }
    loadedBornFiles_.append({label, path});
    bornCombo_->addItem(label, path);
    bornCombo_->setCurrentIndex(bornCombo_->count() - 1);
    updateLoToState();
    refreshPreview();
}

void PhononWizard::updateLoToState()
{
    if (!loToGroup_ || !bornCombo_)
        return;
    const bool on = !bornCombo_->currentData().toString().isEmpty();
    opticsCombo_->setEnabled(on);
    for (QDoubleSpinBox* spin : dielectricSpin_)
        spin->setEnabled(on);

    if (!on) {
        loToNote_->setText(
            bornCombo_->count() > 1
                ? tr("<i>Off. The dispersion will have no LO-TO splitting — "
                     "correct for a non-polar crystal.</i>")
                : tr("<i>No Born Effective Charges run is available. Run one "
                     "from <b>Electronics → Born Effective Charges…</b> on this "
                     "structure, or <b>Load…</b> a born_charges.json from an "
                     "earlier run; either supplies Z*.</i>"));
        return;
    }
    // eps_inf = 1 is vacuum. Nothing screens the field, so the splitting comes
    // out far too large — a silent, physically impossible default is worse
    // than an obvious complaint.
    bool identity = true;
    for (QDoubleSpinBox* spin : dielectricSpin_)
        identity = identity && spin->value() < 1.001;
    loToNote_->setText(
        identity
            ? tr("<b>ε∞ = 1 is vacuum</b> — nothing would screen the field and "
                 "the splitting would come out far too large. Set the "
                 "material's value, or load it from an Optics run.")
            : tr("<i>The LO branch will be corrected at Γ, using the direction "
                 "the q-path approaches Γ from. Requires phonopy in the job "
                 "environment.</i>"));
}

QWidget* PhononWizard::buildSettingsPage()
{
    auto* page = new QWidget(this);
    auto* layout = new QVBoxLayout(page);

    // Stage 3 is purely the q-path: the embedded Brillouin-zone builder, same
    // as the Electronic Structure wizard — a phonon dispersion ω(q) is read
    // along exactly the same high-symmetry lines as an electronic band
    // structure.
    if (periodic_) {
        kpath_ = new EmbeddedKPathEditor(structure_, page);
        layout->addWidget(kpath_, 1);
        connect(kpath_, &EmbeddedKPathEditor::pathChanged, this,
                [this] { refreshPreview(); });
    } else {
        auto* note = new QLabel(
            tr("Γ-point normal modes only: a molecular system has no "
               "Brillouin zone, so there is no dispersion path to define."),
            page);
        note->setWordWrap(true);
        layout->addWidget(note);
        layout->addStretch(1);
    }
    return page;
}

QString PhononWizard::generateScript() const
{
    core::PhononConfig pc;
    pc.calculator = baseCalculatorConfig();
    // The GPAW electronic settings the shared rows collected.
    electronic_.applyTo(pc.calculator);
    pc.periodic = periodic_;
    for (int i = 0; i < 3; ++i)
        pc.supercell[i] = supercellSpins_[i]->value();
    pc.deltaAngstrom = deltaSpin_->value();
    // Single source of truth for path sampling, same as the Electronic
    // Structure wizard: the k-path builder's "points per segment" times the
    // number of segments. The old Stage-2 "Band-path points" box was a second,
    // independent control that could silently disagree with it.
    pc.bandPathPoints = kpath_ ? kpath_->pointsPerSegment() * kpath_->segmentCount()
                               : 100;
    if (kpath_)
        pc.kpath = kpath_->path().toStdString();
    pc.dosKptGrid = meshSpin_->value();
    pc.dosWidthCm = dosWidthSpin_->value();
    pc.acousticSumRule = acousticCheck_->isChecked();
    pc.symmetryReducedDisplacements = symmetryCheck_->isChecked();
    pc.removeResidualForces = residualCheck_->isChecked();
    if (bornCombo_) {
        pc.bornChargesFile =
            bornCombo_->currentData().toString().toStdString();
        // Diagonal only: the off-diagonal components of eps_inf vanish in every
        // crystal class the UI can express with three numbers, and a full
        // tensor entered by hand is far more often a typo than a real
        // low-symmetry measurement.
        for (int axis = 0; axis < 3; ++axis)
            pc.dielectric[axis][axis] = dielectricSpin_[axis]->value();
    }
    return QString::fromStdString(
        core::PhononScriptGenerator::generate(pc, "structure.extxyz"));
}

} // namespace calango::gui
