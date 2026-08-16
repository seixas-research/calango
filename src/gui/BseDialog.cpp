#include "gui/BseDialog.hpp"

#include "core/LocaleSafeNumber.hpp"
#include "gui/SpectrumPlotWidget.hpp"
#include "gui/WannierModelSource.hpp"

#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFile>
#include <QFileDialog>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QTabWidget>
#include <QTextStream>
#include <QVBoxLayout>

namespace calango::gui {

BseDialog::BseDialog(QWidget* parent) : QDialog(parent)
{
    setWindowTitle(tr("Wannier-Based Excitons (Bethe-Salpeter)"));
    auto* outer = new QVBoxLayout(this);

    auto* intro = new QLabel(
        tr("Excitons from the Bethe-Salpeter equation in the basis of "
           "Wannier-interpolated valence and conduction states — 3D "
           "(model-screened Coulomb) or 2D (Rytova-Keldysh/Keldysh "
           "potential)."),
        this);
    intro->setWordWrap(true);
    intro->setToolTip(
        tr("Tamm-Dancoff approximation throughout (the resonant e-h block "
           "only — no coupling to the anti-resonant block). The kernel is "
           "the standard 'point charge at the Wannier centre' simplification"
           ": a model-screened direct term (-W) between same-band-character "
           "pairs, plus a dipole-weighted bare exchange term (+2v, singlets "
           "only). Full ab-initio W would need GW-level data this app does "
           "not have — see the docs for exactly what is and is not "
           "computed."));
    outer->addWidget(intro);

    source_ = new WannierModelSource(this);
    outer->addWidget(source_);
    connect(source_, &WannierModelSource::modelChanged, this, [this] {
        computeButton_->setEnabled(true);
        updateSizeEstimate();
    });

    auto* settings = new QGroupBox(tr("Electron-Hole Basis"), this);
    auto* form = new QFormLayout(settings);

    auto* meshRow = new QHBoxLayout;
    const int defaults[3] = {8, 8, 8};
    for (int axis = 0; axis < 3; ++axis) {
        kmesh_[axis] = new QSpinBox(settings);
        kmesh_[axis]->setRange(1, 200);
        kmesh_[axis]->setValue(defaults[axis]);
        connect(kmesh_[axis], &QSpinBox::valueChanged, this, &BseDialog::updateSizeEstimate);
        meshRow->addWidget(kmesh_[axis]);
    }
    meshRow->addStretch(1);
    form->addRow(tr("k-mesh:"), meshRow);
    auto* meshNote = new QLabel(
        tr("<i>Exciton binding converges slowly with the k-mesh — densify "
           "and re-run to check convergence rather than trusting one "
           "mesh.</i>"),
        settings);
    meshNote->setWordWrap(true);
    form->addRow(QString(), meshNote);

    valenceBandTop_ = new QSpinBox(settings);
    valenceBandTop_->setRange(0, 500);
    valenceBandTop_->setToolTip(
        tr("0-based index (ascending-energy convention, at k=0) of the "
           "highest valence band."));
    form->addRow(tr("Valence band top index:"), valenceBandTop_);
    connect(valenceBandTop_, &QSpinBox::valueChanged, this, &BseDialog::updateSizeEstimate);

    nValence_ = new QSpinBox(settings);
    nValence_->setRange(1, 20);
    nValence_->setValue(1);
    form->addRow(tr("Valence bands:"), nValence_);
    connect(nValence_, &QSpinBox::valueChanged, this, &BseDialog::updateSizeEstimate);

    nConduction_ = new QSpinBox(settings);
    nConduction_->setRange(1, 20);
    nConduction_->setValue(1);
    form->addRow(tr("Conduction bands:"), nConduction_);
    connect(nConduction_, &QSpinBox::valueChanged, this, &BseDialog::updateSizeEstimate);

    spin_ = new QComboBox(settings);
    spin_->addItem(tr("Singlet (+2v exchange)"), static_cast<int>(core::BseSolver::Spin::Singlet));
    spin_->addItem(tr("Triplet (no exchange)"), static_cast<int>(core::BseSolver::Spin::Triplet));
    spin_->setToolTip(
        tr("Singlet includes the bare exchange term, which is repulsive — "
           "the standard singlet-triplet exciton splitting. Triplet is the "
           "pure (screened-direct-only) hydrogenic/Wannier-Mott limit."));
    form->addRow(tr("Spin:"), spin_);

    dimensionality_ = new QComboBox(settings);
    dimensionality_->addItem(tr("Bulk (3D)"), static_cast<int>(core::BseSolver::Dimensionality::Bulk3D));
    dimensionality_->addItem(tr("Monolayer (2D)"), static_cast<int>(core::BseSolver::Dimensionality::Slab2D));
    form->addRow(tr("Dimensionality:"), dimensionality_);
    connect(dimensionality_, &QComboBox::currentIndexChanged, this,
            &BseDialog::updateDimensionalityRows);

    outer->addWidget(settings);

    bulkScreeningGroup_ = new QGroupBox(tr("3D Screening"), this);
    auto* bulkForm = new QFormLayout(bulkScreeningGroup_);
    epsilonInfinity_ = new QDoubleSpinBox(bulkScreeningGroup_);
    epsilonInfinity_->setRange(1.0, 100.0);
    epsilonInfinity_->setValue(1.0);
    epsilonInfinity_->setDecimals(3);
    epsilonInfinity_->setToolTip(
        tr("The macroscopic (static, high-frequency) dielectric constant "
           "screening the direct e-h attraction: W(q) = 4*pi*e^2 / "
           "(epsilon_inf * q^2). User-supplied — nothing here computes it "
           "from first principles."));
    bulkForm->addRow(tr("epsilon_inf:"), epsilonInfinity_);
    outer->addWidget(bulkScreeningGroup_);

    slabScreeningGroup_ = new QGroupBox(tr("2D Screening — Rytova-Keldysh (Keldysh) Potential"), this);
    auto* slabForm = new QFormLayout(slabScreeningGroup_);
    keldyshR0_ = new QDoubleSpinBox(slabScreeningGroup_);
    keldyshR0_->setRange(0.0, 1000.0);
    keldyshR0_->setValue(0.0);
    keldyshR0_->setDecimals(3);
    keldyshR0_->setSuffix(tr(" A"));
    keldyshR0_->setToolTip(
        tr("The 2D screening length r0 in W(q) = 2*pi*e^2 / (A*q*(1+r0*q)) "
           "— commonly r0 = 2*pi*alpha_2D from the layer's own 2D "
           "polarizability. r0 = 0 recovers the BARE (unscreened) 2D "
           "Coulomb potential, whose exciton series is the non-hydrogenic "
           "2D hydrogen one, E_b(n) ~ 1/(n-1/2)^2 — not the 3D 1/n^2 "
           "series."));
    slabForm->addRow(tr("r0 (screening length):"), keldyshR0_);
    environmentEpsilon_ = new QDoubleSpinBox(slabScreeningGroup_);
    environmentEpsilon_->setRange(1.0, 50.0);
    environmentEpsilon_->setValue(1.0);
    environmentEpsilon_->setDecimals(3);
    environmentEpsilon_->setToolTip(
        tr("Effective substrate/environment dielectric constant — the mean "
           "of the media above and below the sheet, "
           "(epsilon_above+epsilon_below)/2. 1.0 is freestanding "
           "(vacuum on both sides)."));
    slabForm->addRow(tr("Environment epsilon:"), environmentEpsilon_);
    outer->addWidget(slabScreeningGroup_);

    auto* convergence = new QGroupBox(tr("Diagonalization"), this);
    auto* convForm = new QFormLayout(convergence);
    lowestExcitons_ = new QSpinBox(convergence);
    lowestExcitons_->setRange(1, 100);
    lowestExcitons_->setValue(10);
    lowestExcitons_->setToolTip(
        tr("How many of the lowest exciton states to report. Dense "
           "diagonalization (small basis) reports the full requested set "
           "exactly; the iterative Lanczos path (large basis) targets only "
           "these lowest states, so the absorption spectrum built from them "
           "covers only the low-energy part of the window in that case."));
    convForm->addRow(tr("Lowest excitons:"), lowestExcitons_);
    broadening_ = new QDoubleSpinBox(convergence);
    broadening_->setRange(0.001, 1.0);
    broadening_->setValue(0.05);
    broadening_->setDecimals(3);
    broadening_->setSuffix(tr(" eV"));
    convForm->addRow(tr("Spectral broadening:"), broadening_);
    spectrumWindow_ = new QDoubleSpinBox(convergence);
    spectrumWindow_->setRange(0.1, 20.0);
    spectrumWindow_->setValue(3.0);
    spectrumWindow_->setDecimals(2);
    spectrumWindow_->setSuffix(tr(" eV"));
    convForm->addRow(tr("Spectrum window:"), spectrumWindow_);
    outer->addWidget(convergence);

    sizeEstimateLabel_ = new QLabel(this);
    sizeEstimateLabel_->setWordWrap(true);
    outer->addWidget(sizeEstimateLabel_);

    auto* row = new QHBoxLayout;
    computeButton_ = new QPushButton(tr("Compute"), this);
    computeButton_->setEnabled(false);
    connect(computeButton_, &QPushButton::clicked, this, &BseDialog::compute);
    row->addWidget(computeButton_);
    row->addStretch(1);
    auto* exportButton = new QPushButton(tr("Export Data…"), this);
    connect(exportButton, &QPushButton::clicked, this, &BseDialog::exportData);
    row->addWidget(exportButton);
    auto* close = new QPushButton(tr("Close"), this);
    connect(close, &QPushButton::clicked, this, &QDialog::accept);
    row->addWidget(close);
    outer->addLayout(row);

    auto* tabs = new QTabWidget(this);
    report_ = new QPlainTextEdit(tabs);
    report_->setReadOnly(true);
    tabs->addTab(report_, tr("Summary"));
    spectrumPlot_ = new SpectrumPlotWidget(tabs);
    spectrumPlot_->setToolTip(
        tr("Independent-particle vs. excitonic optical absorption — the "
           "excitonic curve is redshifted from the bare gap by the binding "
           "energy, and its lowest peaks are the exciton series above."));
    tabs->addTab(spectrumPlot_, tr("Absorption Spectrum"));
    rydbergSeriesPlot_ = new SpectrumPlotWidget(tabs);
    rydbergSeriesPlot_->setToolTip(
        tr("Exciton binding energy vs. state index n. A 3D Wannier-Mott "
           "series follows 1/n^2; the 2D (Rytova-Keldysh) series is "
           "non-hydrogenic and does NOT follow 3D 1/n^2 — plotted here so "
           "the deviation is visible rather than asserted."));
    tabs->addTab(rydbergSeriesPlot_, tr("Exciton Series"));
    outer->addWidget(tabs, 1);

    updateDimensionalityRows();
    resize(920, 900);
}

void BseDialog::updateDimensionalityRows()
{
    const bool is2D = dimensionality_->currentData().toInt()
        == static_cast<int>(core::BseSolver::Dimensionality::Slab2D);
    bulkScreeningGroup_->setVisible(!is2D);
    slabScreeningGroup_->setVisible(is2D);
    if (kmesh_[2])
        kmesh_[2]->setValue(is2D ? 1 : kmesh_[2]->value() == 1 ? 8 : kmesh_[2]->value());
    updateSizeEstimate();
}

core::BseSolver::Options BseDialog::readOptions() const
{
    core::BseSolver::Options opt;
    for (int axis = 0; axis < 3; ++axis)
        opt.kmesh[static_cast<std::size_t>(axis)] = kmesh_[axis]->value();
    opt.valenceBandTop = valenceBandTop_->value();
    opt.nValence = nValence_->value();
    opt.nConduction = nConduction_->value();
    opt.spin = static_cast<core::BseSolver::Spin>(spin_->currentData().toInt());
    opt.dimensionality =
        static_cast<core::BseSolver::Dimensionality>(dimensionality_->currentData().toInt());
    opt.epsilonInfinity = epsilonInfinity_->value();
    opt.keldyshR0Angstrom = keldyshR0_->value();
    opt.environmentEpsilon = environmentEpsilon_->value();
    opt.lowestExcitons = lowestExcitons_->value();
    opt.broadeningEv = broadening_->value();
    opt.spectrumWindowEv = spectrumWindow_->value();
    return opt;
}

void BseDialog::updateSizeEstimate()
{
    if (!sizeEstimateLabel_)
        return;
    if (!source_ || !source_->hasModel()) {
        sizeEstimateLabel_->setText(tr("Load a Wannier Hamiltonian above to see the basis size."));
        return;
    }
    const core::BseSolver solver(source_->model(), readOptions());
    const auto states = solver.basisStates();
    const double mib = solver.estimatedDenseMemoryMiB();
    const std::size_t denseLimit = readOptions().denseSizeLimit;
    QString text = tr("<b>%1 electron-hole basis states</b> (%2 valence x %3 "
                      "conduction x %4 k-points) — dense H_BSE would need "
                      "~%5 MiB.")
                       .arg(states.size())
                       .arg(nValence_->value())
                       .arg(nConduction_->value())
                       .arg(kmesh_[0]->value() * kmesh_[1]->value() * kmesh_[2]->value())
                       .arg(mib, 0, 'g', 4);
    if (states.size() > denseLimit)
        text += tr(" <b style='color:#c0392b;'>Exceeds the dense threshold "
                   "(%1)</b> — this run will use the iterative Lanczos "
                   "solver for the lowest %2 state(s) only.")
                    .arg(denseLimit)
                    .arg(lowestExcitons_->value());
    sizeEstimateLabel_->setText(text);
}

void BseDialog::compute()
{
    if (!source_->hasModel())
        return;
    try {
        const core::BseSolver solver(source_->model(), readOptions());
        lastResult_ = solver.solve();

        QString text;
        text += tr("Bethe-Salpeter exciton summary (Tamm-Dancoff)\n");
        text += QStringLiteral("=====================================\n\n");
        text += tr("  Basis dimension       = %1%2\n")
                    .arg(lastResult_.basisDimension)
                    .arg(lastResult_.usedIterativeSolver
                             ? tr(" (Lanczos, lowest states only)")
                             : tr(" (dense)"));
        text += tr("  Minimum direct gap    = %1 eV\n").arg(lastResult_.minimumDirectGapEv, 0, 'f', 6);
        text += tr("\n  n   E (eV)        E_b (eV)       oscillator strength\n");
        for (std::size_t i = 0; i < lastResult_.excitons.size(); ++i) {
            const auto& x = lastResult_.excitons[i];
            text += QStringLiteral("  %1   %2   %3   %4\n")
                        .arg(i + 1, 2)
                        .arg(x.energy, 12, 'f', 6)
                        .arg(x.bindingEnergy, 12, 'f', 6)
                        .arg(x.oscillatorStrength, 14, 'g', 5);
        }
        report_->setPlainText(text);

        std::vector<QPair<QString, std::vector<double>>> spectrumSeries = {
            {tr("independent-particle"), lastResult_.spectrum.independentParticle},
            {tr("excitonic"), lastResult_.spectrum.excitonic},
        };
        spectrumPlot_->setSeries(lastResult_.spectrum.energiesEv, spectrumSeries,
                                 tr("energy (eV)"), tr("absorption (arb. units)"));

        std::vector<double> n;
        std::vector<double> binding;
        for (std::size_t i = 0; i < lastResult_.excitons.size(); ++i) {
            n.push_back(static_cast<double>(i + 1));
            binding.push_back(-lastResult_.excitons[i].bindingEnergy);
        }
        rydbergSeriesPlot_->setSeries(
            n, {{tr("E_b(n)"), binding}}, tr("state index n"), tr("binding energy (eV)"));
    } catch (const std::exception& e) {
        QMessageBox::warning(this, tr("Wannier-Based Excitons"),
                             tr("The solver refused this input:\n%1").arg(QString::fromUtf8(e.what())));
    }
}

void BseDialog::exportData()
{
    if (lastResult_.excitons.empty()) {
        QMessageBox::information(this, tr("Export Data"), tr("Nothing computed yet."));
        return;
    }
    const QString path = QFileDialog::getSaveFileName(this, tr("Export Exciton Spectrum"),
                                                       QStringLiteral("bse_excitons.csv"),
                                                       tr("CSV (*.csv)"));
    if (path.isEmpty())
        return;
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QMessageBox::warning(this, tr("Export Data"), tr("Could not write %1.").arg(path));
        return;
    }
    QTextStream out(&file);
    out << "# n,energy_eV,binding_energy_eV,oscillator_strength\n";
    for (std::size_t i = 0; i < lastResult_.excitons.size(); ++i) {
        const auto& x = lastResult_.excitons[i];
        out << (i + 1) << ',' << QString::fromStdString(core::localeSafeFormat(x.energy)) << ','
            << QString::fromStdString(core::localeSafeFormat(x.bindingEnergy)) << ','
            << QString::fromStdString(core::localeSafeFormat(x.oscillatorStrength)) << '\n';
    }
    out << "#\n# energy_eV,independent_particle,excitonic\n";
    for (std::size_t i = 0; i < lastResult_.spectrum.energiesEv.size(); ++i)
        out << QString::fromStdString(core::localeSafeFormat(lastResult_.spectrum.energiesEv[i]))
            << ',' << QString::fromStdString(core::localeSafeFormat(
                          lastResult_.spectrum.independentParticle[i]))
            << ',' << QString::fromStdString(core::localeSafeFormat(lastResult_.spectrum.excitonic[i]))
            << '\n';
}

void BseDialog::setWannierRuns(const QList<QPair<QString, QString>>& runs)
{
    if (source_)
        source_->setWannierRuns(runs);
}

} // namespace calango::gui
