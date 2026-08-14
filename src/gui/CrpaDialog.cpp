#include "gui/CrpaDialog.hpp"

#include "gui/WannierRunLoader.hpp"

#include "core/LocaleSafeNumber.hpp"
#include "gui/GuiUtils.hpp"

#include <QCheckBox>
#include <QFileInfo>
#include <QSignalBlocker>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFile>
#include <QFileDialog>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QTableWidget>
#include <QTextStream>
#include <QVBoxLayout>

#include <cmath>

namespace calango::gui {

namespace {
enum Column { ColLabel = 0, ColCorrelated = 1, ColSpread = 2, ColCentre = 3 };
} // namespace

CrpaDialog::CrpaDialog(QWidget* parent) : QDialog(parent)
{
    setWindowTitle(tr("Constrained RPA — Hubbard U and Hund's J"));
    auto* outer = new QVBoxLayout(this);

    auto* intro = new QLabel(
        tr("Screens the bare Coulomb interaction with every RPA process "
           "<b>except</b> those inside the correlated subspace."),
        this);
    intro->setWordWrap(true);
    intro->setTextFormat(Qt::RichText);
    intro->setToolTip(
        tr("The excluded screening is the part a downstream many-body solver "
           "(DMFT, ED) treats explicitly. Leaving it in W would count it "
           "twice, which is the entire reason cRPA exists.\n\n"
           "Input is Wannier data only: H(R), centres, spreads. No plane-wave "
           "coefficients and no pseudopotentials, so any code that can write a "
           "wannier90 basis can drive this."));
    outer->addWidget(intro);

    // -- Source -------------------------------------------------------------
    // A Wannier run this session completed, first: it is the only source that
    // needs nothing from outside Calango, now that the run writes its own
    // H(R). Before that these panels could be driven only by a wannier90 file
    // or by the demo model — a natively-implemented solver reachable only
    // through the code it was written to replace.
    auto* runRow = new QHBoxLayout;
    runRow->addWidget(new QLabel(tr("From a completed run:"), this));
    runCombo_ = new QComboBox(this);
    runCombo_->addItem(tr("(choose a Wannier run)"), QString());
    runCombo_->setToolTip(
        tr("Take H(R) — and the centres and spreads that fill the table "
           "below — straight from a Wannier Functions run that finished in "
           "this session.\n\n"
           "A run started before Calango wrote H(R) will say so when picked; "
           "re-running the Wannierization on the same baseline produces it."));
    connect(runCombo_, &QComboBox::currentIndexChanged, this,
            &CrpaDialog::runSelected);
    runRow->addWidget(runCombo_, 1);
    outer->addLayout(runRow);

    auto* sourceRow = new QHBoxLayout;
    sourceLabel_ = new QLabel(tr("No Hamiltonian loaded."), this);
    sourceLabel_->setWordWrap(true);
    sourceRow->addWidget(sourceLabel_, 1);
    auto* browse = new QPushButton(tr("Open _hr.dat…"), this);
    connect(browse, &QPushButton::clicked, this,
            &CrpaDialog::browseHamiltonian);
    sourceRow->addWidget(browse);
    auto* demo = new QPushButton(tr("Load demo model"), this);
    demo->setToolTip(
        tr("A three-orbital correlated shell hybridised with one ligand, on a "
           "cubic cell. Enough to exercise the solver and see what the "
           "constraint does without needing a Wannier run first."));
    connect(demo, &QPushButton::clicked, this, &CrpaDialog::loadDemoModel);
    sourceRow->addWidget(demo);
    outer->addLayout(sourceRow);

    // -- Correlated subspace ------------------------------------------------
    auto* subspaceGroup = new QGroupBox(tr("Correlated Subspace"), this);
    auto* subspaceLayout = new QVBoxLayout(subspaceGroup);
    auto* subspaceNote = new QLabel(
        tr("Tick the Wannier orbitals whose interaction you want."),
        subspaceGroup);
    subspaceNote->setWordWrap(true);
    subspaceNote->setToolTip(
        tr("These are the target bands of cRPA. Transitions with both ends "
           "inside this set are removed from the polarizability, weighted by "
           "how much of each band actually lives in the subspace — which is "
           "what makes the constraint well defined for entangled bands."));
    subspaceLayout->addWidget(subspaceNote);

    orbitalTable_ = new QTableWidget(0, 4, subspaceGroup);
    orbitalTable_->setHorizontalHeaderLabels(
        {tr("orbital"), tr("correlated"), tr("spread (Å²)"), tr("centre (Å)")});
    orbitalTable_->horizontalHeader()->setSectionResizeMode(
        QHeaderView::Stretch);
    orbitalTable_->setMaximumHeight(200);
    disableTypeToEdit(orbitalTable_);
    subspaceLayout->addWidget(orbitalTable_);

    shellL_ = new QComboBox(subspaceGroup);
    shellL_->addItem(tr("none — report J from (U − U')/2"), 0);
    shellL_->addItem(tr("p shell (l = 1)"), 1);
    shellL_->addItem(tr("d shell (l = 2)"), 2);
    shellL_->addItem(tr("f shell (l = 3)"), 3);
    shellL_->setCurrentIndex(2);
    shellL_->setToolTip(
        tr("Angular momentum of the correlated shell. It buys exactly one "
           "thing: Hund's J.\n\n"
           "The density-density interaction alone gives J = 0 for a degenerate "
           "shell, because orbitals sharing a centre and a spread have "
           "identical spherical densities. Declaring l applies the exact "
           "Slater-Condon angular algebra on top of the radial density, which "
           "is where a real J comes from."));
    auto* shellForm = new QFormLayout;
    shellForm->addRow(tr("Shell angular momentum:"), shellL_);
    subspaceLayout->addLayout(shellForm);
    outer->addWidget(subspaceGroup);

    // -- Screening ----------------------------------------------------------
    auto* screeningGroup = new QGroupBox(tr("Screening"), this);
    auto* form = new QFormLayout(screeningGroup);

    auto* meshRow = new QHBoxLayout;
    for (int axis = 0; axis < 3; ++axis) {
        kmesh_[axis] = new QSpinBox(screeningGroup);
        kmesh_[axis]->setRange(1, 32);
        kmesh_[axis]->setValue(4);
        meshRow->addWidget(kmesh_[axis]);
    }
    meshRow->addStretch(1);
    form->addRow(tr("k-mesh:"), meshRow);

    screeningCutoff_ = new QDoubleSpinBox(screeningGroup);
    screeningCutoff_->setRange(0.1, 200.0);
    screeningCutoff_->setValue(30.0);
    screeningCutoff_->setDecimals(2);
    screeningCutoff_->setSuffix(tr(" eV"));
    screeningCutoff_->setToolTip(
        tr("Transitions above this energy are dropped from the "
           "polarizability. A convergence parameter, not a physical one — "
           "quote it with every U, and check that U has stopped moving before "
           "believing it."));
    form->addRow(tr("Screening band cutoff:"), screeningCutoff_);

    broadening_ = new QDoubleSpinBox(screeningGroup);
    broadening_->setRange(0.001, 2.0);
    broadening_->setValue(0.05);
    broadening_->setDecimals(3);
    broadening_->setSuffix(tr(" eV"));
    form->addRow(tr("Broadening δ:"), broadening_);

    electrons_ = new QDoubleSpinBox(screeningGroup);
    electrons_->setRange(0.0, 200.0);
    electrons_->setValue(4.0);
    electrons_->setDecimals(3);
    electrons_->setToolTip(
        tr("Electrons per cell in the Wannier basis. Sets the Fermi level, "
           "hence which transitions exist at all."));
    form->addRow(tr("Electrons per cell:"), electrons_);
    outer->addWidget(screeningGroup);

    computeButton_ = new QPushButton(tr("Compute U and J"), this);
    computeButton_->setEnabled(false);
    connect(computeButton_, &QPushButton::clicked, this, &CrpaDialog::compute);
    outer->addWidget(computeButton_);

    report_ = new QPlainTextEdit(this);
    report_->setReadOnly(true);
    report_->setMinimumHeight(220);
    outer->addWidget(report_, 1);

    auto* close = new QPushButton(tr("Close"), this);
    connect(close, &QPushButton::clicked, this, &QDialog::accept);
    auto* closeRow = new QHBoxLayout;
    closeRow->addStretch(1);
    closeRow->addWidget(close);
    outer->addLayout(closeRow);

    resize(820, 780);
}

void CrpaDialog::rebuildTable(std::size_t orbitals)
{
    orbitalTable_->setRowCount(static_cast<int>(orbitals));
    for (std::size_t i = 0; i < orbitals; ++i) {
        const int row = static_cast<int>(i);
        orbitalTable_->setItem(
            row, ColLabel,
            new QTableWidgetItem(tr("w%1").arg(row + 1)));
        auto* correlated = new QTableWidgetItem();
        correlated->setFlags(Qt::ItemIsUserCheckable | Qt::ItemIsEnabled
                             | Qt::ItemIsSelectable);
        correlated->setCheckState(Qt::Unchecked);
        orbitalTable_->setItem(row, ColCorrelated, correlated);
        orbitalTable_->setItem(row, ColSpread,
                               new QTableWidgetItem(QStringLiteral("1.0")));
        orbitalTable_->setItem(
            row, ColCentre, new QTableWidgetItem(QStringLiteral("0.0 0.0 0.0")));
    }
}

void CrpaDialog::loadDemoModel()
{
    clearRunSelection();
    // Three correlated orbitals plus a ligand, hybridised, with dispersion
    // along x. The same model the solver's tests use, so what the dialog shows
    // can be checked against them directly.
    hoppings_.clear();
    cell_ = {{{4.0, 0.0, 0.0}, {0.0, 4.0, 0.0}, {0.0, 0.0, 4.0}}};
    const std::size_t n = 4;

    core::CrpaSolver::HoppingBlock onsite;
    onsite.lattice = {0, 0, 0};
    onsite.matrix.assign(n * n, 0.0);
    onsite.matrix[3 * n + 3] = -3.0;
    for (std::size_t m = 0; m < 3; ++m) {
        onsite.matrix[m * n + 3] = 0.5;
        onsite.matrix[3 * n + m] = 0.5;
    }
    hoppings_.push_back(onsite);
    for (int sign : {-1, 1}) {
        core::CrpaSolver::HoppingBlock block;
        block.lattice = {sign, 0, 0};
        block.matrix.assign(n * n, 0.0);
        for (std::size_t m = 0; m < n; ++m)
            block.matrix[m * n + m] = 0.4;
        hoppings_.push_back(block);
    }

    rebuildTable(n);
    for (int row = 0; row < 3; ++row) {
        orbitalTable_->item(row, ColLabel)->setText(tr("d%1").arg(row + 1));
        orbitalTable_->item(row, ColCorrelated)->setCheckState(Qt::Checked);
        orbitalTable_->item(row, ColSpread)->setText(QStringLiteral("0.6"));
    }
    orbitalTable_->item(3, ColLabel)->setText(tr("p"));
    orbitalTable_->item(3, ColSpread)->setText(QStringLiteral("1.2"));
    orbitalTable_->item(3, ColCentre)->setText(QStringLiteral("2.0 0.0 0.0"));

    sourceLabel_->setText(tr("Built-in demo model: 3 correlated + 1 ligand."));
    computeButton_->setEnabled(true);
}

void CrpaDialog::setWannierRuns(const QList<QPair<QString, QString>>& runs)
{
    if (!runCombo_)
        return;
    const QSignalBlocker blocker(runCombo_);
    runCombo_->clear();
    runCombo_->addItem(runs.isEmpty() ? tr("(no completed Wannier runs)")
                                      : tr("(choose a Wannier run)"),
                       QString());
    for (const auto& [label, dir] : runs)
        runCombo_->addItem(label, dir);
    runCombo_->setEnabled(!runs.isEmpty());
}

void CrpaDialog::runSelected(int index)
{
    if (index <= 0 || !runCombo_)
        return;
    const QString dir = runCombo_->itemData(index).toString();
    if (dir.isEmpty())
        return;
    WannierRunData data;
    QString error;
    if (!loadWannierRun(dir, &data, &error)) {
        QMessageBox::warning(this, tr("Constrained RPA"), error);
        const QSignalBlocker blocker(runCombo_);
        runCombo_->setCurrentIndex(0);
        return;
    }
    // The cell has to be adopted BEFORE the table is built: buildModel() hands
    // it to the solver, and the R vectors of the hopping table are integers in
    // it — with the placeholder cubic cell still in place every screened
    // interaction would be computed at the wrong distances.
    cell_ = data.cell;
    if (!loadHamiltonian(data.hrPath, &error)) {
        QMessageBox::warning(this, tr("Constrained RPA"), error);
        const QSignalBlocker blocker(runCombo_);
        runCombo_->setCurrentIndex(0);
        return;
    }

    // The centres and spreads the run measured, in place of the placeholders
    // rebuildTable() writes. cRPA weights each transition by how much of the
    // band lives in the correlated subspace, and the spreads are what the user
    // reads to decide WHICH orbitals that subspace should hold — so filling
    // them in from the run is the difference between choosing and guessing.
    const int rows = orbitalTable_->rowCount();
    for (int row = 0; row < rows; ++row) {
        if (row < data.spreads.size())
            orbitalTable_->item(row, ColSpread)
                ->setText(QString::number(data.spreads.at(row), 'f', 4));
        if (row < data.centres.size()) {
            const core::Vec3& c = data.centres.at(row);
            orbitalTable_->item(row, ColCentre)
                ->setText(QStringLiteral("%1 %2 %3")
                              .arg(c.x, 0, 'f', 4)
                              .arg(c.y, 0, 'f', 4)
                              .arg(c.z, 0, 'f', 4));
        }
    }
    sourceLabel_->setText(tr("%1 — %2 Wannier functions, H(R) from %3")
                              .arg(runCombo_->itemText(index))
                              .arg(data.nWannier)
                              .arg(QFileInfo(data.hrPath).fileName()));
    computeButton_->setEnabled(true);
}

void CrpaDialog::clearRunSelection()
{
    if (!runCombo_ || runCombo_->currentIndex() == 0)
        return;
    const QSignalBlocker blocker(runCombo_);
    runCombo_->setCurrentIndex(0);
}

void CrpaDialog::browseHamiltonian()
{
    clearRunSelection();
    const QString path = QFileDialog::getOpenFileName(
        this, tr("Open a wannier90 Hamiltonian"), QString(),
        tr("wannier90 Hamiltonian (*_hr.dat);;All files (*)"));
    if (path.isEmpty())
        return;
    QString error;
    if (!loadHamiltonian(path, &error)) {
        QMessageBox::warning(this, tr("Constrained RPA"), error);
        return;
    }
    computeButton_->setEnabled(true);
}

bool CrpaDialog::loadHamiltonian(const QString& path, QString* error)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        if (error)
            *error = tr("Could not open %1.").arg(path);
        return false;
    }
    QTextStream stream(&file);

    // wannier90 _hr.dat: a comment line, the orbital count, the number of
    // Wigner-Seitz points, then that many degeneracies, then one line per
    // (R, i, j) carrying Re and Im of H.
    stream.readLine(); // comment / date
    bool ok = false;
    const int orbitals = stream.readLine().trimmed().toInt(&ok);
    if (!ok || orbitals <= 0) {
        if (error)
            *error = tr("%1 does not start like a wannier90 _hr.dat.").arg(path);
        return false;
    }
    const int points = stream.readLine().trimmed().toInt(&ok);
    if (!ok || points <= 0) {
        if (error)
            *error = tr("Could not read the Wigner-Seitz point count.");
        return false;
    }
    std::vector<int> degeneracy;
    degeneracy.reserve(points);
    while (static_cast<int>(degeneracy.size()) < points && !stream.atEnd()) {
        const QStringList parts =
            stream.readLine().split(QRegularExpression(QStringLiteral("\\s+")),
                                    Qt::SkipEmptyParts);
        for (const QString& part : parts) {
            degeneracy.push_back(part.toInt());
            if (static_cast<int>(degeneracy.size()) == points)
                break;
        }
    }

    std::map<std::array<int, 3>, std::vector<double>> blocks;
    while (!stream.atEnd()) {
        const QStringList parts =
            stream.readLine().split(QRegularExpression(QStringLiteral("\\s+")),
                                    Qt::SkipEmptyParts);
        if (parts.size() < 7)
            continue;
        const std::array<int, 3> r{parts[0].toInt(), parts[1].toInt(),
                                   parts[2].toInt()};
        const int i = parts[3].toInt() - 1;
        const int j = parts[4].toInt() - 1;
        if (i < 0 || j < 0 || i >= orbitals || j >= orbitals)
            continue;
        auto& block = blocks[r];
        if (block.empty())
            block.assign(static_cast<std::size_t>(orbitals) * orbitals, 0.0);
        // Real part only: the solver Hermitises H(k), and a Wannier
        // Hamiltonian of a system with time-reversal symmetry is real in this
        // representation anyway.
        block[static_cast<std::size_t>(i) * orbitals + j] =
            core::localeSafeToDouble(parts[5].toStdString(), 0.0);
    }

    hoppings_.clear();
    for (auto& [r, matrix] : blocks)
        hoppings_.push_back({r, std::move(matrix)});
    if (hoppings_.empty()) {
        if (error)
            *error = tr("No hopping blocks were parsed from %1.").arg(path);
        return false;
    }

    rebuildTable(static_cast<std::size_t>(orbitals));
    sourceLabel_->setText(tr("%1 — %2 orbitals, %3 lattice vectors. "
                             "Set the spreads and centres from the .wout.")
                              .arg(QFileInfo(path).fileName())
                              .arg(orbitals)
                              .arg(hoppings_.size()));
    return true;
}

bool CrpaDialog::buildModel(core::CrpaSolver::Model& model,
                            QString* error) const
{
    if (hoppings_.empty()) {
        if (error)
            *error = tr("Load a Hamiltonian first.");
        return false;
    }
    model.hoppings = hoppings_;
    model.cell = cell_;
    model.electrons = electrons_->value();
    model.orbitals.clear();

    const int shellL = shellL_->currentData().toInt();
    for (int row = 0; row < orbitalTable_->rowCount(); ++row) {
        core::CrpaSolver::Orbital orbital;
        orbital.label =
            orbitalTable_->item(row, ColLabel)->text().toStdString();
        orbital.correlated =
            orbitalTable_->item(row, ColCorrelated)->checkState() == Qt::Checked;
        orbital.spread = core::localeSafeToDouble(
            orbitalTable_->item(row, ColSpread)->text().toStdString(), 1.0);
        if (orbital.correlated)
            orbital.angularL = shellL;

        const QStringList parts =
            orbitalTable_->item(row, ColCentre)
                ->text()
                .split(QRegularExpression(QStringLiteral("[\\s,]+")),
                       Qt::SkipEmptyParts);
        for (int axis = 0; axis < 3 && axis < parts.size(); ++axis)
            orbital.centre[axis] =
                core::localeSafeToDouble(parts[axis].toStdString(), 0.0);
        model.orbitals.push_back(orbital);
    }

    const bool anyCorrelated =
        std::any_of(model.orbitals.begin(), model.orbitals.end(),
                    [](const auto& o) { return o.correlated; });
    if (!anyCorrelated) {
        if (error)
            *error = tr("Tick at least one orbital as correlated — that set is "
                        "what the interaction is computed for.");
        return false;
    }
    return true;
}

void CrpaDialog::compute()
{
    core::CrpaSolver::Model model;
    QString error;
    if (!buildModel(model, &error)) {
        QMessageBox::warning(this, tr("Constrained RPA"), error);
        return;
    }

    core::CrpaSolver::Options options;
    for (int axis = 0; axis < 3; ++axis)
        options.kmesh[axis] = kmesh_[axis]->value();
    options.screeningCutoff = screeningCutoff_->value();
    options.broadening = broadening_->value();

    try {
        core::CrpaSolver solver(model, options);
        const auto result = solver.staticInteraction();

        QString text;
        text += tr("Constrained RPA, static limit W(ω = 0)\n");
        text += QStringLiteral("========================================\n\n");
        text += tr("  U        = %1 eV\n").arg(result.u, 0, 'f', 4);
        text += tr("  U'       = %1 eV\n").arg(result.uPrime, 0, 'f', 4);
        text += tr("  J        = %1 eV   (%2)\n")
                    .arg(result.j, 0, 'f', 4)
                    .arg(result.jFromSlater ? tr("Slater-Condon")
                                            : tr("Kanamori (U − U')/2"));
        text += tr("  U_bare   = %1 eV\n").arg(result.uBare, 0, 'f', 4);
        if (result.uBare > 0.0)
            text += tr("  U/U_bare = %1   (how much the constrained screening "
                       "removed)\n")
                        .arg(result.u / result.uBare, 0, 'f', 3);
        if (result.jFromSlater) {
            text += tr("\n  Slater integrals (bare radial):\n");
            text += tr("    F0 = %1   F2 = %2   F4 = %3 eV\n")
                        .arg(result.slaterF[0], 0, 'f', 3)
                        .arg(result.slaterF[1], 0, 'f', 3)
                        .arg(result.slaterF[2], 0, 'f', 3);
            text += tr("    J from (F2 + F4)/14 for d, F2/5 for p.\n");
        }
        text += tr("\n  Screened matrix over the correlated subspace (eV):\n");
        for (const auto& row : result.screenedMatrix) {
            text += QStringLiteral("    ");
            for (double value : row)
                text += QStringLiteral("%1 ").arg(value, 9, 'f', 4);
            text += QStringLiteral("\n");
        }
        text += tr("\n  k-mesh %1x%2x%3, screening cutoff %4 eV.\n")
                    .arg(options.kmesh[0])
                    .arg(options.kmesh[1])
                    .arg(options.kmesh[2])
                    .arg(options.screeningCutoff, 0, 'f', 1);
        text += tr("  Converge U against BOTH before quoting it.\n");
        report_->setPlainText(text);
    } catch (const std::exception& e) {
        QMessageBox::warning(this, tr("Constrained RPA"),
                             tr("The solver refused this input:\n%1")
                                 .arg(QString::fromUtf8(e.what())));
    }
}

} // namespace calango::gui
