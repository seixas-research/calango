#include "gui/PartialChargeDialog.hpp"

#include "core/AseScriptGenerator.hpp"
#include "gui/ViewportWidget.hpp"
#include "render/ColorMap.hpp"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QGroupBox>
#include <QRadioButton>
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

#include <algorithm>
#include <cmath>
#include <map>

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

    // Density source: the completed process whose charge density feeds the
    // partitioning. The engine is auto-detected from that directory; the
    // density is standardized to a common 3D grid before partitioning.
    baselineCombo_ = new QComboBox(this);
    baselineCombo_->addItem(tr("(none — run a fresh GPAW single-point)"),
                            QString());
    baselineCombo_->setToolTip(
        tr("Pick the process / workspace tab that holds the calculated charge "
           "density (GPAW .gpw, a .cube, …). Its engine is detected "
           "automatically and the density is converted to a unified grid."));
    form->addRow(tr("Charge-density source:"), baselineCombo_);

    methodCombo_ = new QComboBox(this);
    methodCombo_->addItem(tr("Bader (grid zero-flux basins)"),
                          static_cast<int>(Method::Bader));
    methodCombo_->addItem(tr("Voronoi (density partition)"),
                          static_cast<int>(Method::Voronoi));
    methodCombo_->addItem(tr("Hirshfeld (stockholder)"),
                          static_cast<int>(Method::Hirshfeld));
    methodCombo_->setToolTip(
        tr("Bader: topological basins of the total electron density.\n"
           "Voronoi: density integrated in each atom's nearest-neighbor cell.\n"
           "Hirshfeld: stockholder partitioning against promolecule densities.\n"
           "All three run on the same standardized density grid.\n\n"
           "Bader needs the ALL-ELECTRON density: partitioned on the "
           "pseudo-valence density alone its basins follow the wrong topology "
           "and the charges come out systematically small. For VASP that "
           "means the run must have written AECCAR0/AECCAR2 "
           "(LAECHG = .TRUE.)."));
    form->addRow(tr("Partitioning scheme:"), methodCombo_);

    // Which acquisition path the script takes. Auto covers the normal case;
    // the explicit entries exist for a directory holding output from more
    // than one engine, where guessing would be a coin toss.
    engineCombo_ = new QComboBox(this);
    engineCombo_->addItem(tr("Auto-detect from the process directory"),
                          static_cast<int>(Engine::Auto));
    engineCombo_->addItem(tr("GPAW — .gpw restart file"),
                          static_cast<int>(Engine::Gpaw));
    engineCombo_->addItem(tr("VASP — CHGCAR / AECCAR0 + AECCAR2"),
                          static_cast<int>(Engine::Vasp));
    engineCombo_->addItem(tr("Quantum ESPRESSO — pp.x density export"),
                          static_cast<int>(Engine::Espresso));
    engineCombo_->setToolTip(
        tr("Only the way the density is READ changes with the engine. The "
           "three partitioning schemes are native and operate on a "
           "standardized grid, so they know nothing about which code produced "
           "it.\n\n"
           "Quantum ESPRESSO stores its density in a binary format inside the "
           ".save directory, so the script runs pp.x (plot_num = 0) to export "
           "a Gaussian cube first — pp.x must be on PATH, or set "
           "CALANGO_PP_X to point at it."));
    form->addRow(tr("Density format:"), engineCombo_);

    layout->addLayout(form);

    // -- Scope --------------------------------------------------------------
    auto* scopeGroup = new QGroupBox(tr("Scope"), this);
    auto* scopeLayout = new QVBoxLayout(scopeGroup);
    scopeCurrentRadio_ =
        new QRadioButton(tr("Current structure only"), scopeGroup);
    scopeCurrentRadio_->setChecked(true);
    scopeTrajectoryRadio_ =
        new QRadioButton(tr("All structures in the trajectory"), scopeGroup);
    scopeTrajectoryRadio_->setToolTip(
        tr("Partition every frame of the active trajectory.\n\n"
           "This is NOT a loop over one density: a charge is a property of a "
           "converged electron density, so each frame needs its own. The run "
           "therefore only produces per-frame charges against a source that "
           "actually dumped a density per frame; where it finds only one, it "
           "says so and partitions that one rather than quietly reusing it as "
           "though it described every geometry."));
    scopeLayout->addWidget(scopeCurrentRadio_);
    scopeLayout->addWidget(scopeTrajectoryRadio_);
    scopeNote_ = new QLabel(scopeGroup);
    scopeNote_->setWordWrap(true);
    scopeNote_->setStyleSheet(QStringLiteral("color: palette(mid);"));
    scopeLayout->addWidget(scopeNote_);
    layout->addWidget(scopeGroup);
    for (QRadioButton* radio : {scopeCurrentRadio_, scopeTrajectoryRadio_})
        connect(radio, &QRadioButton::toggled, this,
                [this](bool) { setTrajectoryFrameCount(frameCount_); });
    setTrajectoryFrameCount(frameCount_);

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

    // The numbers a reader actually needs, above the per-atom table. The NET
    // charge first: for a neutral cell it must come out near zero, and a
    // partitioning that lost electrons to the grid says so here before anyone
    // reads a single per-atom value as chemistry.
    chargeSummary_ = new QLabel(this);
    chargeSummary_->setWordWrap(true);
    chargeSummary_->setTextFormat(Qt::RichText);
    layout->addWidget(chargeSummary_);

    colorCheck_ = new QCheckBox(tr("Color atoms in the 3D viewport by charge"), this);
    colorCheck_->setChecked(true);
    layout->addWidget(colorCheck_);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    auto* loadButton =
        buttons->addButton(tr("Load Results…"), QDialogButtonBox::ActionRole);
    connect(loadButton, &QPushButton::clicked, this, &PartialChargeDialog::loadResults);
    applyButton_ = buttons->addButton(tr("Write into Trajectory"),
                                      QDialogButtonBox::ApplyRole);
    applyButton_->setEnabled(false);
    applyButton_->setToolTip(
        tr("Store the loaded charges on the structure as `initial_charges` — "
           "the array name ASE reads and writes — so saving the document as "
           "extended XYZ carries them as a column of the file.\n\n"
           "The edit goes through the document's undo stack like any other."));
    connect(applyButton_, &QPushButton::clicked, this,
            &PartialChargeDialog::applyToTrajectory);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);

    refreshChargeSummary();
}

void PartialChargeDialog::setTrajectoryFrameCount(int frames)
{
    frameCount_ = std::max(1, frames);
    if (!scopeTrajectoryRadio_)
        return;
    const bool multiFrame = frameCount_ > 1;
    scopeTrajectoryRadio_->setEnabled(multiFrame);
    if (!multiFrame && scopeTrajectoryRadio_->isChecked())
        scopeCurrentRadio_->setChecked(true);
    if (!scopeNote_)
        return;
    if (!multiFrame)
        scopeNote_->setText(
            tr("This document holds a single structure, so there is no "
               "trajectory to sweep."));
    else if (wholeTrajectory())
        scopeNote_->setText(
            tr("%1 frames. Each needs its own converged density: the script "
               "looks for one density file per frame in the source directory "
               "and reports what it found.")
                .arg(frameCount_));
    else
        scopeNote_->setText(
            tr("%1 frames available; only the displayed one is partitioned.")
                .arg(frameCount_));
}

bool PartialChargeDialog::wholeTrajectory() const
{
    return scopeTrajectoryRadio_ && scopeTrajectoryRadio_->isChecked()
        && scopeTrajectoryRadio_->isEnabled();
}

PartialChargeDialog::Engine PartialChargeDialog::currentEngine() const
{
    return engineCombo_
        ? static_cast<Engine>(engineCombo_->currentData().toInt())
        : Engine::Auto;
}

void PartialChargeDialog::refreshChargeSummary()
{
    if (!chargeSummary_)
        return;
    if (charges_.empty()) {
        chargeSummary_->setText(
            tr("<i>No results loaded yet.</i> Run the analysis, then load its "
               "partial_charges.json."));
        return;
    }

    double net = 0.0;
    double lowest = charges_.front();
    double highest = charges_.front();
    for (const double q : charges_) {
        net += q;
        lowest = std::min(lowest, q);
        highest = std::max(highest, q);
    }

    // Per-element means: the number anyone actually quotes from a
    // partitioning ("the oxygens carry -1.1 e"), and the one a per-atom table
    // of 200 rows does not give up easily.
    std::map<QString, QPair<double, int>> perElement;
    if (structure_ && structure_->size() == charges_.size())
        for (std::size_t i = 0; i < charges_.size(); ++i) {
            const QString symbol =
                QLatin1String(structure_->atoms()[i].symbol());
            perElement[symbol].first += charges_[i];
            perElement[symbol].second += 1;
        }
    QStringList means;
    for (const auto& [symbol, sum] : perElement)
        means << tr("%1 %2 e")
                     .arg(symbol)
                     .arg(sum.first / std::max(1, sum.second), 0, 'f', 3);

    QString text = tr("<b>Net charge %1 e</b> over %2 atoms; range %3 … %4 e.")
                       .arg(net, 0, 'f', 4)
                       .arg(static_cast<int>(charges_.size()))
                       .arg(lowest, 0, 'f', 3)
                       .arg(highest, 0, 'f', 3);
    if (!means.isEmpty())
        text += tr("<br>Mean per element: %1.").arg(means.join(QStringLiteral(", ")));
    // A neutral cell must partition to zero. Anything else is electrons the
    // integration lost or double-counted, and it bounds how much any single
    // per-atom number can be trusted.
    if (std::abs(net) > 0.05)
        text += tr("<br><span style='color:#b9770e'>⚠ The net charge should be "
                   "≈ 0 for a neutral cell. A residual this large is density "
                   "the integration lost — usually too coarse a grid, or a "
                   "Bader run given only the pseudo-valence density.</span>");
    chargeSummary_->setText(text);
}

void PartialChargeDialog::applyToTrajectory()
{
    if (charges_.empty() || !structure_
        || charges_.size() != structure_->size()) {
        QMessageBox::information(
            this, windowTitle(),
            tr("Load a result set whose atom count matches this structure "
               "first."));
        return;
    }
    QVector<double> values;
    values.reserve(static_cast<int>(charges_.size()));
    for (const double q : charges_)
        values.append(q);
    Q_EMIT chargesApplied(values, wholeTrajectory());
    QMessageBox::information(
        this, windowTitle(),
        wholeTrajectory()
            ? tr("Charges stored as `initial_charges` on every frame. Save the "
                 "document as .extxyz to write them out as a column.")
            : tr("Charges stored as `initial_charges` on the current frame. "
                 "Save the document as .extxyz to write them out as a "
                 "column."));
}

PartialChargeDialog::Method PartialChargeDialog::currentMethod() const
{
    return static_cast<Method>(methodCombo_->currentData().toInt());
}

void PartialChargeDialog::setDensityBaselines(
    const QList<QPair<QString, QString>>& baselines)
{
    if (!baselineCombo_)
        return;
    for (const auto& [label, dir] : baselines)
        baselineCombo_->addItem(label, dir);
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

    // A trajectory-scope run reports every frame; the table shows the first
    // and the count says how many there are, so a per-frame result is never
    // mistaken for a single one.
    const QJsonArray frames =
        doc.object().value(QStringLiteral("frames")).toArray();

    table_->setRowCount(charges.size());
    std::vector<double> scalarField(structure_ ? structure_->size() : 0, 0.0);
    charges_.assign(static_cast<std::size_t>(charges.size()), 0.0);
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
        charges_[static_cast<std::size_t>(row)] = q;
    }

    // Colour-map the viewport by charge via a per-atom scalar field.
    // `initial_charges` is the array name ASE reads and writes, so the same
    // field that tints the viewport is the one an extended-XYZ save emits —
    // rather than a display-only copy under a second name that would then
    // disagree with the file.
    if (colorCheck_->isChecked() && viewport_ && structure_
        && scalarField.size() == structure_->size()) {
        structure_->setScalarField("initial_charges", scalarField);
        viewport_->setColorMode(render::ColorMode::CustomScalar,
                                QStringLiteral("initial_charges"));
    }

    if (applyButton_)
        applyButton_->setEnabled(structure_
                                 && charges_.size() == structure_->size());
    refreshChargeSummary();
    if (!frames.isEmpty() && chargeSummary_)
        chargeSummary_->setText(
            chargeSummary_->text()
            + tr("<br>%1 frames in this result set; the table shows frame 0.")
                  .arg(frames.size()));
}

QString PartialChargeDialog::densityAcquisition() const
{
    const QString baseline =
        baselineCombo_ ? baselineCombo_->currentData().toString() : QString();
    if (baseline.isEmpty())
        return QStringLiteral(
            "# No source process selected: converge a fresh GPAW ground state\n"
            "# here so the partitioning has an all-electron density to work on.\n"
            "atoms = read('structure.extxyz')\n"
            "_calango_progress(1, 3)\n"
            "from gpaw import GPAW, PW\n"
            "calc = GPAW(mode=PW(500), xc='PBE', kpts=(7, 7, 7), txt='gpaw.txt')\n"
            "atoms.calc = calc\n"
            "atoms.get_potential_energy()\n"
            "rho = np.ascontiguousarray(\n"
            "    calc.get_all_electron_density(gridrefinement=2), dtype=float)\n"
            "_frames = [(atoms, rho)]\n"
            "_calango_progress(2, 3)\n");

    // One reader per engine, each defining `atoms` and a 3D `rho`. They are
    // emitted as functions rather than inline branches because the trajectory
    // scope calls them once per density file.
    const QString readers = QStringLiteral(
        "def _read_gpaw(path):\n"
        "    from gpaw import GPAW\n"
        "    calc = GPAW(path, txt=None)\n"
        "    # All-electron, not the pseudo density: Bader's basins follow the\n"
        "    # topology of the full density and are placed wrongly without the\n"
        "    # core contribution.\n"
        "    return (calc.get_atoms(),\n"
        "            np.ascontiguousarray(\n"
        "                calc.get_all_electron_density(gridrefinement=2),\n"
        "                dtype=float))\n"
        "\n"
        "\n"
        "def _read_cube(path):\n"
        "    from ase.io.cube import read_cube\n"
        "    with open(path) as _fh:\n"
        "        _cd = read_cube(_fh)\n"
        "    return _cd['atoms'], np.ascontiguousarray(_cd['data'], dtype=float)\n"
        "\n"
        "\n"
        "def _read_vasp(directory):\n"
        "    \"\"\"CHGCAR, or AECCAR0 + AECCAR2 when the run wrote them.\n"
        "\n"
        "    VASP's CHGCAR holds the PSEUDO valence density. Bader partitioned\n"
        "    on it alone puts the zero-flux surfaces in the wrong place and\n"
        "    returns charges that are systematically too small, which is why\n"
        "    the all-electron reconstruction AECCAR0 (core) + AECCAR2\n"
        "    (valence) is preferred whenever LAECHG wrote them.\n"
        "\n"
        "    VaspChargeDensity returns the density multiplied by the cell\n"
        "    volume, which is VASP's own file convention; dividing it out gives\n"
        "    the electrons/A^3 every scheme downstream assumes.\n"
        "    \"\"\"\n"
        "    from ase.calculators.vasp import VaspChargeDensity\n"
        "    core_file = os.path.join(directory, 'AECCAR0')\n"
        "    val_file = os.path.join(directory, 'AECCAR2')\n"
        "    if os.path.exists(core_file) and os.path.exists(val_file):\n"
        "        _c = VaspChargeDensity(core_file)\n"
        "        _v = VaspChargeDensity(val_file)\n"
        "        atoms = _v.atoms[-1]\n"
        "        grid = np.asarray(_c.chg[-1], dtype=float) \\\n"
        "            + np.asarray(_v.chg[-1], dtype=float)\n"
        "        _calango_event('info', 'VASP all-electron density '\n"
        "                               '(AECCAR0 + AECCAR2)')\n"
        "    else:\n"
        "        chg = os.path.join(directory, 'CHGCAR')\n"
        "        if not os.path.exists(chg):\n"
        "            raise RuntimeError('No CHGCAR in ' + directory\n"
        "                + '. Re-run VASP with LCHARG = .TRUE.')\n"
        "        _d = VaspChargeDensity(chg)\n"
        "        atoms = _d.atoms[-1]\n"
        "        grid = np.asarray(_d.chg[-1], dtype=float)\n"
        "        _calango_event('warning',\n"
        "                       'Only CHGCAR found: this is the PSEUDO valence '\n"
        "                       'density. Bader charges from it are '\n"
        "                       'systematically small — re-run with '\n"
        "                       'LAECHG = .TRUE. for AECCAR0/AECCAR2.')\n"
        "    return atoms, np.ascontiguousarray(grid, dtype=float)\n"
        "\n"
        "\n"
        "def _read_espresso(directory):\n"
        "    \"\"\"Export the density with pp.x and read the cube it writes.\n"
        "\n"
        "    QE keeps the density in a binary/HDF5 file inside the .save\n"
        "    directory with no public reader, so the only supported route is\n"
        "    pp.x (plot_num = 0, iflag = 3, output_format = 6).\n"
        "    \"\"\"\n"
        "    import subprocess\n"
        "    existing = sorted(glob.glob(os.path.join(directory, '*.cube')))\n"
        "    if existing:\n"
        "        return _read_cube(existing[0])\n"
        "    saves = sorted(glob.glob(os.path.join(directory, '*.save')))\n"
        "    if not saves:\n"
        "        raise RuntimeError('No Quantum ESPRESSO .save directory in '\n"
        "                           + directory)\n"
        "    prefix = os.path.basename(saves[0])[:-len('.save')]\n"
        "    cube = os.path.join(directory, prefix + '_rho.cube')\n"
        "    pp_input = os.path.join(directory, 'calango_pp.in')\n"
        "    with open(pp_input, 'w') as _fh:\n"
        "        _fh.write('&INPUTPP\\n')\n"
        "        _fh.write(\"  prefix = '%s'\\n\" % prefix)\n"
        "        _fh.write(\"  outdir = '%s'\\n\" % directory)\n"
        "        _fh.write('  plot_num = 0\\n')\n"
        "        _fh.write('/\\n&PLOT\\n')\n"
        "        _fh.write('  iflag = 3\\n')\n"
        "        _fh.write('  output_format = 6\\n')\n"
        "        _fh.write(\"  fileout = '%s'\\n\" % cube)\n"
        "        _fh.write('/\\n')\n"
        "    exe = os.environ.get('CALANGO_PP_X', 'pp.x')\n"
        "    with open(pp_input) as _in:\n"
        "        result = subprocess.run([exe], stdin=_in, capture_output=True,\n"
        "                                text=True)\n"
        "    if result.returncode != 0 or not os.path.exists(cube):\n"
        "        raise RuntimeError('pp.x failed to export the density (%s). '\n"
        "                           'Set CALANGO_PP_X to its full path, or run '\n"
        "                           'it by hand and leave the .cube in the '\n"
        "                           'process folder.\\n%s'\n"
        "                           % (exe, result.stderr[-2000:]))\n"
        "    return _read_cube(cube)\n"
        "\n"
        "\n");

    // Engine dispatch. Auto sniffs the directory; the explicit choices commit
    // to one reader so a directory carrying output from two engines is not
    // resolved by whichever glob happens to fire first.
    QString dispatch = QStringLiteral("_base = r\"%1\"\n").arg(baseline);
    switch (currentEngine()) {
    case Engine::Gpaw:
        dispatch += QStringLiteral(
            "_sources = sorted(glob.glob(os.path.join(_base, '*.gpw')))\n"
            "_engine = 'GPAW'\n"
            "_load = _read_gpaw\n");
        break;
    case Engine::Vasp:
        dispatch += QStringLiteral(
            "_sources = [_base]\n"
            "_engine = 'VASP'\n"
            "_load = _read_vasp\n");
        break;
    case Engine::Espresso:
        dispatch += QStringLiteral(
            "_sources = [_base]\n"
            "_engine = 'Quantum ESPRESSO'\n"
            "_load = _read_espresso\n");
        break;
    case Engine::Auto:
        dispatch += QStringLiteral(
            "if sorted(glob.glob(os.path.join(_base, '*.gpw'))):\n"
            "    _sources = sorted(glob.glob(os.path.join(_base, '*.gpw')))\n"
            "    _engine, _load = 'GPAW', _read_gpaw\n"
            "elif (os.path.exists(os.path.join(_base, 'CHGCAR'))\n"
            "      or os.path.exists(os.path.join(_base, 'AECCAR2'))):\n"
            "    _sources, _engine, _load = [_base], 'VASP', _read_vasp\n"
            "elif sorted(glob.glob(os.path.join(_base, '*.save'))):\n"
            "    _sources = [_base]\n"
            "    _engine, _load = 'Quantum ESPRESSO', _read_espresso\n"
            "elif sorted(glob.glob(os.path.join(_base, '*.cube'))):\n"
            "    _sources = sorted(glob.glob(os.path.join(_base, '*.cube')))\n"
            "    _engine, _load = 'cube', _read_cube\n"
            "elif glob.glob(os.path.join(_base, '*.RHO*')):\n"
            "    raise RuntimeError('Detected a SIESTA run in ' + _base\n"
            "        + '. Convert its .RHO grid to a .cube (grid2cube / "
            "rho2xsf) '\n"
            "        + 'into the process folder, then re-run.')\n"
            "else:\n"
            "    raise RuntimeError('No readable charge density in ' + _base)\n");
        break;
    }

    dispatch += QStringLiteral(
        "if not _sources:\n"
        "    raise RuntimeError('No density file for the selected engine in '\n"
        "                       + _base)\n"
        "print('CALANGO_INFO engine=' + _engine, flush=True)\n");

    if (wholeTrajectory())
        // Every frame needs its OWN density. Where the source produced only
        // one, that is reported rather than papered over by reusing it: a
        // charge is a property of a converged density, and repeating one
        // frame's charges across a trajectory would be an assertion about
        // geometries that were never computed.
        dispatch += QStringLiteral(
            "if len(_sources) < 2:\n"
            "    _calango_event('warning',\n"
            "                   'The source holds one density, not one per '\n"
            "                   'frame, so only that structure is partitioned. '\n"
            "                   'A per-frame result needs a run that dumped a '\n"
            "                   'density per frame.')\n"
            "_frames = [_load(_s) for _s in _sources]\n");
    else
        dispatch += QStringLiteral("_frames = [_load(_sources[0])]\n");

    dispatch += QStringLiteral(
        "atoms, rho = _frames[0]\n"
        "from ase.io.cube import write_cube\n"
        "with open('density.cube', 'w') as _fh:\n"
        "    write_cube(_fh, atoms, data=rho)\n"
        "_calango_progress(1, 3)\n");

    return readers + dispatch;
}

QString PartialChargeDialog::generateScript(Method method) const
{
    const QString preamble = QStringLiteral(
                                 "import json\n"
                                 "import os\n"
                                 "import glob\n"
                                 "import numpy as np\n"
                                 "from ase.io import read\n"
                                 "\n")
        + QString::fromStdString(core::AseScriptGenerator::jsonLoggerPreamble());

    const QString acquisition = densityAcquisition();

    // Shared grid setup: consumes the standardized `rho` grid + `atoms` from the
    // acquisition step. All three schemes are native — they operate directly on
    // the unified density grid, no external executables.
    const QString gridSetup = QStringLiteral(
        "if rho.ndim != 3 or rho.size == 0:\n"
        "    raise RuntimeError('Unexpected density grid shape: %r' % (rho.shape,))\n"
        "ng = rho.shape\n"
        "cell = np.asarray(atoms.get_cell(), dtype=float)\n"
        "inv = np.linalg.inv(cell)\n"
        "dV = atoms.get_volume() / float(rho.size)\n"
        "pos = np.asarray(atoms.get_positions(), dtype=float)\n"
        "zval = atoms.get_atomic_numbers()\n"
        "rflat = rho.reshape(-1)\n"
        "# GPAW real-space grid points sit at i/N along each axis (not centered);\n"
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

    QString tail;
    if (wholeTrajectory()) {
        // The partitioning body is written as top-level statements against
        // `atoms` and `rho`. Rather than maintaining a second, indented copy
        // of all three schemes, it is re-indented once here and dropped into a
        // per-frame loop — one body, one place to fix a bug in it.
        QString indented;
        const QStringList lines = body.split(QLatin1Char('\n'));
        for (const QString& line : lines)
            indented += line.isEmpty() ? QStringLiteral("\n")
                                       : QStringLiteral("    ") + line
                    + QLatin1Char('\n');
        body = QStringLiteral(
                   "_all = []\n"
                   "for _fi, (atoms, rho) in enumerate(_frames):\n")
            + indented
            + QStringLiteral(
                   "    _all.append({'frame': _fi, 'charges': out})\n"
                   "    _calango_progress(2 + _fi, 2 + len(_frames))\n");
        tail = QStringLiteral(
            "json.dump({'method': method, 'charges': _all[0]['charges'],\n"
            "           'frames': _all},\n"
            "          open('partial_charges.json', 'w'), indent=2)\n"
            "print('CALANGO_RESULT partial_charges=partial_charges.json',\n"
            "      flush=True)\n"
            "print('CALANGO_INFO partitioned %d frame(s)' % len(_all),\n"
            "      flush=True)\n");
    } else {
        tail = QStringLiteral(
            "json.dump({'method': method, 'charges': out},\n"
            "          open('partial_charges.json', 'w'), indent=2)\n"
            "print('CALANGO_RESULT partial_charges=partial_charges.json',\n"
            "      flush=True)\n"
            "_calango_progress(3, 3)\n");
    }

    return preamble + acquisition + body + tail;
}

} // namespace calango::gui
