#include "gui/ElfDialog.hpp"

#include "gui/VolumeViewWidget.hpp"
#include "render/ColorMap.hpp"

#include <QApplication>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QSaveFile>
#include <QSignalBlocker>
#include <QSlider>
#include <QTextStream>
#include <QVBoxLayout>
#include <QtConcurrent>

#include <algorithm>
#include <array>
#include <cmath>

namespace calango::gui {

namespace {

/// Colormaps offered in the ELF viewer (indices into this table).
const render::ColorGradient kMaps[] = {
    render::ColorGradient::Viridis,
    render::ColorGradient::Plasma,
    render::ColorGradient::Coolwarm,
    render::ColorGradient::Rainbow,
};

QStringList colormapNames()
{
    return {QStringLiteral("Viridis"), QStringLiteral("Plasma"),
            QStringLiteral("Coolwarm"), QStringLiteral("Rainbow")};
}

} // namespace

ElfDialog::ElfDialog(std::shared_ptr<core::Structure> structure, QWidget* parent)
    : QDialog(parent), structure_(std::move(structure))
{
    setWindowTitle(tr("Electron Localization Function (ELF)"));
    resize(980, 620);

    auto* layout = new QHBoxLayout(this);
    view_ = new VolumeViewWidget(this);
    layout->addWidget(view_, 1);

    auto* side = new QVBoxLayout;
    layout->addLayout(side);

    infoLabel_ = new QLabel(this);
    if (structure_)
        infoLabel_->setText(
            tr("Structure: %1 (%2 atoms)")
                .arg(QString::fromStdString(structure_->chemicalFormula()))
                .arg(structure_->size()));
    infoLabel_->setWordWrap(true);
    side->addWidget(infoLabel_);

    // --- Compute (GPAW) ----------------------------------------------------
    auto* computeGroup = new QGroupBox(tr("Compute (GPAW)"), this);
    auto* computeForm = new QFormLayout(computeGroup);

    // Baseline source: a completed process whose wavefunction/density (.gpw)
    // is restarted to build the ELF. When none is selected the script runs a
    // fresh GPAW single-point first.
    baselineCombo_ = new QComboBox(computeGroup);
    baselineCombo_->addItem(tr("(none — run a fresh GPAW single-point)"),
                            QString());
    baselineCombo_->setToolTip(
        tr("Pick the process / workspace tab that holds a calculated "
           "wavefunction (GPAW .gpw). The ELF is built by restarting GPAW from "
           "that directory; otherwise a fresh single-point is run."));
    computeForm->addRow(tr("Wavefunction source:"), baselineCombo_);

    auto* computeHint = new QLabel(
        tr("Generates the ELF η(r) with GPAW and writes elf.cube into the task "
           "directory. When it finishes, load it below."),
        computeGroup);
    computeHint->setWordWrap(true);
    computeForm->addRow(computeHint);

    auto* computeButton = new QPushButton(tr("Compute ELF"), computeGroup);
    computeButton->setToolTip(
        tr("Stage and launch the GPAW ELF job. When it completes, click "
           "\"Load ELF grid…\" and select the elf.cube in its task directory."));
    connect(computeButton, &QPushButton::clicked, this, &ElfDialog::computeElf);
    computeForm->addRow(computeButton);
    side->addWidget(computeGroup);

    // --- Load --------------------------------------------------------------
    auto* loadGroup = new QGroupBox(tr("Load"), this);
    auto* loadLayout = new QVBoxLayout(loadGroup);
    auto* loadButton = new QPushButton(tr("Load ELF grid…"), loadGroup);
    gridLabel_ = new QLabel(tr("(no grid loaded)"), loadGroup);
    gridLabel_->setWordWrap(true);
    loadLayout->addWidget(loadButton);
    loadLayout->addWidget(gridLabel_);
    side->addWidget(loadGroup);
    connect(loadButton, &QPushButton::clicked, this, &ElfDialog::loadGridDialog);

    // --- Isosurface --------------------------------------------------------
    isoGroup_ = new QGroupBox(tr("Isosurface"), this);
    isoGroup_->setCheckable(true);
    auto* isoForm = new QFormLayout(isoGroup_);
    auto* isoRow = new QWidget(isoGroup_);
    auto* isoRowLayout = new QHBoxLayout(isoRow);
    isoRowLayout->setContentsMargins(0, 0, 0, 0);
    // η ∈ [0, 1]: slider 0..1000 maps linearly to the isovalue.
    isoSlider_ = new QSlider(Qt::Horizontal, isoRow);
    isoSlider_->setRange(0, 1000);
    isoSlider_->setValue(850); // η ≈ 0.85 highlights localized (bonding) regions
    isoSpin_ = new QDoubleSpinBox(isoRow);
    isoSpin_->setDecimals(3);
    isoSpin_->setRange(0.0, 1.0);
    isoSpin_->setSingleStep(0.01);
    isoSpin_->setValue(isovalueFromSlider());
    isoRowLayout->addWidget(isoSlider_, 1);
    isoRowLayout->addWidget(isoSpin_);
    isoForm->addRow(tr("η isovalue:"), isoRow);

    isoColormapCombo_ = new QComboBox(isoGroup_);
    isoColormapCombo_->addItems(colormapNames());
    isoForm->addRow(tr("Colormap:"), isoColormapCombo_);
    side->addWidget(isoGroup_);

    connect(isoGroup_, &QGroupBox::toggled, this, &ElfDialog::rebuildIso);
    connect(isoSlider_, &QSlider::valueChanged, this, [this] {
        const QSignalBlocker blocker(isoSpin_);
        isoSpin_->setValue(isovalueFromSlider());
        rebuildIso();
    });
    connect(isoSpin_, &QDoubleSpinBox::valueChanged, this, [this](double value) {
        const QSignalBlocker blocker(isoSlider_);
        isoSlider_->setValue(
            static_cast<int>(std::clamp(value, 0.0, 1.0) * 1000.0));
        rebuildIso();
    });
    connect(isoColormapCombo_, &QComboBox::currentIndexChanged,
            this, &ElfDialog::rebuildIso);

    // --- Slice plane -------------------------------------------------------
    sliceGroup_ = new QGroupBox(tr("Slice plane"), this);
    auto* sliceForm = new QFormLayout(sliceGroup_);
    planeCombo_ = new QComboBox(sliceGroup_);
    planeCombo_->addItems({tr("Off"), QStringLiteral("XY"),
                           QStringLiteral("XZ"), QStringLiteral("YZ")});
    sliceForm->addRow(tr("Plane:"), planeCombo_);
    offsetSlider_ = new QSlider(Qt::Horizontal, sliceGroup_);
    offsetSlider_->setRange(0, 100);
    offsetSlider_->setValue(50);
    sliceForm->addRow(tr("Offset:"), offsetSlider_);
    sliceColormapCombo_ = new QComboBox(sliceGroup_);
    sliceColormapCombo_->addItems(colormapNames());
    sliceForm->addRow(tr("Colormap:"), sliceColormapCombo_);
    side->addWidget(sliceGroup_);

    connect(planeCombo_, &QComboBox::currentIndexChanged,
            this, &ElfDialog::rebuildSlice);
    connect(offsetSlider_, &QSlider::valueChanged,
            this, &ElfDialog::rebuildSlice);
    connect(sliceColormapCombo_, &QComboBox::currentIndexChanged,
            this, &ElfDialog::rebuildSlice);

    // --- Export / close ----------------------------------------------------
    auto* exportRow = new QHBoxLayout;
    auto* exportIsoButton = new QPushButton(tr("Export Isosurface (OBJ)…"), this);
    auto* exportSliceButton = new QPushButton(tr("Export Slice (CSV)…"), this);
    exportRow->addWidget(exportIsoButton);
    exportRow->addWidget(exportSliceButton);
    side->addLayout(exportRow);
    connect(exportIsoButton, &QPushButton::clicked, this, &ElfDialog::exportIso);
    connect(exportSliceButton, &QPushButton::clicked, this, &ElfDialog::exportSlice);

    side->addStretch(1);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    side->addWidget(buttons);

    connect(&isoWatcher_, &QFutureWatcher<core::IsoMesh>::finished,
            this, &ElfDialog::onIsoExtractionFinished);
}

ElfDialog::~ElfDialog()
{
    // QFutureWatcher's destructor does not wait, and the running lambda holds a
    // shared_ptr into a member about to die. Detach from the signal (so no slot
    // fires into a half-destroyed dialog) and block until the worker is done.
    isoWatcher_.disconnect(this);
    if (isoWatcher_.isRunning())
        isoWatcher_.waitForFinished();
}

void ElfDialog::setDensityBaselines(
    const QList<QPair<QString, QString>>& baselines)
{
    if (!baselineCombo_)
        return;
    for (const auto& [label, dir] : baselines)
        baselineCombo_->addItem(label, dir);
}

double ElfDialog::isovalueFromSlider() const
{
    return isoSlider_->value() / 1000.0;
}

void ElfDialog::computeElf()
{
    if (!structure_ || structure_->empty()) {
        QMessageBox::information(this, windowTitle(),
                                 tr("Open a structure first."));
        return;
    }
    Q_EMIT runRequested(generateScript(), tr("Electron Localization Function"));
    QMessageBox::information(
        this, windowTitle(),
        tr("ELF job launched. When it completes, click \"Load ELF grid…\" and "
           "select the elf.cube in its task directory."));
}

void ElfDialog::loadGridDialog()
{
    const QString path = QFileDialog::getOpenFileName(
        this, tr("Load ELF grid"), QString(),
        tr("ELF grids (*.cube ELFCAR* *.xsf);;All files (*)"));
    if (!path.isEmpty())
        loadGrid(path);
}

void ElfDialog::loadGrid(const QString& path)
{
    QApplication::setOverrideCursor(Qt::WaitCursor);
    try {
        // Freshly allocated each load: the previous grid stays alive as long as
        // an in-flight extraction still references it.
        field_ = std::make_shared<const core::VolumetricData>(
            core::VolumetricData::load(path.toStdString()));
        QApplication::restoreOverrideCursor();
    } catch (const std::exception& e) {
        QApplication::restoreOverrideCursor();
        QMessageBox::warning(this, windowTitle(), QString::fromUtf8(e.what()));
        return;
    }
    gridLabel_->setText(QStringLiteral("%1 — %2×%3×%4, [%5, %6]")
                            .arg(QString::fromStdString(field_->label))
                            .arg(field_->nx)
                            .arg(field_->ny)
                            .arg(field_->nz)
                            .arg(field_->minValue(), 0, 'g', 4)
                            .arg(field_->maxValue(), 0, 'g', 4));
    view_->setBox(*field_);
    view_->frameBox();
    rebuildIso();
    rebuildSlice();
}

void ElfDialog::rebuildIso()
{
    if (!field_ || !isoGroup_->isChecked()) {
        // Bump the generation so any in-flight extraction's result is discarded
        // rather than drawn over the cleared view.
        ++isoGeneration_;
        isoRequestPending_ = false;
        isoMesh_ = {};
        view_->clearIsoMesh();
        return;
    }
    isoRequestPending_ = true;
    startIsoExtraction();
}

void ElfDialog::startIsoExtraction()
{
    // One at a time: a slider drag would otherwise spawn an extraction per pixel
    // of travel. The newest request wins when the current one lands.
    if (!isoRequestPending_ || isoWatcher_.isRunning())
        return;
    isoRequestPending_ = false;
    isoRunningGeneration_ = ++isoGeneration_;

    // Capture a shared_ptr copy (a refcount bump, not a grid copy) so the
    // worker's input stays alive and unchanged even if the user loads a new
    // file while it runs.
    FieldPtr field = field_;
    const double isovalue = isoSpin_->value();

    isoWatcher_.setFuture(QtConcurrent::run(
        [field = std::move(field), isovalue] {
            return core::extractIsosurface(*field, isovalue, nullptr);
        }));
}

void ElfDialog::onIsoExtractionFinished()
{
    // Stale result: its inputs changed (or the surface was switched off) while
    // it ran. Drop it and serve whatever is queued now.
    if (isoRunningGeneration_ != isoGeneration_) {
        startIsoExtraction();
        return;
    }

    isoMesh_ = isoWatcher_.result();
    // A single-field surface: no secondary color values, so the isosurface is
    // drawn in a flat shade (colored=false).
    view_->setIsoMesh(isoMesh_, kMaps[isoColormapCombo_->currentIndex()], 0.0,
                      1.0, false);

    startIsoExtraction(); // serve a request that arrived while we were busy
}

void ElfDialog::rebuildSlice()
{
    sliceSamples_.clear();
    const int planeIndex = planeCombo_->currentIndex();
    if (!field_ || planeIndex == 0) { // 0 == "Off"
        view_->clearSlice();
        return;
    }

    const core::VolumetricData* field = field_.get();
    const double lo = field->minValue(), hi = field->maxValue();
    const double range = std::max(hi - lo, 1e-30);
    const auto gradient = kMaps[sliceColormapCombo_->currentIndex()];
    const double offset = offsetSlider_->value() / 100.0;
    // planeCombo_: 0=Off, 1=XY, 2=XZ, 3=YZ -> 0-based plane for the sampler.
    const int plane = planeIndex - 1;
    std::vector<float> stream;

    const auto emit_ = [&](const core::Vec3& p, double value) {
        const QColor c = render::ColorMap::sample(
            gradient, static_cast<float>((value - lo) / range));
        stream.insert(stream.end(),
                      {static_cast<float>(p.x), static_cast<float>(p.y),
                       static_cast<float>(p.z), static_cast<float>(c.redF()),
                       static_cast<float>(c.greenF()),
                       static_cast<float>(c.blueF())});
        sliceSamples_.push_back({p.x, p.y, p.z, value});
    };

    // Grid-aligned slices sample exactly on the field's own axes:
    // XY = (A,B) at fixed C, XZ = (A,C) at fixed B, YZ = (B,C) at fixed A.
    const int uAxis = plane == 2 ? 1 : 0;
    const int vAxis = plane == 0 ? 1 : 2;
    const int wAxis = 3 - uAxis - vAxis;
    const int dims[3] = {field->nx, field->ny, field->nz};
    const int nu = dims[uAxis], nv = dims[vAxis];
    const double w = offset * dims[wAxis];
    const auto gridPoint = [&](double u, double v) {
        double g[3];
        g[uAxis] = u;
        g[vAxis] = v;
        g[wAxis] = w;
        return std::array<double, 3>{g[0], g[1], g[2]};
    };
    for (int iu = 0; iu < nu; ++iu) {
        for (int iv = 0; iv < nv; ++iv) {
            const std::array<double, 3> q[4] = {
                gridPoint(iu, iv), gridPoint(iu + 1, iv),
                gridPoint(iu + 1, iv + 1), gridPoint(iu, iv + 1)};
            static constexpr int kQuad[6] = {0, 1, 2, 0, 2, 3};
            for (const int k : kQuad)
                emit_(field->position(q[k][0], q[k][1], q[k][2]),
                      field->samplePeriodic(q[k][0], q[k][1], q[k][2]));
        }
    }
    view_->setSlice(stream);
}

void ElfDialog::exportIso()
{
    if (isoMesh_.positions.empty()) {
        QMessageBox::information(this, windowTitle(),
                                 tr("Extract an isosurface first."));
        return;
    }
    const QString path = QFileDialog::getSaveFileName(
        this, tr("Export Isosurface"), QStringLiteral("elf_isosurface.obj"),
        tr("Wavefront OBJ (*.obj)"));
    if (path.isEmpty())
        return;
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QMessageBox::warning(this, windowTitle(),
                             tr("Could not write %1").arg(path));
        return;
    }
    QTextStream out(&file);
    out << "# Calango ELF isosurface (" << isoMesh_.positions.size() / 3
        << " triangles)\n";
    for (const auto& p : isoMesh_.positions)
        out << "v " << p.x << ' ' << p.y << ' ' << p.z << '\n';
    for (const auto& n : isoMesh_.normals)
        out << "vn " << n.x << ' ' << n.y << ' ' << n.z << '\n';
    for (std::size_t i = 0; i + 2 < isoMesh_.positions.size(); i += 3)
        out << "f " << i + 1 << "//" << i + 1 << ' ' << i + 2 << "//" << i + 2
            << ' ' << i + 3 << "//" << i + 3 << '\n';
    file.commit();
}

void ElfDialog::exportSlice()
{
    if (sliceSamples_.empty()) {
        QMessageBox::information(this, windowTitle(),
                                 tr("Enable a slice plane first."));
        return;
    }
    const QString path = QFileDialog::getSaveFileName(
        this, tr("Export Slice"), QStringLiteral("elf_slice.csv"),
        tr("CSV (*.csv)"));
    if (path.isEmpty())
        return;
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QMessageBox::warning(this, windowTitle(),
                             tr("Could not write %1").arg(path));
        return;
    }
    QTextStream out(&file);
    out << "x_A,y_A,z_A,eta\n";
    for (const auto& s : sliceSamples_)
        out << s[0] << ',' << s[1] << ',' << s[2] << ',' << s[3] << '\n';
    file.commit();
}

QString ElfDialog::generateScript() const
{
    // Preamble mirrors PartialChargeDialog: JSON/np helpers, ASE reader and the
    // CalangoLog progress logger the controller scrapes for progress markers.
    const QString preamble = QStringLiteral(
        "import json\n"
        "import os\n"
        "import glob\n"
        "# GPAW 25.x computes the ELF (kinetic-energy density) only in its new\n"
        "# engine, so enable it before importing gpaw.\n"
        "os.environ.setdefault('GPAW_NEW', '1')\n"
        "import numpy as np\n"
        "from ase.io import read\n"
        "from calango_log import CalangoLog\n"
        "_log = CalangoLog()\n");

    // Wavefunction acquisition. Either restart GPAW from a baseline process's
    // .gpw, or — when none is selected — run a fresh single-point. Both define
    // `calc` (a GPAW calculator with wavefunctions) and `atoms`.
    const QString baseline =
        baselineCombo_ ? baselineCombo_->currentData().toString() : QString();
    QString acquisition;
    if (!baseline.isEmpty()) {
        acquisition = QStringLiteral(
            "_base = r\"%1\"\n"
            "_gpw = sorted(glob.glob(os.path.join(_base, '*.gpw')))\n"
            "if not _gpw:\n"
            "    raise RuntimeError('No GPAW wavefunction (.gpw) found in '\n"
            "                       + _base + '. The ELF needs the full '\n"
            "                       'wavefunctions — re-run GPAW with '\n"
            "                       \"calc.write('wfs.gpw', mode='all').\")\n"
            "from gpaw import GPAW\n"
            "calc = GPAW(_gpw[0], txt=None)\n"
            "atoms = calc.get_atoms()\n"
            "_log.progress(1, 3)\n").arg(baseline);
    } else {
        acquisition = QStringLiteral(
            "atoms = read('structure.extxyz')\n"
            "_log.progress(1, 3)\n"
            "from gpaw import GPAW, PW\n"
            "calc = GPAW(mode=PW(500), xc='PBE', kpts=(7, 7, 7), txt='gpaw.txt')\n"
            "atoms.calc = calc\n"
            "atoms.get_potential_energy()\n"
            "_log.progress(2, 3)\n");
    }

    // ELF evaluation. The gpaw.elf.ELF path is preferred; API names have shifted
    // across GPAW versions, so fall back to the calculator method and, failing
    // both, surface a RuntimeError carrying the underlying messages.
    // ELF evaluation, hardened against GPAW version drift and grid-layout /
    // spin-shape mismatches:
    //   * ELF.update() takes (wfs) in some versions and no arg in others;
    //   * get_electronic_localization_function() accepts gridrefinement/spin
    //     only in some versions;
    //   * the result may come back spin-resolved (leading spin axis) or as a
    //     non-contiguous / non-float array — normalize to one contiguous
    //     C-ordered float64 3D grid before write_cube.
    const QString compute = QStringLiteral(
        "elf_grid = None\n"
        "_errs = []\n"
        "try:\n"
        "    from gpaw.elf import ELF\n"
        "    elf = ELF(calc)\n"
        "    try:\n"
        "        elf.update(calc.wfs)\n"
        "    except TypeError:\n"
        "        elf.update()\n"
        "    try:\n"
        "        elf_grid = elf.get_electronic_localization_function(\n"
        "            gridrefinement=2)\n"
        "    except TypeError:\n"
        "        elf_grid = elf.get_electronic_localization_function()\n"
        "except Exception as _e:\n"
        "    _errs.append('gpaw.elf.ELF: ' + repr(_e))\n"
        "    try:\n"
        "        elf_grid = calc.get_electronic_localization_function()\n"
        "    except Exception as _e2:\n"
        "        _errs.append('calc.get_electronic_localization_function: '\n"
        "                     + repr(_e2))\n"
        "if elf_grid is None:\n"
        "    raise RuntimeError('Could not evaluate the ELF with this GPAW '\n"
        "                       'version: ' + ' | '.join(_errs))\n"
        "elf_grid = np.asarray(elf_grid, dtype=float)\n"
        "if elf_grid.ndim == 4:  # (spin, nx, ny, nz) -> collapse the spin axis\n"
        "    elf_grid = elf_grid[0]\n"
        "elf_grid = np.ascontiguousarray(elf_grid, dtype=float)\n"
        "_log.progress(3, 3)\n");

    // Write elf.cube on the ELF grid and emit the result marker the controller
    // watches for. `atoms` is always defined by the acquisition step.
    const QString tail = QStringLiteral(
        "from ase.io.cube import write_cube\n"
        "with open('elf.cube', 'w') as _fh:\n"
        "    write_cube(_fh, atoms, data=elf_grid)\n"
        "print('CALANGO_RESULT elf=elf.cube', flush=True)\n");

    return preamble + acquisition + compute + tail;
}

} // namespace calango::gui
