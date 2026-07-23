#include "gui/CondaEnvs.hpp"
#include "gui/MonteCarloDialog.hpp"

#include "core/AseScriptGenerator.hpp"
#include "gui/PythonHighlighter.hpp"
#include "python_bridge/PythonEngine.hpp"

#include <QApplication>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFontDatabase>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSettings>
#include <QSpinBox>
#include <QStackedWidget>
#include <QVBoxLayout>

#include <sstream>

namespace calango::gui {

namespace {

const auto kEnvSettingsKey = QStringLiteral("jobs/environmentPath");

QString optimizerImport(const QString& name)
{
    // ASE class name for the local optimizer used inside a basin-hopping step.
    return name; // combo text already carries the ASE class name
}

} // namespace

MonteCarloDialog::MonteCarloDialog(std::shared_ptr<const core::Structure> structure,
                                   QWidget* parent)
    : QDialog(parent), structure_(std::move(structure))
{
    setWindowTitle(tr("Monte Carlo Simulation"));
    resize(880, 600);

    auto* root = new QVBoxLayout(this);

    auto* methodRow = new QHBoxLayout;
    methodRow->addWidget(new QLabel(tr("Method:"), this));
    methodCombo_ = new QComboBox(this);
    methodCombo_->addItems({tr("Swap-atoms (native alloy MC)"),
                            tr("Basin Hopping (global optimization)")});
    methodRow->addWidget(methodCombo_, 1);
    root->addLayout(methodRow);

    auto* content = new QHBoxLayout;
    stack_ = new QStackedWidget(this);

    // ==================== Swap-atoms page =================================
    auto* swapPage = new QWidget(this);
    auto* swapForm = new QFormLayout(swapPage);
    auto* swapIntro = new QLabel(
        tr("Native Metropolis sampler: randomly swaps unlike species at the "
           "chosen temperature under a nearest-neighbor bond-energy model. "
           "Fast and in-process; produces a trajectory and an energy trace."),
        swapPage);
    swapIntro->setWordWrap(true);
    swapForm->addRow(swapIntro);

    swapTempSpin_ = new QDoubleSpinBox(swapPage);
    swapTempSpin_->setRange(1.0, 100000.0);
    swapTempSpin_->setValue(500.0);
    swapTempSpin_->setSuffix(tr(" K"));
    swapForm->addRow(tr("Temperature:"), swapTempSpin_);

    interactionSpin_ = new QDoubleSpinBox(swapPage);
    interactionSpin_->setRange(-5.0, 5.0);
    interactionSpin_->setDecimals(4);
    interactionSpin_->setSingleStep(0.01);
    interactionSpin_->setValue(-0.05);
    interactionSpin_->setSuffix(tr(" eV"));
    interactionSpin_->setToolTip(
        tr("Unlike-pair interaction V. V < 0 favors ordering (mixing); "
           "V > 0 favors clustering / segregation."));
    swapForm->addRow(tr("Interaction V:"), interactionSpin_);

    swapCutoffSpin_ = new QDoubleSpinBox(swapPage);
    swapCutoffSpin_->setRange(0.5, 20.0);
    swapCutoffSpin_->setValue(3.2);
    swapCutoffSpin_->setSuffix(tr(" Å"));
    swapForm->addRow(tr("Neighbor cutoff:"), swapCutoffSpin_);

    swapStepsSpin_ = new QSpinBox(swapPage);
    swapStepsSpin_->setRange(1, 100000000);
    swapStepsSpin_->setValue(100000);
    swapForm->addRow(tr("MC steps:"), swapStepsSpin_);

    snapshotSpin_ = new QSpinBox(swapPage);
    snapshotSpin_->setRange(1, 10000000);
    snapshotSpin_->setValue(2000);
    swapForm->addRow(tr("Snapshot interval:"), snapshotSpin_);

    swapSeedSpin_ = new QSpinBox(swapPage);
    swapSeedSpin_->setRange(0, 1000000);
    swapSeedSpin_->setValue(42);
    swapForm->addRow(tr("Random seed:"), swapSeedSpin_);
    stack_->addWidget(swapPage);

    // ==================== Basin Hopping page ==============================
    auto* bhPage = new QWidget(this);
    auto* bhForm = new QFormLayout(bhPage);

    calcCombo_ = new QComboBox(bhPage);
    calcCombo_->addItem(tr("EMT (fast test potential)"),
                        static_cast<int>(core::CalculatorKind::EMT));
    calcCombo_->addItem(tr("Lennard-Jones"),
                        static_cast<int>(core::CalculatorKind::LennardJones));
    calcCombo_->addItem(tr("MACE (ML potential)"),
                        static_cast<int>(core::CalculatorKind::Mace));
    calcCombo_->addItem(tr("GPAW (DFT)"),
                        static_cast<int>(core::CalculatorKind::Gpaw));
    calcCombo_->addItem(tr("Quantum ESPRESSO (DFT)"),
                        static_cast<int>(core::CalculatorKind::QuantumEspresso));
    bhForm->addRow(tr("Calculator:"), calcCombo_);

    maceSizeCombo_ = new QComboBox(bhPage);
    maceSizeCombo_->addItems({QStringLiteral("small"), QStringLiteral("medium"),
                              QStringLiteral("large")});
    maceSizeCombo_->setCurrentIndex(1);
    bhForm->addRow(tr("MACE size:"), maceSizeCombo_);

    maceDeviceCombo_ = new QComboBox(bhPage);
    maceDeviceCombo_->addItems({QStringLiteral("cpu"), QStringLiteral("cuda"),
                                QStringLiteral("mps")});
    bhForm->addRow(tr("MACE device:"), maceDeviceCombo_);

    bhCutoffSpin_ = new QDoubleSpinBox(bhPage);
    bhCutoffSpin_->setRange(100.0, 2000.0);
    bhCutoffSpin_->setValue(550.0);
    bhCutoffSpin_->setSuffix(tr(" eV"));
    bhForm->addRow(tr("Plane-wave cutoff:"), bhCutoffSpin_);

    auto* kptRow = new QHBoxLayout;
    for (auto*& spin : kptSpins_) {
        spin = new QSpinBox(bhPage);
        spin->setRange(1, 32);
        spin->setValue(1);
        kptRow->addWidget(spin);
    }
    bhForm->addRow(tr("k-point grid:"), kptRow);

    optimizerCombo_ = new QComboBox(bhPage);
    optimizerCombo_->addItems({QStringLiteral("FIRE"), QStringLiteral("BFGS"),
                               QStringLiteral("LBFGS"),
                               QStringLiteral("QuasiNewton")});
    bhForm->addRow(tr("Local optimizer:"), optimizerCombo_);

    bhTempSpin_ = new QDoubleSpinBox(bhPage);
    bhTempSpin_->setRange(1.0, 100000.0);
    bhTempSpin_->setValue(1000.0);
    bhTempSpin_->setSuffix(tr(" K"));
    bhForm->addRow(tr("Metropolis temperature:"), bhTempSpin_);

    displacementSpin_ = new QDoubleSpinBox(bhPage);
    displacementSpin_->setRange(0.01, 5.0);
    displacementSpin_->setDecimals(3);
    displacementSpin_->setValue(0.4);
    displacementSpin_->setSuffix(tr(" Å"));
    displacementSpin_->setToolTip(tr("Max random atomic displacement per step."));
    bhForm->addRow(tr("Displacement (dr):"), displacementSpin_);

    bhStepsSpin_ = new QSpinBox(bhPage);
    bhStepsSpin_->setRange(1, 1000000);
    bhStepsSpin_->setValue(50);
    bhForm->addRow(tr("Basin-hopping steps:"), bhStepsSpin_);

    fmaxSpin_ = new QDoubleSpinBox(bhPage);
    fmaxSpin_->setRange(0.001, 1.0);
    fmaxSpin_->setDecimals(3);
    fmaxSpin_->setValue(0.05);
    fmaxSpin_->setSuffix(tr(" eV/Å"));
    bhForm->addRow(tr("Local fmax:"), fmaxSpin_);

    maxOptStepsSpin_ = new QSpinBox(bhPage);
    maxOptStepsSpin_->setRange(1, 100000);
    maxOptStepsSpin_->setValue(200);
    bhForm->addRow(tr("Max steps / local opt:"), maxOptStepsSpin_);

    bhSeedSpin_ = new QSpinBox(bhPage);
    bhSeedSpin_->setRange(0, 1000000);
    bhSeedSpin_->setValue(42);
    bhForm->addRow(tr("Random seed:"), bhSeedSpin_);

    auto* envGroup = new QGroupBox(tr("Execution Environment"), bhPage);
    auto* envLayout = new QHBoxLayout(envGroup);
    envEdit_ = new QLineEdit(envGroup);
    envEdit_->setPlaceholderText(
        tr("conda env folder or python (empty = embedded)"));
    auto* envButton = new QPushButton(tr("Browse…"), envGroup);
    envLayout->addWidget(envEdit_, 1);
    envLayout->addWidget(envButton);
    bhForm->addRow(envGroup);
    envStatus_ = new QLabel(bhPage);
    envStatus_->setWordWrap(true);
    bhForm->addRow(envStatus_);
    connect(envButton, &QPushButton::clicked, this,
            &MonteCarloDialog::browseEnvironment);
    stack_->addWidget(bhPage);

    content->addWidget(stack_, 0);

    // --- Right: live script preview (Basin Hopping only) -------------------
    preview_ = new QPlainTextEdit(this);
    preview_->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    new PythonHighlighter(preview_->document());
    connect(preview_, &QPlainTextEdit::textChanged, this, [this] {
        if (updatingPreview_)
            return;
        manuallyEdited_ = true;
    });
    auto* previewColumn = new QVBoxLayout;
    previewColumn->addWidget(new QLabel(tr("Generated ASE script (editable):"), this));
    previewColumn->addWidget(preview_, 1);
    content->addLayout(previewColumn, 1);
    root->addLayout(content, 1);

    statusLabel_ = new QLabel(this);
    statusLabel_->setWordWrap(true);
    root->addWidget(statusLabel_);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Cancel, this);
    auto* runButton = buttons->addButton(tr("Run"), QDialogButtonBox::AcceptRole);
    runButton->setDefault(true);
    root->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    disconnect(buttons, &QDialogButtonBox::accepted, nullptr, nullptr);
    connect(runButton, &QPushButton::clicked, this, &MonteCarloDialog::onRun);

    // Live wiring for the Basin Hopping preview.
    const auto refresh = [this] { refreshPreview(); };
    connect(methodCombo_, &QComboBox::currentIndexChanged, this, [this](int i) {
        stack_->setCurrentIndex(i);
        preview_->setVisible(method() == Method::BasinHopping);
        updateCalculatorEnabled();
        refreshPreview();
    });
    for (QComboBox* c : {calcCombo_, maceSizeCombo_, maceDeviceCombo_, optimizerCombo_})
        connect(c, &QComboBox::currentIndexChanged, this, refresh);
    for (QDoubleSpinBox* s : {bhCutoffSpin_, bhTempSpin_, displacementSpin_, fmaxSpin_})
        connect(s, &QDoubleSpinBox::valueChanged, this, refresh);
    for (QSpinBox* s : {bhStepsSpin_, maxOptStepsSpin_, bhSeedSpin_, kptSpins_[0],
                        kptSpins_[1], kptSpins_[2]})
        connect(s, &QSpinBox::valueChanged, this, refresh);
    connect(calcCombo_, &QComboBox::currentIndexChanged, this,
            [this] { updateCalculatorEnabled(); });

    envEdit_->setText(QSettings().value(kEnvSettingsKey).toString());
    connect(envEdit_, &QLineEdit::textChanged, this, [this] {
        const QString python =
            CondaEnvs::resolvePython(envEdit_->text());
        if (envEdit_->text().trimmed().isEmpty())
            envStatus_->setText(tr("Using embedded interpreter: %1")
                                    .arg(QString::fromStdString(
                                        pybridge::PythonEngine::instance().executable())));
        else if (!python.isEmpty())
            envStatus_->setText(tr("Jobs will run with: %1").arg(python));
        else
            envStatus_->setText(tr("No python interpreter found at this path."));
        QSettings().setValue(kEnvSettingsKey, envEdit_->text());
    });

    preview_->setVisible(method() == Method::BasinHopping);
    updateCalculatorEnabled();
    refreshPreview();
    // Fire the env-status label once.
    envEdit_->textChanged(envEdit_->text());
}

MonteCarloDialog::Method MonteCarloDialog::method() const
{
    return methodCombo_->currentIndex() == 0 ? Method::SwapAtoms
                                             : Method::BasinHopping;
}

void MonteCarloDialog::updateCalculatorEnabled()
{
    const auto kind =
        static_cast<core::CalculatorKind>(calcCombo_->currentData().toInt());
    const bool isMace = kind == core::CalculatorKind::Mace;
    const bool isDft = kind == core::CalculatorKind::Gpaw
        || kind == core::CalculatorKind::QuantumEspresso;
    maceSizeCombo_->setEnabled(isMace);
    maceDeviceCombo_->setEnabled(isMace);
    bhCutoffSpin_->setEnabled(isDft);
    for (auto* s : kptSpins_)
        s->setEnabled(isDft);
}

core::CalculatorConfig MonteCarloDialog::basinCalculatorConfig() const
{
    core::CalculatorConfig c;
    c.calculator =
        static_cast<core::CalculatorKind>(calcCombo_->currentData().toInt());
    c.maceSize = maceSizeCombo_->currentText().toStdString();
    c.maceDevice = maceDeviceCombo_->currentText().toStdString();
    c.planeWaveCutoffEv = bhCutoffSpin_->value();
    for (int i = 0; i < 3; ++i)
        c.kpts[i] = kptSpins_[i]->value();
    return c;
}

QString MonteCarloDialog::buildBasinHoppingScript() const
{
    const std::string calc =
        core::AseScriptGenerator::calculatorSnippet(basinCalculatorConfig());
    const QString optimizer = optimizerImport(optimizerCombo_->currentText());

    std::ostringstream out;
    out << "#!/usr/bin/env python3\n"
           "# Basin Hopping global optimization (generated by Calango).\n"
           "import sys\n"
           "import numpy as np\n"
           "from ase.io import read, write\n"
           "from ase.optimize import " << optimizer.toStdString() << "\n\n"
           "atoms = read(\"structure.extxyz\")\n\n"
        << calc << "\n"
           "def _stream_frame():\n"
           "    lines = []\n"
           "    if atoms.pbc.any():\n"
           "        cell = atoms.cell[:]\n"
           "        lines.append(\"CALANGO_CELL \" + \" \".join(\n"
           "            f\"{v:.8f}\" for row in cell for v in row))\n"
           "    lines.append(f\"CALANGO_FRAME {len(atoms)}\")\n"
           "    for s, p in zip(atoms.get_chemical_symbols(), atoms.get_positions()):\n"
           "        lines.append(f\"{s} {p[0]:.6f} {p[1]:.6f} {p[2]:.6f}\")\n"
           "    sys.stdout.write(\"\\n\".join(lines) + \"\\n\")\n"
           "    sys.stdout.flush()\n\n"
        << "kB = 8.617333262e-5\n"
        << "T = " << bhTempSpin_->value() << "\n"
        << "dr = " << displacementSpin_->value() << "\n"
        << "nsteps = " << bhStepsSpin_->value() << "\n"
        << "fmax = " << fmaxSpin_->value() << "\n"
        << "max_opt = " << maxOptStepsSpin_->value() << "\n"
        << "rng = np.random.default_rng(" << bhSeedSpin_->value() << ")\n\n"
           "def local_opt():\n"
           "    opt = " << optimizer.toStdString() << "(atoms, logfile=None)\n"
           "    opt.run(fmax=fmax, steps=max_opt)\n"
           "    return atoms.get_potential_energy()\n\n"
           "e_curr = local_opt()\n"
           "e_min = e_curr\n"
           "best = atoms.copy()\n"
           "print(f\"CALANGO_ENERGY 0 {e_curr:.6f}\", flush=True)\n"
           "_stream_frame()\n"
           "for step in range(1, nsteps + 1):\n"
           "    pos0 = atoms.get_positions().copy()\n"
           "    atoms.set_positions(pos0 + rng.uniform(-dr, dr, pos0.shape))\n"
           "    e = local_opt()\n"
           "    if e < e_curr or rng.random() < np.exp(-(e - e_curr) / (kB * T)):\n"
           "        e_curr = e\n"
           "        if e < e_min:\n"
           "            e_min = e\n"
           "            best = atoms.copy()\n"
           "    else:\n"
           "        atoms.set_positions(pos0)\n"
           "    print(f\"CALANGO_PROGRESS {step} {nsteps}\", flush=True)\n"
           "    print(f\"CALANGO_ENERGY {step} {e_curr:.6f}\", flush=True)\n"
           "    _stream_frame()\n"
           "write(\"basin_hopping_best.extxyz\", best)\n"
           "print(f\"CALANGO_RESULT e_min_eV={e_min:.6f}\", flush=True)\n"
           "print(\"CALANGO_DONE\", flush=True)\n";
    return QString::fromStdString(out.str());
}

void MonteCarloDialog::refreshPreview()
{
    if (method() != Method::BasinHopping || manuallyEdited_)
        return;
    updatingPreview_ = true;
    preview_->setPlainText(buildBasinHoppingScript());
    updatingPreview_ = false;
}

QString MonteCarloDialog::script() const
{
    return preview_->toPlainText();
}

QString MonteCarloDialog::pythonExecutable() const
{
    const QString resolved =
        CondaEnvs::resolvePython(envEdit_->text());
    if (!resolved.isEmpty())
        return resolved;
    return QString::fromStdString(pybridge::PythonEngine::instance().executable());
}

void MonteCarloDialog::browseEnvironment()
{
    const QString dir = QFileDialog::getExistingDirectory(
        this, tr("Select Conda Environment Folder"));
    if (!dir.isEmpty())
        envEdit_->setText(dir);
}

void MonteCarloDialog::onRun()
{
    if (method() == Method::BasinHopping) {
        accept(); // MainWindow launches the job
        return;
    }

    // Native swap-atoms MC runs here, synchronously.
    core::SwapMonteCarloOptions options;
    options.temperatureK = swapTempSpin_->value();
    options.interactionEv = interactionSpin_->value();
    options.neighborCutoff = swapCutoffSpin_->value();
    options.steps = swapStepsSpin_->value();
    options.snapshotInterval = snapshotSpin_->value();
    options.seed = static_cast<unsigned>(swapSeedSpin_->value());

    QApplication::setOverrideCursor(Qt::WaitCursor);
    core::SwapMonteCarloResult res =
        core::runSwapMonteCarlo(*structure_, options);
    QApplication::restoreOverrideCursor();

    if (res.snapshots.empty()) {
        statusLabel_->setText(res.note.empty()
                                  ? tr("Monte Carlo produced no result.")
                                  : QString::fromStdString(res.note));
        return;
    }
    swapResult_ = std::move(res);
    accept();
}

} // namespace calango::gui
