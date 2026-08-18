#include "gui/CondaEnvs.hpp"
#include "gui/SettingsManager.hpp"
#include "gui/NebDialog.hpp"

#include <QCheckBox>

#include "core/AseScriptGenerator.hpp"
#include "gui/GuiUtils.hpp"
#include "gui/HubbardParametersDialog.hpp"
#include "gui/SimulationWizardBase.hpp"
#include "python_bridge/AseBridge.hpp"
#include "python_bridge/NebBuilder.hpp"
#include "python_bridge/PythonEngine.hpp"

#include <QApplication>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QDoubleValidator>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QSettings>
#include <QLocale>
#include <QSpinBox>
#include <QVBoxLayout>

#include <sstream>

namespace calango::gui {

namespace {


/// Indent every line of a code block by four spaces so it can be spliced into
/// a Python function body (used to attach a calculator per NEB image).
std::string indent(const std::string& code)
{
    std::ostringstream out;
    std::istringstream in(code);
    std::string line;
    while (std::getline(in, line))
        out << "    " << line << "\n";
    return out.str();
}

} // namespace

NebDialog::NebDialog(std::vector<NamedStructure> openDocs, QWidget* parent)
    : QDialog(parent), openDocs_(std::move(openDocs))
{
    setWindowTitle(tr("Nudged Elastic Band (NEB)"));
    setAttribute(Qt::WA_DeleteOnClose);
    resize(600, 720);

    // The settings stack outgrew a fixed dialog once the GPAW group joined it,
    // so it scrolls — with Run / Close pinned outside the scroll area, which is
    // the one pair of buttons that must never be the thing you have to scroll
    // to find.
    auto* rootLayout = new QVBoxLayout(this);
    auto* scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    auto* content = new QWidget(scroll);
    scroll->setWidget(content);
    rootLayout->addWidget(scroll, 1);
    auto* layout = new QVBoxLayout(content);
    layout->setContentsMargins(0, 0, 0, 0);

    // --- Endpoints ---------------------------------------------------------
    auto* endpointsBox = new QGroupBox(tr("Reaction endpoints"), this);
    auto* endForm = new QFormLayout(endpointsBox);
    auto* initialRow = new QHBoxLayout;
    initialCombo_ = new QComboBox(endpointsBox);
    auto* initialBrowse = new QPushButton(tr("File…"), endpointsBox);
    initialRow->addWidget(initialCombo_, 1);
    initialRow->addWidget(initialBrowse);
    endForm->addRow(tr("Initial (reactant):"), initialRow);
    auto* finalRow = new QHBoxLayout;
    finalCombo_ = new QComboBox(endpointsBox);
    auto* finalBrowse = new QPushButton(tr("File…"), endpointsBox);
    finalRow->addWidget(finalCombo_, 1);
    finalRow->addWidget(finalBrowse);
    endForm->addRow(tr("Final (product):"), finalRow);
    layout->addWidget(endpointsBox);
    connect(initialBrowse, &QPushButton::clicked, this, &NebDialog::browseInitial);
    connect(finalBrowse, &QPushButton::clicked, this, &NebDialog::browseFinal);
    repopulateEndpointCombos(0, openDocs_.size() > 1 ? 1 : 0);

    // --- Interpolation -----------------------------------------------------
    auto* interpBox = new QGroupBox(tr("Interpolation"), this);
    auto* interpForm = new QFormLayout(interpBox);
    imagesSpin_ = new QSpinBox(interpBox);
    imagesSpin_->setRange(1, 50);
    imagesSpin_->setValue(5);
    interpForm->addRow(tr("Intermediate images:"), imagesSpin_);
    methodCombo_ = new QComboBox(interpBox);
    methodCombo_->addItem(tr("Linear"), QStringLiteral("linear"));
    methodCombo_->addItem(tr("IDPP (improved tangent)"), QStringLiteral("idpp"));
    interpForm->addRow(tr("Method:"), methodCombo_);
    auto* previewButton = new QPushButton(tr("Preview Interpolation →"), interpBox);
    interpForm->addRow(previewButton);
    layout->addWidget(interpBox);
    connect(previewButton, &QPushButton::clicked, this, &NebDialog::doPreview);

    // --- Solver ------------------------------------------------------------
    auto* solverBox = new QGroupBox(tr("Solver"), this);
    auto* solverForm = new QFormLayout(solverBox);
    variantCombo_ = new QComboBox(solverBox);
    variantCombo_->addItems({tr("Standard NEB"),
                             tr("Climbing-Image NEB (CI-NEB)"),
                             tr("AutoNEB")});
    variantCombo_->setCurrentIndex(1);
    solverForm->addRow(tr("Variant:"), variantCombo_);
    springSpin_ = new QDoubleSpinBox(solverBox);
    springSpin_->setRange(0.001, 100.0);
    springSpin_->setDecimals(3);
    springSpin_->setValue(0.1);
    springSpin_->setSuffix(tr(" eV/Å²"));
    solverForm->addRow(tr("Spring constant k:"), springSpin_);
    fmax_.build(solverForm, solverBox, tr("Force convergence (fmax):"));
    maxStepsSpin_ = new QSpinBox(solverBox);
    maxStepsSpin_->setRange(1, 100000);
    maxStepsSpin_->setValue(200);
    solverForm->addRow(tr("Max optimizer steps:"), maxStepsSpin_);
    optimizerCombo_ = new QComboBox(solverBox);
    optimizerCombo_->addItems({QStringLiteral("FIRE"), QStringLiteral("BFGS"),
                               QStringLiteral("LBFGS"),
                               QStringLiteral("QuasiNewton")});
    solverForm->addRow(tr("Optimizer:"), optimizerCombo_);
    layout->addWidget(solverBox);

    // --- Calculator --------------------------------------------------------
    auto* calcBox = new QGroupBox(tr("Calculator"), this);
    auto* calcForm = new QFormLayout(calcBox);
    calcCombo_ = new QComboBox(calcBox);
    // Grouped the same way the shared wizard combo is (see
    // SimulationWizardBase::buildCalculatorPage): ab-initio first, then the
    // machine-learning potentials, then the classical ones. The list itself is
    // shorter — this dialog only builds the engines whose settings it has rows
    // for — but the ORDER has to match, or the same engines appear in two
    // different places in two dropdowns that look alike.
    const auto addCalc = [this](const QString& label, core::CalculatorKind kind) {
        calcCombo_->addItem(label, static_cast<int>(kind));
    };
    addCalc(tr("GPAW (DFT)"), core::CalculatorKind::Gpaw);
    addCalc(tr("Quantum ESPRESSO (DFT)"),
            core::CalculatorKind::QuantumEspresso);
    calcCombo_->insertSeparator(calcCombo_->count());
    // Machine-learning potentials: the natural fit for NEB, where a band of
    // images needs many force evaluations at near-DFT accuracy.
    addCalc(tr("MACE (ML potential)"), core::CalculatorKind::Mace);
    addCalc(tr("CHGNet (universal ML potential)"),
            core::CalculatorKind::ChgNet);
    addCalc(tr("MatterSim (universal ML potential)"),
            core::CalculatorKind::MatterSim);
    addCalc(tr("FAIRChem / OCP (ML potential)"),
            core::CalculatorKind::FairChem);
    addCalc(tr("NequIP (ML potential)"), core::CalculatorKind::NequIp);
    addCalc(tr("Allegro (ML potential)"), core::CalculatorKind::Allegro);
    addCalc(tr("DeepMD-kit (ML potential)"), core::CalculatorKind::DeepMd);
    calcCombo_->insertSeparator(calcCombo_->count());
    addCalc(tr("EMT (fast test potential)"), core::CalculatorKind::EMT);
    addCalc(tr("Lennard-Jones"), core::CalculatorKind::LennardJones);
    calcForm->addRow(tr("Calculator:"), calcCombo_);

    // One model-file row shared by the file-backed ML potentials (DeepMD,
    // NequIP / Allegro, FAIRChem); CHGNet and MatterSim carry their own
    // pretrained weights and leave it disabled.
    mlipModelEdit_ = new QLineEdit(calcBox);
    mlipModelEdit_->setPlaceholderText(
        tr("path to the model / checkpoint (.pb, .pth, .pt)"));
    auto* mlipBrowse = new QPushButton(tr("Browse…"), calcBox);
    auto* mlipRow = new QWidget(calcBox);
    auto* mlipLayout = new QHBoxLayout(mlipRow);
    mlipLayout->setContentsMargins(0, 0, 0, 0);
    mlipLayout->addWidget(mlipModelEdit_, 1);
    mlipLayout->addWidget(mlipBrowse);
    calcForm->addRow(tr("ML model file:"), mlipRow);
    connect(mlipBrowse, &QPushButton::clicked, this, [this] {
        const QString path = QFileDialog::getOpenFileName(
            this, tr("Select Model File"),
            SettingsManager::mlPotentialsStartPath(mlipModelEdit_->text()),
            tr("Model files (*.pb *.pt *.pth *.model);;All files (*)"));
        if (!path.isEmpty())
            mlipModelEdit_->setText(path);
    });

    mlipDeviceCombo_ = new QComboBox(calcBox);
    // Order matches core::MlipDevice.
    mlipDeviceCombo_->addItems({QStringLiteral("cpu"), QStringLiteral("cuda"),
                                QStringLiteral("mps")});
    calcForm->addRow(tr("ML device:"), mlipDeviceCombo_);
    maceSizeCombo_ = new QComboBox(calcBox);
    maceSizeCombo_->addItems({QStringLiteral("small"), QStringLiteral("medium"),
                              QStringLiteral("large")});
    maceSizeCombo_->setCurrentIndex(1);
    calcForm->addRow(tr("MACE size:"), maceSizeCombo_);
    maceDeviceCombo_ = new QComboBox(calcBox);
    maceDeviceCombo_->addItems({QStringLiteral("cpu"), QStringLiteral("cuda"),
                                QStringLiteral("mps")});
    calcForm->addRow(tr("MACE device:"), maceDeviceCombo_);

    // Dispersion. A barrier is an energy DIFFERENCE along a path, so a missing
    // long-range attraction does not cancel out of it — for a molecule moving
    // across a surface it is often the dominant correction to the barrier.
    dispersionD4Check_ =
        new QCheckBox(tr("van der Waals Correction (DFTD4)"), calcBox);
    dispersionD4Check_->setToolTip(
        tr("Couple the calculator with DFTD4 through ASE's SumCalculator, "
           "adding Grimme's D4 dispersion energy and forces. Needs the dftd4 "
           "package in the job environment."));
    // No connect: this dialog builds its script when the run is launched
    // rather than previewing it live, so the checkbox is simply read then.
    calcForm->addRow(dispersionD4Check_);
    cutoffSpin_ = new QDoubleSpinBox(calcBox);
    cutoffSpin_->setRange(100.0, 2000.0);
    cutoffSpin_->setValue(500.0);
    cutoffSpin_->setSuffix(tr(" eV"));
    calcForm->addRow(tr("Plane-wave cutoff:"), cutoffSpin_);
    auto* kptRow = new QHBoxLayout;
    for (auto*& spin : kptSpins_) {
        spin = new QSpinBox(calcBox);
        spin->setRange(1, 32);
        spin->setValue(7);
        kptRow->addWidget(spin);
    }
    calcForm->addRow(tr("k-point grid:"), kptRow);
    layout->addWidget(calcBox);

    // The GPAW electronic-structure settings, in the same arrangement the
    // simulation wizards use. A NEB runs an SCF per image per optimizer step,
    // so smearing, the eigensolver and the convergence targets matter here at
    // least as much as anywhere else — they were simply absent, which left the
    // band relaxing on GPAW's bare defaults.
    gpawGroup_ = buildGpawGroup();
    layout->addWidget(gpawGroup_);

    connect(calcCombo_, &QComboBox::currentIndexChanged, this,
            &NebDialog::updateCalculatorEnabled);

    // --- Environment -------------------------------------------------------
    auto* envRow = new QHBoxLayout;
    envRow->addWidget(new QLabel(tr("Environment:"), this));
    envEdit_ = new QLineEdit(this);
    envEdit_->setPlaceholderText(tr("conda env / python (empty = embedded)"));
    envEdit_->setText(QSettings().value(SettingsManager::kEnvironmentPath).toString());
    auto* envButton = new QPushButton(tr("Browse…"), this);
    envRow->addWidget(envEdit_, 1);
    envRow->addWidget(envButton);
    layout->addLayout(envRow);
    envStatus_ = new QLabel(this);
    envStatus_->setWordWrap(true);
    layout->addWidget(envStatus_);
    connect(envButton, &QPushButton::clicked, this, &NebDialog::browseEnvironment);
    connect(envEdit_, &QLineEdit::textChanged, this, [this] {
        QSettings().setValue(SettingsManager::kEnvironmentPath, envEdit_->text());
    });

    statusLabel_ = new QLabel(this);
    statusLabel_->setWordWrap(true);
    layout->addWidget(statusLabel_);

    layout->addStretch(1);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    auto* runButton = buttons->addButton(tr("Run NEB"), QDialogButtonBox::AcceptRole);
    runButton->setDefault(true);
    rootLayout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::close);
    disconnect(buttons, &QDialogButtonBox::accepted, nullptr, nullptr);
    connect(runButton, &QPushButton::clicked, this, &NebDialog::doRun);

    updateCalculatorEnabled();
}

void NebDialog::repopulateEndpointCombos(int initialSel, int finalSel)
{
    for (QComboBox* combo : {initialCombo_, finalCombo_}) {
        combo->clear();
        for (std::size_t i = 0; i < openDocs_.size(); ++i)
            combo->addItem(openDocs_[i].name, static_cast<int>(i));
    }
    if (initialSel >= 0 && initialSel < initialCombo_->count())
        initialCombo_->setCurrentIndex(initialSel);
    if (finalSel >= 0 && finalSel < finalCombo_->count())
        finalCombo_->setCurrentIndex(finalSel);
}

std::shared_ptr<const core::Structure> NebDialog::endpoint(QComboBox* combo) const
{
    const int i = combo->currentData().toInt();
    if (i < 0 || i >= static_cast<int>(openDocs_.size()))
        return nullptr;
    return openDocs_[static_cast<std::size_t>(i)].structure;
}

void NebDialog::browseInitial()
{
    const QString path = QFileDialog::getOpenFileName(
        this, tr("Open Initial State"), QString(), structureOpenFilters());
    if (path.isEmpty())
        return;
    try {
        auto s = std::make_shared<core::Structure>(
            pybridge::AseBridge::readStructure(path.toStdString()));
        openDocs_.push_back({QFileInfo(path).fileName(), s});
        repopulateEndpointCombos(static_cast<int>(openDocs_.size()) - 1,
                                 finalCombo_->currentData().toInt());
    } catch (const std::exception& e) {
        statusLabel_->setText(QString::fromUtf8(e.what()));
    }
}

void NebDialog::browseFinal()
{
    const QString path = QFileDialog::getOpenFileName(
        this, tr("Open Final State"), QString(), structureOpenFilters());
    if (path.isEmpty())
        return;
    try {
        auto s = std::make_shared<core::Structure>(
            pybridge::AseBridge::readStructure(path.toStdString()));
        openDocs_.push_back({QFileInfo(path).fileName(), s});
        repopulateEndpointCombos(initialCombo_->currentData().toInt(),
                                 static_cast<int>(openDocs_.size()) - 1);
    } catch (const std::exception& e) {
        statusLabel_->setText(QString::fromUtf8(e.what()));
    }
}

void NebDialog::browseEnvironment()
{
    const QString dir = QFileDialog::getExistingDirectory(
        this, tr("Select Conda Environment Folder"));
    if (!dir.isEmpty())
        envEdit_->setText(dir);
}

void NebDialog::updateCalculatorEnabled()
{
    const auto kind =
        static_cast<core::CalculatorKind>(calcCombo_->currentData().toInt());
    const bool isMace = kind == core::CalculatorKind::Mace;
    const bool isDft = kind == core::CalculatorKind::Gpaw
        || kind == core::CalculatorKind::QuantumEspresso;
    maceSizeCombo_->setEnabled(isMace);
    maceDeviceCombo_->setEnabled(isMace);
    // CHGNet / MatterSim ship their own weights, so only the file-backed ML
    // potentials enable the model-path row.
    const bool needsModelFile = kind == core::CalculatorKind::DeepMd
        || kind == core::CalculatorKind::NequIp
        || kind == core::CalculatorKind::Allegro
        || kind == core::CalculatorKind::FairChem;
    mlipModelEdit_->setEnabled(needsModelFile);
    mlipDeviceCombo_->setEnabled(core::isMlipCalculator(kind) && !isMace);
    cutoffSpin_->setEnabled(isDft);
    for (auto* s : kptSpins_)
        s->setEnabled(isDft);
    // Hidden rather than greyed out for the non-GPAW engines: none of these
    // map onto EMT or a MACE model, and a dozen dead rows would be pure noise
    // on a dialog that already scrolls.
    if (gpawGroup_)
        gpawGroup_->setVisible(kind == core::CalculatorKind::Gpaw);
}

QGroupBox* NebDialog::buildGpawGroup()
{
    auto* group = new QGroupBox(tr("Calculator && Convergence Settings"), this);
    auto* form = new QFormLayout(group);

    gpawXcCombo_ = new QComboBox(group);
    gpawXcCombo_->setEditable(true);
    gpawXcCombo_->addItems({QStringLiteral("PBE"), QStringLiteral("LDA"),
                            QStringLiteral("revPBE"), QStringLiteral("RPBE"),
                            QStringLiteral("PBEsol"), QStringLiteral("HSE06"),
                            QStringLiteral("B3LYP"), QStringLiteral("SCAN"),
                            QStringLiteral("r2SCAN")});
    gpawXcCombo_->setToolTip(
        tr("The hybrids (HSE06, B3LYP) and meta-GGAs (SCAN, r2SCAN) need a "
           "GPAW build with libxc, and are far more expensive than the GGAs — "
           "which a band multiplies by the image count."));
    hubbardButton_ = new QPushButton(tr("Hubbard parameters…"), group);
    hubbardButton_->setToolTip(
        tr("Add an on-site Coulomb repulsion U to a named orbital shell "
           "(GPAW setups={…}). For narrow d/f bands that a semilocal "
           "functional over-delocalizes."));
    connect(hubbardButton_, &QPushButton::clicked, this,
            &NebDialog::editHubbardParameters);
    // DFT+U is a correction TO the functional, so it sits on the functional's
    // own row rather than among the convergence settings below.
    auto* xcRow = new QWidget(group);
    auto* xcLayout = new QHBoxLayout(xcRow);
    xcLayout->setContentsMargins(0, 0, 0, 0);
    xcLayout->addWidget(gpawXcCombo_, 1);
    xcLayout->addWidget(hubbardButton_);
    form->addRow(tr("XC functional:"), xcRow);

    // Smearing method + width, and the SCF tolerance / step cap, from the same
    // shared rows the wizards use.
    electronic_.buildConvergenceRows(form, this);

    gpawEigensolverCombo_ = new QComboBox(group);
    // Same list, same order and the same itemData mapping as the wizards': a
    // second spelling of the solver names here is how the two drift apart.
    gpawEigensolverCombo_->addItem(QStringLiteral("Davidson"),
                                   static_cast<int>(core::GpawEigensolver::Davidson));
    gpawEigensolverCombo_->addItem(QStringLiteral("RMM-DIIS"),
                                   static_cast<int>(core::GpawEigensolver::RmmDiis));
    gpawEigensolverCombo_->addItem(
        QStringLiteral("CG"),
        static_cast<int>(core::GpawEigensolver::ConjugateGradient));
    gpawEigensolverCombo_->addItem(QStringLiteral("Direct"),
                                   static_cast<int>(core::GpawEigensolver::Direct));
    gpawEigensolverCombo_->setToolTip(
        tr("davidson: robust general default.\n"
           "cg: slower but very stable — try it when the SCF oscillates.\n"
           "rmm-diis: cheapest per step for large metallic systems.\n"
           "direct: exact diagonalization (LCAO / small systems)."));
    // The solver and the cap on its iterations are one thought: "how the SCF
    // is solved, and how long it may try".
    if (QWidget* steps = electronic_.scfStepsWidget()) {
        auto* solverRow = new QWidget(group);
        auto* solverLayout = new QHBoxLayout(solverRow);
        solverLayout->setContentsMargins(0, 0, 0, 0);
        solverLayout->addWidget(gpawEigensolverCombo_, 1);
        solverLayout->addWidget(new QLabel(tr("max SCF steps"), solverRow));
        solverLayout->addWidget(steps);
        form->addRow(tr("Eigensolver:"), solverRow);
    } else {
        form->addRow(tr("Eigensolver:"), gpawEigensolverCombo_);
    }

    const auto toleranceEdit = [](QWidget* parent, double initial,
                                  double minimum, double maximum,
                                  const QString& tip) {
        auto* edit = new QLineEdit(QString::number(initial, 'g', 6), parent);
        auto* validator = new QDoubleValidator(minimum, maximum, 12, edit);
        validator->setNotation(QDoubleValidator::ScientificNotation);
        validator->setLocale(QLocale::c());
        edit->setValidator(validator);
        edit->setToolTip(tip);
        return edit;
    };
    // The three SCF thresholds are converged together and read as a group, so
    // they share one row — energy first, because it is the one a user sets and
    // the other two support.
    auto* tolRowWidget = new QWidget(group);
    auto* tolRow = new QHBoxLayout(tolRowWidget);
    tolRow->setContentsMargins(0, 0, 0, 0);
    if (QWidget* energy = electronic_.energyToleranceWidget()) {
        tolRow->addWidget(new QLabel(tr("energy"), tolRowWidget));
        tolRow->addWidget(energy, 1);
    }
    gpawEigenTolEdit_ = toleranceEdit(
        tolRowWidget, 4e-8, 1e-12, 1e-2,
        tr("GPAW convergence['eigenstates'] — integrated eigenstate residual, "
           "in eV² per valence electron (e.g. 4e-8)."));
    gpawDensityTolEdit_ = toleranceEdit(
        tolRowWidget, 1e-4, 1e-9, 1e-1,
        tr("GPAW convergence['density'] — change in the density integrated "
           "over the cell, in electrons per valence electron (e.g. 1e-4)."));
    tolRow->addWidget(new QLabel(tr("eigenstates"), tolRowWidget));
    tolRow->addWidget(gpawEigenTolEdit_, 1);
    tolRow->addWidget(new QLabel(tr("density"), tolRowWidget));
    tolRow->addWidget(gpawDensityTolEdit_, 1);
    form->addRow(tr("Convergence tolerances:"), tolRowWidget);

    // Spin polarization and the initial moments, same shared rows again.
    electronic_.buildSpinRows(form, this);

    // Deliberately no "Density Exports" group. That block writes a .cube after
    // an SCF converges; a band converges one SCF per image per optimizer step,
    // so there is no single density to export and asking for one would produce
    // either hundreds of files or an arbitrary pick. Run the converged saddle
    // point through Single-point Calculation when you want its density.
    return group;
}

void NebDialog::editHubbardParameters()
{
    // Seed the element completer from the endpoints, so a U cannot be set on a
    // species the band does not contain. Both ends, because an NEB is allowed
    // to relax a composition-preserving rearrangement in which one endpoint
    // happens to be listed first.
    QStringList elements;
    for (QComboBox* combo : {initialCombo_, finalCombo_})
        elements << structureElements(endpoint(combo).get());
    elements.removeDuplicates();
    elements.sort();

    HubbardParametersDialog dialog(hubbardEnabled_, hubbardParameters_,
                                   elements, this);
    if (dialog.exec() != QDialog::Accepted)
        return;
    hubbardEnabled_ = dialog.isEnabled();
    hubbardParameters_ = dialog.parameters();
}

bool NebDialog::computeBand()
{
    auto initial = endpoint(initialCombo_);
    auto final = endpoint(finalCombo_);
    if (!initial || !final) {
        statusLabel_->setText(tr("Select both endpoints."));
        return false;
    }
    if (initial->size() != final->size()) {
        statusLabel_->setText(
            tr("Endpoints must have the same number of atoms (%1 vs %2).")
                .arg(initial->size())
                .arg(final->size()));
        return false;
    }
    QApplication::setOverrideCursor(Qt::WaitCursor);
    try {
        auto band = pybridge::NebBuilder::interpolate(
            *initial, *final, imagesSpin_->value(),
            methodCombo_->currentData().toString().toStdString());
        band_.clear();
        for (auto& img : band)
            band_.push_back(std::make_shared<core::Structure>(std::move(img)));
        QApplication::restoreOverrideCursor();
        return true;
    } catch (const std::exception& e) {
        QApplication::restoreOverrideCursor();
        statusLabel_->setText(QString::fromUtf8(e.what()));
        return false;
    }
}

void NebDialog::doPreview()
{
    if (!computeBand())
        return;
    statusLabel_->setText(tr("Interpolated %1 images — scrub the preview tab.")
                              .arg(band_.size()));
    Q_EMIT previewRequested(band_);
}

core::CalculatorConfig NebDialog::calculatorConfig() const
{
    core::CalculatorConfig c;
    c.calculator =
        static_cast<core::CalculatorKind>(calcCombo_->currentData().toInt());
    c.maceSize = maceSizeCombo_->currentText().toStdString();
    c.maceDevice = maceDeviceCombo_->currentText().toStdString();
    c.mlipDevice =
        static_cast<core::MlipDevice>(mlipDeviceCombo_->currentIndex());
    c.dispersionD4 = dispersionD4Check_ && dispersionD4Check_->isChecked();
    const std::string modelPath = mlipModelEdit_->text().trimmed().toStdString();
    switch (c.calculator) {
    case core::CalculatorKind::DeepMd: c.deepmdModelPath = modelPath; break;
    case core::CalculatorKind::NequIp:
    case core::CalculatorKind::Allegro: c.nequipModelPath = modelPath; break;
    case core::CalculatorKind::FairChem:
        c.fairChemCheckpointPath = modelPath;
        break;
    default: break;
    }
    c.planeWaveCutoffEv = cutoffSpin_->value();
    for (int i = 0; i < 3; ++i)
        c.kpts[i] = kptSpins_[i]->value();

    // VASP's POTCAR directory is an installation-wide setting shared with the
    // wizards, so a band relaxation picks it up without asking again. The rest
    // of the INCAR tags keep their defaults here: NEB drives the relaxation
    // from ASE, so VASP only ever runs single points inside it.
    c.vaspPotcarPath =
        SimulationWizardBase::vaspPotcarDirectory().trimmed().toStdString();

    if (c.calculator == core::CalculatorKind::Gpaw) {
        c.gpawXc = gpawXcCombo_->currentText().trimmed().toStdString();
        c.gpawEigensolver = static_cast<core::GpawEigensolver>(
            gpawEigensolverCombo_->currentData().toInt());
        bool ok = false;
        const double eigen =
            QLocale::c().toDouble(gpawEigenTolEdit_->text(), &ok);
        if (ok && eigen > 0.0)
            c.gpawConvEigenstates = eigen;
        const double density =
            QLocale::c().toDouble(gpawDensityTolEdit_->text(), &ok);
        if (ok && density > 0.0)
            c.gpawConvDensity = density;
        c.useHubbardU = hubbardEnabled_ && !hubbardParameters_.empty();
        c.hubbardU = hubbardParameters_;
        // Smearing, SCF tolerance / steps and spin, from the shared rows.
        electronic_.applyTo(c);
    }
    return c;
}

QString NebDialog::buildNebScript() const
{
    const std::string calcAttach =
        indent(core::AseScriptGenerator::calculatorSnippet(calculatorConfig()));
    const std::string optimizer = optimizerCombo_->currentText().toStdString();
    const int variant = variantCombo_->currentIndex(); // 0 std, 1 CI, 2 AutoNEB
    const bool climb = variant >= 1;

    std::ostringstream out;
    out << "#!/usr/bin/env python3\n"
           "# Nudged Elastic Band (generated by Calango).\n"
           "import sys\n"
           "from ase.io import read, write\n"
           "try:\n"
           "    from ase.mep import NEB\n"
           "except ImportError:\n"
           "    from ase.neb import NEB\n"
           "from ase.optimize import " << optimizer << "\n\n"
        << core::AseScriptGenerator::jsonLoggerPreamble()
        << "images = read(\"band.extxyz\", index=\":\")\n"
           "n_images = len(images)\n\n"
           "def _attach(atoms):\n"
        << calcAttach
        << "    return atoms\n\n"
           "for _img in images:\n"
           "    _attach(_img)\n\n"
           "def _emit(a):\n"
           "    lines = []\n"
           "    if a.pbc.any():\n"
           "        cell = a.cell[:]\n"
           "        lines.append(\"CALANGO_CELL \" + \" \".join(\n"
           "            f\"{v:.8f}\" for row in cell for v in row))\n"
           "    lines.append(f\"CALANGO_FRAME {len(a)}\")\n"
           "    for s, p in zip(a.get_chemical_symbols(), a.get_positions()):\n"
           "        lines.append(f\"{s} {p[0]:.6f} {p[1]:.6f} {p[2]:.6f}\")\n"
           "    sys.stdout.write(\"\\n\".join(lines) + \"\\n\")\n"
           "    sys.stdout.flush()\n\n"
           "def _stream_band():\n"
           "    for _img in images:\n"
           "        _emit(_img)\n\n"
        << "k_spring = " << springSpin_->value() << "\n"
        << "fmax = " << fmax_.value() << "\n"
        << "max_steps = " << maxStepsSpin_->value() << "\n";

    if (variant == 2) {
        // AutoNEB: stage the interpolated band to prefix files, then run.
        out << "\n# AutoNEB grows and climbs the band adaptively.\n"
               "try:\n"
               "    from ase.mep import AutoNEB\n"
               "except ImportError:\n"
               "    from ase.autoneb import AutoNEB\n"
               "prefix = \"autoneb\"\n"
               "for i, _img in enumerate(images):\n"
               "    write(f\"{prefix}{i:03d}.traj\", _img)\n\n"
               "def attach_calculators(imgs):\n"
               "    for _img in imgs:\n"
               "        _attach(_img)\n\n"
            << "autoneb = AutoNEB(attach_calculators, prefix=prefix,\n"
               "                  n_simul=1, n_max=n_images + 4,\n"
               "                  climb=True, fmax=fmax, k=k_spring,\n"
            << "                  optimizer=\"" << optimizer << "\",\n"
               "                  maxsteps=max_steps)\n"
               "autoneb.run()\n"
               "images = autoneb.all_images\n";
    } else {
        out << "\nneb = NEB(images, k=k_spring, climb="
            << (climb ? "True" : "False")
            << ", method=\"improvedtangent\")\n"
            << "opt = " << optimizer << "(neb, logfile=\"-\")\n\n"
               "def _report():\n"
               "    _calango_progress(opt.nsteps, max_steps)\n"
               "    _stream_band()\n\n"
               "opt.attach(_report)\n"
               "_stream_band()\n"
               "opt.run(fmax=fmax, steps=max_steps)\n";
    }

    out << "\nwrite(\"neb_final.extxyz\", images)\n"
           "_stream_band()\n"
           "energies = [img.get_potential_energy() for img in images]\n"
           "e0 = energies[0]\n"
           "for i, e in enumerate(energies):\n"
           "    _calango_metric(i, energy=e - e0)\n"
           "barrier = max(energies) - e0\n"
           "print(f\"CALANGO_RESULT barrier_eV={barrier:.6f}\", flush=True)\n"
           "print(\"CALANGO_DONE\", flush=True)\n";
    return QString::fromStdString(out.str());
}

void NebDialog::doRun()
{
    // Interpolate now if the user did not preview first.
    if (band_.empty() && !computeBand())
        return;
    script_ = buildNebScript();
    Q_EMIT runRequested();
}

QString NebDialog::pythonExecutable() const
{
    const QString resolved =
        CondaEnvs::resolvePython(envEdit_->text());
    if (!resolved.isEmpty())
        return resolved;
    return QString::fromStdString(pybridge::PythonEngine::instance().executable());
}

} // namespace calango::gui
