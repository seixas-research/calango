#include "gui/EffectiveBandsWizard.hpp"

#include "gui/EmbeddedKPathEditor.hpp"

#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QDoubleValidator>
#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QLabel>
#include <QLineEdit>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QVBoxLayout>

#include <cmath>

namespace calango::gui {

namespace {

/// Above this the two cells are not an integer multiple of one another and
/// the unfolding is undefined. Matches the guard in the generated script.
constexpr double kCommensurateTolerance = 1e-3;

QString describe(const core::Structure& s)
{
    const auto& v = s.cell().vectors();
    if (!s.cell().isDefined())
        return QObject::tr("%1, %2 atoms — no unit cell")
            .arg(QString::fromStdString(s.chemicalFormula()))
            .arg(s.size());
    return QObject::tr("%1, %2 atoms — a=%3 b=%4 c=%5 Å, V=%6 Å³")
        .arg(QString::fromStdString(s.chemicalFormula()))
        .arg(s.size())
        .arg(v[0].norm(), 0, 'f', 3)
        .arg(v[1].norm(), 0, 'f', 3)
        .arg(v[2].norm(), 0, 'f', 3)
        .arg(s.cell().volume(), 0, 'f', 2);
}

} // namespace

EffectiveBandsWizard::EffectiveBandsWizard(
    std::shared_ptr<const core::Structure> supercell,
    std::vector<NamedStructure> openDocuments, QWidget* parent)
    : SimulationWizardBase(parent)
    , supercell_(std::move(supercell))
    , openDocuments_(std::move(openDocuments))
{
    buildUi();
}

QString EffectiveBandsWizard::wizardTitle() const
{
    return tr("Effective Band Structure — Band Unfolding");
}

QString EffectiveBandsWizard::settingsHeader() const
{
    return tr("Structure & Geometry Link");
}

QString EffectiveBandsWizard::calculatorSettingsHeader() const
{
    return tr("Calculator & Unfolding Settings");
}

QString EffectiveBandsWizard::reviewHeader() const
{
    return tr("k-Path & ASE Script Review");
}

QString EffectiveBandsWizard::exportFileName() const
{
    return QStringLiteral("effective_bands.py");
}

bool EffectiveBandsWizard::calculatorAllowed(core::CalculatorKind kind) const
{
    // The projection needs the plane-wave expansion coefficients of the
    // supercell eigenstates. Only these three have a route to them; the
    // classical potentials have no wavefunctions at all.
    return kind == core::CalculatorKind::Gpaw
        || kind == core::CalculatorKind::QuantumEspresso
        || kind == core::CalculatorKind::Siesta;
}

std::shared_ptr<const core::Structure>
EffectiveBandsWizard::primitiveStructure() const
{
    if (!primitiveCombo_)
        return nullptr;
    const int index = primitiveCombo_->currentData().toInt();
    if (index < 0 || index >= static_cast<int>(openDocuments_.size()))
        return nullptr;
    return openDocuments_[static_cast<std::size_t>(index)].structure;
}

// ---------------------------------------------------------------------------
// Stage 1 — Structure & Geometry Link
// ---------------------------------------------------------------------------

QWidget* EffectiveBandsWizard::buildSettingsPage()
{
    auto* page = new QWidget(this);
    auto* layout = new QVBoxLayout(page);

    auto* intro = new QLabel(
        tr("Unfolding maps the supercell's bands back onto the Brillouin zone "
           "of a pristine primitive cell, so a defect or alloy supercell can "
           "be read against the host band structure. Both cells are needed, "
           "and the supercell must be an integer multiple of the primitive "
           "one."),
        page);
    intro->setWordWrap(true);
    layout->addWidget(intro);

    auto* cellsGroup = new QGroupBox(tr("Cells"), page);
    auto* cellsForm = new QFormLayout(cellsGroup);

    supercellLabel_ = new QLabel(
        supercell_ ? describe(*supercell_) : tr("(none)"), cellsGroup);
    supercellLabel_->setWordWrap(true);
    cellsForm->addRow(tr("Supercell (active):"), supercellLabel_);

    primitiveCombo_ = new QComboBox(cellsGroup);
    for (std::size_t i = 0; i < openDocuments_.size(); ++i) {
        primitiveCombo_->addItem(openDocuments_[i].name, static_cast<int>(i));
    }
    primitiveCombo_->setToolTip(
        tr("The pristine primitive cell of the host lattice. Open it in "
           "another tab (Build → From Database…, or a saved file) and select "
           "it here — its reciprocal lattice defines the zone the effective "
           "band structure is drawn in."));
    cellsForm->addRow(tr("Primitive reference:"), primitiveCombo_);
    if (openDocuments_.empty()) {
        primitiveCombo_->addItem(tr("(no other structure is open)"), -1);
        primitiveCombo_->setEnabled(false);
    }
    layout->addWidget(cellsGroup);

    // --- Mapping matrix -----------------------------------------------------
    auto* matrixGroup = new QGroupBox(tr("Mapping matrix  (supercell = M · primitive)"),
                                      page);
    auto* matrixLayout = new QVBoxLayout(matrixGroup);

    autoMatrixCheck_ = new QCheckBox(tr("Deduce M from the two cells"), matrixGroup);
    autoMatrixCheck_->setChecked(true);
    autoMatrixCheck_->setToolTip(
        tr("M = S · P⁻¹, rounded to integers. Uncheck to enter M by hand — "
           "useful when the primitive cell is expressed in a different but "
           "equivalent setting."));
    matrixLayout->addWidget(autoMatrixCheck_);

    auto* grid = new QGridLayout;
    for (int i = 0; i < 3; ++i) {
        for (int j = 0; j < 3; ++j) {
            auto* spin = new QSpinBox(matrixGroup);
            spin->setRange(-99, 99);
            spin->setValue(i == j ? 1 : 0);
            spin->setEnabled(false); // auto mode owns it initially
            matrixSpins_[i][j] = spin;
            grid->addWidget(spin, i, j);
            connect(spin, &QSpinBox::valueChanged, this, [this] {
                if (!autoMatrixCheck_->isChecked()) {
                    refreshMatrix();
                    refreshPreview();
                }
            });
        }
    }
    matrixLayout->addLayout(grid);

    matrixVerdict_ = new QLabel(matrixGroup);
    matrixVerdict_->setWordWrap(true);
    matrixLayout->addWidget(matrixVerdict_);
    layout->addWidget(matrixGroup);

    connect(autoMatrixCheck_, &QCheckBox::toggled, this, [this](bool automatic) {
        for (auto& row : matrixSpins_)
            for (QSpinBox* spin : row)
                spin->setEnabled(!automatic);
        refreshMatrix();
        refreshPreview();
    });
    connect(primitiveCombo_, &QComboBox::currentIndexChanged, this, [this] {
        refreshMatrix();
        // The k-path lives on the primitive lattice, so changing the primitive
        // cell invalidates the Stage-4 picker entirely.
        rebuildKPathEditor();
        refreshPreview();
    });

    layout->addStretch(1);
    refreshMatrix();
    return page;
}

void EffectiveBandsWizard::refreshMatrix()
{
    const auto primitive = primitiveStructure();
    if (!supercell_ || !primitive || !supercell_->cell().isDefined()
        || !primitive->cell().isDefined()) {
        matrixVerdict_->setStyleSheet(QStringLiteral("color: #d9534f;"));
        matrixVerdict_->setText(
            tr("Both a supercell and a primitive cell with defined lattices "
               "are required."));
        return;
    }

    if (autoMatrixCheck_->isChecked()) {
        double residual = 0.0;
        const auto deduced = core::deduceSupercellMatrix(
            primitive->cell(), supercell_->cell(), &residual);
        for (int i = 0; i < 3; ++i) {
            for (int j = 0; j < 3; ++j) {
                const QSignalBlocker blocker(matrixSpins_[i][j]);
                matrixSpins_[i][j]->setValue(deduced.m[i][j]);
            }
        }
        if (!std::isfinite(residual) || residual > kCommensurateTolerance) {
            matrixVerdict_->setStyleSheet(QStringLiteral("color: #d9534f;"));
            matrixVerdict_->setText(
                tr("<b>Not commensurate</b> — the best integer fit is off by "
                   "%1. These two cells are not related by an integer "
                   "transformation, so the unfolding would be meaningless. "
                   "Check that the primitive cell really is the host of this "
                   "supercell.")
                    .arg(std::isfinite(residual)
                             ? QString::number(residual, 'g', 3)
                             : tr("a singular cell")));
            return;
        }
    }

    core::SupercellMatrix manual;
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 3; ++j)
            manual.m[i][j] = matrixSpins_[i][j]->value();
    const int cells = std::abs(manual.determinant());
    if (cells == 0) {
        matrixVerdict_->setStyleSheet(QStringLiteral("color: #d9534f;"));
        matrixVerdict_->setText(tr("<b>Singular matrix</b> — |det M| = 0."));
        return;
    }
    matrixVerdict_->setStyleSheet(QString());
    // A cross-check the user can actually act on: |det M| must equal the atom
    // count ratio for a pristine supercell. A mismatch is the signature of a
    // defect (vacancy/interstitial), which is legitimate — so report it as
    // information, not an error.
    const int ratio = primitive->size() > 0
        ? static_cast<int>(supercell_->size() / primitive->size())
        : 0;
    QString text = tr("<b>|det M| = %1</b> primitive cells.").arg(cells);
    if (ratio > 0 && ratio != cells) {
        text += tr(" Atom counts give %1× — the difference (%2 atoms) is "
                   "consistent with a defect, which is what unfolding is for.")
                    .arg(ratio)
                    .arg(static_cast<int>(supercell_->size())
                         - cells * static_cast<int>(primitive->size()));
    }
    matrixVerdict_->setText(text);
}

// ---------------------------------------------------------------------------
// Stage 3 — unfolding settings (appended to Calculator Settings)
// ---------------------------------------------------------------------------

QWidget* EffectiveBandsWizard::buildCalculatorExtras()
{
    auto* group = new QGroupBox(tr("Unfolding & spectral function"), this);
    auto* form = new QFormLayout(group);

    energyMinSpin_ = new QDoubleSpinBox(group);
    energyMinSpin_->setRange(-200.0, 0.0);
    energyMinSpin_->setValue(-10.0);
    energyMinSpin_->setSuffix(tr(" eV"));
    energyMaxSpin_ = new QDoubleSpinBox(group);
    energyMaxSpin_->setRange(0.0, 200.0);
    energyMaxSpin_->setValue(10.0);
    energyMaxSpin_->setSuffix(tr(" eV"));
    form->addRow(tr("Energy window min:"), energyMinSpin_);
    form->addRow(tr("Energy window max:"), energyMaxSpin_);

    energyBinsSpin_ = new QSpinBox(group);
    energyBinsSpin_->setRange(50, 5000);
    energyBinsSpin_->setValue(400);
    energyBinsSpin_->setToolTip(
        tr("Resolution of the energy axis of the A(k, E) heatmap."));
    form->addRow(tr("Energy mesh bins:"), energyBinsSpin_);

    sigmaSpin_ = new QDoubleSpinBox(group);
    sigmaSpin_->setRange(0.001, 2.0);
    sigmaSpin_->setDecimals(3);
    sigmaSpin_->setSingleStep(0.01);
    sigmaSpin_->setValue(0.05);
    sigmaSpin_->setSuffix(tr(" eV"));
    sigmaSpin_->setToolTip(
        tr("Gaussian broadening of each eigenvalue.\n"
           "σ must exceed the eigenvalue spacing or the map degenerates into "
           "isolated dots instead of continuous bands; too large and distinct "
           "branches merge."));
    form->addRow(tr("Gaussian broadening σ:"), sigmaSpin_);

    thresholdEdit_ = new QLineEdit(QStringLiteral("1e-4"), group);
    auto* validator = new QDoubleValidator(1e-12, 1.0, 12, thresholdEdit_);
    validator->setNotation(QDoubleValidator::ScientificNotation);
    validator->setLocale(QLocale::c());
    thresholdEdit_->setValidator(validator);
    thresholdEdit_->setToolTip(
        tr("States whose spectral weight P_Km(k) falls below this are not "
           "drawn. Unfolding produces a long tail of ~1e-6 weights that add "
           "nothing but cost."));
    form->addRow(tr("Weight threshold:"), thresholdEdit_);

    auto* note = new QLabel(
        tr("GPAW is the reference backend — its plane-wave mode exposes the "
           "expansion coefficients the projection needs. The other engines "
           "generate a template with the wavefunction-reading hook left to "
           "complete."),
        group);
    note->setWordWrap(true);
    form->addRow(note);

    for (QDoubleSpinBox* spin : {energyMinSpin_, energyMaxSpin_, sigmaSpin_})
        connect(spin, &QDoubleSpinBox::valueChanged, this,
                [this] { refreshPreview(); });
    connect(energyBinsSpin_, &QSpinBox::valueChanged, this,
            [this] { refreshPreview(); });
    connect(thresholdEdit_, &QLineEdit::textChanged, this,
            [this] { refreshPreview(); });
    return group;
}

// ---------------------------------------------------------------------------
// Stage 4 — k-path (shares the review stage with the script)
// ---------------------------------------------------------------------------

QWidget* EffectiveBandsWizard::buildReviewExtras()
{
    kpathHost_ = new QWidget(this);
    auto* layout = new QVBoxLayout(kpathHost_);
    layout->setContentsMargins(0, 0, 0, 0);
    auto* caption = new QLabel(
        tr("k-path on the <b>primitive</b> lattice — the zone the effective "
           "band structure is drawn in."),
        kpathHost_);
    caption->setWordWrap(true);
    caption->setTextFormat(Qt::RichText);
    layout->addWidget(caption);
    rebuildKPathEditor();
    return kpathHost_;
}

void EffectiveBandsWizard::rebuildKPathEditor()
{
    if (!kpathHost_)
        return; // Stage 4 not built yet (Stage 1 runs first)
    if (kpath_) {
        kpathHost_->layout()->removeWidget(kpath_);
        kpath_->deleteLater();
        kpath_ = nullptr;
    }
    kpath_ = new EmbeddedKPathEditor(primitiveStructure(), kpathHost_);
    kpathHost_->layout()->addWidget(kpath_);
    connect(kpath_, &EmbeddedKPathEditor::pathChanged, this,
            [this] { refreshPreview(); });
}

// ---------------------------------------------------------------------------
// Script
// ---------------------------------------------------------------------------

core::UnfoldingConfig EffectiveBandsWizard::runConfig() const
{
    core::UnfoldingConfig config;
    switch (selectedCalculator()) {
    case core::CalculatorKind::QuantumEspresso:
        config.backend = core::UnfoldingBackend::Espresso;
        break;
    case core::CalculatorKind::Siesta:
        config.backend = core::UnfoldingBackend::Siesta;
        break;
    default:
        config.backend = core::UnfoldingBackend::Gpaw;
        break;
    }
    config.calculator = baseCalculatorConfig();
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 3; ++j)
            config.matrix.m[i][j] = matrixSpins_[i][j]->value();
    if (kpath_) {
        config.kpath = kpath_->path().toStdString();
        config.pointsPerSegment = kpath_->pointsPerSegment();
    }
    config.spectral.energyMin = energyMinSpin_->value();
    config.spectral.energyMax = energyMaxSpin_->value();
    config.spectral.energyBins = energyBinsSpin_->value();
    config.spectral.sigma = sigmaSpin_->value();
    bool ok = false;
    if (const double threshold = thresholdEdit_->text().toDouble(&ok);
        ok && threshold > 0.0) {
        config.spectral.weightThreshold = threshold;
    }
    return config;
}

QString EffectiveBandsWizard::generateScript() const
{
    return QString::fromStdString(
        core::generateUnfoldingScript(runConfig()));
}

} // namespace calango::gui
