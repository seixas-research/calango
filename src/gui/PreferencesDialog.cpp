#include "gui/PreferencesDialog.hpp"

#include "gui/CondaEnvs.hpp"
#include "gui/EnginePresets.hpp"
#include "gui/RunCommands.hpp"
#include "gui/EnvFile.hpp"
#include "gui/SettingsManager.hpp"
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
#include <QLineEdit>
#include <QPushButton>
#include <QSettings>
#include <QSpinBox>
#include <QTableWidget>
#include <QTabWidget>
#include <QThread>
#include <QVBoxLayout>

namespace calango::gui {

PreferencesDialog::PreferencesDialog(QWidget* parent)
    : QDialog(parent)
    , envPathEdit_(new QLineEdit(this))
    , statusLabel_(new QLabel(this))
{
    setWindowTitle(tr("Preferences"));
    resize(560, 460);

    auto* envGroup = new QGroupBox(tr("Environment File (.env)"), this);
    auto* envLayout = new QVBoxLayout(envGroup);

    auto* note = new QLabel(
        tr("At launch Calango reads this file and exports MP_API_KEY (the "
           "Materials Project API key) into the environment. A key already "
           "set in the shell takes precedence at startup; \"Reload Now\" "
           "applies the file unconditionally."),
        envGroup);
    note->setWordWrap(true);
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
        QSettings().value(QLatin1String(SettingsManager::kOmpThreads),
                          QThread::idealThreadCount()).toInt());
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
    generalPageLayout->addWidget(computeGroup);
    generalPageLayout->addStretch(1);

    auto* tabs = new QTabWidget(this);
    tabs->addTab(generalPage, tr("General"));
    tabs->addTab(buildPythonEnvTab(), tr("Python && Environments"));
    tabs->addTab(buildRunTab(), tr("Run"));

    auto* layout = new QVBoxLayout(this);
    layout->addWidget(tabs, 1);
    layout->addWidget(buttons);

    updateStatus();
    updateCondaStatus();
}

QWidget* PreferencesDialog::buildPythonEnvTab()
{
    auto* page = new QWidget(this);
    auto* layout = new QVBoxLayout(page);

    auto* note = new QLabel(
        tr("Map each calculator engine to the Conda environment (or python "
           "executable) its jobs should run in. The simulation wizards resolve "
           "these automatically — no per-run prompting. Leave a row blank to "
           "fall back to the active $PATH / embedded interpreter."),
        page);
    note->setWordWrap(true);
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

    auto* note = new QLabel(
        tr("Shell command each engine's jobs are launched with. Placeholders: "
           "<b>{cores}</b> MPI ranks, <b>{script}</b> the generated script, "
           "<b>{python}</b> the engine's interpreter, <b>{input}</b> / "
           "<b>{output}</b> the solver's files.<br><br>"
           "A template containing <b>{script}</b> launches the script itself — "
           "that is how a parallel GPAW run works. A template with "
           "<b>{input}</b>/<b>{output}</b> is instead handed to ASE as the "
           "solver command (ASE_ESPRESSO_COMMAND and friends), because running "
           "the whole Python script under mpirun would start N independent "
           "copies of it rather than one script driving an N-rank solver."),
        page);
    note->setWordWrap(true);
    note->setTextFormat(Qt::RichText);
    layout->addWidget(note);

    auto* coresRow = new QHBoxLayout;
    coresRow->addWidget(new QLabel(tr("Cores ({cores}):"), page));
    runCoresSpin_ = new QSpinBox(page);
    runCoresSpin_->setRange(1, 4096);
    runCoresSpin_->setValue(RunCommands::cores());
    runCoresSpin_->setToolTip(
        tr("MPI rank count substituted for {cores}. Defaults to 1: a template "
           "that silently claimed every core would oversubscribe a machine "
           "already running something else."));
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

} // namespace calango::gui
