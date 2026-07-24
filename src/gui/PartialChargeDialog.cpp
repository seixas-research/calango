#include "gui/PartialChargeDialog.hpp"

#include "gui/ViewportWidget.hpp"
#include "render/ColorMap.hpp"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFile>
#include <QFileDialog>
#include <QFormLayout>
#include <QHeaderView>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QTableWidget>
#include <QVBoxLayout>

namespace calango::gui {

PartialChargeDialog::PartialChargeDialog(std::shared_ptr<core::Structure> structure,
                                         ViewportWidget* viewport, QWidget* parent)
    : QDialog(parent), structure_(std::move(structure)), viewport_(viewport)
{
    setWindowTitle(tr("Partial Charge Analysis"));
    resize(560, 560);

    auto* layout = new QVBoxLayout(this);

    infoLabel_ = new QLabel(this);
    if (structure_)
        infoLabel_->setText(
            tr("Structure: %1 (%2 atoms)")
                .arg(QString::fromStdString(structure_->chemicalFormula()))
                .arg(structure_->size()));
    layout->addWidget(infoLabel_);

    auto* form = new QFormLayout;
    methodCombo_ = new QComboBox(this);
    methodCombo_->addItem(tr("Bader (bader / pymatgen)"),
                          static_cast<int>(Method::Bader));
    methodCombo_->addItem(tr("Voronoi (density partition)"),
                          static_cast<int>(Method::Voronoi));
    methodCombo_->addItem(tr("Hirshfeld (GPAW)"),
                          static_cast<int>(Method::Hirshfeld));
    methodCombo_->setToolTip(
        tr("Bader: topological basins of the total electron density.\n"
           "Voronoi: density integrated in each atom's nearest-neighbour cell.\n"
           "Hirshfeld: stockholder partitioning against promolecule densities.\n"
           "All three run a GPAW single-point to obtain the density (Bader also "
           "needs the 'bader' executable)."));
    form->addRow(tr("Partitioning scheme:"), methodCombo_);
    layout->addLayout(form);

    auto* runButton = new QPushButton(tr("Generate && Run Analysis"), this);
    runButton->setToolTip(
        tr("Stage and launch the DFT charge-partitioning job. When it finishes, "
           "load its partial_charges.json below."));
    connect(runButton, &QPushButton::clicked, this, &PartialChargeDialog::runAnalysis);
    layout->addWidget(runButton);

    table_ = new QTableWidget(0, 4, this);
    table_->setHorizontalHeaderLabels(
        {tr("Atom #"), tr("Element"), tr("Net charge q (e)"),
         tr("Atomic volume (Å³)")});
    table_->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    layout->addWidget(table_, 1);

    colorCheck_ = new QCheckBox(tr("Colour atoms in the 3D viewport by charge"), this);
    colorCheck_->setChecked(true);
    layout->addWidget(colorCheck_);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    auto* loadButton =
        buttons->addButton(tr("Load Results…"), QDialogButtonBox::ActionRole);
    connect(loadButton, &QPushButton::clicked, this, &PartialChargeDialog::loadResults);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);
}

PartialChargeDialog::Method PartialChargeDialog::currentMethod() const
{
    return static_cast<Method>(methodCombo_->currentData().toInt());
}

void PartialChargeDialog::runAnalysis()
{
    if (!structure_ || structure_->empty()) {
        QMessageBox::information(this, windowTitle(),
                                 tr("Open a structure first."));
        return;
    }
    const Method method = currentMethod();
    const QString label = tr("Partial Charge (%1)").arg(methodCombo_->currentText());
    Q_EMIT runRequested(generateScript(method), label);
    QMessageBox::information(
        this, windowTitle(),
        tr("Charge-partitioning job launched. When it completes, click "
           "\"Load Results…\" and select the partial_charges.json in its "
           "task directory."));
}

void PartialChargeDialog::loadResults()
{
    const QString path = QFileDialog::getOpenFileName(
        this, tr("Load partial_charges.json"), QString(),
        tr("Partial charge results (partial_charges.json *.json)"));
    if (!path.isEmpty())
        loadResultsFile(path);
}

void PartialChargeDialog::loadResultsFile(const QString& path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        QMessageBox::warning(this, windowTitle(),
                             tr("Could not open %1").arg(path));
        return;
    }
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    const QJsonArray charges = doc.object().value(QStringLiteral("charges")).toArray();
    if (charges.isEmpty()) {
        QMessageBox::warning(this, windowTitle(),
                             tr("No 'charges' array found in %1").arg(path));
        return;
    }

    table_->setRowCount(charges.size());
    std::vector<double> scalarField(structure_ ? structure_->size() : 0, 0.0);
    for (int row = 0; row < charges.size(); ++row) {
        const QJsonObject entry = charges.at(row).toObject();
        const int index = entry.value(QStringLiteral("index")).toInt(row);
        const QString element = entry.value(QStringLiteral("element")).toString();
        const double q = entry.value(QStringLiteral("charge")).toDouble();
        const double volume = entry.value(QStringLiteral("volume")).toDouble();
        const auto cell = [&](int col, const QString& text) {
            table_->setItem(row, col, new QTableWidgetItem(text));
        };
        cell(0, QString::number(index));
        cell(1, element);
        cell(2, QString::number(q, 'f', 4));
        cell(3, volume > 0.0 ? QString::number(volume, 'f', 3) : QStringLiteral("—"));
        if (index >= 0 && index < static_cast<int>(scalarField.size()))
            scalarField[static_cast<std::size_t>(index)] = q;
    }

    // Colour-map the viewport by charge via a per-atom scalar field.
    if (colorCheck_->isChecked() && viewport_ && structure_
        && scalarField.size() == structure_->size()) {
        structure_->setScalarField("partial_charge", scalarField);
        viewport_->setColorMode(render::ColorMode::CustomScalar,
                                QStringLiteral("partial_charge"));
    }
}

QString PartialChargeDialog::generateScript(Method method) const
{
    // Shared preamble: read the staged geometry and converge a GPAW density.
    // (structure.extxyz is written into the job directory by the controller.)
    const QString preamble = QStringLiteral(
        "import json\n"
        "import numpy as np\n"
        "from ase.io import read\n"
        "from calango_log import CalangoLog\n"
        "_log = CalangoLog()\n"
        "atoms = read('structure.extxyz')\n"
        "_log.progress(1, 3)\n");

    const QString gpawScf = QStringLiteral(
        "from gpaw import GPAW, PW\n"
        "calc = GPAW(mode=PW(400), xc='PBE', kpts=(3, 3, 3), txt='gpaw.txt')\n"
        "atoms.calc = calc\n"
        "atoms.get_potential_energy()\n"
        "_log.progress(2, 3)\n");

    // Shared grid setup: the all-electron density and per-atom minimum-image
    // Cartesian distances to every grid point. All three schemes are native —
    // they read the GPAW density grid directly, no external executables.
    const QString gridSetup = QStringLiteral(
        "try:\n"
        "    rho = np.ascontiguousarray(\n"
        "        calc.get_all_electron_density(gridrefinement=2), dtype=float)\n"
        "except Exception as _e:\n"
        "    raise RuntimeError('Could not read the GPAW all-electron density: '\n"
        "                       + str(_e))\n"
        "if rho.ndim != 3 or rho.size == 0:\n"
        "    raise RuntimeError('Unexpected density grid shape: %r' % (rho.shape,))\n"
        "ng = rho.shape\n"
        "cell = np.asarray(atoms.get_cell(), dtype=float)\n"
        "inv = np.linalg.inv(cell)\n"
        "dV = atoms.get_volume() / float(rho.size)\n"
        "pos = np.asarray(atoms.get_positions(), dtype=float)\n"
        "zval = atoms.get_atomic_numbers()\n"
        "rflat = rho.reshape(-1)\n"
        "# GPAW real-space grid points sit at i/N along each axis (not centred);\n"
        "# a half-cell offset would mis-assign points to the wrong atom.\n"
        "frac = np.stack(np.meshgrid(\n"
        "    *[np.arange(n) / float(n) for n in ng], indexing='ij'),\n"
        "    axis=-1).reshape(-1, 3)\n"
        "grid = frac @ cell\n"
        "npts = grid.shape[0]\n"
        "CHUNK = 100000  # bound peak memory on fine density grids\n"
        "def _mic_dist(i, lo, hi):\n"
        "    df = (grid[lo:hi] - pos[i]) @ inv\n"
        "    df -= np.round(df)\n"
        "    return np.linalg.norm(df @ cell, axis=1)\n");

    QString body;
    switch (method) {
    case Method::Bader:
        // On-grid (Henkelman) Bader: follow each grid point's steepest-ascent
        // path to a density maximum, group points into basins, assign each
        // basin to its nearest atom, and integrate the density per atom.
        body = gridSetup + QStringLiteral(
            "labels = -np.ones(rho.shape, dtype=np.int64)\n"
            "maxima = []\n"
            "offs = [(a, b, c) for a in (-1, 0, 1) for b in (-1, 0, 1)\n"
            "        for c in (-1, 0, 1) if (a, b, c) != (0, 0, 0)]\n"
            "def _climb(start):\n"
            "    path = []\n"
            "    idx = start\n"
            "    while True:\n"
            "        if labels[idx] >= 0:\n"
            "            basin = labels[idx]; break\n"
            "        path.append(idx)\n"
            "        best, bestv = None, rho[idx]\n"
            "        for o in offs:\n"
            "            n = ((idx[0]+o[0]) % ng[0], (idx[1]+o[1]) % ng[1],\n"
            "                 (idx[2]+o[2]) % ng[2])\n"
            "            if rho[n] > bestv:\n"
            "                bestv, best = rho[n], n\n"
            "        if best is None:\n"
            "            basin = len(maxima); maxima.append(idx)\n"
            "            labels[idx] = basin; break\n"
            "        idx = best\n"
            "    for p in path:\n"
            "        labels[p] = basin\n"
            "    return basin\n"
            "for idx in np.ndindex(rho.shape):\n"
            "    if labels[idx] < 0:\n"
            "        _climb(idx)\n"
            "# Nearest atom (minimum image) for each basin maximum.\n"
            "basin_atom = []\n"
            "for m in maxima:\n"
            "    cart = (np.array(m, dtype=float) / ng) @ cell\n"
            "    df = (cart - pos) @ inv; df -= np.round(df)\n"
            "    basin_atom.append(int(np.argmin(\n"
            "        np.linalg.norm(df @ cell, axis=1))))\n"
            "elec = np.zeros(len(atoms)); vol = np.zeros(len(atoms))\n"
            "for b in range(len(maxima)):\n"
            "    mask = labels == b; a = basin_atom[b]\n"
            "    elec[a] += rho[mask].sum() * dV\n"
            "    vol[a] += int(mask.sum()) * dV\n"
            "out = [{'index': i, 'element': atoms[i].symbol,\n"
            "        'charge': float(zval[i] - elec[i]), 'volume': float(vol[i])}\n"
            "       for i in range(len(atoms))]\n"
            "method = 'Bader'\n");
        break;
    case Method::Hirshfeld:
        // Stockholder partitioning: build a promolecule from isolated spherical
        // atom references (exponential ρ_i^0 ∝ Z·exp(-r/λ), λ from the covalent
        // radius) and weight the density by w_i = ρ_i^0 / Σ_j ρ_j^0.
        body = gridSetup + QStringLiteral(
            "from ase.data import covalent_radii\n"
            "lam = np.array([max(covalent_radii[z], 0.3) for z in zval])\n"
            "elec = np.zeros(len(atoms)); vol = np.zeros(len(atoms))\n"
            "for lo in range(0, npts, CHUNK):\n"
            "    hi = min(lo + CHUNK, npts)\n"
            "    refs = np.stack([zval[i] * np.exp(-_mic_dist(i, lo, hi) / lam[i])\n"
            "                     for i in range(len(atoms))], axis=1)\n"
            "    den = refs.sum(axis=1); den[den == 0.0] = 1e-30\n"
            "    w = refs / den[:, None]\n"
            "    r = rflat[lo:hi]\n"
            "    elec += (w * r[:, None]).sum(axis=0) * dV\n"
            "    vol += w.sum(axis=0) * dV\n"
            "out = [{'index': i, 'element': atoms[i].symbol,\n"
            "        'charge': float(zval[i] - elec[i]), 'volume': float(vol[i])}\n"
            "       for i in range(len(atoms))]\n"
            "method = 'Hirshfeld'\n");
        break;
    case Method::Voronoi:
        // Assign every grid point to its nearest atom (a 3D Voronoi cell over
        // the density grid) and integrate the density in each cell. Chunked so
        // the (grid × atoms) distance matrix never exceeds bounded memory.
        body = gridSetup + QStringLiteral(
            "elec = np.zeros(len(atoms)); vol = np.zeros(len(atoms))\n"
            "for lo in range(0, npts, CHUNK):\n"
            "    hi = min(lo + CHUNK, npts)\n"
            "    d = np.stack([_mic_dist(i, lo, hi) for i in range(len(atoms))],\n"
            "                 axis=1)\n"
            "    owner = np.argmin(d, axis=1)\n"
            "    r = rflat[lo:hi]\n"
            "    for i in range(len(atoms)):\n"
            "        m = owner == i\n"
            "        elec[i] += float(r[m].sum() * dV)\n"
            "        vol[i] += float(int(m.sum()) * dV)\n"
            "out = [{'index': i, 'element': atoms[i].symbol,\n"
            "        'charge': float(zval[i] - elec[i]), 'volume': float(vol[i])}\n"
            "       for i in range(len(atoms))]\n"
            "method = 'Voronoi'\n");
        break;
    }

    const QString tail = QStringLiteral(
        "json.dump({'method': method, 'charges': out},\n"
        "          open('partial_charges.json', 'w'), indent=2)\n"
        "print('CALANGO_RESULT partial_charges=partial_charges.json', flush=True)\n"
        "_log.progress(3, 3)\n");

    return preamble + gpawScf + body + tail;
}

} // namespace calango::gui
