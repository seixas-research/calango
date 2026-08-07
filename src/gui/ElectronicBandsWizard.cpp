#include "gui/ElectronicBandsWizard.hpp"

#include "gui/GuiUtils.hpp"

#include "core/ElectronicScriptGenerator.hpp"
#include "core/Structure.hpp"
#include "gui/EmbeddedKPathEditor.hpp"

#include "core/Element.hpp"

#include <QGroupBox>
#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QTableWidget>
#include <QVBoxLayout>
#include <QWidget>

#include <algorithm>

namespace calango::gui {

namespace {

/// The orbital choices offered per fatband channel, in the order the combo
/// lists them. `m` is GPAW's index within the shell: for p, 0/1/2 is y/z/x;
/// for d, 0…4 is xy/yz/3z²−r²/zx/x²−y². -1 sums the whole shell.
///
/// The individual m entries are not padding. Separating p_z from (p_x, p_y) is
/// what separates the π bands of a layered material from its σ bands, and
/// picking d_z² out of a transition-metal d manifold is what identifies the
/// orbital a ligand binds — neither is visible in a shell-summed projection.
struct OrbitalChoice {
    const char* label;
    int l;
    int m;
};

constexpr OrbitalChoice kOrbitals[] = {
    {"s", 0, -1},
    {"p (total)", 1, -1},
    {"p_x", 1, 2},   {"p_y", 1, 0},   {"p_z", 1, 1},
    {"d (total)", 2, -1},
    {"d_xy", 2, 0},  {"d_yz", 2, 1},  {"d_z²", 2, 2},
    {"d_xz", 2, 3},  {"d_x²−y²", 2, 4},
    {"f (total)", 3, -1},
};

constexpr int kOrbitalCount =
    static_cast<int>(sizeof(kOrbitals) / sizeof(kOrbitals[0]));

enum FatbandColumn { kAtomsColumn = 0, kOrbitalColumn, kLabelColumn };

} // namespace

ElectronicBandsWizard::ElectronicBandsWizard(
    std::shared_ptr<const core::Structure> structure, QWidget* parent)
    : SimulationWizardBase(parent)
    , structure_(std::move(structure))
{
    buildUi();
    // The NSCF-from-baseline workflow (mandatory density, cutoff/XC/mode
    // inheritance, PDOS) is GPAW-specific, so open on GPAW rather than the
    // first allowed engine.
    selectCalculator(core::CalculatorKind::Gpaw);
}

QString ElectronicBandsWizard::wizardTitle() const
{
    return tr("Electronic Structure Setup");
}

QStringList ElectronicBandsWizard::calculatorElements() const
{
    return structureElements(structure_.get());
}

QWidget* ElectronicBandsWizard::buildCalculatorExtras()
{
    // The former separate k-Path stage is merged here: this single page hosts
    // baseline selection + PDOS settings + the interactive k-path builder.
    pdosGroup_ = new QGroupBox(tr("Density of states"), this);
    auto* form = new QFormLayout(pdosGroup_);

    // Charge-density baseline: a completed Single-Point Calculation whose
    // converged density (.gpw) the bands/PDOS run reuses non-self-consistently
    // (GPAW calc.fixed_density). This is MANDATORY — the controller
    // (MainWindow::showBandStructure) refuses to open the wizard when no
    // completed SCF baseline exists, so there is no inline-SCF fallback option.
    baselineCombo_ = new QComboBox(pdosGroup_);
    baselineCombo_->setToolTip(
        tr("The completed Single-Point Calculation whose converged charge "
           "density this run restarts from. The band structure and PDOS are "
           "evaluated at fixed density (NSCF); cutoff, XC and mode are inherited "
           "from it."));
    form->addRow(tr("Baseline SCF density:"), baselineCombo_);
    connect(baselineCombo_, &QComboBox::currentIndexChanged, this,
            [this] { refreshPreview(); });

    pdosCheck_ = new QCheckBox(
        tr("Compute element/orbital PDOS (GPAW backend)"), pdosGroup_);
    pdosCheck_->setChecked(true);
    form->addRow(QString(), pdosCheck_);

    // Fixed-density PDOS k-mesh, defaulted to 2× the baseline SCF grid along
    // each non-vacuum direction (a vacuum direction sampled with 1 k-point
    // stays 1). The PDOS is re-sampled at this denser mesh via fixed_density.
    auto* pdosKptRow = new QHBoxLayout;
    for (auto*& spin : pdosKptsSpin_) {
        spin = new QSpinBox(pdosGroup_);
        // 1024, not 128. The PDOS is a Brillouin-zone integral and converges
        // far more slowly with k-points than the total energy does; a fine
        // d-band feature or a small Fermi pocket can need a mesh well past the
        // old ceiling, and the ceiling was a UI limit rather than a physical
        // one. The cost is the user's to weigh — the tool tip states how it
        // grows — so the field should not decide for them.
        spin->setRange(1, 1024);
        spin->setValue(14);
        spin->setToolTip(
            tr("Monkhorst-Pack divisions for the fixed-density PDOS mesh, per "
               "axis.\n\n"
               "The PDOS is a Brillouin-zone integral and converges far more "
               "slowly with k-points than the total energy does, so the grid "
               "that converged the SCF is routinely too coarse for a clean "
               "density of states.\n\n"
               "Cost grows as the PRODUCT of the three divisions: 64x64x64 is "
               "262 144 k-points. A 2D sheet should leave its vacuum "
               "direction at 1."));
        pdosKptRow->addWidget(spin);
        connect(spin, &QSpinBox::valueChanged, this, [this] {
            // A manual edit freezes the auto-scaling.
            pdosKptsUserEdited_ = true;
            refreshPreview();
        });
    }
    form->addRow(tr("PDOS k-mesh:"), pdosKptRow);

    energyPointsSpin_ = new QSpinBox(pdosGroup_);
    energyPointsSpin_->setRange(50, 20000);
    energyPointsSpin_->setValue(401);
    energyPointsSpin_->setSingleStep(50);
    energyPointsSpin_->setToolTip(
        tr("Number of energy sampling points for the projected DOS; higher "
           "values give a finer energy grid."));
    form->addRow(tr("Energy points (N):"), energyPointsSpin_);
    connect(energyPointsSpin_, &QSpinBox::valueChanged, this,
            [this] { refreshPreview(); });

    // No Gaussian-smearing control here any more. The run writes the
    // unbroadened eigenvalue histogram and the viewer convolves it with a
    // slider, so asking for sigma before the calculation would be asking for a
    // decision that no longer has to be made — and one the user is in a far
    // better position to make afterwards, looking at the curve.
    // The PDOS controls only make sense when PDOS is requested.
    for (QWidget* w : {static_cast<QWidget*>(pdosKptsSpin_[0]),
                       static_cast<QWidget*>(pdosKptsSpin_[1]),
                       static_cast<QWidget*>(pdosKptsSpin_[2]),
                       static_cast<QWidget*>(energyPointsSpin_)}) {
        connect(pdosCheck_, &QCheckBox::toggled, w, &QWidget::setEnabled);
    }
    connect(pdosCheck_, &QCheckBox::toggled, this,
            [this] { refreshPreview(); });
    // Seed the PDOS k-mesh from the (default) SCF k-grid now that the base DFT
    // controls exist.
    applyPdosKmeshDefault();

    // --- Spin Configurations ---------------------------------------------
    // The SCF's collinear polarization is inherited from the baseline density
    // along with everything else about the SCF. What is still open here is how
    // the BANDS are evaluated on top of it — and that is where spin-orbit
    // coupling lives, because SOC is applied to the converged states rather
    // than to the density.
    spinGroup_ = new QGroupBox(tr("Spin Configurations"), this);
    auto* spinForm = new QFormLayout(spinGroup_);

    spinOrbitCheck_ = new QCheckBox(tr("Spin-Orbit Coupling"), spinGroup_);
    spinOrbitCheck_->setToolTip(
        tr("Re-diagonalize the band energies in the spinor basis "
           "(gpaw.spinorbit.soc_eigenstates), non-perturbatively.\n\n"
           "This is what lifts the degeneracies a scalar-relativistic run "
           "leaves in place: the Γ-point valence band splitting of a III-V "
           "semiconductor, Rashba splitting at a heavy-element surface, the "
           "band inversion of a topological insulator.\n\n"
           "The result is ONE channel of doubled, spin-mixed bands rather than "
           "a spin-up/spin-down pair, and the Fermi level moves with them. For "
           "light elements the shift is meV; for 5d/6p systems it is the "
           "difference between the right answer and the wrong one."));
    spinForm->addRow(QString(), spinOrbitCheck_);
    connect(spinOrbitCheck_, &QCheckBox::toggled, this, [this] {
        updateSpinOrbitExclusions();
        refreshPreview();
    });

    // DFT+U on this page too: a band structure is exactly where a missing U
    // shows up (a known insulator coming out metallic), so being sent back to
    // re-run the baseline to add one is the wrong shape of workflow.
    auto* hubbardButton = new QPushButton(tr("Hubbard parameters…"), spinGroup_);
    hubbardButton->setToolTip(
        tr("Add an on-site Coulomb repulsion U to a named orbital shell "
           "(GPAW setups={…}). For narrow d/f bands that a semilocal "
           "functional over-delocalizes — the usual symptom is a metallic "
           "band structure for a known insulator.\n\n"
           "Note: with a baseline density selected the bands are evaluated at "
           "that FIXED density, so the U in force is the one the baseline was "
           "converged with. A U set here applies when this run converges its "
           "own SCF."));
    connect(hubbardButton, &QPushButton::clicked, this,
            &ElectronicBandsWizard::editHubbardParameters);
    spinForm->addRow(QString(), hubbardButton);

    // Merged stage: the interactive Brillouin-zone / k-path builder comes
    // first, with the spin and PDOS configuration below it, in one container so
    // the wizard has a single setup stage.
    // TWO COLUMNS, because stacked this page no longer fits on a laptop
    // screen: the k-path editor alone is ~470 px tall (its Brillouin-zone view
    // carries a 460x420 minimum) and the four settings groups add ~700 px more.
    //
    // The split is not merely to halve the height — it is the distinction the
    // page already makes:
    //
    //   LEFT   what is CALCULATED — where in the Brillouin zone the bands are
    //          sampled, and how the electrons are treated getting there
    //          (spin-orbit coupling, Hubbard U). These change the eigenvalues.
    //   RIGHT  what is WRITTEN OUT — the projected DOS, the irrep labels, the
    //          orbital weights. Each is a readout of those same eigenvalues
    //          and produces its own result file, and none of them changes a
    //          number the others see.
    //
    // It balances too: ~580 px against ~620 px, where stacking was ~1190.
    auto* container = new QWidget(this);
    auto* columns = new QHBoxLayout(container);
    columns->setContentsMargins(0, 0, 0, 0);

    auto* left = new QVBoxLayout;
    left->addWidget(new QLabel(tr("High-symmetry k-path:"), container));
    kpath_ = new EmbeddedKPathEditor(structure_, container);
    // Stretch 1 on the editor and on this whole column: the Brillouin-zone
    // view is the one widget here that reads better the more room it gets, so
    // spare width and height go to it rather than stretching the form rows.
    left->addWidget(kpath_, 1);
    connect(kpath_, &EmbeddedKPathEditor::pathChanged, this,
            [this] { refreshPreview(); });
    left->addWidget(spinGroup_);
    columns->addLayout(left, 1);

    auto* right = new QVBoxLayout;
    right->addWidget(pdosGroup_);
    // Two readouts of the SAME band run, so they sit with the PDOS rather than
    // with the k-path: what each band IS by symmetry, and what each band is
    // MADE OF by orbital character.
    right->addWidget(buildSymmetryGroup());
    // The fatband channel table is the only resizable content on this side, so
    // it takes the slack instead of a trailing spacer.
    right->addWidget(buildFatbandGroup(), 1);
    // Stretch 0: these are form rows at their natural width. Widening the
    // dialog should grow the Brillouin zone, not stretch three combo boxes.
    columns->addLayout(right, 0);

    updateSpinOrbitExclusions();

    return container;
}

QGroupBox* ElectronicBandsWizard::buildSymmetryGroup()
{
    symmetryGroup_ = new QGroupBox(tr("Band symmetry"), this);
    auto* form = new QFormLayout(symmetryGroup_);

    symmetryCheck_ = new QCheckBox(
        tr("Assign irreducible representations at high-symmetry k-points"),
        symmetryGroup_);
    symmetryCheck_->setToolTip(
        tr("At a high-symmetry k-point the operations that leave k invariant "
           "form the LITTLE GROUP, and every band there realizes one of its "
           "irreducible representations. The label is what tells you which "
           "crossings are protected (bands of different irreps cannot "
           "hybridize, so they cross rather than repel), which optical "
           "transitions are allowed, and what a degeneracy is made of — a "
           "two-dimensional irrep at the Fermi level is a Dirac point.\n\n"
           "The characters are computed from the Kohn-Sham states themselves, "
           "and the character table of the little group from its own class-sum "
           "algebra. Written to band_symmetry.json and drawn beside the "
           "high-symmetry ticks in the viewer."));
    form->addRow(QString(), symmetryCheck_);

    symmetryLinesCheck_ =
        new QCheckBox(tr("Also classify the symmetry lines between them"),
                      symmetryGroup_);
    symmetryLinesCheck_->setChecked(true);
    symmetryLinesCheck_->setToolTip(
        tr("Classify a generic point of each path segment as well as its "
           "endpoints. This is what makes the COMPATIBILITY RELATIONS "
           "readable: the irrep at a point has to decompose into the irreps of "
           "the lines running out of it, which is how a degenerate level is "
           "seen to split and which branch goes where.\n\n"
           "Costs one extra k-point per segment."));
    form->addRow(QString(), symmetryLinesCheck_);

    symmetryTolSpin_ = new QDoubleSpinBox(symmetryGroup_);
    symmetryTolSpin_->setDecimals(5);
    symmetryTolSpin_->setRange(1e-5, 1.0);
    symmetryTolSpin_->setSingleStep(1e-4);
    symmetryTolSpin_->setValue(1e-4);
    symmetryTolSpin_->setSuffix(tr(" Å"));
    symmetryTolSpin_->setToolTip(
        tr("spglib symmetry-finding tolerance (symprec) for the space-group "
           "operations. Too tight and a relaxed structure loses operations it "
           "physically has; too loose and it gains ones it does not."));
    form->addRow(tr("Symmetry tolerance:"), symmetryTolSpin_);

    symmetryDegenSpin_ = new QDoubleSpinBox(symmetryGroup_);
    symmetryDegenSpin_->setDecimals(4);
    symmetryDegenSpin_->setRange(1e-4, 1.0);
    symmetryDegenSpin_->setSingleStep(0.005);
    symmetryDegenSpin_->setValue(0.02);
    symmetryDegenSpin_->setSuffix(tr(" eV"));
    symmetryDegenSpin_->setToolTip(
        tr("Bands closer than this share one character and are labelled as a "
           "single degenerate multiplet.\n\n"
           "It has to be a window rather than an exact test: two states that "
           "are degenerate BY SYMMETRY still come out of a diagonalization a "
           "few µeV apart, and splitting them yields two meaningless "
           "one-dimensional characters instead of one correct two-dimensional "
           "one."));
    form->addRow(tr("Degeneracy window:"), symmetryDegenSpin_);

    symmetryWindowSpin_ = new QDoubleSpinBox(symmetryGroup_);
    symmetryWindowSpin_->setDecimals(1);
    symmetryWindowSpin_->setRange(1.0, 500.0);
    symmetryWindowSpin_->setSingleStep(5.0);
    symmetryWindowSpin_->setValue(25.0);
    symmetryWindowSpin_->setSuffix(tr(" eV"));
    symmetryWindowSpin_->setToolTip(
        tr("Only bands within this distance of the Fermi level are "
           "classified. Each one costs a transform per symmetry operation, and "
           "the states worth naming are the ones near the gap."));
    form->addRow(tr("Energy window (±E<sub>F</sub>):"), symmetryWindowSpin_);

    for (QWidget* w : {static_cast<QWidget*>(symmetryLinesCheck_),
                       static_cast<QWidget*>(symmetryTolSpin_),
                       static_cast<QWidget*>(symmetryDegenSpin_),
                       static_cast<QWidget*>(symmetryWindowSpin_)}) {
        w->setEnabled(false);
        connect(symmetryCheck_, &QCheckBox::toggled, w, &QWidget::setEnabled);
    }
    connect(symmetryCheck_, &QCheckBox::toggled, this,
            [this] { refreshPreview(); });
    connect(symmetryLinesCheck_, &QCheckBox::toggled, this,
            [this] { refreshPreview(); });
    for (QDoubleSpinBox* spin : {symmetryTolSpin_, symmetryDegenSpin_,
                                 symmetryWindowSpin_}) {
        connect(spin, &QDoubleSpinBox::valueChanged, this,
                [this] { refreshPreview(); });
    }
    return symmetryGroup_;
}

QGroupBox* ElectronicBandsWizard::buildFatbandGroup()
{
    fatbandGroup_ = new QGroupBox(tr("Orbital projections (fatbands)"), this);
    auto* layout = new QVBoxLayout(fatbandGroup_);

    fatbandCheck_ = new QCheckBox(
        tr("Compute orbital-projected bands (fatbands)"), fatbandGroup_);
    fatbandCheck_->setToolTip(
        tr("Carry the weight |⟨φ|ψ⟩|² of the selected orbitals alongside every "
           "band energy, so the viewer can draw each band with a thickness or "
           "a colour proportional to it.\n\n"
           "A band structure says where the states are; this says what they "
           "are made of — whether the band crossing E_F is metal d or ligand "
           "p, which is the difference between a description and an "
           "explanation. Written to fatbands.json."));
    layout->addWidget(fatbandCheck_);

    fatbandTable_ = new QTableWidget(0, 3, fatbandGroup_);
    fatbandTable_->setHorizontalHeaderLabels(
        {tr("Atoms"), tr("Orbital"), tr("Label")});
    fatbandTable_->horizontalHeader()->setSectionResizeMode(
        QHeaderView::Stretch);
    fatbandTable_->verticalHeader()->setVisible(false);
    fatbandTable_->setMinimumHeight(120);
    fatbandTable_->setToolTip(
        tr("One row per projection channel.\n\n"
           "Atoms: an element symbol (\"Fe\"), a 0-based index list "
           "(\"0, 2, 5-8\"), or empty for every atom. Selecting individual "
           "indices is what distinguishes a surface layer from the bulk "
           "underneath it — the same element, a different question.\n\n"
           "Orbital: the shell, or one magnetic sub-level of it.\n\n"
           "Leave the table EMPTY for one channel per element and per shell "
           "present in the structure."));
    layout->addWidget(fatbandTable_);

    auto* buttons = new QHBoxLayout;
    auto* addButton = new QPushButton(tr("Add channel"), fatbandGroup_);
    auto* removeButton = new QPushButton(tr("Remove"), fatbandGroup_);
    auto* defaultsButton = new QPushButton(tr("From structure"), fatbandGroup_);
    defaultsButton->setToolTip(
        tr("One channel per element in this structure, on its valence shell."));
    buttons->addWidget(addButton);
    buttons->addWidget(removeButton);
    buttons->addWidget(defaultsButton);
    buttons->addStretch(1);
    layout->addLayout(buttons);

    connect(addButton, &QPushButton::clicked, this, [this] {
        addFatbandRow(QString(), 1, QString());
        refreshPreview();
    });
    connect(removeButton, &QPushButton::clicked, this, [this] {
        const int row = fatbandTable_->currentRow();
        if (row >= 0)
            fatbandTable_->removeRow(row);
        refreshPreview();
    });
    connect(defaultsButton, &QPushButton::clicked, this, [this] {
        fatbandTable_->setRowCount(0);
        seedFatbandRows();
    });
    connect(fatbandTable_, &QTableWidget::cellChanged, this,
            [this] { refreshPreview(); });

    for (QWidget* w : {static_cast<QWidget*>(fatbandTable_),
                       static_cast<QWidget*>(addButton),
                       static_cast<QWidget*>(removeButton),
                       static_cast<QWidget*>(defaultsButton)}) {
        w->setEnabled(false);
        connect(fatbandCheck_, &QCheckBox::toggled, w, &QWidget::setEnabled);
    }
    connect(fatbandCheck_, &QCheckBox::toggled, this,
            [this] { refreshPreview(); });

    seedFatbandRows();
    return fatbandGroup_;
}

void ElectronicBandsWizard::addFatbandRow(const QString& atoms,
                                          int orbitalIndex,
                                          const QString& label)
{
    if (!fatbandTable_)
        return;
    const int row = fatbandTable_->rowCount();
    fatbandTable_->insertRow(row);

    auto* atomsEdit = new QLineEdit(atoms, fatbandTable_);
    atomsEdit->setPlaceholderText(tr("all — or \"Fe\", or \"0, 2, 5-8\""));
    connect(atomsEdit, &QLineEdit::textChanged, this,
            [this] { refreshPreview(); });
    fatbandTable_->setCellWidget(row, kAtomsColumn, atomsEdit);

    auto* combo = new QComboBox(fatbandTable_);
    for (const OrbitalChoice& choice : kOrbitals)
        combo->addItem(QString::fromUtf8(choice.label));
    combo->setCurrentIndex(std::clamp(orbitalIndex, 0, kOrbitalCount - 1));
    connect(combo, &QComboBox::currentIndexChanged, this,
            [this] { refreshPreview(); });
    fatbandTable_->setCellWidget(row, kOrbitalColumn, combo);

    auto* labelEdit = new QLineEdit(label, fatbandTable_);
    labelEdit->setPlaceholderText(tr("auto"));
    connect(labelEdit, &QLineEdit::textChanged, this,
            [this] { refreshPreview(); });
    fatbandTable_->setCellWidget(row, kLabelColumn, labelEdit);
    // No refreshPreview() here: the callers do it, once, after appending the
    // rows they wanted. Refreshing per row would regenerate the whole script
    // once per element on a "From structure" reset — and would do it during
    // construction, before there is a preview to write into.
}

void ElectronicBandsWizard::seedFatbandRows()
{
    // One channel per element, on the shell that actually carries its valence
    // states: s for H/He, p for the main group, d for the transition metals.
    // Seeding every element with "p" would put an empty channel on iron.
    //
    // The preview is refreshed ONCE, after the rows are in. During
    // construction that call is a no-op — the review page, and the text edit
    // it owns, are built after every settings page — and on a "From structure"
    // reset it regenerates the script once instead of once per element.
    for (const QString& symbol : structureElements(structure_.get())) {
        const int z = core::Elements::atomicNumber(symbol.toStdString());
        int orbital = 1; // "p (total)"
        if (z <= 2 || z == 3 || z == 11 || z == 19 || z == 37 || z == 55)
            orbital = 0; // s block
        else if ((z >= 21 && z <= 30) || (z >= 39 && z <= 48)
                 || (z >= 72 && z <= 80))
            orbital = 5; // "d (total)"
        addFatbandRow(symbol, orbital, QString());
    }
    refreshPreview();
}

std::vector<int>
ElectronicBandsWizard::parseAtomSelection(const QString& text) const
{
    // Shared parser — the same spelling as the Born-charges wizard's atom
    // field, so one selection syntax is learned once.
    return parseAtomIndexList(
        text, structure_ ? static_cast<int>(structure_->size()) : 0);
}

std::vector<core::FatbandProjection>
ElectronicBandsWizard::fatbandProjections() const
{
    std::vector<core::FatbandProjection> channels;
    if (!fatbandTable_)
        return channels;
    for (int row = 0; row < fatbandTable_->rowCount(); ++row) {
        const auto* atomsEdit = qobject_cast<QLineEdit*>(
            fatbandTable_->cellWidget(row, kAtomsColumn));
        const auto* combo = qobject_cast<QComboBox*>(
            fatbandTable_->cellWidget(row, kOrbitalColumn));
        const auto* labelEdit = qobject_cast<QLineEdit*>(
            fatbandTable_->cellWidget(row, kLabelColumn));
        if (!combo)
            continue;
        const OrbitalChoice& orbital =
            kOrbitals[std::clamp(combo->currentIndex(), 0,
                                 kOrbitalCount - 1)];

        core::FatbandProjection projection;
        projection.angularMomentum = orbital.l;
        projection.magnetic = orbital.m;

        const QString selection =
            atomsEdit ? atomsEdit->text().trimmed() : QString();
        // A selection that is not a number list is read as an element symbol:
        // "Fe" and "0, 2" are both natural ways to say which atoms, and which
        // one was meant is unambiguous from the text.
        projection.atoms = parseAtomSelection(selection);
        QString scope = tr("all");
        if (!projection.atoms.empty()) {
            scope = selection;
        } else if (!selection.isEmpty()) {
            projection.element = selection.toStdString();
            scope = selection;
        }

        const QString label =
            labelEdit ? labelEdit->text().trimmed() : QString();
        projection.label =
            (label.isEmpty()
                 ? QStringLiteral("%1 %2").arg(
                       scope, QString::fromUtf8(orbital.label))
                 : label)
                .toStdString();
        channels.push_back(std::move(projection));
    }
    return channels;
}

void ElectronicBandsWizard::updateSpinOrbitExclusions()
{
    // With SOC on, `bs` holds the spinor bands while the projections and
    // characters describe the scalar-relativistic states they were built from
    // — a different set, in a different number. Rather than emit labels
    // attached to the wrong bands, the two are made unavailable and say why.
    const bool soc = spinOrbitCheck_ && spinOrbitCheck_->isChecked();
    // The explanation goes on the GROUP, not on the check boxes: a disabled
    // check box would have to have its own tool tip swapped out and back, and
    // the one it already carries explains what the feature IS, which is still
    // what a reader wants to know while it is unavailable.
    const QString reason =
        tr("Unavailable with spin-orbit coupling: the spinor bands are a "
           "different set of states from the scalar-relativistic ones these "
           "describe, and classifying them needs the double groups.");
    for (QCheckBox* box : {symmetryCheck_, fatbandCheck_}) {
        if (box && soc && box->isChecked())
            box->setChecked(false);
    }
    for (QGroupBox* group : {symmetryGroup_, fatbandGroup_}) {
        if (!group)
            continue;
        group->setEnabled(!soc);
        group->setToolTip(soc ? reason : QString());
    }
}

void ElectronicBandsWizard::applyPdosKmeshDefault()
{
    if (pdosKptsUserEdited_)
        return;
    for (int axis = 0; axis < 3; ++axis) {
        if (!pdosKptsSpin_[axis])
            continue;
        const int base = calculatorKpoint(axis);
        // Vacuum directions (single k-point) stay at 1; everything else ×2.
        const int scaled = base <= 1 ? base : base * 2;
        const QSignalBlocker blocker(pdosKptsSpin_[axis]);
        pdosKptsSpin_[axis]->setValue(scaled);
    }
}

void ElectronicBandsWizard::calculatorKgridChanged()
{
    // The baseline SCF k-grid changed — rescale the PDOS mesh default unless
    // the user has taken it over.
    applyPdosKmeshDefault();
}

void ElectronicBandsWizard::updateCalculatorExtras(core::CalculatorKind kind)
{
    // Only the GPAW backend produces a projected DOS, applies spin-orbit
    // coupling or takes a `setups` DFT+U dictionary; showing the controls for
    // the others would promise output they cannot generate.
    // Band symmetry and fatbands go further still: both read the Kohn-Sham
    // states through GPAW's own APIs (the pseudo wave functions and the PAW
    // projector overlaps), which the file-based DFT templates never expose.
    const bool gpaw = kind == core::CalculatorKind::Gpaw;
    for (QGroupBox* group : {pdosGroup_, spinGroup_, symmetryGroup_,
                             fatbandGroup_}) {
        if (group)
            group->setVisible(gpaw);
    }
}

bool ElectronicBandsWizard::calculatorAllowed(core::CalculatorKind kind) const
{
    switch (kind) {
    case core::CalculatorKind::Gpaw:
    case core::CalculatorKind::Siesta:
    case core::CalculatorKind::Vasp:
    case core::CalculatorKind::QuantumEspresso:
        return true;
    default:
        return false;
    }
}

QString ElectronicBandsWizard::generateScript() const
{
    core::ElectronicConfig config;
    switch (selectedCalculator()) {
    case core::CalculatorKind::Gpaw:
        config.backend = core::ElectronicBackend::Gpaw;
        break;
    case core::CalculatorKind::QuantumEspresso:
        config.backend = core::ElectronicBackend::Espresso;
        break;
    case core::CalculatorKind::Siesta:
        config.backend = core::ElectronicBackend::Siesta;
        break;
    case core::CalculatorKind::Vasp:
        config.backend = core::ElectronicBackend::Vasp;
        break;
    default:
        // The combo is DFT-only, so this is unreachable; fall back to the
        // free-electron reference model for safety.
        config.backend = core::ElectronicBackend::FreeElectrons;
        break;
    }

    // Keep the ',' section breaks; ASE's cell.bandpath() understands them.
    config.kpath = kpath_->path().toStdString();
    // Single source of truth for path sampling: the k-path builder's
    // "points per segment" times the number of segments. The wizard used to
    // carry a second, independent "k-points along path" box that could
    // silently disagree with it.
    config.npoints = kpath_->pointsPerSegment() * kpath_->segmentCount();
    config.spinOrbit = spinOrbitCheck_ && spinOrbitCheck_->isChecked();

    config.bandSymmetry = symmetryCheck_ && symmetryCheck_->isChecked();
    if (config.bandSymmetry) {
        config.symmetry.symprec = symmetryTolSpin_->value();
        config.symmetry.degeneracyEv = symmetryDegenSpin_->value();
        config.symmetry.windowEv = symmetryWindowSpin_->value();
        config.symmetry.classifyLines = symmetryLinesCheck_->isChecked();
    }

    config.fatbands = fatbandCheck_ && fatbandCheck_->isChecked();
    if (config.fatbands)
        config.fatbandProjections = fatbandProjections();

    config.pdos = pdosCheck_->isChecked();
    config.pdosPoints = energyPointsSpin_->value();
    for (int axis = 0; axis < 3; ++axis)
        config.pdosKpts[axis] = pdosKptsSpin_[axis]->value();
    // Full GPAW parameter set from Stage 2 (mode, xc, eigensolver, mixer,
    // convergence, smearing, k-grid) — the same controls Geometry
    // Optimization and Single-point use.
    config.gpaw = baseCalculatorConfig();

    // SCF baseline from the shared Stage-3 DFT knobs.
    const core::CalculatorConfig base = baseCalculatorConfig();
    config.ecutEv = base.planeWaveCutoffEv;
    config.scfKpts = base.kpts[0];

    // A selected charge-density baseline turns the run NSCF: evaluate
    // bands/PDOS at the fixed density of a prior single point instead of
    // converging a new one. The path is absolute, so the job reads that
    // density in place — no staging.
    //
    // Both DFT engines, differing only in the file: GPAW restarts from a .gpw,
    // VASP copies the CHGCAR in and sets ICHARG = 11.
    if (baselineCombo_
        && (config.backend == core::ElectronicBackend::Gpaw
            || config.backend == core::ElectronicBackend::Vasp)) {
        const QString path = baselineCombo_->currentData().toString();
        if (!path.isEmpty())
            config.baselineDensityPath = path.toStdString();
    }

    return QString::fromStdString(core::generateElectronicScript(config));
}

void ElectronicBandsWizard::setDensityBaselines(
    const QList<QPair<QString, QString>>& baselines)
{
    if (!baselineCombo_)
        return;
    for (const auto& [label, path] : baselines)
        baselineCombo_->addItem(label, path);
}

} // namespace calango::gui
