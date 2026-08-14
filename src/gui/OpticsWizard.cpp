#include "gui/OpticsWizard.hpp"

#include "gui/GuiUtils.hpp"

#include "core/OpticsScriptGenerator.hpp"
#include "core/Structure.hpp"

#include <QCheckBox>
#include <QDoubleSpinBox>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QComboBox>
#include <QGroupBox>
#include <QLabel>
#include <QSpinBox>
#include <QStandardItemModel>
#include <QVBoxLayout>
#include <QWidget>

#include <algorithm>

namespace calango::gui {

OpticsWizard::OpticsWizard(std::shared_ptr<core::Structure> structure,
                           bool twoDimensional, QWidget* parent)
    : SimulationWizardBase(parent)
    , structure_(std::move(structure))
    , twoDimensional_(twoDimensional)
{
    buildUi();
    // Now that every stage exists, bring the engine-dependent visibility,
    // labels and the hidden base engine selection into their initial state.
    onEngineChanged();
}

QString OpticsWizard::wizardTitle() const
{
    return twoDimensional_ ? tr("2D Optics Setup")
                           : tr("Optical Properties Setup");
}

QString OpticsWizard::settingsHeader() const
{
    return tr("Optical Response Settings");
}

QWidget* OpticsWizard::buildSettingsPage()
{
    auto* page = new QWidget(this);
    auto* layout = new QVBoxLayout(page);

    // -- Engine ---------------------------------------------------------------
    // Chosen here rather than on a Calculator Settings stage: the two engines
    // need entirely different stage-1 content (a baseline selector vs. a
    // compact ground-state group), so the choice must precede the page.
    auto* engineRow = new QHBoxLayout;
    engineRow->addWidget(new QLabel(tr("Engine:"), page));
    engineCombo_ = new QComboBox(page);
    engineCombo_->addItem(
        tr("GPAW — inherit a converged ground state (.gpw)"),
        static_cast<int>(core::CalculatorKind::Gpaw));
    engineCombo_->addItem(
        tr("VASP — SCF + LOPTICS in one job"),
        static_cast<int>(core::CalculatorKind::Vasp));
    engineCombo_->setToolTip(
        tr("GPAW evaluates the response at the fixed density of a completed "
           "single point (gpaw.response.df). VASP runs the standard LOPTICS "
           "protocol: a normal SCF, then an exact-diagonalization restart "
           "(ICHARG=11) with LOPTICS=.TRUE., enlarged NBANDS, CSHIFT "
           "broadening and a NEDOS frequency grid."));
    engineRow->addWidget(engineCombo_, 1);
    layout->addLayout(engineRow);
    connect(engineCombo_, &QComboBox::currentIndexChanged, this,
            [this] { onEngineChanged(); });

    // -- Mandatory ground-state baseline (GPAW) ------------------------------
    baselineGroup_ = new QGroupBox(tr("Ground-State Baseline"), page);
    auto* baselineGroup = baselineGroup_;
    auto* baselineForm = new QFormLayout(baselineGroup);
    auto* baselineNote = new QLabel(
        tr("The response is evaluated at the FIXED density of a completed "
           "Single-Point Calculation; its SCF is inherited, never re-run."),
        baselineGroup);
    baselineNote->setWordWrap(true);
    baselineNote->setToolTip(
        tr("Re-converging here would give a spectrum from a different ground "
           "state than the one you validated."));
    baselineForm->addRow(baselineNote);
    baselineCombo_ = new QComboBox(baselineGroup);
    baselineForm->addRow(tr("Baseline SCF (.gpw):"), baselineCombo_);
    // What is being inherited, spelled out. With no Calculator Settings stage
    // this is the only place the run's cutoff / xc / k-grid are visible, and
    // they are exactly what determines whether the spectrum is trustworthy.
    inheritanceNote_ = new QLabel(baselineGroup);
    inheritanceNote_->setWordWrap(true);
    inheritanceNote_->setTextFormat(Qt::RichText);
    baselineForm->addRow(inheritanceNote_);
    connect(baselineCombo_, &QComboBox::currentIndexChanged, this,
            [this] { onBaselineChanged(); });
    layout->addWidget(baselineGroup);

    // -- VASP ground state (self-contained run) ------------------------------
    vaspGroup_ = new QGroupBox(tr("VASP Ground State"), page);
    auto* vaspForm = new QFormLayout(vaspGroup_);
    auto* vaspNote = new QLabel(
        tr("Self-contained: the job runs its own SCF, then restarts at fixed "
           "density for the LOPTICS step."),
        vaspGroup_);
    vaspNote->setWordWrap(true);
    vaspNote->setToolTip(
        tr("POTCARs come from Preferences → External Files (VASP_PP_PATH)."));
    vaspForm->addRow(vaspNote);
    vaspEncutSpin_ = new QDoubleSpinBox(vaspGroup_);
    vaspEncutSpin_->setRange(100.0, 2000.0);
    vaspEncutSpin_->setDecimals(0);
    vaspEncutSpin_->setValue(500.0);
    vaspEncutSpin_->setSuffix(tr(" eV"));
    vaspEncutSpin_->setToolTip(
        tr("ENCUT for both steps. Optical spectra inherit the ground "
           "state's convergence — run the Plane-wave Cutoff Convergence "
           "module first if in doubt."));
    vaspForm->addRow(tr("Plane-wave cutoff (ENCUT):"), vaspEncutSpin_);
    auto* vaspKptRow = new QHBoxLayout;
    for (int axis = 0; axis < 3; ++axis) {
        vaspKptSpins_[axis] = new QSpinBox(vaspGroup_);
        vaspKptSpins_[axis]->setRange(1, 64);
        vaspKptSpins_[axis]->setValue(7);
        vaspKptRow->addWidget(vaspKptSpins_[axis]);
        connect(vaspKptSpins_[axis], &QSpinBox::valueChanged, this,
                [this] { refreshPreview(); });
    }
    vaspKptRow->addStretch(1);
    vaspForm->addRow(tr("SCF k-point grid:"), vaspKptRow);
    vaspXcCombo_ = new QComboBox(vaspGroup_);
    // The functionals the AseScriptGenerator VASP branch accepts; the hybrids
    // switch the LOPTICS step to ALGO=Eigenval automatically.
    vaspXcCombo_->addItems({QStringLiteral("PBE"), QStringLiteral("PBEsol"),
                            QStringLiteral("LDA"), QStringLiteral("SCAN"),
                            QStringLiteral("HSE06"), QStringLiteral("PBE0")});
    vaspXcCombo_->setToolTip(
        tr("Exchange-correlation functional for both steps. With a hybrid "
           "(exact exchange) the optics restart uses ALGO=Eigenval, as the "
           "semilocal ALGO=Exact path does not apply the exact-exchange "
           "operator to the new empty states."));
    vaspForm->addRow(tr("XC functional:"), vaspXcCombo_);
    layout->addWidget(vaspGroup_);

    connect(vaspEncutSpin_, &QDoubleSpinBox::valueChanged, this,
            [this] { refreshPreview(); });
    connect(vaspXcCombo_, &QComboBox::currentIndexChanged, this,
            [this] { refreshPreview(); });

    if (twoDimensional_) {
        auto* sheetGroup = new QGroupBox(tr("2D Sheet"), page);
        auto* sheetForm = new QFormLayout(sheetGroup);
        auto* sheetNote = new QLabel(
            tr("A supercell dielectric function is diluted by the vacuum used, "
               "so ε₃D is not a property of the sheet."),
            sheetGroup);
        sheetNote->setWordWrap(true);
        sheetNote->setToolTip(
            tr("Double the vacuum and ε₃D moves. Dividing that thickness back "
               "out gives α₂D, σ₂D and the absorbance, which are properties of "
               "the sheet."));
        sheetForm->addRow(sheetNote);
        vacuumAxisCombo_ = new QComboBox(sheetGroup);
        vacuumAxisCombo_->addItem(tr("a₁ (x)"), 0);
        vacuumAxisCombo_->addItem(tr("a₂ (y)"), 1);
        vacuumAxisCombo_->addItem(tr("a₃ (z)"), 2);
        const int guessed = calango::gui::guessVacuumAxis(structure_.get());
        vacuumAxisCombo_->setCurrentIndex(guessed >= 0 ? guessed : 2);
        vacuumAxisCombo_->setToolTip(
            tr("Which cell axis carries the vacuum. Seeded from the cell (the "
               "long axis the atoms only partly occupy) but confirm it: "
               "getting it wrong rescales every 2D quantity by the wrong "
               "length, silently."));
        sheetForm->addRow(tr("Vacuum axis:"), vacuumAxisCombo_);
        layout->addWidget(sheetGroup);
    }

    auto* intro = new QLabel(
        tr("Frequency-dependent ε(ω) and the derived spectra (absorption, "
           "reflectivity, refractive index, energy loss) from GPAW."),
        page);
    intro->setWordWrap(true);
    intro->setToolTip(
        tr("The ground-state cutoff and k-grid come from the baseline above. A "
           "spectrum is only as converged as the SCF it is built on, so a "
           "coarse baseline k-grid needs a denser one — not a finer η here."));
    layout->addWidget(intro);

    auto* form = new QFormLayout;
    responseForm_ = form;
    layout->addLayout(form);

    broadeningSpin_ = new QDoubleSpinBox(page);
    broadeningSpin_->setDecimals(3);
    broadeningSpin_->setRange(0.001, 5.0);
    broadeningSpin_->setSingleStep(0.05);
    broadeningSpin_->setValue(0.1);
    broadeningSpin_->setSuffix(tr(" eV"));
    broadeningSpin_->setToolTip(
        tr("Lorentzian broadening η the dielectric function is COMPUTED at — "
           "and the floor the viewer can later raise, but never lower.\n\n"
           "Unlike a DOS smearing this cannot be left out of the calculation: "
           "η is a lifetime and sits inside the response function GPAW "
           "inverts. What it can do afterwards is grow, because Lorentzian "
           "widths add under convolution — so the results window offers a "
           "slider from this value upwards, and every curve there follows it.\n\n"
           "Set it as SMALL as the k-mesh supports. A sharp η on a coarse mesh "
           "produces sampling spikes, not resolution; a broad one throws away "
           "structure that no amount of post-processing brings back."));
    form->addRow(tr("Broadening η (minimum):"), broadeningSpin_);

    omegaMinSpin_ = new QDoubleSpinBox(page);
    omegaMinSpin_->setDecimals(2);
    omegaMinSpin_->setRange(0.0, 1000.0);
    omegaMinSpin_->setValue(0.0);
    omegaMinSpin_->setSuffix(tr(" eV"));
    omegaMinSpin_->setToolTip(tr("Lower bound of the photon-energy window."));
    form->addRow(tr("Energy window min:"), omegaMinSpin_);

    omegaMaxSpin_ = new QDoubleSpinBox(page);
    omegaMaxSpin_->setDecimals(2);
    omegaMaxSpin_->setRange(0.1, 1000.0);
    omegaMaxSpin_->setValue(20.0);
    omegaMaxSpin_->setSuffix(tr(" eV"));
    omegaMaxSpin_->setToolTip(tr("Upper bound of the photon-energy window."));
    form->addRow(tr("Energy window max:"), omegaMaxSpin_);

    // --- Response k-mesh -------------------------------------------------
    // The dielectric function converges far more slowly with k-points than the
    // total energy, so the grid that converged the baseline SCF is routinely
    // too coarse for the spectrum. Re-sampling at fixed density is cheap.
    auto* meshRow = new QHBoxLayout;
    for (int i = 0; i < 3; ++i) {
        responseKptsSpin_[i] = new QSpinBox(page);
        responseKptsSpin_[i]->setRange(0, 200);
        responseKptsSpin_[i]->setValue(0);
        responseKptsSpin_[i]->setSpecialValueText(tr("auto"));
        responseKptsSpin_[i]->setToolTip(
            tr("Monkhorst-Pack divisions for the fixed-density response step, "
               "set PER AXIS. \"auto\" keeps the baseline's own value on that "
               "axis, so a 2D sheet can be given a dense in-plane mesh while "
               "its out-of-plane direction stays where the ground state put "
               "it — e.g. 24, 24, auto.\n\n"
               "The dielectric function is a Brillouin-zone integral over "
               "interband transitions and converges far more slowly with "
               "k-points than the total energy does, so the grid that "
               "converged the SCF is routinely too coarse for the "
               "spectrum.\n\n"
               "Under tetrahedron integration these are held EVEN and stepped "
               "by two: GPAW raises on an odd grid rather than rejecting it, "
               "so an odd entry could not even be tested against its "
               "IBZ-vertex predicate."));
        connect(responseKptsSpin_[i], &QSpinBox::valueChanged, this,
                [this] { refreshPreview(); });
        // Rounded on editingFinished rather than valueChanged: the latter
        // fires per keystroke, and a user typing "16" would see the leading
        // "1" jump to "2" under their cursor.
        connect(responseKptsSpin_[i], &QSpinBox::editingFinished, this,
                [this] { applyTetrahedronMeshConstraints(); });
        meshRow->addWidget(responseKptsSpin_[i]);
    }
    meshRow->addStretch(1);
    form->addRow(tr("Response k-mesh:"), meshRow);

    // Short label, full story in the tooltip: checked REDUCES the mesh to the
    // irreducible wedge. The long-form caveat that used to sit under this row
    // as a wrapped QLabel clipped the page at ordinary dialog widths, so it
    // now lives in the tooltip below, which is where the rest of this page
    // keeps its detail.
    ibzCheck_ = new QCheckBox(tr("Use IBZ symmetry"), page);
    ibzCheck_->setToolTip(
        tr("Reduce the response mesh to the irreducible Brillouin zone, "
           "weighting each point by its symmetry degeneracy, instead of "
           "sampling the full zone. The weights are GPAW's own, derived from "
           "the cell's symmetry.\n\n"
           "Under POINT integration this is free accuracy-wise: measured on "
           "bulk Si at 8×8×8, 29 irreducible points against 512 in the full "
           "zone give ε₁(0) and ε₂ agreeing to every printed digit — the same "
           "spectrum for a seventeenth of the work.\n\n"
           "Under TETRAHEDRON integration it is not exact, because the "
           "tessellation is anchored on the wedge rather than the whole zone: "
           "ε₁(0) came out 12.879 reduced against 12.596 unreduced at 8×8×8 "
           "(2.2 %), narrowing to 12.336 against 12.351 at 16×16×16 (0.12 %). "
           "That is interpolation error converging away with the mesh, not "
           "one of the two being wrong.\n\n"
           "Independent of the IBZ-VERTEX condition tetrahedron integration "
           "imposes on the mesh — that is a separate switch the run handles "
           "itself, and turning this off does not relax it."));
    form->addRow(QString(), ibzCheck_);
    // Only the preview depends on this now. It used to re-run onEngineChanged()
    // as well, purely so the paired tetrahedron caveat label could appear and
    // disappear with the combination; that label is gone.
    connect(ibzCheck_, &QCheckBox::toggled, this,
            [this] { refreshPreview(); });

    tetrahedronCheck_ = new QCheckBox(
        tr("Tetrahedron integration (For even, Γ-centered mesh)"), page);
    tetrahedronCheck_->setToolTip(
        tr("Integrate the response by linear tetrahedron interpolation instead "
           "of summing over k-points.\n"
           "Point integration gives every transition a Lorentzian of width η, "
           "so any feature narrower than η — a van Hove singularity, a 2D "
           "absorption edge — is smeared rather than resolved. Tetrahedron "
           "integration resolves those on a mesh where point integration is "
           "still noisy.\n\n"
           "It requires a response k-mesh containing every vertex of the "
           "irreducible Brillouin zone — GPAW tessellates the zone and keeps "
           "the k-points inside it, so a mesh that misses the vertices leaves "
           "that tessellation with nothing to anchor on. That is a narrow "
           "condition: the grid must be EVEN and Γ-centred on every periodic "
           "axis, and each axis restricted to multiples of a number set by "
           "the lattice — 8 for fcc, diamond and rocksalt, 4 for bcc, 6 for "
           "hexagonal. An ordinary Monkhorst-Pack grid usually does not "
           "qualify.\n\n"
           "The run therefore checks the mesh against GPAW's own predicate "
           "BEFORE the expensive step and raises it to the cheapest grid that "
           "does qualify, Γ-centred, saying so in the log. Expect that to "
           "cost real time — 9×9×9 on an fcc cell becomes 16×16×16, five "
           "times the k-points.\n\n"
           "Rhombohedral, monoclinic and triclinic cells have zone vertices "
           "fixed by the cell angles, so NO mesh of any size qualifies. There "
           "the run keeps tetrahedron integration and drops symmetry reduction "
           "of the response instead, costing roughly the number of symmetry "
           "operations more. It logs whichever route it took. "
           "A run whose mesh was adjusted records the "
           "grid it actually used alongside the spectrum."));
    form->addRow(QString(), tetrahedronCheck_);

    // --- Free-carrier (Drude) term ---------------------------------------
    // A metal's low-energy spectrum is dominated by it, and a gapped system
    // is unaffected either way (GPAW gates the term on `gs.metallic`), so
    // the default is on and the control exists for the comparison rather
    // than for the common case.
    drudeCheck_ =
        new QCheckBox(tr("Intraband (Drude) free-carrier term"), page);
    drudeCheck_->setChecked(true);
    drudeCheck_->setToolTip(
        tr("Add the free-carrier response of partially occupied bands to the "
           "optical limit — the negative ε₁ and the intraband absorption that "
           "dominate a metal below its interband onset.\n\n"
           "GPAW applies it only when the ground state is actually metallic, "
           "so on a gapped system leaving it on is a verified no-op "
           "(bitwise-identical spectra) rather than a risk. Turn it off to "
           "look at a metal's interband structure with the Drude tail "
           "removed — not to describe a semiconductor.\n\n"
           "Its strength is the plasma frequency ω_p, a FERMI-SURFACE "
           "integral: it converges with the k-mesh far more slowly, and less "
           "monotonically, than the interband spectrum beside it. The "
           "K-points Convergence module can measure it directly."));
    form->addRow(QString(), drudeCheck_);

    drudeRateCombo_ = new QComboBox(page);
    drudeRateCombo_->addItem(tr("Follow the broadening η"), 1);
    drudeRateCombo_->addItem(tr("From a relaxation time τ"), 0);
    drudeRateCombo_->setToolTip(
        tr("Where the Drude relaxation rate comes from.\n\n"
           "\"Follow the broadening η\" is GPAW's own idiom (rate=\"eta\"): "
           "one number does both jobs, so they cannot disagree. It is the "
           "safe default, but η is a plotting choice — not a scattering "
           "time.\n\n"
           "\"From a relaxation time τ\" sets the free-carrier lifetime "
           "independently, which is what comparing against a measured Drude "
           "edge or a resistivity needs. Room-temperature values are a few "
           "to a few tens of femtoseconds (Au ≈ 9 fs, Al ≈ 8 fs)."));
    form->addRow(tr("Drude relaxation rate:"), drudeRateCombo_);

    // A container widget rather than a bare QHBoxLayout: QFormLayout's
    // getWidgetPosition only finds widgets added directly as fields, so a spin
    // box nested inside a layout cannot be addressed by setRowVisible at all —
    // the row would silently never hide.
    drudeTauRow_ = new QWidget(page);
    auto* tauRow = new QHBoxLayout(drudeTauRow_);
    tauRow->setContentsMargins(0, 0, 0, 0);
    drudeTauSpin_ = new QDoubleSpinBox(drudeTauRow_);
    drudeTauSpin_->setDecimals(2);
    drudeTauSpin_->setRange(0.01, 10000.0);
    drudeTauSpin_->setSingleStep(1.0);
    drudeTauSpin_->setValue(10.0);
    drudeTauSpin_->setSuffix(tr(" fs"));
    drudeTauSpin_->setToolTip(
        tr("Free-carrier relaxation time τ. Converted to the rate GPAW takes "
           "as rate = ħ/(2τ).\n\n"
           "The factor of two is GPAW's convention, not a fudge: it damps as "
           "ω_p²/(ω + i·rate)² where the textbook Drude function is "
           "ω_p²/(ω(ω + iΓ)) with Γ = ħ/τ, so Γ = 2·rate. GPAW's own "
           "documentation flags the same discrepancy. A τ set here and a rate "
           "quoted in a paper are therefore not interchangeable — the "
           "conversion is shown beside this box and written into the "
           "script."));
    tauRow->addWidget(drudeTauSpin_);
    drudeRateLabel_ = new QLabel(drudeTauRow_);
    tauRow->addWidget(drudeRateLabel_);
    tauRow->addStretch(1);
    form->addRow(tr("Relaxation time τ:"), drudeTauRow_);

    connect(drudeCheck_, &QCheckBox::toggled, this, [this] {
        updateDrudeRows();
        refreshPreview();
    });
    connect(drudeRateCombo_, &QComboBox::currentIndexChanged, this, [this] {
        updateDrudeRows();
        refreshPreview();
    });
    connect(drudeTauSpin_, &QDoubleSpinBox::valueChanged, this, [this] {
        updateDrudeRows();
        refreshPreview();
    });

    npointsSpin_ = new QSpinBox(page);
    npointsSpin_->setRange(2, 100000);
    npointsSpin_->setValue(500);
    npointsSpin_->setToolTip(
        tr("Number of samples on the photon-energy grid, spread linearly over "
           "the window above.\n\n"
           "This grid is handed to GPAW explicitly, which means the response "
           "is evaluated at exactly these frequencies — so the cost grows with "
           "the count rather than being amortized over an internal transform. "
           "Raise it to resolve narrow structure; a few hundred points is "
           "usually enough for an overview spectrum."));
    form->addRow(tr("Number of points:"), npointsSpin_);

    // Engine-independent: GPAW's fixed-density NSCF and VASP's LOPTICS
    // NBANDS both size their empty-state set from this.
    emptyBandsSpin_ = new QSpinBox(page);
    emptyBandsSpin_->setRange(25, 1000);
    emptyBandsSpin_->setSingleStep(25);
    emptyBandsSpin_->setValue(200);
    emptyBandsSpin_->setSuffix(tr(" %"));
    emptyBandsSpin_->setToolTip(
        tr("Additional empty bands as a percentage of the occupied bands: "
           "100 % allocates as many empty bands as occupied ones. The "
           "dielectric function sums interband transitions INTO these "
           "states, so this bounds the photon energy up to which the "
           "spectrum's high-energy tail is converged. A floor of 12 empty "
           "bands is always kept."));
    form->addRow(tr("Additional empty bands:"), emptyBandsSpin_);
    connect(emptyBandsSpin_, &QSpinBox::valueChanged, this,
            [this] { refreshPreview(); });

    // The three diagonal components εxx / εyy / εzz. Off-diagonal terms are not
    // requested here, so this covers the full response of an orthorhombic (or
    // higher-symmetry) cell; for isotropic systems the three coincide.
    auto* dirRow = new QHBoxLayout;
    dirXxCheck_ = new QCheckBox(tr("xx"), page);
    dirYyCheck_ = new QCheckBox(tr("yy"), page);
    dirZzCheck_ = new QCheckBox(tr("zz"), page);
    dirXxCheck_->setChecked(true);
    dirYyCheck_->setChecked(true);
    dirZzCheck_->setChecked(true);
    dirRow->addWidget(dirXxCheck_);
    dirRow->addWidget(dirYyCheck_);
    dirRow->addWidget(dirZzCheck_);
    dirRow->addStretch(1);
    form->addRow(tr("Polarization directions:"), dirRow);

    // Keep the script preview live as the user tunes the settings.
    connect(broadeningSpin_, &QDoubleSpinBox::valueChanged, this,
            [this] { refreshPreview(); });
    connect(omegaMinSpin_, &QDoubleSpinBox::valueChanged, this,
            [this] { refreshPreview(); });
    connect(omegaMaxSpin_, &QDoubleSpinBox::valueChanged, this,
            [this] { refreshPreview(); });
    connect(npointsSpin_, &QSpinBox::valueChanged, this,
            [this] { refreshPreview(); });
    connect(tetrahedronCheck_, &QCheckBox::toggled, this, [this] {
        onEngineChanged(); // the note appears/disappears with the choice
        refreshPreview();
    });
    connect(dirXxCheck_, &QCheckBox::toggled, this,
            [this] { refreshPreview(); });
    connect(dirYyCheck_, &QCheckBox::toggled, this,
            [this] { refreshPreview(); });
    connect(dirZzCheck_, &QCheckBox::toggled, this,
            [this] { refreshPreview(); });

    layout->addStretch(1);
    // Initial visibility only — the full onEngineChanged() also refreshes
    // the preview and syncs the base engine combo, neither of which exists
    // yet while this page is being built (it is constructed first).
    vaspGroup_->setVisible(false);
    return page;
}

double OpticsWizard::drudeRateEvForTauFs(double tauFs)
{
    // ħ = 0.6582119569 eV·fs. rate = ħ/(2τ) — see the τ tooltip for why the
    // 2 is there and why it must not be dropped when quoting the result.
    constexpr double kHbarEvFs = 0.6582119569;
    return kHbarEvFs / (2.0 * std::max(tauFs, 1e-6));
}

void OpticsWizard::updateDrudeRows()
{
    if (!drudeCheck_ || !drudeRateCombo_ || !drudeTauSpin_ || !responseForm_)
        return;
    const bool drude = drudeCheck_->isChecked();
    const bool fromTau = drudeRateCombo_->currentData().toInt() == 0;
    const bool vasp = selectedEngine() == core::CalculatorKind::Vasp;

    const auto setRowVisible = [this](QWidget* field, bool visible) {
        int row = -1;
        QFormLayout::ItemRole role{};
        responseForm_->getWidgetPosition(field, &row, &role);
        if (row >= 0)
            responseForm_->setRowVisible(row, visible);
    };
    // The whole group is a gpaw.response concept; VASP's LOPTICS has no
    // intraband switch, so it is hidden there rather than shown inert.
    setRowVisible(drudeCheck_, !vasp);
    setRowVisible(drudeRateCombo_, !vasp && drude);
    // Only meaningful once the rate is NOT tied to η — shown rather than
    // merely disabled, so the dialog does not carry a dead number that looks
    // like it is being used.
    setRowVisible(drudeTauRow_, !vasp && drude && fromTau);

    const double rate = drudeRateEvForTauFs(drudeTauSpin_->value());
    drudeRateLabel_->setText(
        tr("→ rate = %1 eV   (damping Γ = 2·rate = %2 eV)")
            .arg(rate, 0, 'f', 4)
            .arg(2.0 * rate, 0, 'f', 4));
}

void OpticsWizard::applyTetrahedronMeshConstraints()
{
    if (!tetrahedronCheck_ || !responseKptsSpin_[0])
        return;
    const bool tetra = tetrahedronCheck_->isChecked()
        && selectedEngine() == core::CalculatorKind::Gpaw;
    for (auto* spin : responseKptsSpin_) {
        spin->setSingleStep(tetra ? 2 : 1);
        // 0 is "auto" — inherit the baseline's divisions on that axis — and
        // is left alone: what it resolves to is only known once the run has
        // opened the .gpw, and the script grows it there.
        if (tetra && spin->value() > 0 && spin->value() % 2 != 0) {
            const QSignalBlocker block(spin);
            spin->setValue(spin->value() + 1);
        }
    }
}

bool OpticsWizard::calculatorAllowed(core::CalculatorKind kind) const
{
    return kind == core::CalculatorKind::Gpaw
        || kind == core::CalculatorKind::Vasp;
}

core::CalculatorKind OpticsWizard::selectedEngine() const
{
    return engineCombo_ ? static_cast<core::CalculatorKind>(
               engineCombo_->currentData().toInt())
                        : core::CalculatorKind::Gpaw;
}

void OpticsWizard::onEngineChanged()
{
    const core::CalculatorKind engine = selectedEngine();
    const bool vasp = engine == core::CalculatorKind::Vasp;
    // Keep the base class's (hidden) engine selection in step, so the launch
    // command template, the calculator provenance and the interpreter all
    // resolve for the engine actually chosen here.
    selectCalculator(engine);

    if (baselineGroup_)
        baselineGroup_->setVisible(!vasp);
    if (vaspGroup_)
        vaspGroup_->setVisible(vasp);

    if (responseForm_) {
        // The integration options are gpaw.response knobs; VASP's LOPTICS
        // has no tetrahedron/IBZ switch of this kind.
        const auto setRowVisible = [this](QWidget* field, bool visible) {
            int row = -1;
            QFormLayout::ItemRole role{};
            responseForm_->getWidgetPosition(field, &row, &role);
            if (row >= 0)
                responseForm_->setRowVisible(row, visible);
        };
        setRowVisible(ibzCheck_, !vasp);
        setRowVisible(tetrahedronCheck_, !vasp);
        // Shared spins, engine-specific vocabulary: the broadening is GPAW's
        // η and VASP's CSHIFT; the sample count is an explicit grid for GPAW
        // and the NEDOS tag for VASP.
        if (auto* label = qobject_cast<QLabel*>(
                responseForm_->labelForField(broadeningSpin_)))
            label->setText(vasp ? tr("Broadening (CSHIFT):")
                                : tr("Broadening η:"));
        if (auto* label = qobject_cast<QLabel*>(
                responseForm_->labelForField(npointsSpin_)))
            label->setText(vasp ? tr("Frequency grid (NEDOS):")
                                : tr("Number of points:"));
    }
    applyTetrahedronMeshConstraints();
    updateDrudeRows();
    refreshPreview();
}


void OpticsWizard::setDensityBaselines(
    const QList<QPair<QString, QString>>& baselines)
{
    if (!baselineCombo_)
        return;
    baselineCombo_->clear();
    for (const auto& [label, path] : baselines)
        baselineCombo_->addItem(label, path);
    // Without a .gpw the GPAW path has nothing to inherit — steer to VASP
    // (self-contained) instead of refusing to open, and say why.
    if (baselines.isEmpty() && engineCombo_) {
        const int gpawIndex = engineCombo_->findData(
            static_cast<int>(core::CalculatorKind::Gpaw));
        if (auto* model =
                qobject_cast<QStandardItemModel*>(engineCombo_->model());
            model && gpawIndex >= 0) {
            model->item(gpawIndex)->setEnabled(false);
            engineCombo_->setItemData(
                gpawIndex,
                tr("Needs a completed GPAW Single-Point Calculation that "
                   "saved its wavefunctions (.gpw) — run one first."),
                Qt::ToolTipRole);
        }
        engineCombo_->setCurrentIndex(engineCombo_->findData(
            static_cast<int>(core::CalculatorKind::Vasp)));
    }
    onBaselineChanged();
}

void OpticsWizard::onBaselineChanged()
{
    inherited_ = applyBaselineProvenance(baselineCombo_, inheritanceNote_);
    refreshPreview();
}

QString OpticsWizard::pythonExecutable() const
{
    // The baseline's interpreter only binds the GPAW path — a VASP run has
    // no baseline and resolves through the standard per-engine mapping.
    if (selectedEngine() == core::CalculatorKind::Gpaw && inherited_
        && !inherited_->pythonExecutable.isEmpty())
        return inherited_->pythonExecutable;
    return SimulationWizardBase::pythonExecutable();
}

QString OpticsWizard::generateScript() const
{
    core::OpticsConfig cfg;
    if (baselineCombo_)
        cfg.baselineDensityPath =
            baselineCombo_->currentData().toString().toStdString();
    if (twoDimensional_ && vacuumAxisCombo_)
        cfg.vacuumAxis = vacuumAxisCombo_->currentData().toInt();
    cfg.calculator = baseCalculatorConfig();
    cfg.calculator.calculator = selectedEngine();
    if (selectedEngine() == core::CalculatorKind::Vasp) {
        // The VASP run is self-contained, so its ground-state knobs come
        // from the stage-1 group rather than an inherited .gpw.
        cfg.calculator.task = core::TaskKind::SinglePoint;
        cfg.calculator.planeWaveCutoffEv = vaspEncutSpin_->value();
        for (int axis = 0; axis < 3; ++axis)
            cfg.calculator.kpts[axis] = vaspKptSpins_[axis]->value();
        cfg.calculator.vaspXc =
            vaspXcCombo_->currentText().toStdString();
        // The restart step reads the density and wavefunctions from disk.
        cfg.calculator.vaspLcharg = true;
        cfg.calculator.vaspLwave = true;
    }
    cfg.emptyBandsPercent = emptyBandsSpin_->value();
    cfg.broadeningEv = broadeningSpin_->value();
    cfg.omegaMinEv = omegaMinSpin_->value();
    cfg.omegaMaxEv = omegaMaxSpin_->value();
    cfg.npoints = npointsSpin_->value();
    cfg.tetrahedronIntegration = tetrahedronCheck_->isChecked();
    for (int i = 0; i < 3; ++i)
        cfg.responseKpts[i] = responseKptsSpin_[i]->value();
    cfg.includeIbzPoints = ibzCheck_->isChecked();
    cfg.intrabandDrude = drudeCheck_->isChecked();
    cfg.drudeRateFromBroadening = drudeRateCombo_->currentData().toInt() == 1;
    cfg.drudeRelaxationTimeFs = drudeTauSpin_->value();
    cfg.dirX = dirXxCheck_->isChecked();
    cfg.dirY = dirYyCheck_->isChecked();
    cfg.dirZ = dirZzCheck_->isChecked();
    return QString::fromStdString(core::generateOpticsScript(cfg));
}

} // namespace calango::gui
