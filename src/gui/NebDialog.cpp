#include "gui/CondaEnvs.hpp"
#include "gui/NebDialog.hpp"

#include "core/AseScriptGenerator.hpp"
#include "python_bridge/AseBridge.hpp"
#include "python_bridge/NebBuilder.hpp"
#include "python_bridge/PythonEngine.hpp"

#include <QApplication>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSettings>
#include <QSpinBox>
#include <QVBoxLayout>

#include <sstream>

namespace calango::gui {

namespace {

const auto kEnvSettingsKey = QStringLiteral("jobs/environmentPath");

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
    resize(560, 640);

    auto* layout = new QVBoxLayout(this);

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
    fmaxSpin_ = new QDoubleSpinBox(solverBox);
    fmaxSpin_->setRange(0.001, 2.0);
    fmaxSpin_->setDecimals(3);
    fmaxSpin_->setValue(0.05);
    fmaxSpin_->setSuffix(tr(" eV/Å"));
    solverForm->addRow(tr("Force convergence (fmax):"), fmaxSpin_);
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
    calcForm->addRow(tr("Calculator:"), calcCombo_);
    maceSizeCombo_ = new QComboBox(calcBox);
    maceSizeCombo_->addItems({QStringLiteral("small"), QStringLiteral("medium"),
                              QStringLiteral("large")});
    maceSizeCombo_->setCurrentIndex(1);
    calcForm->addRow(tr("MACE size:"), maceSizeCombo_);
    maceDeviceCombo_ = new QComboBox(calcBox);
    maceDeviceCombo_->addItems({QStringLiteral("cpu"), QStringLiteral("cuda"),
                                QStringLiteral("mps")});
    calcForm->addRow(tr("MACE device:"), maceDeviceCombo_);
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
    connect(calcCombo_, &QComboBox::currentIndexChanged, this,
            &NebDialog::updateCalculatorEnabled);

    // --- Environment -------------------------------------------------------
    auto* envRow = new QHBoxLayout;
    envRow->addWidget(new QLabel(tr("Environment:"), this));
    envEdit_ = new QLineEdit(this);
    envEdit_->setPlaceholderText(tr("conda env / python (empty = embedded)"));
    envEdit_->setText(QSettings().value(kEnvSettingsKey).toString());
    auto* envButton = new QPushButton(tr("Browse…"), this);
    envRow->addWidget(envEdit_, 1);
    envRow->addWidget(envButton);
    layout->addLayout(envRow);
    envStatus_ = new QLabel(this);
    envStatus_->setWordWrap(true);
    layout->addWidget(envStatus_);
    connect(envButton, &QPushButton::clicked, this, &NebDialog::browseEnvironment);
    connect(envEdit_, &QLineEdit::textChanged, this, [this] {
        QSettings().setValue(kEnvSettingsKey, envEdit_->text());
    });

    statusLabel_ = new QLabel(this);
    statusLabel_->setWordWrap(true);
    layout->addWidget(statusLabel_);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    auto* runButton = buttons->addButton(tr("Run NEB"), QDialogButtonBox::AcceptRole);
    runButton->setDefault(true);
    layout->addWidget(buttons);
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
    const QString path = QFileDialog::getOpenFileName(this, tr("Open Initial State"));
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
    const QString path = QFileDialog::getOpenFileName(this, tr("Open Final State"));
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
    cutoffSpin_->setEnabled(isDft);
    for (auto* s : kptSpins_)
        s->setEnabled(isDft);
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
    c.planeWaveCutoffEv = cutoffSpin_->value();
    for (int i = 0; i < 3; ++i)
        c.kpts[i] = kptSpins_[i]->value();
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
        << "fmax = " << fmaxSpin_->value() << "\n"
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
               "    _calango_log.progress(opt.nsteps, max_steps)\n"
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
           "    _calango_log.metric(i, energy=e - e0)\n"
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
