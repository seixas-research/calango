#include "gui/MaceTrainerDialog.hpp"

#include "gui/CondaEnvs.hpp"
#include "gui/MlipTrainerBackend.hpp"
#include "gui/PythonPackagePreflight.hpp"
#include "gui/SettingsManager.hpp"
#include "python_bridge/PythonEngine.hpp"

#include <QApplication>
#include <QComboBox>
#include <QFile>
#include <QFileDialog>
#include <QFontDatabase>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QAbstractItemView>
#include <QListWidget>
#include <QListWidgetItem>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScreen>
#include <QSettings>
#include <QSignalBlocker>
#include <QVBoxLayout>
#include <QWizardPage>

#include <algorithm>

namespace calango::gui {

namespace {

/// The framework page. Its completeness is the whole framework gate: a
/// framework with no backend leaves Next disabled, and the details pane
/// says what its backend would need.
class FrameworkPage : public QWizardPage {
public:
    FrameworkPage(QWidget* parent, const bool& implemented)
        : QWizardPage(parent)
        , implemented_(implemented)
    {
    }

    bool isComplete() const override { return implemented_; }
    /// Called by the wizard when the selection changes, so Next re-evaluates.
    void selectionChanged() { Q_EMIT completeChanged(); }

private:
    const bool& implemented_;
};

} // namespace

MaceTrainerDialog::MaceTrainerDialog(QWidget* parent) : QWizard(parent)
{
    setWindowTitle(tr("ML Potential Trainer"));
    setWizardStyle(QWizard::ModernStyle);
    // Fixed and comfortably inside a laptop screen, capped further on a
    // genuinely small display — the same screen-aware idiom
    // SimulationWizardBase::buildUi() uses. Content taller than this scrolls
    // inside its page (every parameter page is wrapped in a vertical
    // QScrollArea); it never pushes the wizard past the available screen,
    // which is precisely what the old single-page dialog did.
    const QScreen* display = screen() ? screen() : QGuiApplication::primaryScreen();
    const QSize available = display ? display->availableGeometry().size()
                                    : QSize(1440, 900);
    resize(std::min(880, available.width() * 90 / 100),
           std::min(640, available.height() * 85 / 100));

    setOption(QWizard::HaveCustomButton1, true); // Export
    setOption(QWizard::HaveCustomButton2, true); // Run (Remote)
    setOption(QWizard::HaveCustomButton3, true); // Run (Local)
    setOption(QWizard::NoDefaultButton, true);
    setButtonText(QWizard::CustomButton1, tr("Export Config…"));
    setButtonText(QWizard::CustomButton2, tr("Run (Remote)"));
    setButtonText(QWizard::CustomButton3, tr("Run (Local)"));
    // No Finish button: "finishing" this wizard means LAUNCHING something,
    // and there are two ways to do that (local, remote) plus one way to
    // leave with only a file (Export). A Finish that did one of the three
    // and left the others as side buttons would be lying about which is the
    // commitment.
    setButtonLayout({QWizard::Stretch, QWizard::BackButton,
                     QWizard::NextButton, QWizard::CustomButton1,
                     QWizard::CustomButton2, QWizard::CustomButton3,
                     QWizard::CancelButton});

    setPage(kFrameworkPageId, buildFrameworkPage());
    setPage(kConfigPageId, buildConfigPage());
    installBackend(framework_);

    connect(this, &QWizard::currentIdChanged, this,
            [this](int id) { syncCustomButtons(id); });
    connect(button(QWizard::CustomButton1), &QAbstractButton::clicked, this,
            &MaceTrainerDialog::exportYaml);
    connect(button(QWizard::CustomButton2), &QAbstractButton::clicked, this,
            [this] {
                // Remote: the check still runs against the LOCAL interpreter
                // field, which is what the user has told Calango to resolve
                // for this node — the actual remote host may differ, and
                // there is no way to probe it from here. Worth doing anyway:
                // the common case is a local conda env name reused verbatim
                // as the remote one, and catching a missing package before
                // anything is even staged is strictly better than catching
                // it only once a cluster job fails.
                if (!preflightPackage())
                    return;
                action_ = Action::RunRemote;
                accept();
            });
    connect(button(QWizard::CustomButton3), &QAbstractButton::clicked, this,
            [this] {
                if (!preflightPackage())
                    return;
                action_ = Action::RunLocal;
                accept();
            });
    syncCustomButtons(currentId());
    refreshPreview();
}

MaceTrainerDialog::~MaceTrainerDialog() = default;

QWizardPage* MaceTrainerDialog::buildFrameworkPage()
{
    // The page reads this flag by reference rather than owning it: "is the
    // selected framework implemented" is the wizard's state (the selection
    // drives which backend is installed), and the page only has to report it
    // as its own completeness.
    auto* page = new FrameworkPage(this, frameworkImplemented_);
    page->setTitle(tr("Framework"));
    page->setSubTitle(
        tr("Which model type to train. Every machine-learning potential "
           "Calango can RUN is listed; the ones with no trainer yet say what "
           "theirs would need."));
    auto* layout = new QVBoxLayout(page);

    frameworkList_ = new QListWidget(page);
    frameworkList_->setObjectName(QStringLiteral("trainerFrameworkList"));
    // A read-only chooser: nothing in it is editable, so it carries no edit
    // triggers at all — which also keeps it clear of the dead-key recursion
    // every item view in this application is checked for.
    frameworkList_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    for (const MlipTrainerFramework& entry : mlipTrainerFrameworks()) {
        auto* item = new QListWidgetItem(
            entry.implemented
                ? entry.name
                : tr("%1 — not yet supported").arg(entry.name),
            frameworkList_);
        item->setData(Qt::UserRole, static_cast<int>(entry.kind));
        // Selectable, deliberately, even when unsupported: a disabled row
        // cannot be selected, and a row that cannot be selected cannot show
        // you WHY it is unsupported. The gate is Next, not the list.
        if (!entry.implemented) {
            QFont font = item->font();
            font.setItalic(true);
            item->setFont(font);
        }
    }
    layout->addWidget(frameworkList_, 1);

    frameworkDetails_ = new QLabel(page);
    frameworkDetails_->setWordWrap(true);
    frameworkDetails_->setTextFormat(Qt::RichText);
    frameworkDetails_->setMinimumHeight(120);
    frameworkDetails_->setAlignment(Qt::AlignTop);
    layout->addWidget(frameworkDetails_);

    connect(frameworkList_, &QListWidget::currentRowChanged, this,
            [this, page](int) {
                const QListWidgetItem* item = frameworkList_->currentItem();
                if (!item)
                    return;
                const int value = item->data(Qt::UserRole).toInt();
                if (!core::isValidCalculatorKind(value))
                    return;
                framework_ = static_cast<core::CalculatorKind>(value);
                const MlipTrainerFramework* entry =
                    mlipTrainerFramework(framework_);
                if (!entry)
                    return;
                frameworkImplemented_ = entry->implemented;
                if (entry->implemented) {
                    frameworkDetails_->setText(
                        tr("<b>%1</b> — config: %2 (<tt>%3</tt>) · trainer: "
                           "<tt>%4</tt><br><br>Calango generates that config "
                           "and a self-contained launcher for it. The next "
                           "pages set the parameters; the last one shows the "
                           "file, editable, before anything runs.")
                            .arg(entry->name, entry->configFormat,
                                 entry->configFileName, entry->entryPoint));
                    installBackend(framework_);
                } else {
                    frameworkDetails_->setText(
                        tr("<b>%1 — not yet supported.</b> Its trainer reads "
                           "%2 (<tt>%3</tt>) and runs through "
                           "<tt>%4</tt>.<br><br>%5")
                            .arg(entry->name, entry->configFormat,
                                 entry->configFileName, entry->entryPoint,
                                 entry->status));
                }
                page->selectionChanged();
            });
    // Select the implemented one, so an untouched wizard is usable.
    for (int row = 0; row < frameworkList_->count(); ++row) {
        const int value =
            frameworkList_->item(row)->data(Qt::UserRole).toInt();
        if (core::isValidCalculatorKind(value)
            && static_cast<core::CalculatorKind>(value) == framework_) {
            frameworkList_->setCurrentRow(row);
            break;
        }
    }
    return page;
}

void MaceTrainerDialog::installBackend(core::CalculatorKind kind)
{
    if (backend_ && backend_->kind() == kind)
        return;
    // removePage() gives ownership BACK to the caller rather than deleting,
    // so the outgoing backend's pages have to be deleted here or every
    // framework switch leaks a stack of them. Unreachable today (MACE is the
    // only implemented backend, so this never runs twice) and written
    // correctly anyway, because "unreachable" is exactly the state a second
    // backend changes.
    for (int id : parameterPageIds_) {
        QWizardPage* outgoing = page(id);
        removePage(id);
        delete outgoing;
    }
    parameterPageIds_.clear();
    backend_ = makeMlipTrainerBackend(kind);
    if (!backend_)
        return;

    int id = kFrameworkPageId + 1;
    const QList<QWizardPage*> pages = backend_->createParameterPages(this);
    for (QWizardPage* page : pages) {
        setPage(id, page);
        parameterPageIds_ << id;
        ++id;
    }
    // A settings change makes the shown config stale. It does NOT overwrite
    // a config the user has edited — that text is theirs, and the page says
    // so instead.
    connect(backend_.get(), &MlipTrainerBackend::settingsChanged, this,
            &MaceTrainerDialog::refreshPreview);
    if (envEdit_)
        envEdit_->setPlaceholderText(
            tr("conda env folder or python (empty = embedded); needs %1")
                .arg(backend_->pipPackage()));
    refreshPreview();
}

QWizardPage* MaceTrainerDialog::buildConfigPage()
{
    auto* page = new QWizardPage(this);
    page->setTitle(tr("Config and launch"));
    page->setSubTitle(
        tr("The generated config file — editable. Whatever is in this editor "
           "is what gets written and run, verbatim."));
    auto* layout = new QVBoxLayout(page);

    auto* header = new QHBoxLayout;
    header->addWidget(new QLabel(tr("Config file (editable):"), page));
    header->addStretch(1);
    auto* regenerate =
        new QPushButton(tr("Regenerate from settings"), page);
    regenerate->setObjectName(QStringLiteral("trainerRegenerate"));
    regenerate->setToolTip(
        tr("Rebuild this text from the pages before it. Any hand edits are "
           "discarded — you are asked first."));
    header->addWidget(regenerate);
    layout->addLayout(header);

    preview_ = new QPlainTextEdit(page);
    preview_->setObjectName(QStringLiteral("trainerConfigEditor"));
    preview_->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    layout->addWidget(preview_, 1);

    previewStale_ = new QLabel(page);
    previewStale_->setWordWrap(true);
    previewStale_->setTextFormat(Qt::RichText);
    layout->addWidget(previewStale_);

    auto* envGroup = new QGroupBox(tr("Execution environment"), page);
    auto* envLayout = new QVBoxLayout(envGroup);
    const auto condaEnvs = CondaEnvs::discover();
    if (!condaEnvs.isEmpty()) {
        auto* condaCombo = new QComboBox(envGroup);
        condaCombo->addItem(tr("(custom / embedded — use field below)"),
                            QString());
        for (const auto& env : condaEnvs)
            condaCombo->addItem(env.name, env.path);
        envLayout->addWidget(condaCombo);
        connect(condaCombo, &QComboBox::currentIndexChanged, this,
                [this, condaCombo](int) {
                    const QString path = condaCombo->currentData().toString();
                    if (!path.isEmpty())
                        envEdit_->setText(path);
                });
    }
    envEdit_ = new QLineEdit(
        QSettings().value(SettingsManager::kEnvironmentPath).toString(),
        envGroup);
    envEdit_->setObjectName(QStringLiteral("trainerEnvironment"));
    // The package name comes from the backend, so this row says what THIS
    // framework actually needs rather than naming MACE forever.
    envEdit_->setPlaceholderText(
        tr("conda env folder or python (empty = embedded); needs %1")
            .arg(backend_ ? backend_->pipPackage()
                          : QStringLiteral("mace-torch")));
    envLayout->addWidget(envEdit_);
    auto* checkRow = new QHBoxLayout;
    checkEnvButton_ = new QPushButton(tr("Check Environment"), envGroup);
    checkEnvButton_->setToolTip(
        tr("Probes the interpreter above for the framework's package "
           "(reporting its version) and for which PyTorch compute devices it "
           "can actually use — cpu always, cuda/mps only when the installed "
           "PyTorch build and hardware support them. Nothing here is "
           "vendored or hard-depended-on by Calango itself; this is the same "
           "check either Run button runs automatically before launching."));
    checkRow->addWidget(checkEnvButton_);
    checkRow->addStretch(1);
    envLayout->addLayout(checkRow);
    envStatus_ = new QLabel(
        tr("Not checked yet — press Check Environment, or Run."), envGroup);
    envStatus_->setWordWrap(true);
    envLayout->addWidget(envStatus_);
    layout->addWidget(envGroup);

    connect(checkEnvButton_, &QPushButton::clicked, this,
            &MaceTrainerDialog::checkEnvironment);
    connect(regenerate, &QPushButton::clicked, this, [this] {
        if (manuallyEdited_
            && QMessageBox::question(
                   this, tr("Regenerate from settings"),
                   tr("This config has been edited by hand. Regenerating "
                      "rebuilds it from the pages before this one and "
                      "discards those edits.\n\nRegenerate anyway?"))
                != QMessageBox::Yes)
            return;
        manuallyEdited_ = false;
        refreshPreview();
    });
    connect(preview_, &QPlainTextEdit::textChanged, this, [this] {
        manuallyEdited_ = true;
        if (previewStale_)
            previewStale_->clear();
    });
    return page;
}

void MaceTrainerDialog::syncCustomButtons(int pageId)
{
    // Export and the two Run buttons belong to the config page and to no
    // other: a Run offered from the Model page would launch a config the
    // user has not been shown.
    const bool onConfigPage = pageId == kConfigPageId;
    for (QWizard::WizardButton which : {QWizard::CustomButton1,
                                        QWizard::CustomButton2,
                                        QWizard::CustomButton3})
        if (QAbstractButton* b = button(which))
            b->setVisible(onConfigPage);
}

void MaceTrainerDialog::checkEnvironment()
{
    if (!backend_ || !envStatus_)
        return;
    envStatus_->setStyleSheet(QString());
    envStatus_->setText(tr("Checking %1…").arg(pythonExecutable()));
    QApplication::setOverrideCursor(Qt::WaitCursor);
    QApplication::processEvents();

    const PythonPackagePreflightResult found =
        checkPythonPackage(pythonExecutable(), backend_->pythonModule());
    lastCheckAvailable_ = found.available;
    QString text;
    if (found.available) {
        detectedVersion_ = found.version;
        text = tr("%1 %2 found.")
                   .arg(backend_->pipPackage(),
                        found.version.isEmpty() ? tr("(version unknown)")
                                                : found.version);
        const TorchDeviceAvailability devices =
            probeTorchDevices(pythonExecutable());
        if (devices.probeSucceeded) {
            QStringList availableDevices{QStringLiteral("cpu")};
            if (devices.cuda)
                availableDevices << QStringLiteral("cuda");
            if (devices.mps)
                availableDevices << QStringLiteral("mps");
            text += tr(" PyTorch devices available: %1.")
                        .arg(availableDevices.join(QStringLiteral(", ")));
            backend_->applyTorchDevices(devices.cuda, devices.mps, true);
            const QString chosen = backend_->selectedDevice();
            const bool chosenAvailable = chosen.isEmpty()
                || chosen == QStringLiteral("cpu")
                || (chosen == QStringLiteral("cuda") && devices.cuda)
                || (chosen == QStringLiteral("mps") && devices.mps);
            if (!chosenAvailable)
                text += tr(" Warning: \"%1\" is selected but was not "
                           "reported as available — the run will likely "
                           "fail or silently fall back.")
                            .arg(chosen);
        } else {
            text += tr(" (could not probe which devices PyTorch itself "
                       "sees — device availability is unknown, not "
                       "necessarily absent.)");
        }
        envStatus_->setStyleSheet(QStringLiteral("color: #2e7d32;"));
    } else {
        detectedVersion_.clear();
        text = tr("%1 was not found under %2: %3\n\nInstall it "
                  "with: pip install %1")
                   .arg(backend_->pipPackage(), pythonExecutable(),
                        found.errorMessage);
        envStatus_->setStyleSheet(QStringLiteral("color: #c0392b;"));
    }
    envStatus_->setText(text);
    QApplication::restoreOverrideCursor();
    refreshPreview();
}

bool MaceTrainerDialog::preflightPackage()
{
    if (!backend_) {
        QMessageBox::warning(
            this, tr("No trainer for this framework"),
            tr("Calango has no trainer implementation for the framework "
               "selected on the first page. Nothing was launched."));
        return false;
    }
    // Re-checked every time rather than trusting a stale
    // lastCheckAvailable_: the interpreter field is freely editable right up
    // to the moment Run is pressed, and a check against yesterday's choice
    // would be worse than no check at all — it would say "fine" about an
    // environment nobody is about to use.
    checkEnvironment();
    if (lastCheckAvailable_)
        return true;
    QMessageBox::warning(
        this, tr("%1 not found").arg(backend_->pipPackage()),
        tr("%1\n\nNothing was launched.").arg(envStatus_->text()));
    return false;
}

void MaceTrainerDialog::refreshPreview()
{
    if (!preview_ || !backend_)
        return;
    if (manuallyEdited_) {
        // The text is the user's. Say the settings moved under it rather
        // than replacing what they wrote — this is the whole difference
        // between an editable config and a preview that pretends to be one.
        if (previewStale_)
            previewStale_->setText(
                tr("<i>Settings on the earlier pages have changed since this "
                   "text was generated. It is used exactly as it stands; "
                   "press <b>Regenerate from settings</b> to rebuild it.</i>"));
        return;
    }
    const QString note = detectedVersion_.isEmpty()
        ? QString()
        : tr("%1 %2 (detected under %3)")
              .arg(backend_->pipPackage(), detectedVersion_,
                   pythonExecutable());
    const QSignalBlocker blocker(preview_);
    preview_->setPlainText(backend_->buildConfig(note));
    if (previewStale_)
        previewStale_->clear();
}

QString MaceTrainerDialog::yaml() const
{
    return preview_ ? preview_->toPlainText() : QString();
}

void MaceTrainerDialog::setInitialYaml(const QString& yaml)
{
    if (!preview_)
        return;
    const QSignalBlocker blocker(preview_);
    preview_->setPlainText(yaml);
    // Marks it hand-edited so refreshPreview() (fired by every settings
    // change) never clobbers the restored text with a freshly regenerated
    // config — the individual widgets keep their OWN defaults, not whatever
    // the saved config actually contains, since this restores the text only,
    // not the widget state it was generated from.
    manuallyEdited_ = true;
}

void MaceTrainerDialog::prefillFromDatasetManifest(const QString& trainPath,
                                                   const QString& validPath,
                                                   const QString& energyKey,
                                                   const QString& forcesKey)
{
    if (backend_)
        backend_->prefillFromDatasetManifest(trainPath, validPath, energyKey,
                                             forcesKey);
}

QString MaceTrainerDialog::runnerScript() const
{
    // The EDITED text, not a freshly built one: what the user reviewed on
    // the last page is what the launcher must carry.
    return backend_ ? backend_->runnerScript(yaml()) : QString();
}

QString MaceTrainerDialog::pythonExecutable() const
{
    const QString resolved =
        CondaEnvs::resolvePython(envEdit_ ? envEdit_->text() : QString());
    if (!resolved.isEmpty())
        return resolved;
    return QString::fromStdString(pybridge::PythonEngine::instance().executable());
}

void MaceTrainerDialog::exportYaml()
{
    const QString suggested =
        backend_ ? backend_->configFileName() : QStringLiteral("config.yaml");
    const QString path = QFileDialog::getSaveFileName(
        this, tr("Export Training Config"), suggested,
        tr("YAML files (*.yaml *.yml);;All files (*)"));
    if (path.isEmpty())
        return;
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QMessageBox::warning(this, tr("Export Training Config"),
                             tr("Could not write %1").arg(path));
        return;
    }
    file.write(yaml().toUtf8());
}

} // namespace calango::gui
