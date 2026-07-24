#include "gui/WannierDialog.hpp"

#include "gui/VolumeViewWidget.hpp"
#include "render/ColorMap.hpp"

#include <QApplication>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSlider>
#include <QSpinBox>
#include <QTableWidget>
#include <QVBoxLayout>
#include <QtConcurrent>

#include <algorithm>
#include <array>
#include <cmath>

namespace calango::gui {

namespace {

/// Colormaps offered in the Wannier viewer (indices into this table).
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

WannierDialog::WannierDialog(std::shared_ptr<core::Structure> structure,
                             QWidget* parent)
    : QDialog(parent), structure_(std::move(structure))
{
    setWindowTitle(tr("Maximally Localized Wannier Functions (MLWF)"));
    resize(1040, 660);

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

    // --- Compute (GPAW MLWF) ----------------------------------------------
    auto* computeGroup = new QGroupBox(tr("Compute (GPAW MLWF)"), this);
    auto* computeForm = new QFormLayout(computeGroup);

    // Baseline source: a completed single-point whose Bloch wavefunctions
    // (.gpw) are restarted to run the localization. When none is selected the
    // script runs a fresh GPAW SCF first (Wannier needs a converged calc).
    baselineCombo_ = new QComboBox(computeGroup);
    baselineCombo_->addItem(tr("(none — run a fresh GPAW SCF)"), QString());
    baselineCombo_->setToolTip(
        tr("Pick the single-point calculation that holds the Bloch "
           "wavefunctions (GPAW .gpw). The localization restarts GPAW from "
           "that directory; otherwise a fresh SCF is run first."));
    computeForm->addRow(tr("Wavefunction source:"), baselineCombo_);

    nWannier_ = new QSpinBox(computeGroup);
    nWannier_->setRange(1, 512);
    nWannier_->setValue(4);
    nWannier_->setToolTip(
        tr("Number of Wannier functions to localize (typically the number of "
           "occupied bands / valence orbitals)."));
    computeForm->addRow(tr("Wannier functions:"), nWannier_);

    // Trial-orbital initialization. The untranslated key stored as itemData is
    // what generateScript() maps onto ASE's `initialwannier` argument.
    projectionCombo_ = new QComboBox(computeGroup);
    projectionCombo_->addItem(tr("Automatic (orbitals)"),
                              QStringLiteral("orbitals"));
    projectionCombo_->addItem(tr("Bloch"), QStringLiteral("bloch"));
    projectionCombo_->addItem(tr("Random"), QStringLiteral("random"));
    projectionCombo_->addItem(QStringLiteral("s"), QStringLiteral("s"));
    projectionCombo_->addItem(QStringLiteral("p"), QStringLiteral("p"));
    projectionCombo_->addItem(QStringLiteral("d"), QStringLiteral("d"));
    projectionCombo_->addItem(QStringLiteral("sp3"), QStringLiteral("sp3"));
    projectionCombo_->addItem(QStringLiteral("dxy"), QStringLiteral("dxy"));
    projectionCombo_->setToolTip(
        tr("Trial-orbital guess used to seed the localization. The atomic sets "
           "(s, p, d, sp3, dxy) fall back to ASE's 'orbitals' initializer, "
           "which derives the projections from the atomic orbitals."));
    computeForm->addRow(tr("Initial projection:"), projectionCombo_);

    auto* computeButton = new QPushButton(tr("Compute MLWF"), computeGroup);
    computeButton->setToolTip(
        tr("Stage and launch the GPAW/ASE Wannier job. When it completes, click "
           "\"Load results…\" and select the wannier.json in its task "
           "directory."));
    connect(computeButton, &QPushButton::clicked, this,
            &WannierDialog::computeMlwf);
    computeForm->addRow(computeButton);
    side->addWidget(computeGroup);

    // --- Wannier functions (results) --------------------------------------
    auto* resultsGroup = new QGroupBox(tr("Wannier functions"), this);
    auto* resultsLayout = new QVBoxLayout(resultsGroup);

    table_ = new QTableWidget(0, 5, resultsGroup);
    table_->setHorizontalHeaderLabels(
        {tr("#"), tr("center x (Å)"), tr("center y (Å)"), tr("center z (Å)"),
         tr("spread Ω (Å²)")});
    table_->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    resultsLayout->addWidget(table_);

    totalSpreadLabel_ = new QLabel(tr("Total spread Ω: —"), resultsGroup);
    totalSpreadLabel_->setWordWrap(true);
    resultsLayout->addWidget(totalSpreadLabel_);

    auto* orbitalRow = new QHBoxLayout;
    orbitalRow->addWidget(new QLabel(tr("Show orbital:"), resultsGroup));
    orbitalCombo_ = new QComboBox(resultsGroup);
    orbitalCombo_->setToolTip(
        tr("Load the selected Wannier orbital's real-space isosurface "
           "(wannier_<n>.cube) from the results directory."));
    orbitalRow->addWidget(orbitalCombo_, 1);
    resultsLayout->addLayout(orbitalRow);
    side->addWidget(resultsGroup);
    connect(orbitalCombo_, &QComboBox::currentIndexChanged, this,
            &WannierDialog::orbitalSelected);

    // --- Load results ------------------------------------------------------
    auto* loadGroup = new QGroupBox(tr("Load results…"), this);
    auto* loadLayout = new QVBoxLayout(loadGroup);
    auto* loadButton = new QPushButton(tr("Load wannier.json…"), loadGroup);
    gridLabel_ = new QLabel(tr("(no orbital loaded)"), loadGroup);
    gridLabel_->setWordWrap(true);
    loadLayout->addWidget(loadButton);
    loadLayout->addWidget(gridLabel_);
    side->addWidget(loadGroup);
    connect(loadButton, &QPushButton::clicked, this,
            &WannierDialog::loadResultsDialog);

    // --- Isosurface --------------------------------------------------------
    isoGroup_ = new QGroupBox(tr("Isosurface"), this);
    isoGroup_->setCheckable(true);
    auto* isoForm = new QFormLayout(isoGroup_);
    auto* isoRow = new QWidget(isoGroup_);
    auto* isoRowLayout = new QHBoxLayout(isoRow);
    isoRowLayout->setContentsMargins(0, 0, 0, 0);
    // Wannier orbitals ψ(r) are signed; the slider 0..1000 maps linearly onto
    // [0, fieldMax_], the amplitude ceiling of the loaded cube (see loadCube).
    isoSlider_ = new QSlider(Qt::Horizontal, isoRow);
    isoSlider_->setRange(0, 1000);
    isoSlider_->setValue(200); // a low amplitude reveals the orbital's lobes
    isoSpin_ = new QDoubleSpinBox(isoRow);
    isoSpin_->setDecimals(4);
    isoSpin_->setRange(0.0, 1.0);
    isoSpin_->setSingleStep(0.01);
    isoSpin_->setValue(isovalueFromSlider());
    isoRowLayout->addWidget(isoSlider_, 1);
    isoRowLayout->addWidget(isoSpin_);
    isoForm->addRow(tr("Isovalue:"), isoRow);

    isoColormapCombo_ = new QComboBox(isoGroup_);
    isoColormapCombo_->addItems(colormapNames());
    isoForm->addRow(tr("Colormap:"), isoColormapCombo_);
    side->addWidget(isoGroup_);

    connect(isoGroup_, &QGroupBox::toggled, this, &WannierDialog::rebuildIso);
    connect(isoSlider_, &QSlider::valueChanged, this, [this] {
        const QSignalBlocker blocker(isoSpin_);
        isoSpin_->setValue(isovalueFromSlider());
        rebuildIso();
    });
    connect(isoSpin_, &QDoubleSpinBox::valueChanged, this, [this](double value) {
        const QSignalBlocker blocker(isoSlider_);
        const double frac = fieldMax_ > 0.0 ? value / fieldMax_ : 0.0;
        isoSlider_->setValue(
            static_cast<int>(std::clamp(frac, 0.0, 1.0) * 1000.0));
        rebuildIso();
    });
    connect(isoColormapCombo_, &QComboBox::currentIndexChanged, this,
            &WannierDialog::rebuildIso);

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

    connect(planeCombo_, &QComboBox::currentIndexChanged, this,
            &WannierDialog::rebuildSlice);
    connect(offsetSlider_, &QSlider::valueChanged, this,
            &WannierDialog::rebuildSlice);
    connect(sliceColormapCombo_, &QComboBox::currentIndexChanged, this,
            &WannierDialog::rebuildSlice);

    side->addStretch(1);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    side->addWidget(buttons);

    connect(&isoWatcher_, &QFutureWatcher<core::IsoMesh>::finished, this,
            &WannierDialog::onIsoExtractionFinished);
}

WannierDialog::~WannierDialog()
{
    // Same teardown as ElfDialog: QFutureWatcher's destructor does not wait, and
    // the running lambda holds a shared_ptr into a member about to die. Detach
    // from the signal, then block until the worker is done.
    isoWatcher_.disconnect(this);
    if (isoWatcher_.isRunning())
        isoWatcher_.waitForFinished();
}

void WannierDialog::setDensityBaselines(
    const QList<QPair<QString, QString>>& baselines)
{
    if (!baselineCombo_)
        return;
    for (const auto& [label, dir] : baselines)
        baselineCombo_->addItem(label, dir);
}

double WannierDialog::isovalueFromSlider() const
{
    return isoSlider_->value() / 1000.0 * fieldMax_;
}

void WannierDialog::computeMlwf()
{
    if (!structure_ || structure_->empty()) {
        QMessageBox::information(this, windowTitle(),
                                 tr("Open a structure first."));
        return;
    }
    Q_EMIT runRequested(generateScript(),
                        tr("Maximally Localized Wannier Functions"));
    QMessageBox::information(
        this, windowTitle(),
        tr("MLWF job launched. When it completes, click \"Load results…\" and "
           "select the wannier.json in its task directory."));
}

void WannierDialog::loadResultsDialog()
{
    const QString path = QFileDialog::getOpenFileName(
        this, tr("Load Wannier results"), QString(),
        tr("Wannier results (wannier.json *.json);;All files (*)"));
    if (!path.isEmpty())
        loadResults(path);
}

void WannierDialog::loadResults(const QString& jsonPath)
{
    QFile file(jsonPath);
    if (!file.open(QIODevice::ReadOnly)) {
        QMessageBox::warning(this, windowTitle(),
                             tr("Could not open %1").arg(jsonPath));
        return;
    }
    const QJsonObject root = QJsonDocument::fromJson(file.readAll()).object();
    const QJsonArray centers = root.value(QStringLiteral("centers")).toArray();
    if (centers.isEmpty()) {
        QMessageBox::warning(this, windowTitle(),
                             tr("No 'centers' array found in %1").arg(jsonPath));
        return;
    }
    const QJsonArray spreads = root.value(QStringLiteral("spreads")).toArray();

    // The cubes live alongside the JSON; remember that directory so the orbital
    // selector can resolve wannier_<n>.cube.
    jobDir_ = QFileInfo(jsonPath).absolutePath();

    table_->setRowCount(centers.size());
    for (int row = 0; row < centers.size(); ++row) {
        const QJsonArray c = centers.at(row).toArray();
        const auto cell = [&](int col, const QString& text) {
            table_->setItem(row, col, new QTableWidgetItem(text));
        };
        cell(0, QString::number(row));
        for (int k = 0; k < 3; ++k)
            cell(k + 1, QString::number(c.at(k).toDouble(), 'f', 4));
        const bool hasSpread = row < spreads.size();
        cell(4, hasSpread ? QString::number(spreads.at(row).toDouble(), 'f', 4)
                          : QStringLiteral("—"));
    }

    if (root.contains(QStringLiteral("total_spread"))) {
        const double omega = root.value(QStringLiteral("total_spread")).toDouble();
        totalSpreadLabel_->setText(
            tr("Total spread Ω: %1 Å²").arg(omega, 0, 'f', 4));
    } else {
        totalSpreadLabel_->setText(tr("Total spread Ω: —"));
    }

    // Repopulate the orbital selector; block signals while filling, then load
    // the first orbital explicitly (setCurrentIndex may not emit if it stays 0).
    {
        const QSignalBlocker blocker(orbitalCombo_);
        orbitalCombo_->clear();
        for (int i = 0; i < centers.size(); ++i)
            orbitalCombo_->addItem(tr("Wannier %1").arg(i), i);
    }
    if (orbitalCombo_->count() > 0) {
        orbitalCombo_->setCurrentIndex(0);
        orbitalSelected(0);
    }
}

void WannierDialog::orbitalSelected(int index)
{
    if (index < 0 || jobDir_.isEmpty())
        return;
    const QString path =
        QStringLiteral("%1/wannier_%2.cube").arg(jobDir_).arg(index);
    if (!QFileInfo::exists(path)) {
        gridLabel_->setText(tr("Missing %1").arg(QFileInfo(path).fileName()));
        return;
    }
    loadCube(path);
}

void WannierDialog::loadCube(const QString& path)
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

    // Wannier amplitudes are signed; scale the isovalue slider to the peak
    // magnitude of this cube so its full dynamic range is reachable.
    fieldMax_ = std::max({std::abs(field_->minValue()),
                          std::abs(field_->maxValue()), 1e-12});
    {
        const QSignalBlocker sBlocker(isoSpin_);
        isoSpin_->setRange(0.0, fieldMax_);
        isoSpin_->setSingleStep(fieldMax_ / 100.0);
        isoSpin_->setValue(isovalueFromSlider());
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

void WannierDialog::rebuildIso()
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

void WannierDialog::startIsoExtraction()
{
    // One at a time: a slider drag would otherwise spawn an extraction per pixel
    // of travel. The newest request wins when the current one lands.
    if (!isoRequestPending_ || isoWatcher_.isRunning())
        return;
    isoRequestPending_ = false;
    isoRunningGeneration_ = ++isoGeneration_;

    // Capture a shared_ptr copy (a refcount bump, not a grid copy) so the
    // worker's input stays alive and unchanged even if the user loads a new
    // orbital while it runs.
    FieldPtr field = field_;
    const double isovalue = isoSpin_->value();

    isoWatcher_.setFuture(QtConcurrent::run(
        [field = std::move(field), isovalue] {
            return core::extractIsosurface(*field, isovalue, nullptr);
        }));
}

void WannierDialog::onIsoExtractionFinished()
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

void WannierDialog::rebuildSlice()
{
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

QString WannierDialog::generateScript() const
{
    // Preamble mirrors ElfDialog/PartialChargeDialog: JSON/np helpers, the ASE
    // reader and the CalangoLog progress logger the controller scrapes.
    const QString preamble = QStringLiteral(
        "import json\n"
        "import os\n"
        "import glob\n"
        "import numpy as np\n"
        "from ase.io import read\n"
        "from calango_log import CalangoLog\n"
        "_log = CalangoLog()\n");

    // Wavefunction acquisition. Either restart GPAW from a baseline process's
    // .gpw, or — when none is selected — run a fresh SCF. Both define `calc`
    // (a GPAW calculator holding the Bloch wavefunctions) and `atoms`.
    const QString baseline =
        baselineCombo_ ? baselineCombo_->currentData().toString() : QString();
    QString acquisition;
    if (!baseline.isEmpty()) {
        acquisition = QStringLiteral(
            "_base = r\"%1\"\n"
            "_gpw = sorted(glob.glob(os.path.join(_base, '*.gpw')))\n"
            "if not _gpw:\n"
            "    raise RuntimeError('No GPAW wavefunction (.gpw) found in '\n"
            "                       + _base + '. The MLWF localization needs '\n"
            "                       'the Bloch wavefunctions — re-run the '\n"
            "                       'single-point with '\n"
            "                       \"calc.write('single_point.gpw', \"\n"
            "                       \"mode='all').\")\n"
            "from gpaw import GPAW\n"
            "calc = GPAW(_gpw[0], txt='gpaw_wannier.txt')\n"
            "atoms = calc.get_atoms()\n"
            "_log.progress(1, 3)\n").arg(baseline);
    } else {
        acquisition = QStringLiteral(
            "atoms = read('structure.extxyz')\n"
            "_log.progress(1, 3)\n"
            "from gpaw import GPAW, PW\n"
            "# symmetry='off' — ASE's Wannier requires the full (unsymmetrized)\n"
            "# Brillouin zone; it raises otherwise.\n"
            "calc = GPAW(mode=PW(500), xc='PBE', kpts=(7, 7, 7),\n"
            "            symmetry='off', txt='gpaw.txt')\n"
            "atoms.calc = calc\n"
            "atoms.get_potential_energy()\n"
            "calc.write('wannier.gpw', mode='all')\n"
            "_log.progress(2, 3)\n");
    }

    const QString nWan = QString::number(nWannier_ ? nWannier_->value() : 4);

    // Map the (untranslated) projection key onto ASE's `initialwannier` arg.
    // The atomic sets fall back to 'orbitals', from which ASE derives the
    // projections; 'bloch' / 'random' pass straight through.
    const QString key =
        projectionCombo_ ? projectionCombo_->currentData().toString()
                         : QStringLiteral("orbitals");
    const QString init = (key == QLatin1String("bloch")
                          || key == QLatin1String("random"))
                             ? key
                             : QStringLiteral("orbitals");

    // Marzari-Vanderbilt localization via ASE. Guard the import so a missing
    // ase.dft.wannier surfaces a clear error rather than a bare ImportError.
    const QString compute =
        QStringLiteral(
            "try:\n"
            "    from ase.dft.wannier import Wannier\n"
            "except Exception as _e:\n"
            "    raise RuntimeError('Maximally Localized Wannier Functions '\n"
            "                       'require ase.dft.wannier, which is not '\n"
            "                       'available in this ASE install: ' "
            "+ repr(_e))\n"
            "nwannier = %1\n"
            "# initialwannier='orbitals' seeds the trial projections from the\n"
            "# atomic orbitals (good initial overlaps + centre estimates).\n"
            "wan = Wannier(nwannier=nwannier, calc=calc,\n"
            "              initialwannier='%2')\n"
            "# Iterative Marzari-Vanderbilt minimization (Omega = Omega_I +\n"
            "# Omega_tilde_D). Repeat localize() until the spread functional\n"
            "# stops decreasing, so convergence of Omega is guaranteed rather\n"
            "# than relying on a single fixed-iteration call.\n"
            "_prev = None\n"
            "for _it in range(50):\n"
            "    wan.localize(step=0.25, tolerance=1e-6)\n"
            "    _val = float(wan.get_functional_value())\n"
            "    if _prev is not None and abs(_val - _prev) < 1e-6:\n"
            "        break\n"
            "    _prev = _val\n"
            "_log.progress(3, 3)\n")
            .arg(nWan, init);

    // Evaluation. Centres are in Å; the total spread comes from the localization
    // functional. Per-orbital spreads use get_radii() (Å) squared as an Ω-in-Å²
    // proxy; every optional API is guarded so version drift degrades gracefully.
    const QString evaluate = QStringLiteral(
        "centers = np.asarray(wan.get_centers(), dtype=float)\n"
        "# Per-Wannier spread Ω_n (Å²): get_spreads() is the physical spread;\n"
        "# get_radii() returns the (often zero) trial-projection radii and\n"
        "# get_functional_value() is the localization functional, not Ω.\n"
        "try:\n"
        "    spreads = [float(s) for s in np.asarray(wan.get_spreads(),\n"
        "                                            dtype=float)]\n"
        "except Exception:\n"
        "    try:\n"
        "        _radii = np.asarray(wan.get_radii(), dtype=float)\n"
        "        spreads = [float(r * r) for r in _radii]\n"
        "    except Exception:\n"
        "        spreads = [float('nan')] * nwannier\n"
        "# Total spread Ω = Σ_n Ω_n.\n"
        "total_spread = float(np.nansum(spreads))\n"
        "cubes = []\n"
        "for _i in range(nwannier):\n"
        "    _fn = 'wannier_%d.cube' % _i\n"
        "    wan.write_cube(_i, _fn)\n"
        "    cubes.append(_fn)\n");

    // Write wannier.json and emit the result marker the controller watches for.
    const QString tail = QStringLiteral(
        "result = {\n"
        "    'total_spread': total_spread,\n"
        "    'centers': [[float(v) for v in row] for row in centers],\n"
        "    'spreads': [float(s) for s in spreads],\n"
        "    'cubes': cubes,\n"
        "}\n"
        "json.dump(result, open('wannier.json', 'w'), indent=2)\n"
        "print('CALANGO_RESULT wannier=wannier.json', flush=True)\n");

    return preamble + acquisition + compute + evaluate + tail;
}

} // namespace calango::gui
