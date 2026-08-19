#include "gui/LdosWizard.hpp"

#include "core/LdosScriptGenerator.hpp"
#include "core/Structure.hpp"
#include "gui/EnergyWindowWidget.hpp"
#include "gui/GpawEigenvaluePeek.hpp"

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QSignalBlocker>
#include <QVBoxLayout>
#include <QWidget>

namespace calango::gui {

LdosWizard::LdosWizard(std::shared_ptr<core::Structure> structure,
                       QWidget* parent)
    : SimulationWizardBase(parent), structure_(std::move(structure))
{
    buildUi();
    selectCalculator(core::CalculatorKind::Gpaw);
    onBaselineChanged();
}

void LdosWizard::setDensityBaselines(
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

QString LdosWizard::wizardTitle() const
{
    return tr("Local Density of States (LDOS) Setup");
}

QString LdosWizard::settingsHeader() const
{
    return tr("Baseline & Energy Window");
}

QWidget* LdosWizard::buildSettingsPage()
{
    auto* page = new QWidget(this);
    auto* layout = new QVBoxLayout(page);

    auto* intro = new QLabel(
        tr("Sum |psi(r)|^2 over every stored Kohn-Sham state in an energy "
           "window, weighted by k-point, on top of a completed GPAW "
           "single-point that saved its wavefunctions."),
        page);
    intro->setWordWrap(true);
    layout->addWidget(intro);

    // --- Baseline ------------------------------------------------------
    auto* sourceGroup = new QGroupBox(tr("Baseline SCF process"), page);
    auto* sourceForm = new QFormLayout(sourceGroup);

    baselineCombo_ = new QComboBox(sourceGroup);
    baselineCombo_->setToolTip(
        tr("Completed GPAW Single-Point calculations that saved their "
           "wavefunctions (.gpw, mode='all'). LDOS is a post-process on an "
           "existing calculation — there is no fresh-SCF fallback."));
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
            &LdosWizard::onBaselineChanged);

    // --- Energy window ---------------------------------------------------
    auto* windowGroup = new QGroupBox(tr("Energy window"), page);
    auto* windowLayout = new QVBoxLayout(windowGroup);

    energyWindow_ = new EnergyWindowWidget(windowGroup);
    windowLayout->addWidget(energyWindow_);
    connect(energyWindow_, &EnergyWindowWidget::windowChanged, this,
            &LdosWizard::syncSpinBoxesFromWidget);

    auto* spinRow = new QHBoxLayout();
    minSpin_ = new QDoubleSpinBox(windowGroup);
    minSpin_->setDecimals(3);
    minSpin_->setRange(-1000.0, 1000.0);
    minSpin_->setSuffix(tr(" eV"));
    minSpin_->setValue(-0.5);
    maxSpin_ = new QDoubleSpinBox(windowGroup);
    maxSpin_->setDecimals(3);
    maxSpin_->setRange(-1000.0, 1000.0);
    maxSpin_->setSuffix(tr(" eV"));
    maxSpin_->setValue(0.0);
    relativeCheck_ = new QCheckBox(tr("Relative to Fermi level"), windowGroup);
    relativeCheck_->setChecked(true);
    relativeCheck_->setToolTip(
        tr("Checked: the two bounds are offsets from E_F, and the run stays "
           "correct even if the baseline is re-run with a slightly "
           "different Fermi level.\n\n"
           "Unchecked: absolute Kohn-Sham eigenvalues."));
    spinRow->addWidget(new QLabel(tr("Min:"), windowGroup));
    spinRow->addWidget(minSpin_);
    spinRow->addWidget(new QLabel(tr("Max:"), windowGroup));
    spinRow->addWidget(maxSpin_);
    spinRow->addWidget(relativeCheck_);
    spinRow->addStretch(1);
    windowLayout->addLayout(spinRow);

    auto* presetRow = new QHBoxLayout();
    presetWidthSpin_ = new QDoubleSpinBox(windowGroup);
    presetWidthSpin_->setDecimals(2);
    presetWidthSpin_->setRange(0.01, 50.0);
    presetWidthSpin_->setValue(0.5);
    presetWidthSpin_->setSuffix(tr(" eV"));
    presetWidthSpin_->setToolTip(
        tr("Width of the one-click presets below, measured from E_F."));
    auto* occupiedButton = new QPushButton(tr("Occupied near E_F"), windowGroup);
    auto* unoccupiedButton =
        new QPushButton(tr("Unoccupied near E_F"), windowGroup);
    presetRow->addWidget(new QLabel(tr("Preset width:"), windowGroup));
    presetRow->addWidget(presetWidthSpin_);
    presetRow->addWidget(occupiedButton);
    presetRow->addWidget(unoccupiedButton);
    presetRow->addStretch(1);
    windowLayout->addLayout(presetRow);
    connect(occupiedButton, &QPushButton::clicked, this,
            [this] { applyPreset(true); });
    connect(unoccupiedButton, &QPushButton::clicked, this,
            [this] { applyPreset(false); });

    auto* spinChannelRow = new QHBoxLayout();
    spinCombo_ = new QComboBox(windowGroup);
    spinCombo_->addItem(tr("Sum (both channels, or the only one)"),
                        static_cast<int>(core::LdosSpinChannel::Sum));
    spinCombo_->addItem(tr("Spin up"),
                        static_cast<int>(core::LdosSpinChannel::Up));
    spinCombo_->addItem(tr("Spin down"),
                        static_cast<int>(core::LdosSpinChannel::Down));
    spinChannelRow->addWidget(new QLabel(tr("Spin channel:"), windowGroup));
    spinChannelRow->addWidget(spinCombo_);
    spinChannelRow->addStretch(1);
    windowLayout->addLayout(spinChannelRow);

    layout->addWidget(windowGroup);

    connect(minSpin_, &QDoubleSpinBox::valueChanged, this,
            [this] { syncWidgetFromSpinBoxes(); });
    connect(maxSpin_, &QDoubleSpinBox::valueChanged, this,
            [this] { syncWidgetFromSpinBoxes(); });
    connect(relativeCheck_, &QCheckBox::toggled, this, [this](bool relative) {
        if (energyWindow_)
            energyWindow_->setRelativeToFermi(relative);
        // The spin boxes' own displayed convention flips with it — refresh
        // their text from the widget's (always-absolute) window rather
        // than reinterpreting whatever number happened to be typed.
        if (energyWindow_) {
            const auto [lo, hi] = energyWindow_->window();
            syncSpinBoxesFromWidget(lo, hi);
        }
        refreshPreview();
    });
    connect(spinCombo_, &QComboBox::currentIndexChanged, this,
            [this] { refreshPreview(); });

    layout->addStretch(1);
    return page;
}

void LdosWizard::onBaselineChanged()
{
    const QString dir =
        baselineCombo_ ? baselineCombo_->currentData().toString() : QString();
    inherited_ = dir.isEmpty() ? std::nullopt : readCalculatorProvenance(dir);
    spectrumLoaded_ = false;

    if (inheritedLabel_) {
        if (dir.isEmpty()) {
            inheritedLabel_->setText(
                tr("<i>No baseline selected.</i>"));
        } else if (inherited_) {
            QString note = inherited_->summary().toHtmlEscaped();
            if (!inherited_->condaEnv.isEmpty())
                note += tr(" · env %1").arg(inherited_->condaEnv.toHtmlEscaped());
            inheritedLabel_->setText(note);
        } else {
            inheritedLabel_->setText(
                tr("<i>Restarting from the saved wavefunctions (.gpw); "
                   "parameters come from the restart file.</i>"));
        }
    }

    if (dir.isEmpty()) {
        if (baselineSummaryLabel_)
            baselineSummaryLabel_->setText(
                tr("<i>Select a completed Single-Point Calculation.</i>"));
        if (energyWindow_)
            energyWindow_->setLevels({}, 0.0);
        refreshPreview();
        return;
    }

    if (baselineSummaryLabel_)
        baselineSummaryLabel_->setText(tr("Reading eigenvalue spectrum…"));
    if (energyWindow_)
        energyWindow_->setLoading(true);
    if (peekErrorLabel_)
        peekErrorLabel_->setVisible(false);
    QApplication::setOverrideCursor(Qt::WaitCursor);

    const GpawEigenvalueSpectrum spectrum =
        peekGpawEigenvalues(pythonExecutable(), dir);

    QApplication::restoreOverrideCursor();

    if (!spectrum.ok) {
        if (energyWindow_)
            energyWindow_->setLoading(false);
        if (baselineSummaryLabel_)
            baselineSummaryLabel_->setText(
                tr("<span style='color:#d9534f;'>Could not read this "
                   "baseline's eigenvalues.</span>"));
        if (peekErrorLabel_) {
            peekErrorLabel_->setText(spectrum.errorMessage);
            peekErrorLabel_->setVisible(true);
        }
        refreshPreview();
        return;
    }

    fermiLevelEv_ = spectrum.fermiLevelEv;
    spectrumLoaded_ = true;

    std::vector<EnergyWindowWidget::Level> levels;
    levels.reserve(spectrum.states.size());
    for (const GpawState& s : spectrum.states)
        levels.push_back({s.energyEv, s.kWeight});
    if (energyWindow_) {
        energyWindow_->setLevels(levels, fermiLevelEv_);
        energyWindow_->setRelativeToFermi(
            relativeCheck_ && relativeCheck_->isChecked());
    }

    if (spinCombo_)
        spinCombo_->setEnabled(spectrum.nspins > 1);

    if (baselineSummaryLabel_)
        baselineSummaryLabel_->setText(
            tr("%1 stored state(s) · E_F = %2 eV · %3")
                .arg(spectrum.states.size())
                .arg(fermiLevelEv_, 0, 'f', 3)
                .arg(spectrum.nspins > 1 ? tr("spin-polarized")
                                        : tr("spin-unpolarized")));

    // Default window: occupied states within the preset width of E_F —
    // matches the "Occupied near E_F" preset button below.
    applyPreset(true);
}

void LdosWizard::applyPreset(bool occupied)
{
    const double width = presetWidthSpin_ ? presetWidthSpin_->value() : 0.5;
    const double lo = occupied ? fermiLevelEv_ - width : fermiLevelEv_;
    const double hi = occupied ? fermiLevelEv_ : fermiLevelEv_ + width;
    if (energyWindow_)
        energyWindow_->setWindow(lo, hi);
    syncSpinBoxesFromWidget(lo, hi);
}

void LdosWizard::syncSpinBoxesFromWidget(double minEv, double maxEv)
{
    if (syncing_ || !minSpin_ || !maxSpin_)
        return;
    syncing_ = true;
    const bool relative = relativeCheck_ && relativeCheck_->isChecked();
    const QSignalBlocker blockMin(minSpin_);
    const QSignalBlocker blockMax(maxSpin_);
    minSpin_->setValue(relative ? minEv - fermiLevelEv_ : minEv);
    maxSpin_->setValue(relative ? maxEv - fermiLevelEv_ : maxEv);
    syncing_ = false;
    refreshPreview();
}

void LdosWizard::syncWidgetFromSpinBoxes()
{
    if (syncing_ || !minSpin_ || !maxSpin_)
        return;
    syncing_ = true;
    const bool relative = relativeCheck_ && relativeCheck_->isChecked();
    const double lo =
        relative ? minSpin_->value() + fermiLevelEv_ : minSpin_->value();
    const double hi =
        relative ? maxSpin_->value() + fermiLevelEv_ : maxSpin_->value();
    if (energyWindow_)
        energyWindow_->setWindow(lo, hi);
    syncing_ = false;
    refreshPreview();
}

QString LdosWizard::pythonExecutable() const
{
    if (inherited_ && !inherited_->pythonExecutable.isEmpty())
        return inherited_->pythonExecutable;
    return SimulationWizardBase::pythonExecutable();
}

QString LdosWizard::generateScript() const
{
    core::LdosConfig cfg;
    cfg.baselineDir =
        baselineCombo_ ? baselineCombo_->currentData().toString().toStdString()
                       : std::string();

    const bool relative = relativeCheck_ && relativeCheck_->isChecked();
    cfg.relativeToFermi = relative;
    if (minSpin_ && maxSpin_) {
        // The spin boxes already display in whichever convention the
        // checkbox selected, so their raw values ARE the config's values —
        // no further conversion needed here (see syncSpinBoxesFromWidget).
        cfg.energyMin = minSpin_->value();
        cfg.energyMax = maxSpin_->value();
    }

    cfg.spin = spinCombo_
        ? static_cast<core::LdosSpinChannel>(spinCombo_->currentData().toInt())
        : core::LdosSpinChannel::Sum;

    return QString::fromStdString(core::generateLdosScript(cfg));
}

} // namespace calango::gui
