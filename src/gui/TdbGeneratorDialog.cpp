#include "gui/TdbGeneratorDialog.hpp"

#include "core/PhononThermodynamics.hpp"
#include "core/TdbWriter.hpp"

#include <QAction>
#include <QCheckBox>
#include <QDialogButtonBox>
#include <QFontDatabase>
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
#include <QLineEdit>
#include <QMenu>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QTableWidget>
#include <QTextStream>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace calango::gui {

namespace {

/// Column layout of the configuration table.
enum Column {
    ColumnLabel = 0,
    ColumnComposition,
    ColumnEnergy,
    ColumnFormation,
    ColumnAboveHull,
    ColumnPhonons,
    ColumnCount
};

/// The leading element symbol of a formula ("Ag3Au1" -> "Ag"). Used only to
/// GUESS the two endpoint names when the ensemble file does not say; the user
/// can overwrite them, and a wrong guess costs a label, not a number.
QString leadingElement(const QString& formula)
{
    QString out;
    for (const QChar c : formula) {
        if (c.isUpper() && !out.isEmpty())
            break;
        if (c.isDigit())
            break;
        out.append(c);
    }
    return out;
}

} // namespace

TdbGeneratorDialog::TdbGeneratorDialog(QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle(tr("CALPHAD — Database from First Principles"));
    resize(880, 720);

    auto* layout = new QVBoxLayout(this);

    auto* intro = new QLabel(
        tr("Fit a <b>Redlich-Kister</b> excess Gibbs energy to formation "
           "energies computed here, and write it as a <tt>.tdb</tt>. "
           "The excess energy is what a solution model adds <i>on top of</i> "
           "the ideal configurational entropy — the entropy is supplied by the "
           "model, not by the DFT energies, and is not subtracted here."),
        this);
    intro->setWordWrap(true);
    intro->setTextFormat(Qt::RichText);
    layout->addWidget(intro);

    auto* fileRow = new QHBoxLayout;
    fileRow->addWidget(new QLabel(tr("Ensemble:"), this));
    pathEdit_ = new QLineEdit(this);
    pathEdit_->setReadOnly(true);
    pathEdit_->setPlaceholderText(
        tr("a finished cluster-expansion run's cluster_expansion.json"));
    fileRow->addWidget(pathEdit_, 1);
    auto* browse = new QPushButton(tr("Import…"), this);
    connect(browse, &QPushButton::clicked, this,
            &TdbGeneratorDialog::browseForEnsemble);
    fileRow->addWidget(browse);
    layout->addLayout(fileRow);

    statusLabel_ = new QLabel(tr("No ensemble loaded"), this);
    statusLabel_->setWordWrap(true);
    statusLabel_->setStyleSheet(QStringLiteral("color: gray;"));
    layout->addWidget(statusLabel_);

    table_ = new QTableWidget(0, ColumnCount, this);
    table_->setHorizontalHeaderLabels(
        {tr("Configuration"), tr("x"), tr("E (eV/atom)"),
         tr("E_form (eV/atom)"), tr("above hull"), tr("F_vib")});
    table_->horizontalHeader()->setStretchLastSection(true);
    table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table_->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(table_, &QTableWidget::customContextMenuRequested, this,
            [this](const QPoint& at) {
                const int row = table_->rowAt(at.y());
                if (row < 0)
                    return;
                QMenu menu(this);
                QAction* attach =
                    menu.addAction(tr("Attach phonon_dos.json…"));
                if (menu.exec(table_->viewport()->mapToGlobal(at)) != attach)
                    return;
                const QString path = QFileDialog::getOpenFileName(
                    this, tr("Phonon Density of States"), QString(),
                    tr("Phonon DOS (phonon_dos.json);;JSON (*.json)"));
                if (!path.isEmpty())
                    loadPhononDos(row, path);
            });
    layout->addWidget(table_, 1);

    // --- The model -------------------------------------------------------
    auto* modelGroup = new QGroupBox(tr("Model"), this);
    auto* modelLayout = new QHBoxLayout(modelGroup);
    auto* left = new QFormLayout;
    elementAEdit_ = new QLineEdit(modelGroup);
    elementAEdit_->setToolTip(tr("The x = 0 endpoint."));
    left->addRow(tr("Element A (x = 0):"), elementAEdit_);
    elementBEdit_ = new QLineEdit(modelGroup);
    elementBEdit_->setToolTip(tr("The x = 1 endpoint; x is its mole fraction."));
    left->addRow(tr("Element B (x = 1):"), elementBEdit_);
    phaseEdit_ = new QLineEdit(QStringLiteral("FCC_A1"), modelGroup);
    left->addRow(tr("Phase name:"), phaseEdit_);
    orderSpin_ = new QSpinBox(modelGroup);
    orderSpin_->setRange(0, 5);
    orderSpin_->setValue(2);
    orderSpin_->setToolTip(
        tr("Highest Redlich-Kister order. Order 0 is the regular solution; "
           "each further order adds an asymmetry the compositions must be "
           "able to resolve."));
    left->addRow(tr("Redlich-Kister order:"), orderSpin_);
    modelLayout->addLayout(left);

    auto* right = new QFormLayout;
    minTemperature_ = new QDoubleSpinBox(modelGroup);
    minTemperature_->setRange(1.0, 6000.0);
    minTemperature_->setValue(300.0);
    minTemperature_->setSuffix(tr(" K"));
    right->addRow(tr("Lowest temperature:"), minTemperature_);
    maxTemperature_ = new QDoubleSpinBox(modelGroup);
    maxTemperature_->setRange(1.0, 6000.0);
    maxTemperature_->setValue(1200.0);
    maxTemperature_->setSuffix(tr(" K"));
    right->addRow(tr("Highest temperature:"), maxTemperature_);
    temperatureSteps_ = new QSpinBox(modelGroup);
    temperatureSteps_->setRange(1, 50);
    temperatureSteps_->setValue(5);
    right->addRow(tr("Temperature samples:"), temperatureSteps_);
    temperatureDependent_ = new QCheckBox(
        tr("Fit the excess entropy (L = a + bT)"), modelGroup);
    temperatureDependent_->setChecked(true);
    temperatureDependent_->setToolTip(
        tr("Needs vibrational free energies: without them every temperature "
           "gives the same excess energy and b is not determined. The "
           "assessment demotes itself to static and says so."));
    right->addRow(QString(), temperatureDependent_);
    modelLayout->addLayout(right);
    layout->addWidget(modelGroup);

    fitLabel_ = new QLabel(this);
    fitLabel_->setWordWrap(true);
    fitLabel_->setTextFormat(Qt::RichText);
    layout->addWidget(fitLabel_);

    preview_ = new QPlainTextEdit(this);
    preview_->setReadOnly(true);
    preview_->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    preview_->setMinimumHeight(150);
    layout->addWidget(preview_, 1);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    saveButton_ =
        buttons->addButton(tr("Save .tdb…"), QDialogButtonBox::ActionRole);
    saveButton_->setEnabled(false);
    connect(saveButton_, &QPushButton::clicked, this,
            &TdbGeneratorDialog::saveDatabase);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    for (QPushButton* button : buttons->findChildren<QPushButton*>())
        button->setAutoDefault(false);
    layout->addWidget(buttons);

    // Only now that every control exists: each of these triggers refresh(),
    // and a refresh from a half-built dialog reads widgets that are still
    // null. That exact ordering hazard has crashed a wizard in this project
    // before, which is why the connections are made last rather than beside
    // the widgets they belong to.
    for (QLineEdit* edit : {elementAEdit_, elementBEdit_, phaseEdit_})
        connect(edit, &QLineEdit::textChanged, this,
                &TdbGeneratorDialog::refresh);
    connect(orderSpin_, &QSpinBox::valueChanged, this,
            &TdbGeneratorDialog::refresh);
    connect(temperatureSteps_, &QSpinBox::valueChanged, this,
            &TdbGeneratorDialog::refresh);
    for (QDoubleSpinBox* spin : {minTemperature_, maxTemperature_})
        connect(spin, &QDoubleSpinBox::valueChanged, this,
                &TdbGeneratorDialog::refresh);
    connect(temperatureDependent_, &QCheckBox::toggled, this,
            &TdbGeneratorDialog::refresh);

    refresh();
}

void TdbGeneratorDialog::browseForEnsemble()
{
    const QString path = QFileDialog::getOpenFileName(
        this, tr("Import Formation Energies"), QString(),
        tr("Cluster expansion (cluster_expansion.json);;JSON (*.json)"));
    if (!path.isEmpty())
        loadEnsemble(path);
}

bool TdbGeneratorDialog::loadEnsemble(const QString& path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        setStatus(tr("Could not open %1.").arg(QFileInfo(path).fileName()),
                  false);
        return false;
    }
    pathEdit_->setText(path);
    return loadEnsembleJson(QString::fromUtf8(file.readAll()),
                            QFileInfo(path).fileName());
}

bool TdbGeneratorDialog::loadEnsembleJson(const QString& json,
                                          const QString& label)
{
    QJsonParseError error{};
    const QJsonDocument document =
        QJsonDocument::fromJson(json.toUtf8(), &error);
    if (!document.isObject()) {
        setStatus(tr("%1 is not a results file — %2.")
                      .arg(label, error.errorString()),
                  false);
        return false;
    }
    const QJsonObject root = document.object();
    const QJsonArray configurations =
        root.value(QStringLiteral("configurations")).toArray();
    if (configurations.isEmpty()) {
        setStatus(tr("%1 carries no configurations. A cluster-expansion run "
                     "writes them into cluster_expansion.json alongside its "
                     "trajectory.")
                      .arg(label),
                  false);
        return false;
    }

    core::CalphadAssessmentInput input;
    QString formulaAtZero;
    QString formulaAtOne;
    double lowest = 2.0;
    double highest = -1.0;
    bool haveZero = false;
    bool haveOne = false;

    for (const QJsonValue& value : configurations) {
        const QJsonObject entry = value.toObject();
        // A configuration whose relaxation failed carries a null formation
        // energy. Dropped rather than read as zero, which would look like a
        // perfectly ideal alloy at that composition.
        const QJsonValue formation = entry.value(QStringLiteral("formation_energy"));
        if (formation.isNull() || formation.isUndefined())
            continue;
        const double x = entry.value(QStringLiteral("concentration")).toDouble();
        const double energy =
            entry.value(QStringLiteral("energy_per_atom")).toDouble();
        const QString formula = entry.value(QStringLiteral("formula")).toString();

        // The endpoints are the REFERENCES, not configurations: everything
        // else is measured against them, and letting one into the fit as well
        // would give it a zero row and a misleading residual.
        if (x <= 1e-9) {
            if (!haveZero || energy < input.referenceEnergyAEvPerAtom) {
                input.referenceEnergyAEvPerAtom = energy;
                formulaAtZero = formula;
                haveZero = true;
            }
            continue;
        }
        if (x >= 1.0 - 1e-9) {
            if (!haveOne || energy < input.referenceEnergyBEvPerAtom) {
                input.referenceEnergyBEvPerAtom = energy;
                formulaAtOne = formula;
                haveOne = true;
            }
            continue;
        }
        core::CalphadConfiguration config;
        config.label = formula.isEmpty()
            ? QStringLiteral("x = %1").arg(x, 0, 'f', 3).toStdString()
            : formula.toStdString();
        config.moleFractionB = x;
        config.energyEvPerAtom = energy;
        input.configurations.push_back(config);
        lowest = std::min(lowest, x);
        highest = std::max(highest, x);
    }

    if (input.configurations.empty()) {
        setStatus(tr("%1 has no configuration at an intermediate composition. "
                     "The two pure endpoints define the reference and nothing "
                     "else; an assessment needs alloys between them.")
                      .arg(label),
                  false);
        return false;
    }
    if (!haveZero || !haveOne) {
        setStatus(
            tr("%1 has no configuration at x = 0 or x = 1. Both pure "
               "endpoints are needed: the formation energies are referenced "
               "to them, and without one every interaction parameter absorbs "
               "the missing endpoint's energy.")
                .arg(label),
            false);
        return false;
    }

    // Endpoint names. The axis element the hull viewer records is the better
    // source; the formulas are the fallback.
    const QString axis =
        root.value(QStringLiteral("concentration_element")).toString();
    input.elementB = axis.isEmpty()
        ? leadingElement(formulaAtOne).toUpper().toStdString()
        : axis.toUpper().toStdString();
    input.elementA = leadingElement(formulaAtZero).toUpper().toStdString();
    if (input.elementA.empty())
        input.elementA = "A";
    if (input.elementB.empty() || input.elementB == input.elementA)
        input.elementB = "B";

    input_ = std::move(input);
    loaded_ = true;
    elementAEdit_->setText(QString::fromStdString(input_.elementA));
    elementBEdit_->setText(QString::fromStdString(input_.elementB));
    setStatus(tr("%1 — %n configuration(s) between x = %2 and x = %3, plus "
                 "both endpoints.",
                 nullptr, static_cast<int>(input_.configurations.size()))
                  .arg(label)
                  .arg(lowest, 0, 'f', 3)
                  .arg(highest, 0, 'f', 3),
              true);
    refresh();
    return true;
}

bool TdbGeneratorDialog::loadPhononDos(int row, const QString& path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        setStatus(tr("Could not open %1.").arg(QFileInfo(path).fileName()),
                  false);
        return false;
    }
    const QJsonObject root =
        QJsonDocument::fromJson(file.readAll()).object();
    const QJsonArray frequencies = root.value(QStringLiteral("frequencies")).toArray();
    const QJsonArray dos = root.value(QStringLiteral("dos")).toArray();
    if (frequencies.size() < 2 || dos.size() != frequencies.size()) {
        setStatus(tr("%1 is not a phonon density of states: it needs matching "
                     "\"frequencies\" (cm⁻¹) and \"dos\" arrays.")
                      .arg(QFileInfo(path).fileName()),
                  false);
        return false;
    }
    std::vector<double> omega;
    std::vector<double> weight;
    omega.reserve(static_cast<std::size_t>(frequencies.size()));
    weight.reserve(static_cast<std::size_t>(dos.size()));
    for (int i = 0; i < frequencies.size(); ++i) {
        omega.push_back(frequencies[i].toDouble());
        weight.push_back(dos[i].toDouble());
    }
    // HISTOGRAM -> DENSITY, exactly as PhononPlotWindow does before handing the
    // same file to the thermodynamics dialog. What the phonon script writes is
    // the mode weight IN each bin; computePhononThermodynamics integrates by
    // the trapezoidal rule and therefore wants g(ω), which is that weight
    // divided by the bin width. Skipping the division scales F_vib — and with
    // it every excess entropy fitted below — by a factor of the bin width,
    // which is ~5 cm⁻¹ and looks nothing like a unit error.
    //
    // "broadened" absent means a run from before the smearing moved into the
    // viewer: that curve is already a density and must not be divided again.
    const bool broadened =
        root.value(QStringLiteral("broadened")).toBool(true);
    double binWidth = root.value(QStringLiteral("bin_width")).toDouble(0.0);
    if (binWidth <= 0.0 && omega.size() > 1)
        binWidth = omega[1] - omega[0];
    if (!broadened && binWidth > 0.0)
        for (double& value : weight)
            value /= binWidth;

    // The temperature grid the assessment will use. Sampled here rather than
    // stored as a DOS so that changing the grid re-reads nothing — and so that
    // the harmonic integrals are the project's existing ones and not a second
    // copy of the same formulas.
    const int steps = std::max(1, temperatureSteps_->value());
    std::vector<double> temperatures;
    for (int i = 0; i < steps; ++i) {
        const double fraction = steps == 1
            ? 0.0
            : static_cast<double>(i) / static_cast<double>(steps - 1);
        temperatures.push_back(minTemperature_->value()
                               + fraction * (maxTemperature_->value()
                                             - minTemperature_->value()));
    }
    std::vector<double> free_energies;
    double modes = 0.0;
    for (const double temperature : temperatures) {
        const core::PhononThermoResult result = core::computePhononThermodynamics(
            omega, weight, temperature, temperature, 1);
        modes = result.totalModes;
        free_energies.push_back(result.points.front().freeEnergyEv);
    }
    // computePhononThermodynamics reports PER CELL, and the assessment works
    // per atom. A converged DOS integrates to 3N, so the mode count is the
    // divisor — and forgetting it scales the whole vibrational term by the
    // supercell size, which is the one error in this pipeline that looks
    // plausible at every step.
    const double atoms = modes > 0.0 ? modes / 3.0 : 1.0;
    for (double& value : free_energies)
        value /= atoms;

    if (row < 0)
        return false;
    // Table rows: 0 = endpoint A, 1..N = configurations, N+1 = endpoint B —
    // the same layout rebuildTable() writes, because the row the user
    // right-clicked IS the identifier here and a second numbering would be one
    // more thing to keep in step.
    const int configCount = static_cast<int>(input_.configurations.size());
    if (row == 0)
        input_.referenceVibAEvPerAtom = free_energies;
    else if (row == configCount + 1)
        input_.referenceVibBEvPerAtom = free_energies;
    else if (row >= 1 && row <= configCount)
        input_.configurations[static_cast<std::size_t>(row - 1)]
            .vibFreeEnergyEvPerAtom = free_energies;
    else
        return false;
    refresh();
    return true;
}

core::CalphadAssessmentInput TdbGeneratorDialog::buildInput() const
{
    core::CalphadAssessmentInput input = input_;
    input.elementA = elementAEdit_->text().trimmed().toUpper().toStdString();
    input.elementB = elementBEdit_->text().trimmed().toUpper().toStdString();
    input.phaseName = phaseEdit_->text().trimmed().toUpper().toStdString();
    if (input.elementA.empty())
        input.elementA = "A";
    if (input.elementB.empty())
        input.elementB = "B";
    if (input.phaseName.empty())
        input.phaseName = "SOLUTION";
    input.order = orderSpin_->value();
    input.temperatureDependent = temperatureDependent_->isChecked();

    const int steps = std::max(1, temperatureSteps_->value());
    input.temperaturesK.clear();
    for (int i = 0; i < steps; ++i) {
        const double fraction = steps == 1
            ? 0.0
            : static_cast<double>(i) / static_cast<double>(steps - 1);
        input.temperaturesK.push_back(
            minTemperature_->value()
            + fraction * (maxTemperature_->value() - minTemperature_->value()));
    }
    return input;
}

void TdbGeneratorDialog::refresh()
{
    if (!loaded_) {
        fitLabel_->setText(tr("<i>Import a cluster-expansion result to fit an "
                              "assessment.</i>"));
        preview_->setPlainText(QString());
        if (saveButton_)
            saveButton_->setEnabled(false);
        return;
    }
    const core::CalphadAssessmentInput input = buildInput();
    assessment_ = core::assessBinaryFromFirstPrinciples(input);
    rebuildTable();

    // A vibrational free energy is sampled on the temperature grid that was
    // current when it was attached. Moving the grid afterwards leaves arrays
    // of the wrong length, which the assessment correctly refuses to mix — but
    // it refuses by demoting itself to STATIC, and "no vibrational data" is
    // the wrong explanation when the user attached some and then nudged a spin
    // box. Named here rather than left to be inferred from a diagram that
    // quietly lost its excess entropy.
    const std::size_t grid = input.temperaturesK.size();
    bool stale = false;
    const auto mismatched = [grid](const std::vector<double>& values) {
        return !values.empty() && values.size() != grid;
    };
    stale = mismatched(input_.referenceVibAEvPerAtom)
        || mismatched(input_.referenceVibBEvPerAtom);
    for (const core::CalphadConfiguration& config : input_.configurations)
        if (mismatched(config.vibFreeEnergyEvPerAtom))
            stale = true;
    if (stale)
        setStatus(tr("The temperature grid changed after the phonon data was "
                     "attached, so it no longer lines up and is being ignored. "
                     "Re-attach the phonon_dos.json files, or put the grid "
                     "back."),
                  false);

    if (!assessment_.ok) {
        fitLabel_->setText(QStringLiteral("<span style='color:#c0392b;'>%1</span>")
                               .arg(QString::fromStdString(assessment_.note)
                                        .toHtmlEscaped()));
        preview_->setPlainText(QString());
        saveButton_->setEnabled(false);
        return;
    }

    QStringList terms;
    for (std::size_t nu = 0; nu < assessment_.fit.terms.size(); ++nu) {
        const core::RedlichKisterTerm& term = assessment_.fit.terms[nu];
        terms << tr("L<sub>%1</sub> = %2%3")
                     .arg(nu)
                     .arg(term.a, 0, 'f', 1)
                     .arg(term.b == 0.0
                              ? QString()
                              : tr(" %1 %2·T")
                                    .arg(term.b < 0.0 ? QStringLiteral("−")
                                                      : QStringLiteral("+"))
                                    .arg(std::fabs(term.b), 0, 'f', 4));
    }
    fitLabel_->setText(
        tr("%1 &nbsp;&nbsp; J/mol<br>RMS residual <b>%2 J/mol</b> "
           "(worst %3) over %4 sample(s).%5")
            .arg(terms.join(QStringLiteral(" &nbsp; ")))
            .arg(assessment_.fit.rmsResidualJPerMol, 0, 'f', 1)
            .arg(assessment_.fit.maxResidualJPerMol, 0, 'f', 1)
            .arg(assessment_.fit.usedSamples)
            .arg(assessment_.vibrational
                     ? tr("<br>Includes the harmonic vibrational free energy, "
                          "so the excess entropy is fitted.")
                     : tr("<br><i>Static: no vibrational data, so every excess "
                          "entropy is exactly zero.</i>")));

    preview_->setPlainText(databaseText());
    saveButton_->setEnabled(true);
}

QString TdbGeneratorDialog::databaseText() const
{
    if (!assessment_.ok)
        return QString();
    return QString::fromStdString(
        core::writeTdb(core::tdbOptionsForAssessment(buildInput(),
                                                     assessment_)));
}

void TdbGeneratorDialog::rebuildTable()
{
    const int configCount = static_cast<int>(input_.configurations.size());
    table_->setRowCount(configCount + 2);

    // Above-hull distances, keyed by the frame index the assessment recorded.
    // Read out of the hull rather than recomputed: the two must agree, and the
    // only way to guarantee that is not to have two of them.
    std::vector<double> aboveHull(static_cast<std::size_t>(configCount), 0.0);
    for (const core::HullPoint& point : assessment_.staticHull.points)
        if (point.frameIndex >= 0 && point.frameIndex < configCount)
            aboveHull[static_cast<std::size_t>(point.frameIndex)] =
                point.energyAboveHull;

    const auto setCell = [this](int row, int column, const QString& text) {
        auto* item = new QTableWidgetItem(text);
        item->setTextAlignment(column == ColumnLabel
                                   ? Qt::AlignLeft | Qt::AlignVCenter
                                   : Qt::AlignRight | Qt::AlignVCenter);
        table_->setItem(row, column, item);
    };
    const auto vibText = [](const std::vector<double>& values) {
        return values.empty() ? QObject::tr("—")
                              : QObject::tr("%1 pt").arg(values.size());
    };

    setCell(0, ColumnLabel,
            tr("%1 (endpoint)").arg(QString::fromStdString(input_.elementA)));
    setCell(0, ColumnComposition, QStringLiteral("0.000"));
    setCell(0, ColumnEnergy,
            QString::number(input_.referenceEnergyAEvPerAtom, 'f', 5));
    setCell(0, ColumnFormation, QStringLiteral("0.00000"));
    setCell(0, ColumnAboveHull, QStringLiteral("—"));
    setCell(0, ColumnPhonons, vibText(input_.referenceVibAEvPerAtom));

    for (int i = 0; i < configCount; ++i) {
        const core::CalphadConfiguration& config =
            input_.configurations[static_cast<std::size_t>(i)];
        const double formation = core::formationEnergyPerAtom(
            config.energyEvPerAtom, config.moleFractionB,
            input_.referenceEnergyAEvPerAtom, input_.referenceEnergyBEvPerAtom);
        setCell(i + 1, ColumnLabel, QString::fromStdString(config.label));
        setCell(i + 1, ColumnComposition,
                QString::number(config.moleFractionB, 'f', 3));
        setCell(i + 1, ColumnEnergy,
                QString::number(config.energyEvPerAtom, 'f', 5));
        setCell(i + 1, ColumnFormation, QString::number(formation, 'f', 5));
        setCell(i + 1, ColumnAboveHull,
                QString::number(aboveHull[static_cast<std::size_t>(i)], 'f', 5));
        setCell(i + 1, ColumnPhonons, vibText(config.vibFreeEnergyEvPerAtom));
    }

    setCell(configCount + 1, ColumnLabel,
            tr("%1 (endpoint)").arg(QString::fromStdString(input_.elementB)));
    setCell(configCount + 1, ColumnComposition, QStringLiteral("1.000"));
    setCell(configCount + 1, ColumnEnergy,
            QString::number(input_.referenceEnergyBEvPerAtom, 'f', 5));
    setCell(configCount + 1, ColumnFormation, QStringLiteral("0.00000"));
    setCell(configCount + 1, ColumnAboveHull, QStringLiteral("—"));
    setCell(configCount + 1, ColumnPhonons,
            vibText(input_.referenceVibBEvPerAtom));
}

void TdbGeneratorDialog::saveDatabase()
{
    const QString text = databaseText();
    if (text.isEmpty())
        return;
    const QString path = QFileDialog::getSaveFileName(
        this, tr("Save Thermodynamic Database"),
        QStringLiteral("%1-%2.tdb")
            .arg(QString::fromStdString(input_.elementA).toLower(),
                 QString::fromStdString(input_.elementB).toLower()),
        tr("Thermodynamic databases (*.tdb);;All files (*)"));
    if (path.isEmpty())
        return;
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        setStatus(tr("Could not write %1.").arg(QFileInfo(path).fileName()),
                  false);
        return;
    }
    QTextStream(&file) << text;
    setStatus(tr("Wrote %1.").arg(QFileInfo(path).fileName()), true);
}

void TdbGeneratorDialog::setStatus(const QString& text, bool ok)
{
    statusLabel_->setText(text);
    statusLabel_->setStyleSheet(ok ? QStringLiteral("color: gray;")
                                   : QStringLiteral("color: #c0392b;"));
}

} // namespace calango::gui
