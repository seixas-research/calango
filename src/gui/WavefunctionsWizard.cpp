#include "gui/WavefunctionsWizard.hpp"

#include "core/Structure.hpp"
#include "core/WavefunctionScriptGenerator.hpp"
#include "gui/GpawEigenvaluePeek.hpp"

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHeaderView>
#include <QLabel>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QVBoxLayout>
#include <QWidget>

namespace calango::gui {

WavefunctionsWizard::WavefunctionsWizard(
    std::shared_ptr<core::Structure> structure, QWidget* parent)
    : SimulationWizardBase(parent), structure_(std::move(structure))
{
    buildUi();
    selectCalculator(core::CalculatorKind::Gpaw);
    onBaselineChanged();
}

void WavefunctionsWizard::setDensityBaselines(
    const QList<QPair<QString, QString>>& baselines)
{
    if (!baselineCombo_)
        return;
    for (const auto& [label, dir] : baselines)
        baselineCombo_->addItem(label, dir);
    if (baselineCombo_->count() > 0)
        baselineCombo_->setCurrentIndex(0);
    else
        onBaselineChanged();
}

QString WavefunctionsWizard::wizardTitle() const
{
    return tr("Wavefunctions Setup");
}

QString WavefunctionsWizard::settingsHeader() const
{
    return tr("Baseline & States");
}

QWidget* WavefunctionsWizard::buildSettingsPage()
{
    auto* page = new QWidget(this);
    auto* layout = new QVBoxLayout(page);

    auto* intro = new QLabel(
        tr("Real-space Kohn-Sham orbitals as volumetric data — pseudo-"
           "wavefunctions, or the all-electron PAW reconstruction where the "
           "engine supports it. Select one or more states below to write "
           "one cube each in a single pass."),
        page);
    intro->setWordWrap(true);
    layout->addWidget(intro);

    auto* sourceGroup = new QGroupBox(tr("Baseline SCF process"), page);
    auto* sourceForm = new QFormLayout(sourceGroup);

    baselineCombo_ = new QComboBox(sourceGroup);
    baselineCombo_->setToolTip(
        tr("Completed GPAW Single-Point calculations that saved their "
           "wavefunctions (.gpw, mode='all')."));
    sourceForm->addRow(tr("Process:"), baselineCombo_);

    inheritedLabel_ = new QLabel(sourceGroup);
    inheritedLabel_->setWordWrap(true);
    inheritedLabel_->setTextFormat(Qt::RichText);
    sourceForm->addRow(tr("Inherited calculator:"), inheritedLabel_);

    baselineSummaryLabel_ = new QLabel(sourceGroup);
    baselineSummaryLabel_->setWordWrap(true);
    baselineSummaryLabel_->setTextFormat(Qt::RichText);
    sourceForm->addRow(tr("Baseline:"), baselineSummaryLabel_);

    peekErrorLabel_ = new QLabel(sourceGroup);
    peekErrorLabel_->setWordWrap(true);
    peekErrorLabel_->setStyleSheet(QStringLiteral("color: #d9534f;"));
    peekErrorLabel_->setVisible(false);
    sourceForm->addRow(peekErrorLabel_);

    layout->addWidget(sourceGroup);
    connect(baselineCombo_, &QComboBox::currentIndexChanged, this,
            &WavefunctionsWizard::onBaselineChanged);

    auto* optionsGroup = new QGroupBox(tr("What to write"), page);
    auto* optionsForm = new QFormLayout(optionsGroup);

    quantityCombo_ = new QComboBox(optionsGroup);
    quantityCombo_->addItem(tr("|psi|^2 (density)"),
                            static_cast<int>(core::WavefunctionQuantity::Density));
    quantityCombo_->addItem(tr("Real part"),
                            static_cast<int>(core::WavefunctionQuantity::Real));
    quantityCombo_->addItem(tr("Imaginary part"),
                            static_cast<int>(core::WavefunctionQuantity::Imaginary));
    quantityCombo_->setToolTip(
        tr("|psi|^2 is always real and non-negative. The real part IS the "
           "signed, two-lobe orbital picture for a real (Gamma-point or "
           "molecular) state — the Volumetric Data dock already gives any "
           "field with negative values the positive/negative isosurface "
           "treatment, so there is no separate 'signed' option. The "
           "imaginary part is identically zero for a real orbital and "
           "offered for completeness with a genuinely complex k != Gamma "
           "state."));
    optionsForm->addRow(tr("Quantity:"), quantityCombo_);

    allElectronCheck_ =
        new QCheckBox(tr("All-electron (PAW reconstruction)"), optionsGroup);
    allElectronCheck_->setToolTip(
        tr("Off (default): the pseudo-wavefunction, smooth by construction "
           "— what the plane-wave/grid basis actually represents.\n\n"
           "On: reconstructs the true, cusped all-electron orbital near "
           "each nucleus via the PAW correction "
           "(calc.dft.ibzwfs.get_all_electron_wave_function) — needs the "
           "NEW GPAW engine; a legacy restart raises a clear error rather "
           "than silently falling back to pseudo."));
    optionsForm->addRow(allElectronCheck_);

    gridSpacingSpin_ = new QDoubleSpinBox(optionsGroup);
    gridSpacingSpin_->setDecimals(3);
    gridSpacingSpin_->setRange(0.01, 1.0);
    gridSpacingSpin_->setValue(0.05);
    gridSpacingSpin_->setSuffix(tr(" Å"));
    gridSpacingSpin_->setEnabled(false);
    gridSpacingSpin_->setToolTip(
        tr("Real-space grid spacing for the all-electron reconstruction "
           "only — the pseudo path reuses the SCF's own grid."));
    optionsForm->addRow(tr("All-electron grid spacing:"), gridSpacingSpin_);
    connect(allElectronCheck_, &QCheckBox::toggled, gridSpacingSpin_,
            &QDoubleSpinBox::setEnabled);
    connect(allElectronCheck_, &QCheckBox::toggled, this,
            [this] { refreshPreview(); });
    connect(quantityCombo_, &QComboBox::currentIndexChanged, this,
            [this] { refreshPreview(); });
    connect(gridSpacingSpin_, &QDoubleSpinBox::valueChanged, this,
            [this] { refreshPreview(); });

    layout->addWidget(optionsGroup);

    auto* stateGroup = new QGroupBox(tr("States to export"), page);
    auto* stateLayout = new QVBoxLayout(stateGroup);
    stateTable_ = new QTableWidget(stateGroup);
    stateTable_->setColumnCount(5);
    stateTable_->setHorizontalHeaderLabels(
        {QString(), tr("Spin"), tr("k-point"), tr("Band"),
         tr("Energy (eV) / Occupation")});
    stateTable_->horizontalHeader()->setStretchLastSection(true);
    stateTable_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    stateTable_->setSelectionMode(QAbstractItemView::NoSelection);
    stateLayout->addWidget(stateTable_);
    // Connected HERE, once — stateTable_ itself is a stable widget across
    // baseline changes (only its ROWS get torn down and rebuilt, in
    // rebuildStateTable()), so this does not need to be re-established
    // every time that runs. It used to be connected there, WITH
    // Qt::UniqueConnection guarding against exactly the duplication that
    // would cause — but Qt::UniqueConnection silently has no effect on a
    // lambda target ("unique connections require a pointer to member
    // function", logged at runtime, not a hard error), so on inspection
    // every baseline switch was ACTUALLY adding a second, third, ...
    // duplicate connection, each firing the same lambda again per
    // checkbox click. Connecting once here needs no such guard at all.
    connect(stateTable_, &QTableWidget::itemChanged, this,
            [this](QTableWidgetItem*) {
                updateSelectionWarning();
                refreshPreview();
            });

    selectionWarningLabel_ = new QLabel(stateGroup);
    selectionWarningLabel_->setWordWrap(true);
    selectionWarningLabel_->setStyleSheet(QStringLiteral("color: #d9534f;"));
    stateLayout->addWidget(selectionWarningLabel_);

    layout->addWidget(stateGroup, 1);

    updateSelectionWarning();
    return page;
}

void WavefunctionsWizard::updateSelectionWarning()
{
    if (!selectionWarningLabel_)
        return;
    int checked = 0;
    if (stateTable_) {
        for (int row = 0; row < stateTable_->rowCount(); ++row) {
            const QTableWidgetItem* item = stateTable_->item(row, 0);
            if (item && item->checkState() == Qt::Checked)
                ++checked;
        }
    }
    selectionWarningLabel_->setVisible(checked == 0);
    if (checked == 0)
        selectionWarningLabel_->setText(
            tr("⚠ No states are ticked — tick at least one row above. "
               "Running now would produce nothing to add to the Volumetric "
               "Data dock (the generated script refuses rather than "
               "completing silently with no result)."));
}

void WavefunctionsWizard::onBaselineChanged()
{
    const QString dir =
        baselineCombo_ ? baselineCombo_->currentData().toString() : QString();
    inherited_ = dir.isEmpty() ? std::nullopt : readCalculatorProvenance(dir);
    peekedStates_.clear();

    if (inheritedLabel_) {
        inheritedLabel_->setText(
            dir.isEmpty()
                ? tr("<i>No baseline selected.</i>")
                : (inherited_ ? inherited_->summary().toHtmlEscaped()
                              : tr("<i>Restarting from the saved "
                                   "wavefunctions (.gpw).</i>")));
    }

    if (dir.isEmpty()) {
        if (baselineSummaryLabel_)
            baselineSummaryLabel_->setText(
                tr("<i>Select a completed Single-Point Calculation.</i>"));
        if (peekErrorLabel_)
            peekErrorLabel_->setVisible(false);
        rebuildStateTable();
        refreshPreview();
        return;
    }

    if (baselineSummaryLabel_)
        baselineSummaryLabel_->setText(tr("Reading available states…"));
    QApplication::setOverrideCursor(Qt::WaitCursor);
    const GpawEigenvalueSpectrum spectrum =
        peekGpawEigenvalues(pythonExecutable(), dir);
    QApplication::restoreOverrideCursor();

    if (!spectrum.ok) {
        if (baselineSummaryLabel_)
            baselineSummaryLabel_->setText(
                tr("<span style='color:#d9534f;'>Could not read this "
                   "baseline.</span>"));
        if (peekErrorLabel_) {
            peekErrorLabel_->setText(spectrum.errorMessage);
            peekErrorLabel_->setVisible(true);
        }
        rebuildStateTable();
        refreshPreview();
        return;
    }

    if (peekErrorLabel_)
        peekErrorLabel_->setVisible(false);
    for (const GpawState& s : spectrum.states)
        peekedStates_.push_back({s.spin, s.kpt, s.band, s.energyEv,
                                 s.occupation});

    if (baselineSummaryLabel_)
        baselineSummaryLabel_->setText(
            tr("%1 stored state(s) · E_F = %2 eV")
                .arg(spectrum.states.size())
                .arg(spectrum.fermiLevelEv, 0, 'f', 3));

    rebuildStateTable();
    refreshPreview();
}

void WavefunctionsWizard::rebuildStateTable()
{
    if (!stateTable_)
        return;
    stateTable_->setRowCount(static_cast<int>(peekedStates_.size()));
    for (std::size_t row = 0; row < peekedStates_.size(); ++row) {
        const PeekedState& s = peekedStates_[row];
        const int r = static_cast<int>(row);
        auto* check = new QTableWidgetItem();
        check->setFlags(Qt::ItemIsUserCheckable | Qt::ItemIsEnabled);
        check->setCheckState(Qt::Unchecked);
        stateTable_->setItem(r, 0, check);
        stateTable_->setItem(r, 1,
                             new QTableWidgetItem(s.spin == 0 ? tr("up")
                                                              : tr("down")));
        stateTable_->setItem(r, 2, new QTableWidgetItem(QString::number(s.kpt)));
        stateTable_->setItem(r, 3, new QTableWidgetItem(QString::number(s.band)));
        const QString occText = s.occupation >= 0.0
            ? QString::number(s.occupation, 'f', 3)
            : tr("n/a");
        stateTable_->setItem(
            r, 4,
            new QTableWidgetItem(QStringLiteral("%1 / %2")
                                     .arg(s.energyEv, 0, 'f', 4)
                                     .arg(occText)));
    }
    stateTable_->resizeColumnsToContents();
    // The connection lives in buildSettingsPage() now (see its own comment)
    // — setRowCount()/setItem() above emit itemChanged for every new row's
    // initial (Unchecked) state, so updateSelectionWarning() is already
    // current by the time this returns; the explicit call below is only
    // for the empty-table case (no rows means no itemChanged at all).
    updateSelectionWarning();
}

QString WavefunctionsWizard::pythonExecutable() const
{
    if (inherited_ && !inherited_->pythonExecutable.isEmpty())
        return inherited_->pythonExecutable;
    return SimulationWizardBase::pythonExecutable();
}

QString WavefunctionsWizard::generateScript() const
{
    core::WavefunctionsConfig cfg;
    cfg.baselineDir =
        baselineCombo_ ? baselineCombo_->currentData().toString().toStdString()
                       : std::string();
    cfg.quantity = quantityCombo_
        ? static_cast<core::WavefunctionQuantity>(
              quantityCombo_->currentData().toInt())
        : core::WavefunctionQuantity::Density;
    cfg.allElectron = allElectronCheck_ && allElectronCheck_->isChecked();
    cfg.allElectronGridSpacing =
        gridSpacingSpin_ ? gridSpacingSpin_->value() : 0.05;

    if (stateTable_) {
        for (int row = 0; row < stateTable_->rowCount(); ++row) {
            const QTableWidgetItem* check = stateTable_->item(row, 0);
            if (check && check->checkState() == Qt::Checked
                && static_cast<std::size_t>(row) < peekedStates_.size()) {
                const PeekedState& s = peekedStates_[static_cast<std::size_t>(row)];
                cfg.states.push_back({s.spin, s.kpt, s.band});
            }
        }
    }

    return QString::fromStdString(core::generateWavefunctionsScript(cfg));
}

} // namespace calango::gui
