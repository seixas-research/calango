#include "gui/PreferencesDialog.hpp"

#include "gui/CondaEnvs.hpp"
#include "gui/EnginePresets.hpp"
#include "gui/GuiUtils.hpp"
#include "gui/RunCommands.hpp"
#include "gui/EnvFile.hpp"
#include "gui/SettingsManager.hpp"
#include "gui/ShortcutRegistry.hpp"
#include "gui/ThemeManager.hpp"

#include <QAbstractItemView>
#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QKeySequenceEdit>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QSettings>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QTableWidget>

#include <QTabWidget>
#include <QVBoxLayout>

namespace calango::gui {

// The "Rendering" page is gone. Its two useful choices moved to where the work
// happens — the shading model to the Representation panel's "Shading" row, the
// isosurface profile to a checkbox in "Edit Volumetric Render" — and what was
// left was per-slot plumbing plus a driver read-out.
//
// The impostor geometry paths and the GL_RENDERER summary therefore have no UI
// now. The profile ids are still readable and editable as render/atomShaderProfile
// and render/bondShaderProfile in ~/.calango/settings.json.
PreferencesDialog::PreferencesDialog(QWidget* parent)
    : QDialog(parent)
    , envPathEdit_(new QLineEdit(this))
    , statusLabel_(new QLabel(this))
{
    setWindowTitle(tr("Preferences"));
    // Sized so no page has to scroll or elide at the default font. The Run tab
    // shows full shell command templates and the Python tab full interpreter
    // paths — both are long single lines that a narrow dialog turns into
    // ellipses, which is the one thing a settings field must never do.
    resize(820, 640);

    auto* envGroup = new QGroupBox(tr("Environment File (.env)"), this);
    auto* envLayout = new QVBoxLayout(envGroup);

    auto* note = new QLabel(
        tr("Read at launch to export MP_API_KEY (Materials Project) into the "
           "environment."),
        envGroup);
    note->setWordWrap(true);
    // The precedence rule is what someone comes back to check, but it is not
    // what they need on first read — so it hovers rather than occupying two
    // more wrapped lines above the path field.
    note->setToolTip(
        tr("A key already set in the shell takes precedence at startup. "
           "\"Reload Now\" applies the file unconditionally."));
    envLayout->addWidget(note);

    auto* pathRow = new QHBoxLayout;
    envPathEdit_->setText(envFilePath());
    auto* browseButton = new QPushButton(tr("Browse…"), envGroup);
    auto* reloadButton = new QPushButton(tr("Reload Now"), envGroup);
    pathRow->addWidget(envPathEdit_, 1);
    pathRow->addWidget(browseButton);
    pathRow->addWidget(reloadButton);
    envLayout->addLayout(pathRow);

    statusLabel_->setWordWrap(true);
    envLayout->addWidget(statusLabel_);

    connect(envPathEdit_, &QLineEdit::textChanged, this, [this](const QString& path) {
        setEnvFilePath(path.trimmed());
        updateStatus();
    });
    connect(browseButton, &QPushButton::clicked,
            this, &PreferencesDialog::browseEnvFile);
    connect(reloadButton, &QPushButton::clicked,
            this, &PreferencesDialog::reloadEnvFile);

    // -- Appearance ---------------------------------------------------------
    auto* appearanceGroup = new QGroupBox(tr("Appearance"), this);
    auto* appearanceForm = new QFormLayout(appearanceGroup);
    themeCombo_ = new QComboBox(appearanceGroup);
    // Order mirrors ThemeManager::Theme.
    themeCombo_->addItem(tr("System Default"),
                         ThemeManager::toString(ThemeManager::Theme::System));
    themeCombo_->addItem(tr("Dark Mode"),
                         ThemeManager::toString(ThemeManager::Theme::Dark));
    themeCombo_->addItem(tr("Light Mode"),
                         ThemeManager::toString(ThemeManager::Theme::Light));
    themeCombo_->setCurrentIndex(
        static_cast<int>(ThemeManager::current()));
    appearanceForm->addRow(tr("Theme:"), themeCombo_);
    connect(themeCombo_, &QComboBox::currentIndexChanged, this, [this](int) {
        QSettings().setValue(QLatin1String(SettingsManager::kTheme),
                             themeCombo_->currentData().toString());
    });

    // -- Computation --------------------------------------------------------
    auto* computeGroup = new QGroupBox(tr("Computation"), this);
    auto* computeLayout = new QVBoxLayout(computeGroup);
    auto* computeForm = new QFormLayout;
    computeLayout->addLayout(computeForm);

    threadsSpin_ = new QSpinBox(computeGroup);
    threadsSpin_->setRange(0, 1024);
    threadsSpin_->setSpecialValueText(tr("auto (library default)"));
    threadsSpin_->setValue(
        QSettings().value(QLatin1String(SettingsManager::kOmpThreads), 1)
            .toInt());
    threadsSpin_->setToolTip(
        tr("OMP_NUM_THREADS injected into simulation runs (also MKL / "
           "OpenBLAS). 0 leaves the libraries to auto-detect."));
    computeForm->addRow(tr("ASE threads (OMP_NUM_THREADS):"), threadsSpin_);
    connect(threadsSpin_, QOverload<int>::of(&QSpinBox::valueChanged), this,
            [](int value) {
                QSettings().setValue(
                    QLatin1String(SettingsManager::kOmpThreads), value);
            });

    auto* condaRow = new QHBoxLayout;
    condaDirEdit_ = new QLineEdit(computeGroup);
    condaDirEdit_->setText(
        QSettings().value(QLatin1String(SettingsManager::kCondaDir)).toString());
    condaDirEdit_->setPlaceholderText(
        tr("e.g. ~/miniconda3/envs or /opt/anaconda3/envs (empty = auto)"));
    auto* condaBrowse = new QPushButton(tr("Browse…"), computeGroup);
    condaRow->addWidget(condaDirEdit_, 1);
    condaRow->addWidget(condaBrowse);
    computeForm->addRow(tr("Conda Directory Path:"), condaRow);
    condaStatusLabel_ = new QLabel(computeGroup);
    condaStatusLabel_->setWordWrap(true);
    computeLayout->addWidget(condaStatusLabel_);
    connect(condaDirEdit_, &QLineEdit::textChanged, this, [this](const QString& t) {
        QSettings().setValue(QLatin1String(SettingsManager::kCondaDir), t.trimmed());
        updateCondaStatus();
    });
    connect(condaBrowse, &QPushButton::clicked, this,
            &PreferencesDialog::browseCondaDir);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    // -- General ------------------------------------------------------------
    // -- Simulation files ---------------------------------------------------
    auto* storageGroup = new QGroupBox(tr("Simulation Files"), this);
    auto* storageLayout = new QVBoxLayout(storageGroup);

    auto* storageRow = new QHBoxLayout;
    simulationsDirEdit_ = new QLineEdit(storageGroup);
    simulationsDirEdit_->setText(
        QSettings()
            .value(QLatin1String(SettingsManager::kSimulationsDir))
            .toString());
    simulationsDirEdit_->setPlaceholderText(
        SettingsManager::defaultSimulationsDirectory());
    simulationsDirEdit_->setToolTip(
        tr("Where a run's working directory is created — its script, logs, "
           "metrics and trajectory.\n\n"
           "Leave empty to use %1. Jobs used to go to the platform's "
           "application-data folder, which is the right place for the "
           "application's own state and the wrong place for output you want "
           "to find again.")
            .arg(SettingsManager::defaultSimulationsDirectory()));
    storageRow->addWidget(simulationsDirEdit_, 1);

    auto* browseSimulations = new QPushButton(tr("Browse…"), storageGroup);
    connect(browseSimulations, &QPushButton::clicked, this, [this] {
        const QString start = simulationsDirEdit_->text().trimmed().isEmpty()
            ? SettingsManager::defaultSimulationsDirectory()
            : simulationsDirEdit_->text();
        const QString chosen = QFileDialog::getExistingDirectory(
            this, tr("Simulation files directory"), start);
        if (!chosen.isEmpty()) {
            simulationsDirEdit_->setText(chosen);
            updateSimulationsStatus();
        }
    });
    storageRow->addWidget(browseSimulations);

    auto* resetSimulations = new QPushButton(tr("Reset"), storageGroup);
    resetSimulations->setToolTip(tr("Return to %1.")
                                    .arg(SettingsManager::defaultSimulationsDirectory()));
    connect(resetSimulations, &QPushButton::clicked, this, [this] {
        simulationsDirEdit_->clear();
        updateSimulationsStatus();
    });
    storageRow->addWidget(resetSimulations);
    storageLayout->addLayout(storageRow);

    simulationsStatusLabel_ = new QLabel(storageGroup);
    simulationsStatusLabel_->setWordWrap(true);
    simulationsStatusLabel_->setTextFormat(Qt::RichText);
    storageLayout->addWidget(simulationsStatusLabel_);

    connect(simulationsDirEdit_, &QLineEdit::textChanged, this, [this] {
        QSettings().setValue(QLatin1String(SettingsManager::kSimulationsDir),
                             simulationsDirEdit_->text().trimmed());
        updateSimulationsStatus();
    });

    auto* generalGroup = new QGroupBox(tr("General"), this);
    auto* generalLayout = new QVBoxLayout(generalGroup);
    auto* welcomeCheck = new QCheckBox(tr("Show Welcome Screen on Startup"),
                                       generalGroup);
    welcomeCheck->setToolTip(
        tr("Re-enable the Welcome Screen if you previously dismissed it. Bound "
           "to show_welcome_screen in ~/.calango/settings.json."));
    welcomeCheck->setChecked(
        QSettings().value(QLatin1String(SettingsManager::kShowWelcome), true)
            .toBool());
    generalLayout->addWidget(welcomeCheck);
    connect(welcomeCheck, &QCheckBox::toggled, this, [](bool on) {
        QSettings().setValue(QLatin1String(SettingsManager::kShowWelcome), on);
        SettingsManager::save(); // flush to settings.json immediately
    });

    // Group the existing setting boxes into a "General" tab page.
    auto* generalPage = new QWidget(this);
    auto* generalPageLayout = new QVBoxLayout(generalPage);
    generalPageLayout->addWidget(generalGroup);
    generalPageLayout->addWidget(envGroup);
    generalPageLayout->addWidget(appearanceGroup);
    generalPageLayout->addWidget(storageGroup);
    generalPageLayout->addWidget(computeGroup);
    generalPageLayout->addStretch(1);

    auto* tabs = new QTabWidget(this);
    tabs->addTab(generalPage, tr("General"));
    tabs->addTab(buildPythonEnvTab(), tr("Python && Environments"));
    // Directly after Python & Environments, and shaped like it: both answer
    // "where does this machine keep the thing a run needs", one for the
    // interpreter, one for the data files.
    tabs->addTab(buildExternalFilesTab(), tr("External Files"));
    tabs->addTab(buildRunTab(), tr("Run"));
    tabs->addTab(buildHotkeysTab(), tr("Hotkeys"));

    auto* layout = new QVBoxLayout(this);
    layout->addWidget(tabs, 1);
    layout->addWidget(buttons);

    updateStatus();
    updateCondaStatus();
    updateSimulationsStatus();
}

void PreferencesDialog::updateSimulationsStatus()
{
    if (!simulationsStatusLabel_)
        return;
    const QString configured = simulationsDirEdit_->text().trimmed();
    const QString effective = SettingsManager::simulationsDirectory();
    const bool usingDefault = configured.isEmpty();
    // Report the path that will ACTUALLY be used, not the one that was typed:
    // an unusable directory falls back silently in the resolver, and a user
    // who is not told would look for their output in the wrong place.
    const bool fellBack =
        !usingDefault && QDir(effective) != QDir(configured);
    QString text = usingDefault
        ? tr("Using the default: <b>%1</b>").arg(effective)
        : tr("Runs will be written to <b>%1</b>").arg(effective);
    if (fellBack)
        text = tr("<span style='color:#d9534f;'>⚠ <b>%1</b> is not usable "
                  "(missing, not a directory, or not writable). Falling back "
                  "to <b>%2</b>.</span>")
                   .arg(configured, effective);
    text += QStringLiteral("<br><i>%1</i>")
                .arg(tr("Applies to unsaved sessions and to new runs only; a "
                        "saved project uses .calango_tmp/ beside its .calproj."));
    simulationsStatusLabel_->setText(text);
    // The consequences the one-liner drops: nothing is migrated, and the
    // per-project store is what keeps a .calproj self-contained.
    simulationsStatusLabel_->setToolTip(
        tr("Existing job folders are not moved when this changes.\n\n"
           "A saved project keeps its runs beside the .calproj so the project "
           "stays self-contained."));
}

QWidget* PreferencesDialog::buildExternalFilesTab()
{
    auto* page = new QWidget(this);
    auto* layout = new QVBoxLayout(page);

    auto* note = new QLabel(
        tr("Where this machine keeps the data files a run needs but does not "
           "generate. Set once here rather than per run."),
        page);
    note->setWordWrap(true);
    note->setToolTip(
        tr("A job that silently used the wrong pseudopotential set produces a "
           "plausible number, not an error — which is why this is configured "
           "once, centrally.\n\n"
           "Each path is exported as the environment variable its engine "
           "already reads, so a blank row leaves the environment untouched and "
           "whatever the shell already exports keeps working."));
    layout->addWidget(note);

    // Same two-column table as the Python page: name on the left, an editable
    // value with its own browse button on the right.
    struct Row {
        const char* key;
        QString label;
        QString tip;
    };
    const QVector<Row> rows = {
        {SettingsManager::kPseudopotentialsVasp, tr("VASP (VASP_PP_PATH)"),
         tr("The directory CONTAINING the potpaw_* folders, not one of them: "
            "VASP appends potpaw_PBE/<element>/POTCAR to this. Licensed "
            "separately from the code, so it is never where the binary is.")},
        {SettingsManager::kPseudopotentialsEspresso,
         tr("Quantum Espresso (ESPRESSO_PSEUDO)"),
         tr("Directory of .UPF files. Mixing generations of the same library "
            "in one run is the usual cause of an irreproducible total "
            "energy, so one directory per library is worth keeping.")},
        {SettingsManager::kPseudopotentialsSiesta, tr("SIESTA (SIESTA_PP_PATH)"),
         tr("Directory of .psf / .psml files, named by element symbol.")},
        {SettingsManager::kGpawLcaoBasisDir,
         tr("GPAW custom LCAO basis sets (GPAW_SETUP_PATH)"),
         tr("Directory of custom GPAW LCAO basis files, named "
            "<symbol>.<name>.basis — the form GPAW looks for when a "
            "calculation asks for basis=\"<name>\".\n\n"
            "Only needed for basis sets that did not ship with GPAW: the "
            "built-in sz / dz / dzp / szp come with the datasets and need "
            "nothing here.\n\n"
            "This is PREPENDED to any GPAW_SETUP_PATH already set rather than "
            "replacing it — the same search list carries the PAW datasets, so "
            "overwriting it would find the basis and lose the setups.")},
        {SettingsManager::kMlPotentialsDir, tr("ML potentials"),
         tr("Directory of machine-learning potential checkpoints "
            "(.model / .pt). Used to pre-fill the model-file browser in the "
            "MACE and MLIP calculator groups; the foundation models download "
            "and cache their own weights and do not need it.")},
    };

    externalFilesTable_ = new QTableWidget(rows.size(), 2, page);
    externalFilesTable_->setHorizontalHeaderLabels(
        {tr("Resource"), tr("Directory (blank = leave the environment alone)")});
    externalFilesTable_->verticalHeader()->setVisible(false);
    externalFilesTable_->setSelectionMode(QAbstractItemView::NoSelection);
    externalFilesTable_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    externalFilesTable_->horizontalHeader()->setSectionResizeMode(
        0, QHeaderView::ResizeToContents);
    externalFilesTable_->horizontalHeader()->setSectionResizeMode(
        1, QHeaderView::Stretch);

    for (int row = 0; row < rows.size(); ++row) {
        const Row& spec = rows.at(row);
        const QString key = QLatin1String(spec.key);

        auto* nameItem = new QTableWidgetItem(spec.label);
        nameItem->setFlags(Qt::ItemIsEnabled);
        if (!spec.tip.isEmpty())
            nameItem->setToolTip(spec.tip);
        externalFilesTable_->setItem(row, 0, nameItem);

        auto* cell = new QWidget(externalFilesTable_);
        auto* cellLayout = new QHBoxLayout(cell);
        cellLayout->setContentsMargins(2, 0, 2, 0);
        cellLayout->setSpacing(4);
        auto* edit = new QLineEdit(cell);
        edit->setText(QSettings().value(key).toString());
        edit->setPlaceholderText(
            row == rows.size() - 1
                ? tr("e.g. ~/models — where trained ML potentials are saved "
                     "and loaded")
                : tr("e.g. ~/pseudos/…"));
        if (!spec.tip.isEmpty())
            edit->setToolTip(spec.tip);
        cellLayout->addWidget(edit, 1);
        auto* browse = new QPushButton(tr("Browse…"), cell);
        cellLayout->addWidget(browse);
        externalFilesTable_->setCellWidget(row, 1, cell);

        // Written on every keystroke, like the other path fields on this
        // dialog: there is no OK button to defer to, and SettingsManager::save()
        // mirrors QSettings to settings.json when the dialog closes.
        connect(edit, &QLineEdit::textChanged, this, [key](const QString& text) {
            QSettings().setValue(key, text.trimmed());
        });
        connect(browse, &QPushButton::clicked, this,
                [this, edit, label = spec.label] {
                    const QString chosen = QFileDialog::getExistingDirectory(
                        this, label, edit->text().trimmed());
                    if (!chosen.isEmpty())
                        edit->setText(chosen);
                });
    }
    layout->addWidget(externalFilesTable_, 1);
    return page;
}

QWidget* PreferencesDialog::buildPythonEnvTab()
{
    auto* page = new QWidget(this);
    auto* layout = new QVBoxLayout(page);

    auto* note = new QLabel(
        tr("Map each engine to the Conda environment (or python executable) "
           "its jobs run in. Blank falls back to $PATH."),
        page);
    note->setWordWrap(true);
    note->setToolTip(
        tr("The simulation wizards resolve these automatically, so there is no "
           "per-run prompting. A blank row uses the active $PATH or the "
           "embedded interpreter."));
    layout->addWidget(note);

    const auto& engines = EnginePresets::configurableEngines();
    const auto discovered = CondaEnvs::discover();

    engineEnvTable_ = new QTableWidget(engines.size(), 2, page);
    engineEnvTable_->setHorizontalHeaderLabels(
        {tr("Engine"), tr("Conda environment / python (blank = system $PATH)")});
    engineEnvTable_->verticalHeader()->setVisible(false);
    engineEnvTable_->setSelectionMode(QAbstractItemView::NoSelection);
    engineEnvTable_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    engineEnvTable_->horizontalHeader()->setSectionResizeMode(
        0, QHeaderView::ResizeToContents);
    engineEnvTable_->horizontalHeader()->setSectionResizeMode(
        1, QHeaderView::Stretch);

    for (int row = 0; row < engines.size(); ++row) {
        const core::CalculatorKind kind = engines.at(row);

        auto* nameItem =
            new QTableWidgetItem(EnginePresets::displayName(kind));
        nameItem->setFlags(Qt::ItemIsEnabled);
        engineEnvTable_->setItem(row, 0, nameItem);

        // An editable combo per row: free-text path plus the discovered envs as
        // quick-pick entries. currentText is the persisted value.
        auto* combo = new QComboBox(engineEnvTable_);
        combo->setEditable(true);
        combo->addItem(QString()); // empty = system / embedded
        for (const auto& env : discovered)
            combo->addItem(env.path);
        combo->setCurrentText(EnginePresets::envFor(kind));
        combo->lineEdit()->setPlaceholderText(
            tr("e.g. ~/miniconda3/envs/gpaw_env (blank = system $PATH)"));

        // Persist on any edit (typed or picked).
        connect(combo, &QComboBox::currentTextChanged, this,
                [kind](const QString& text) {
                    EnginePresets::setEnvFor(kind, text.trimmed());
                });

        auto* cellWidget = new QWidget(engineEnvTable_);
        auto* cellLayout = new QHBoxLayout(cellWidget);
        cellLayout->setContentsMargins(2, 2, 2, 2);
        cellLayout->addWidget(combo, 1);
        auto* browse = new QPushButton(tr("Browse…"), cellWidget);
        cellLayout->addWidget(browse);
        connect(browse, &QPushButton::clicked, this, [this, combo] {
            const QString dir = QFileDialog::getExistingDirectory(
                this, tr("Select Conda Environment Folder"),
                combo->currentText());
            if (!dir.isEmpty())
                combo->setCurrentText(dir);
        });
        engineEnvTable_->setCellWidget(row, 1, cellWidget);
    }

    layout->addWidget(engineEnvTable_, 1);
    return page;
}

void PreferencesDialog::browseEnvFile()
{
    const QString path = QFileDialog::getOpenFileName(
        this, tr("Select Environment File"), envFilePath(),
        tr("Environment files (*.env .env);;All files (*)"));
    if (!path.isEmpty())
        envPathEdit_->setText(path);
}

void PreferencesDialog::reloadEnvFile()
{
    loadEnvironmentFile(/*overrideExisting=*/true);
    updateStatus();
}

void PreferencesDialog::browseCondaDir()
{
    const QString dir = QFileDialog::getExistingDirectory(
        this, tr("Select Conda Directory (root or envs folder)"),
        condaDirEdit_->text());
    if (!dir.isEmpty())
        condaDirEdit_->setText(dir);
}

void PreferencesDialog::updateCondaStatus()
{
    const auto envs = CondaEnvs::discover();
    const QString dir = CondaEnvs::envsDirectory();
    if (envs.isEmpty()) {
        condaStatusLabel_->setStyleSheet(QStringLiteral("color: #b07d2a;"));
        condaStatusLabel_->setText(
            dir.isEmpty()
                ? tr("No conda environments directory found.")
                : tr("No environments found under %1").arg(dir));
        return;
    }
    QStringList names;
    for (const auto& env : envs)
        names << env.name;
    condaStatusLabel_->setStyleSheet(QString());
    condaStatusLabel_->setText(
        tr("%n environment(s) found: %1", nullptr, static_cast<int>(envs.size()))
            .arg(names.join(QStringLiteral(", "))));
}

void PreferencesDialog::updateStatus()
{
    const QString path = envFilePath();
    if (!QFileInfo::exists(path)) {
        statusLabel_->setStyleSheet(QStringLiteral("color: #b07d2a;"));
        statusLabel_->setText(tr("File not found: %1").arg(path));
        return;
    }
    const auto values = parseEnvFile(path);
    if (values.contains(QStringLiteral("MP_API_KEY"))
        && !values.value(QStringLiteral("MP_API_KEY")).isEmpty()) {
        statusLabel_->setStyleSheet(QString());
        statusLabel_->setText(tr("MP_API_KEY found (%n entries in file).", nullptr,
                                 static_cast<int>(values.size())));
    } else {
        statusLabel_->setStyleSheet(QStringLiteral("color: #b07d2a;"));
        statusLabel_->setText(tr("File read, but it defines no MP_API_KEY "
                                 "(%n entries).", nullptr,
                                 static_cast<int>(values.size())));
    }
}


QWidget* PreferencesDialog::buildRunTab()
{
    auto* page = new QWidget(this);
    auto* layout = new QVBoxLayout(page);

    // The placeholder list stays visible — it is reference material consulted
    // WHILE typing a template, and a tooltip that covers the field you are
    // typing into is the wrong place for it. What moved to the tooltip is the
    // paragraph explaining which of the two dispatch routes a template picks:
    // read once, then rarely.
    auto* note = new QLabel(
        tr("Shell command each engine's jobs are launched with. Placeholders: "
           "{cores} ranks, {script}, {python}, {input}/{output}."),
        page);
    note->setWordWrap(true);
    note->setTextFormat(Qt::RichText);
    note->setToolTip(
        tr("A template containing {script} launches the script itself — that "
           "is how a parallel GPAW run works.\n\n"
           "A template with {input}/{output} is instead handed to ASE as the "
           "solver command (ASE_ESPRESSO_COMMAND and friends), because running "
           "the whole Python script under mpirun would start N independent "
           "copies of it rather than one script driving an N-rank solver."));
    layout->addWidget(note);

    auto* coresRow = new QHBoxLayout;
    coresRow->addWidget(new QLabel(tr("Cores ({cores}):"), page));
    runCoresSpin_ = new QSpinBox(page);
    runCoresSpin_->setRange(1, 4096);
    runCoresSpin_->setValue(RunCommands::cores());
    runCoresSpin_->setToolTip(
        tr("MPI rank count substituted for {cores}. Defaults to half the "
           "machine's cores: a template that silently claimed every core "
           "would oversubscribe a machine already running something else."));
    coresRow->addWidget(runCoresSpin_);
    coresRow->addStretch(1);
    layout->addLayout(coresRow);
    connect(runCoresSpin_, QOverload<int>::of(&QSpinBox::valueChanged), this,
            [](int value) { RunCommands::setCores(value); });

    const auto& engines = EnginePresets::configurableEngines();
    runCommandTable_ = new QTableWidget(engines.size(), 2, page);
    runCommandTable_->setHorizontalHeaderLabels(
        {tr("Engine"), tr("Execution command template")});
    runCommandTable_->verticalHeader()->setVisible(false);
    runCommandTable_->setSelectionMode(QAbstractItemView::NoSelection);
    runCommandTable_->horizontalHeader()->setSectionResizeMode(
        0, QHeaderView::ResizeToContents);
    runCommandTable_->horizontalHeader()->setSectionResizeMode(
        1, QHeaderView::Stretch);
    // A dead key on an item view recurses through type-to-edit until the
    // stack dies; enforced by the dialog-construction test. Column 1's cells
    // are QLineEdit widgets (safe on their own), but column 0's plain,
    // read-only items are not, and either can be the view's current index.
    disableTypeToEdit(runCommandTable_);

    for (int row = 0; row < engines.size(); ++row) {
        const core::CalculatorKind kind = engines.at(row);

        auto* nameItem = new QTableWidgetItem(EnginePresets::displayName(kind));
        nameItem->setFlags(Qt::ItemIsEnabled);
        runCommandTable_->setItem(row, 0, nameItem);

        auto* edit = new QLineEdit(runCommandTable_);
        edit->setText(RunCommands::templateFor(kind));
        edit->setPlaceholderText(RunCommands::defaultTemplate(kind));
        const QString variable = RunCommands::solverCommandVariable(kind);
        edit->setToolTip(
            variable.isEmpty()
                ? tr("Launches the generated script directly.")
                : tr("Without {script} this is exported as %1 for ASE to run "
                     "the solver with.").arg(variable));
        runCommandTable_->setCellWidget(row, 1, edit);
        connect(edit, &QLineEdit::editingFinished, this, [edit, kind] {
            RunCommands::setTemplateFor(kind, edit->text());
            // Clearing the field falls back to the default, so show what will
            // actually run rather than leaving the row looking unset.
            edit->setText(RunCommands::templateFor(kind));
        });
    }
    layout->addWidget(runCommandTable_, 1);

    auto* restore = new QPushButton(tr("Restore Defaults"), page);
    restore->setToolTip(
        tr("Reset every engine to its shipped command template."));
    connect(restore, &QPushButton::clicked, this, [this, engines] {
        for (int row = 0; row < engines.size(); ++row) {
            const core::CalculatorKind kind = engines.at(row);
            RunCommands::setTemplateFor(kind, QString());
            if (auto* edit = qobject_cast<QLineEdit*>(
                    runCommandTable_->cellWidget(row, 1)))
                edit->setText(RunCommands::defaultTemplate(kind));
        }
    });
    auto* restoreRow = new QHBoxLayout;
    restoreRow->addStretch(1);
    restoreRow->addWidget(restore);
    layout->addLayout(restoreRow);

    return page;
}

QWidget* PreferencesDialog::buildHotkeysTab()
{
    auto* page = new QWidget(this);
    auto* layout = new QVBoxLayout(page);

    auto* note = new QLabel(
        tr("Click a row's shortcut field, then press the new key or "
           "combination. A key already used elsewhere — including by a "
           "fixed, non-remappable shortcut — is refused rather than "
           "silently duplicated."),
        page);
    note->setWordWrap(true);
    layout->addWidget(note);

    const QVector<ShortcutAction>& actions = ShortcutRegistry::actions();
    hotkeyTable_ = new QTableWidget(actions.size(), 3, page);
    hotkeyTable_->setHorizontalHeaderLabels(
        {tr("Action"), tr("Shortcut"), QString()});
    hotkeyTable_->verticalHeader()->setVisible(false);
    hotkeyTable_->setSelectionMode(QAbstractItemView::NoSelection);
    hotkeyTable_->horizontalHeader()->setSectionResizeMode(
        0, QHeaderView::ResizeToContents);
    hotkeyTable_->horizontalHeader()->setSectionResizeMode(
        1, QHeaderView::Stretch);
    hotkeyTable_->horizontalHeader()->setSectionResizeMode(
        2, QHeaderView::ResizeToContents);
    // A dead key on an item view recurses through type-to-edit until the
    // stack dies; enforced by the dialog-construction test.
    disableTypeToEdit(hotkeyTable_);

    hotkeyEdits_.clear();
    hotkeyEdits_.reserve(actions.size());
    for (int row = 0; row < actions.size(); ++row) {
        const ShortcutAction& action = actions.at(row);

        auto* nameItem = new QTableWidgetItem(action.label);
        nameItem->setFlags(Qt::ItemIsEnabled);
        nameItem->setToolTip(action.category);
        hotkeyTable_->setItem(row, 0, nameItem);

        auto* edit = new QKeySequenceEdit(ShortcutRegistry::binding(action.id),
                                          hotkeyTable_);
        // These are all single-press actions (a mouse mode, undo, next tab,
        // ...) — capping the recorder at one key/chord keeps a stray second
        // keystroke from being silently folded into a two-chord sequence
        // nothing here would ever match against.
        edit->setMaximumSequenceLength(1);
        edit->setToolTip(action.category);
        hotkeyTable_->setCellWidget(row, 1, edit);
        hotkeyEdits_.push_back(edit);

        const QString id = action.id;
        connect(edit, &QKeySequenceEdit::editingFinished, this,
                [this, edit, id] {
                    const QKeySequence proposed = edit->keySequence();
                    const QString clash =
                        ShortcutRegistry::conflictFor(proposed, id);
                    if (!clash.isEmpty()) {
                        QMessageBox::warning(
                            this, tr("Shortcut already in use"),
                            tr("%1 is already bound to \"%2\". Choose a "
                               "different key, or clear this one first.")
                                .arg(proposed.toString(QKeySequence::NativeText),
                                     clash));
                        // Refuse the silent duplicate: revert to whatever is
                        // actually bound rather than accept the collision.
                        const QSignalBlocker blocker(edit);
                        edit->setKeySequence(ShortcutRegistry::binding(id));
                        return;
                    }
                    ShortcutRegistry::setBinding(id, proposed);
                    refreshHotkeyConflicts();
                });

        auto* reset = new QPushButton(tr("Reset"), hotkeyTable_);
        reset->setToolTip(
            tr("Restore \"%1\" to %2.")
                .arg(action.label,
                     action.defaultKey.isEmpty()
                         ? tr("no shortcut")
                         : action.defaultKey.toString(QKeySequence::NativeText)));
        const QKeySequence defaultKey = action.defaultKey;
        connect(reset, &QPushButton::clicked, this,
                [this, edit, id, defaultKey] {
                    ShortcutRegistry::resetToDefault(id);
                    const QSignalBlocker blocker(edit);
                    edit->setKeySequence(defaultKey);
                    refreshHotkeyConflicts();
                });
        hotkeyTable_->setCellWidget(row, 2, reset);
    }
    layout->addWidget(hotkeyTable_, 1);

    hotkeyConflictLabel_ = new QLabel(page);
    hotkeyConflictLabel_->setWordWrap(true);
    hotkeyConflictLabel_->setStyleSheet(QStringLiteral("color: #d9534f;"));
    layout->addWidget(hotkeyConflictLabel_);

    auto* restoreAll = new QPushButton(tr("Reset All to Defaults"), page);
    connect(restoreAll, &QPushButton::clicked, this, [this]() {
        ShortcutRegistry::resetAllToDefaults();
        const QVector<ShortcutAction>& current = ShortcutRegistry::actions();
        for (int row = 0; row < current.size() && row < hotkeyEdits_.size();
             ++row) {
            const QSignalBlocker blocker(hotkeyEdits_.at(row));
            hotkeyEdits_.at(row)->setKeySequence(current.at(row).defaultKey);
        }
        refreshHotkeyConflicts();
    });
    auto* restoreAllRow = new QHBoxLayout;
    restoreAllRow->addStretch(1);
    restoreAllRow->addWidget(restoreAll);
    layout->addLayout(restoreAllRow);

    refreshHotkeyConflicts();
    return page;
}

void PreferencesDialog::refreshHotkeyConflicts()
{
    if (!hotkeyConflictLabel_)
        return;
    const QVector<ShortcutAction>& actions = ShortcutRegistry::actions();
    QStringList problems;
    for (int i = 0; i < actions.size(); ++i) {
        const QKeySequence key = ShortcutRegistry::binding(actions.at(i).id);
        if (key.isEmpty())
            continue;
        for (int j = i + 1; j < actions.size(); ++j) {
            if (ShortcutRegistry::binding(actions.at(j).id) == key) {
                problems << tr("\"%1\" and \"%2\" are both bound to %3 — a "
                              "hand-edited settings file, most likely; fix "
                              "one of them above.")
                                .arg(actions.at(i).label, actions.at(j).label,
                                     key.toString(QKeySequence::NativeText));
            }
        }
    }
    hotkeyConflictLabel_->setText(problems.join(QStringLiteral("\n")));
    hotkeyConflictLabel_->setVisible(!problems.isEmpty());
}

} // namespace calango::gui
