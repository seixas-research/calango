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

    QString body;
    switch (method) {
    case Method::Bader:
        // Total (all-electron) density → cube → the 'bader' executable → ACF.dat.
        body = QStringLiteral(
            "from ase.io.cube import write_cube\n"
            "import subprocess\n"
            "rho = calc.get_all_electron_density(gridrefinement=4)\n"
            "with open('density.cube', 'w') as fh:\n"
            "    write_cube(fh, atoms, data=rho)\n"
            "subprocess.run(['bader', 'density.cube'], check=True)\n"
            "# ACF.dat columns: id X Y Z CHARGE MIN_DIST ATOMIC_VOL\n"
            "acf = np.loadtxt('ACF.dat', skiprows=2, max_rows=len(atoms))\n"
            "zval = atoms.get_atomic_numbers()\n"
            "out = []\n"
            "for i, atom in enumerate(atoms):\n"
            "    q = float(zval[i] - acf[i, 4])  # net charge = Z - Bader pop.\n"
            "    vol = float(acf[i, 6])\n"
            "    out.append({'index': i, 'element': atom.symbol,\n"
            "                'charge': q, 'volume': vol})\n"
            "method = 'Bader'\n");
        break;
    case Method::Hirshfeld:
        body = QStringLiteral(
            "from gpaw.analyse.hirshfeld import HirshfeldPartitioning\n"
            "hp = HirshfeldPartitioning(calc)\n"
            "q = hp.get_charges()\n"
            "try:\n"
            "    vols = hp.get_effective_volume_ratios()\n"
            "except Exception:\n"
            "    vols = [0.0] * len(atoms)\n"
            "out = [{'index': i, 'element': atoms[i].symbol,\n"
            "        'charge': float(q[i]), 'volume': float(vols[i])}\n"
            "       for i in range(len(atoms))]\n"
            "method = 'Hirshfeld'\n");
        break;
    case Method::Voronoi:
        // Assign every real-space grid point of the all-electron density to its
        // nearest atom (a Voronoi partition) and integrate the density in each
        // cell; the cell volume follows from the grid-point count.
        body = QStringLiteral(
            "rho = calc.get_all_electron_density(gridrefinement=2)\n"
            "ng = rho.shape\n"
            "cell = atoms.get_cell()\n"
            "dV = atoms.get_volume() / (ng[0] * ng[1] * ng[2])\n"
            "pos = atoms.get_positions()\n"
            "frac = np.stack(np.meshgrid(\n"
            "    np.arange(ng[0]) / ng[0], np.arange(ng[1]) / ng[1],\n"
            "    np.arange(ng[2]) / ng[2], indexing='ij'), axis=-1)\n"
            "grid = frac @ np.asarray(cell)\n"
            "gflat = grid.reshape(-1, 3)\n"
            "rflat = rho.reshape(-1)\n"
            "elec = np.zeros(len(atoms))\n"
            "vol = np.zeros(len(atoms))\n"
            "# Chunk the nearest-atom search to keep memory bounded.\n"
            "for s in range(0, gflat.shape[0], 200000):\n"
            "    chunk = gflat[s:s + 200000]\n"
            "    d = np.linalg.norm(chunk[:, None, :] - pos[None, :, :], axis=2)\n"
            "    owner = np.argmin(d, axis=1)\n"
            "    for i in range(len(atoms)):\n"
            "        m = owner == i\n"
            "        elec[i] += rflat[s:s + 200000][m].sum() * dV\n"
            "        vol[i] += int(m.sum()) * dV\n"
            "zval = atoms.get_atomic_numbers()\n"
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
