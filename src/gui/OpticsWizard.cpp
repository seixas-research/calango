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
           "Single-Point Calculation — its SCF is inherited, never re-run. "
           "Re-converging here would give a spectrum from a different ground "
           "state than the one you validated."),
        baselineGroup);
    baselineNote->setWordWrap(true);
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
           "density for the LOPTICS step. POTCARs come from Preferences → "
           "External Files (VASP_PP_PATH)."),
        vaspGroup_);
    vaspNote->setWordWrap(true);
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
            tr("A supercell dielectric function is diluted by whatever vacuum "
               "was used — double the vacuum and ε₃D moves, so it is not a "
               "property of the sheet. Dividing that thickness back out gives "
               "α₂D, σ₂D and the absorbance, which are."),
            sheetGroup);
        sheetNote->setWordWrap(true);
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
        tr("Compute the frequency-dependent dielectric function ε(ω) and the "
           "derived optical spectra (absorption, reflectivity, refractive "
           "index and energy loss) from GPAW's linear-response module. The "
           "ground-state cutoff and k-grid come from the baseline above — a "
           "spectrum is only as converged as the SCF it is built on, so a "
           "coarse baseline k-grid needs a denser one, not a finer η here."),
        page);
    intro->setWordWrap(true);
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
        tr("Lorentzian broadening η applied to the dielectric function. "
           "Smaller values resolve sharp features but need a denser k-mesh."));
    form->addRow(tr("Broadening η:"), broadeningSpin_);

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
               "converged the SCF is routinely too coarse for the spectrum."));
        connect(responseKptsSpin_[i], &QSpinBox::valueChanged, this,
                [this] { refreshPreview(); });
        meshRow->addWidget(responseKptsSpin_[i]);
    }
    meshRow->addStretch(1);
    form->addRow(tr("Response k-mesh:"), meshRow);

    ibzCheck_ = new QCheckBox(tr("Include IBZ points"), page);
    ibzCheck_->setToolTip(
        tr("Reduce the response mesh to the irreducible Brillouin zone, "
           "weighting each point by its symmetry degeneracy, instead of "
           "sampling the full zone.\n"
           "Measured on bulk Si at 6×6×6: 28 irreducible points against 216 "
           "in the full zone, with ε₂ agreeing to 0.6 % — the same spectrum "
           "for a fraction of the work.\n"
           "The weights are GPAW's own, derived from the cell's symmetry."));
    form->addRow(QString(), ibzCheck_);
    connect(ibzCheck_, &QCheckBox::toggled, this,
            [this] { refreshPreview(); });

    tetrahedronCheck_ =
        new QCheckBox(tr("Tetrahedron integration (Brillouin zone)"), page);
    tetrahedronCheck_->setToolTip(
        tr("Integrate the response by linear tetrahedron interpolation instead "
           "of summing over k-points.\n"
           "Point integration gives every transition a Lorentzian of width η, "
           "so any feature narrower than η — a van Hove singularity, a 2D "
           "absorption edge — is smeared rather than resolved. Tetrahedron "
           "integration resolves those on a mesh where point integration is "
           "still noisy.\n\n"
           "Requires the BASELINE's k-grid to contain every vertex of the "
           "irreducible Brillouin zone (gpaw.bztools."
           "find_high_symmetry_monkhorst_pack). An ordinary Monkhorst-Pack "
           "grid usually does not, and the run will stop with that message "
           "rather than quietly switch back to point integration."));
    form->addRow(QString(), tetrahedronCheck_);

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
    connect(tetrahedronCheck_, &QCheckBox::toggled, this,
            [this] { refreshPreview(); });
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
    const QString gpw =
        baselineCombo_ ? baselineCombo_->currentData().toString() : QString();
    // The combo holds the .gpw; the provenance sidecar sits in its job dir.
    const QString dir =
        gpw.isEmpty() ? QString() : QFileInfo(gpw).absolutePath();
    inherited_ = dir.isEmpty() ? std::nullopt : readCalculatorProvenance(dir);

    if (inheritanceNote_) {
        if (inherited_) {
            QString note = tr("Inherited: %1")
                               .arg(inherited_->summary().toHtmlEscaped());
            if (!inherited_->condaEnv.isEmpty())
                note += tr(" — env <code>%1</code>")
                            .arg(inherited_->condaEnv.toHtmlEscaped());
            inheritanceNote_->setText(note);
        } else if (gpw.isEmpty()) {
            inheritanceNote_->clear();
        } else {
            inheritanceNote_->setText(
                tr("This baseline carries no <code>calculator.json</code>, so "
                   "its parameters cannot be shown. GPAW still restores them "
                   "from the <code>.gpw</code> at run time."));
        }
    }
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
    cfg.dirX = dirXxCheck_->isChecked();
    cfg.dirY = dirYyCheck_->isChecked();
    cfg.dirZ = dirZzCheck_->isChecked();
    return QString::fromStdString(core::generateOpticsScript(cfg));
}

} // namespace calango::gui
