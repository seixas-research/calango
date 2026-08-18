#include "gui/HpcPanel.hpp"

#include "ui/IconManager.hpp"

#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollBar>
#include <QSettings>
#include <QSpinBox>
#include <QTabWidget>
#include <QTextStream>
#include <QVBoxLayout>

namespace calango::gui {

namespace {

/// Files worth pulling back after a run: structures/trajectories, the
/// scheduler logs, and any CSV metric exports the script produced.
const QStringList kResultPatterns = {
    QStringLiteral("*.extxyz"), QStringLiteral("*.traj"),
    QStringLiteral("*.xyz"),    QStringLiteral("*.cif"),
    QStringLiteral("*.out"),    QStringLiteral("*.err"),
    QStringLiteral("*.log"),    QStringLiteral("*.csv"),
};

} // namespace

HpcPanel::HpcPanel(const QString& pythonExe, QWidget* parent)
    : QWidget(parent)
    , client_(new remote::RemoteClient(pythonExe, this))
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(4, 4, 4, 4);

    // ---- Cluster presets --------------------------------------------------
    // Above the tabs, because a preset spans both of them: a cluster is an
    // address AND the queue shape its scheduler expects, and saving one
    // without the other would still leave half the form to retype.
    auto* presetRow = new QWidget(this);
    auto* presetLayout = new QHBoxLayout(presetRow);
    presetLayout->setContentsMargins(4, 0, 4, 0);
    presetLayout->addWidget(new QLabel(tr("Cluster:"), presetRow));
    presetCombo_ = new QComboBox(presetRow);
    presetCombo_->setEditable(true);
    presetCombo_->setInsertPolicy(QComboBox::NoInsert);
    presetCombo_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    presetCombo_->lineEdit()->setPlaceholderText(
        tr("unsaved — type a name and press Save"));
    presetCombo_->setToolTip(
        tr("Saved cluster configurations: host, port, account, key path, "
           "remote directory, scheduler and the queue shape.\n\n"
           "Editable, so the text you type IS the name Save uses. Saving over "
           "an existing name edits that entry rather than adding a second one."
           "\n\nPasswords are never stored — only what is safe to keep on "
           "disk and copy between machines."));
    presetLayout->addWidget(presetCombo_, 1);
    presetSaveButton_ = new QPushButton(presetRow);
    ui::IconManager::bind(presetSaveButton_, QStringLiteral("save-line"));
    presetSaveButton_->setIconSize(QSize(20, 20));
    presetSaveButton_->setFocusPolicy(Qt::NoFocus);
    presetSaveButton_->setToolTip(
        tr("Save connection profile — store the current Connection and "
           "Scheduler settings under the name in the box."));
    presetSaveButton_->setAccessibleName(tr("Save connection profile"));
    presetLayout->addWidget(presetSaveButton_);
    presetDeleteButton_ = new QPushButton(presetRow);
    ui::IconManager::bind(presetDeleteButton_, QStringLiteral("delete-bin-line"));
    presetDeleteButton_->setIconSize(QSize(20, 20));
    presetDeleteButton_->setFocusPolicy(Qt::NoFocus);
    presetDeleteButton_->setToolTip(
        tr("Delete connection profile — forget the selected cluster."));
    presetDeleteButton_->setAccessibleName(tr("Delete connection profile"));
    presetLayout->addWidget(presetDeleteButton_);
    layout->addWidget(presetRow);

    auto* tabs = new QTabWidget(this);
    layout->addWidget(tabs);

    // ---- Connection tab ---------------------------------------------------
    auto* connectionPage = new QWidget(tabs);
    auto* form = new QFormLayout(connectionPage);
    form->setContentsMargins(8, 8, 8, 8);
    form->setVerticalSpacing(4);

    hostEdit_ = new QLineEdit(connectionPage);
    hostEdit_->setPlaceholderText(tr("cluster.university.edu"));
    auto* hostRow = new QWidget(connectionPage);
    auto* hostLayout = new QHBoxLayout(hostRow);
    hostLayout->setContentsMargins(0, 0, 0, 0);
    portSpin_ = new QSpinBox(connectionPage);
    portSpin_->setRange(1, 65535);
    portSpin_->setValue(22);
    hostLayout->addWidget(hostEdit_, 1);
    hostLayout->addWidget(new QLabel(tr("Port:"), connectionPage));
    hostLayout->addWidget(portSpin_);
    form->addRow(tr("Host:"), hostRow);

    userEdit_ = new QLineEdit(connectionPage);
    form->addRow(tr("User:"), userEdit_);

    // No explicit auth-method picker: which method is used is INFERRED from
    // what these two fields hold, each time a connection is attempted (see
    // configFromUi()) — a key file present selects key auth (with the
    // password field, if also filled, used as that key's passphrase, the
    // SSH-conventional precedence); no key but a password selects password
    // auth; neither filled attempts the connection anyway with no explicit
    // credential, so the SSH layer's own defaults (agent keys, then
    // ~/.ssh/id_ed25519 / id_ecdsa / id_rsa) apply.
    auto* keyRow = new QWidget(connectionPage);
    auto* keyLayout = new QHBoxLayout(keyRow);
    keyLayout->setContentsMargins(0, 0, 0, 0);
    keyPathEdit_ = new QLineEdit(connectionPage);
    keyPathEdit_->setPlaceholderText(tr("~/.ssh/id_rsa (empty = agent/default keys)"));
    keyPathEdit_->setToolTip(
        tr("A key file here selects key authentication, tried first — the "
           "Password field below, if also filled, is used as this key's "
           "passphrase. Leave both fields empty to try the SSH agent and "
           "the default key files with no explicit credential at all."));
    keyBrowseButton_ = new QPushButton(tr("…"), connectionPage);
    keyBrowseButton_->setFixedWidth(28);
    keyLayout->addWidget(keyPathEdit_, 1);
    keyLayout->addWidget(keyBrowseButton_);
    form->addRow(tr("Key file:"), keyRow);
    connect(keyBrowseButton_, &QPushButton::clicked, this, [this] {
        const QString path = QFileDialog::getOpenFileName(
            this, tr("Select Private Key"), QDir::homePath() + QStringLiteral("/.ssh"));
        if (!path.isEmpty())
            keyPathEdit_->setText(path);
    });

    passwordEdit_ = new QLineEdit(connectionPage);
    passwordEdit_->setEchoMode(QLineEdit::Password);
    passwordEdit_->setPlaceholderText(tr("password / key passphrase — never stored"));
    passwordEdit_->setToolTip(
        tr("Used as the key file's passphrase when a Key file is set above; "
           "otherwise used directly as the login password. Never stored — "
           "retype it after loading a saved cluster or reopening Calango."));
    form->addRow(tr("Password:"), passwordEdit_);

    remoteDirEdit_ = new QLineEdit(connectionPage);
    remoteDirEdit_->setPlaceholderText(tr("calango_jobs (relative to $HOME)"));
    form->addRow(tr("Remote dir:"), remoteDirEdit_);

    auto* testRow = new QWidget(connectionPage);
    auto* testLayout = new QHBoxLayout(testRow);
    testLayout->setContentsMargins(0, 0, 0, 0);
    connectionButton_ = new QPushButton(tr("Connect"), connectionPage);
    connectionButton_->setToolTip(
        tr("Open the SSH session and keep it open. Everything after this — "
           "uploads, submission, status polls, log tailing, downloads — "
           "shares this one connection, so a cluster that asks for a "
           "one-time code asks exactly once.\n\nIt is also the button to "
           "press after a session drops: re-authenticating is deliberately "
           "never automatic when a code was involved.\n\nBecomes Disconnect "
           "once the session is open — closing it leaves a monitored job "
           "running on the cluster."));
    statusLabel_ = new QLabel(tr("Not connected"), connectionPage);
    statusLabel_->setStyleSheet(QStringLiteral("color: gray;"));
    // Never let a long status/error string widen the dock: Ignored drops the
    // label's sizeHint from the layout's width calculation, so it only ever
    // takes the space testLayout has left over, and word-wrapping keeps
    // whatever doesn't fit on one line legible instead of clipped.
    statusLabel_->setWordWrap(true);
    statusLabel_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    statusLabel_->setMinimumWidth(0);
    testLayout->addWidget(connectionButton_);
    testLayout->addWidget(statusLabel_, 1);
    form->addRow(QString(), testRow);
    connect(connectionButton_, &QPushButton::clicked, this, [this] {
        using State = remote::RemoteClient::SessionState;
        switch (connectionState_) {
        case State::Connected:
        case State::Reconnecting:
            closeConnection();
            break;
        case State::Connecting:
        case State::Authenticating:
            // The button is disabled in this state (see
            // updateConnectionButton()) — RemoteClient has no way to cancel
            // an in-flight connect attempt, so there is nothing to wire a
            // click to here.
            break;
        case State::Disconnected:
        case State::NeedsReauth:
            testConnection();
            break;
        }
    });

    tabs->addTab(connectionPage, tr("Connection"));

    // ---- Scheduler tab ----------------------------------------------------
    auto* schedulerPage = new QWidget(tabs);
    auto* schedulerForm = new QFormLayout(schedulerPage);
    schedulerForm_ = schedulerForm;
    schedulerForm->setContentsMargins(8, 8, 8, 8);
    schedulerForm->setVerticalSpacing(4);

    schedulerCombo_ = new QComboBox(schedulerPage);
    // Same order as core::Scheduler.
    schedulerCombo_->addItems({QStringLiteral("SLURM"), QStringLiteral("PBS"),
                               QStringLiteral("SGE")});
    schedulerForm->addRow(tr("Scheduler:"), schedulerCombo_);

    queueEdit_ = new QLineEdit(schedulerPage);
    queueEdit_->setPlaceholderText(tr("partition / queue (empty = default)"));
    schedulerForm->addRow(tr("Queue:"), queueEdit_);

    // Nodes and ranks share a row: they are one decision ("how much machine"),
    // and the panel lives at bottom-row height where every row costs.
    auto* shapeRow = new QWidget(schedulerPage);
    auto* shapeLayout = new QHBoxLayout(shapeRow);
    shapeLayout->setContentsMargins(0, 0, 0, 0);
    nodesSpin_ = new QSpinBox(shapeRow);
    nodesSpin_->setRange(1, 4096);
    nodesSpin_->setValue(1);
    nodesSpin_->setToolTip(tr("Whole machines requested (SLURM --nodes, PBS "
                              "select chunks)."));
    shapeLayout->addWidget(nodesSpin_);
    shapeLayout->addWidget(new QLabel(QStringLiteral("×"), shapeRow));
    tasksSpin_ = new QSpinBox(shapeRow);
    tasksSpin_->setRange(1, 4096);
    tasksSpin_->setValue(1);
    tasksSpin_->setToolTip(
        tr("Ranks (or cores) on each node. The total is nodes × this, which "
           "is what SGE is given directly since it requests slots rather "
           "than machines."));
    shapeLayout->addWidget(tasksSpin_);
    shapeLayout->addStretch(1);
    schedulerForm->addRow(tr("Nodes × tasks:"), shapeRow);

    memorySpin_ = new QSpinBox(schedulerPage);
    memorySpin_->setRange(0, 4096000);
    memorySpin_->setSingleStep(1024);
    memorySpin_->setValue(0);
    memorySpin_->setSuffix(tr(" MB"));
    memorySpin_->setSpecialValueText(tr("cluster default"));
    memorySpin_->setToolTip(
        tr("Memory PER NODE. Zero requests the cluster's own default.\n\n"
           "Per node is how the three schedulers differ: SLURM's --mem and "
           "PBS's chunk memory are per node and take this unchanged, while "
           "SGE's h_vmem is per SLOT — so the run divides it by the tasks "
           "per node. Asking SGE for a node's worth on every slot multiplies "
           "the request by the core count and the job never starts."));
    schedulerForm->addRow(tr("Memory / node:"), memorySpin_);

    walltimeEdit_ = new QLineEdit(QStringLiteral("01:00:00"), schedulerPage);
    walltimeEdit_->setToolTip(tr("HH:MM:SS."));
    schedulerForm->addRow(tr("Walltime:"), walltimeEdit_);

    // SGE alone needs this, and only SGE shows it: the parallel environment
    // is a site-defined name, and "smp" is single-node almost everywhere, so
    // a multi-node SGE job needs whatever that cluster called its MPI PE.
    peEdit_ = new QLineEdit(QStringLiteral("smp"), schedulerPage);
    peEdit_->setToolTip(
        tr("SGE parallel environment (-pe). Site-specific: \"smp\" is "
           "single-node shared memory nearly everywhere, so a multi-node job "
           "needs this cluster's MPI environment name."));
    schedulerForm->addRow(tr("Parallel env:"), peEdit_);
    peRow_ = schedulerForm->rowCount() - 1;

    setupEdit_ = new QPlainTextEdit(schedulerPage);
    setupEdit_->setPlaceholderText(
        tr("# environment setup, e.g.\nmodule load python\nsource ~/venv/bin/activate"));
    setupEdit_->setMaximumHeight(64);
    schedulerForm->addRow(tr("Setup:"), setupEdit_);

    // The paragraph that used to sit here explaining the submission flow has
    // moved to the tab's own tooltip. It was four wrapped lines of prose in a
    // dock docked at the bottom of the window, where vertical space is the
    // scarce resource and the text was read once and then scrolled past
    // forever — the information is worth keeping, the permanent real estate
    // is not.
    connect(schedulerCombo_, &QComboBox::currentIndexChanged, this,
            [this] { updateSchedulerRows(); });

    tabs->addTab(schedulerPage, tr("Scheduler"));
    tabs->setTabToolTip(
        tabs->count() - 1,
        tr("Resource request for jobs submitted from a simulation wizard: "
           "pick Remote in Stage 2, then Run (Remote) in Stage 4. Calango "
           "uploads run.py, the structure and a generated job.sh, submits it, "
           "and tracks the job in Queue & Logs."));

    // ---- Queue & Logs tab -------------------------------------------------
    auto* queuePage = new QWidget(tabs);
    auto* queueLayout = new QVBoxLayout(queuePage);
    queueLayout->setContentsMargins(8, 8, 8, 8);
    queueLayout->setSpacing(4);

    auto* statusRow = new QHBoxLayout;
    jobLabel_ = new QLabel(tr("No remote job"), queuePage);
    stateLabel_ = new QLabel(queuePage);
    stateLabel_->setStyleSheet(QStringLiteral("font-weight: bold;"));
    cancelButton_ = new QPushButton(tr("Cancel Job"), queuePage);
    cancelButton_->setEnabled(false);
    downloadButton_ = new QPushButton(tr("Download Results"), queuePage);
    downloadButton_->setEnabled(false);
    statusRow->addWidget(jobLabel_);
    statusRow->addWidget(stateLabel_);
    statusRow->addStretch(1);
    statusRow->addWidget(cancelButton_);
    statusRow->addWidget(downloadButton_);
    queueLayout->addLayout(statusRow);

    logView_ = new QPlainTextEdit(queuePage);
    logView_->setReadOnly(true);
    logView_->setMaximumBlockCount(5000);
    QFont mono = logView_->font();
    mono.setFamilies({QStringLiteral("Menlo"), QStringLiteral("Consolas"),
                      QStringLiteral("monospace")});
    logView_->setFont(mono);
    queueLayout->addWidget(logView_, 1);

    tabs->addTab(queuePage, tr("Queue && Logs"));

    connect(cancelButton_, &QPushButton::clicked,
            this, &HpcPanel::cancelRemoteJob);
    connect(downloadButton_, &QPushButton::clicked,
            this, &HpcPanel::downloadResults);

    // ---- Client wiring ----------------------------------------------------
    connect(client_, &remote::RemoteClient::probeFinished,
            this, &HpcPanel::onProbeFinished);
    connect(client_, &remote::RemoteClient::authPromptRequested,
            this, &HpcPanel::onAuthPromptRequested);
    connect(client_, &remote::RemoteClient::sessionStateChanged,
            this, &HpcPanel::onSessionStateChanged);
    connect(client_, &remote::RemoteClient::connectFinished, this,
            [this](bool ok, const QString& error, remote::RemoteClient::ErrorKind kind) {
                if (ok)
                    return;
                // Status line stays a short, human-readable cause; the raw
                // SSH/library text goes in the tooltip and the log, not on
                // the line that has to fit in the dock.
                QString cause = shortConnectionError(error, kind);
                using Kind = remote::RemoteClient::ErrorKind;
                // Worth naming specifically: with no auth-method picker left,
                // a bare "Authentication failed" no longer even implies which
                // method was tried — say plainly that neither field was set.
                if ((kind == Kind::Auth || kind == Kind::AuthRequired)
                    && keyPathEdit_->text().trimmed().isEmpty()
                    && passwordEdit_->text().isEmpty())
                    cause += tr(" — no password or key provided");
                setStatus(cause, false, error);
                appendLog(error, true);
            });
    connect(client_, &remote::RemoteClient::busyChanged, this, [this](bool busy) {
        connectionBusy_ = busy;
        updateConnectionButton();
    });
    connect(client_, &remote::RemoteClient::fileUploaded, this, [this](const QString& f) {
        appendLog(tr("uploaded %1\n").arg(f));
    });
    connect(client_, &remote::RemoteClient::submitFinished,
            this, &HpcPanel::onSubmitFinished);
    connect(client_, &remote::RemoteClient::jobStateChanged, this,
            [this](const QString& state) { stateLabel_->setText(state); });
    connect(client_, &remote::RemoteClient::remoteLog, this,
            [this](const QString& stream, const QString& text) {
                appendLog(text, stream == QLatin1String("err"));
            });
    connect(client_, &remote::RemoteClient::monitorFinished,
            this, &HpcPanel::onMonitorFinished);
    connect(client_, &remote::RemoteClient::fileDownloaded, this, [this](const QString& f) {
        appendLog(tr("downloaded %1\n").arg(f));
    });

    restoreSettings();

    // Presets last: they load on top of the plain restored settings, and the
    // signals are connected only now so refreshing the combo during startup
    // cannot fire applySelectedPreset() over a form that is still being
    // filled in.
    presets_ = ClusterPresets::load();
    refreshPresetCombo(QSettings()
                           .value(QStringLiteral("hpc/lastCluster"))
                           .toString());
    connect(presetSaveButton_, &QPushButton::clicked, this,
            &HpcPanel::saveCurrentPreset);
    connect(presetDeleteButton_, &QPushButton::clicked, this,
            &HpcPanel::deleteCurrentPreset);
    connect(presetCombo_, &QComboBox::activated, this,
            [this](int) { applySelectedPreset(); });
}

core::Scheduler HpcPanel::scheduler() const
{
    return static_cast<core::Scheduler>(schedulerCombo_->currentIndex());
}

remote::SshConfig HpcPanel::configFromUi() const
{
    remote::SshConfig config;
    config.host = hostEdit_->text().trimmed();
    config.port = portSpin_->value();
    config.username = userEdit_->text().trimmed();
    config.keyPath = keyPathEdit_->text().trimmed();
    config.password = passwordEdit_->text();
    // Inferred, not picked: a key file present means key auth (the password,
    // if also given, is that key's passphrase — see _try_publickey() in
    // calango_remote.py, which already loads it that way regardless of this
    // flag). No key but a password means password auth. Neither means Key
    // with an empty path, which is also what "no explicit credential" needs:
    // the helper still tries the SSH agent and the default key files, and
    // falls through to a live keyboard-interactive prompt if the server asks
    // for something else.
    config.auth = (config.keyPath.isEmpty() && !config.password.isEmpty())
        ? remote::SshConfig::Auth::Password
        : remote::SshConfig::Auth::Key;
    const QString dir = remoteDirEdit_->text().trimmed();
    config.remoteDir = dir.isEmpty() ? QStringLiteral("calango_jobs") : dir;
    return config;
}

core::RemoteJobSpec HpcPanel::specFromUi(const QString& jobName) const
{
    core::RemoteJobSpec spec;
    spec.scheduler = scheduler();
    spec.jobName = jobName.toStdString();
    spec.queue = queueEdit_->text().trimmed().toStdString();
    spec.nodes = nodesSpin_->value();
    spec.tasksPerNode = tasksSpin_->value();
    spec.memoryMbPerNode = memorySpin_->value();
    spec.parallelEnvironment = peEdit_->text().trimmed().toStdString();
    spec.walltime = walltimeEdit_->text().trimmed().toStdString();
    spec.setupLines = setupEdit_->toPlainText().toStdString();
    return spec;
}

void HpcPanel::testConnection()
{
    const remote::SshConfig config = configFromUi();
    if (config.host.isEmpty() || config.username.isEmpty()) {
        setStatus(tr("Enter host and user first"), false);
        return;
    }
    saveSettings();
    // Drop whatever session exists first: the form may now describe a
    // different cluster, and after a NeedsReauth this is the deliberate,
    // user-initiated login the policy waits for.
    client_->disconnectFromHost();
    client_->setConfig(config);
    setStatus(tr("Connecting…"));
    client_->probe();
}

void HpcPanel::closeConnection()
{
    client_->disconnectFromHost();
    setStatus(tr("Disconnected"), true);
}

void HpcPanel::onAuthPromptRequested(
    const QString& name, const QString& instruction,
    const QVector<remote::AuthPrompt>& prompts)
{
    // Non-modal open() rather than exec(): the challenge arrives from inside
    // the helper's stdout handler, and a nested event loop there would let a
    // second pass over the same output buffer run underneath the first.
    auto* dialog = new QDialog(this);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->setWindowTitle(name.isEmpty() ? tr("SSH Authentication") : name);
    dialog->setModal(true);

    auto* layout = new QVBoxLayout(dialog);
    auto* header = new QLabel(
        instruction.isEmpty()
            ? tr("%1 is asking for another authentication factor.")
                  .arg(hostEdit_->text())
            : instruction,
        dialog);
    header->setWordWrap(true);
    layout->addWidget(header);

    auto* form = new QFormLayout;
    QVector<QLineEdit*> fields;
    for (const remote::AuthPrompt& prompt : prompts) {
        auto* edit = new QLineEdit(dialog);
        // The server's echo flag decides this, not a guess about the prompt
        // text: a one-time code always arrives with echo off.
        if (!prompt.echo)
            edit->setEchoMode(QLineEdit::Password);
        form->addRow(prompt.text, edit);
        fields.append(edit);
    }
    layout->addLayout(form);

    auto* buttons = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, dialog);
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, dialog, &QDialog::reject);
    connect(dialog, &QDialog::accepted, this, [this, fields] {
        QStringList responses;
        for (QLineEdit* field : fields)
            responses << field->text();
        client_->answerAuthPrompt(responses);
        // Nothing keeps a copy: the fields die with the dialog and the codes
        // are single-use anyway.
    });
    connect(dialog, &QDialog::rejected, this, [this] {
        client_->cancelAuthPrompt();
        setStatus(tr("Authentication cancelled"), false);
    });

    dialog->open();
    if (!fields.isEmpty())
        fields.first()->setFocus();
}

void HpcPanel::onSessionStateChanged(
    remote::RemoteClient::SessionState state, const QString& detail)
{
    using State = remote::RemoteClient::SessionState;
    connectionState_ = state;
    updateConnectionButton();
    switch (state) {
    case State::Disconnected:
        // A non-empty detail here is the reason the session went away, not
        // an ordinary "you pressed Disconnect" — keep the line terse and
        // put the reason where the rest of this panel's detail lives.
        setStatus(detail.isEmpty() ? tr("Not connected") : tr("Disconnected"),
                  detail.isEmpty(), detail);
        break;
    case State::Connecting:
        setStatus(tr("Connecting to %1…").arg(detail));
        break;
    case State::Authenticating:
        setStatus(tr("Authenticating…"));
        break;
    case State::Connected:
        setStatus(tr("Connected to %1").arg(detail), true);
        break;
    case State::Reconnecting:
        setStatus(tr("Reconnecting…"), false);
        break;
    case State::NeedsReauth:
        // Deliberately a message and not a dialog: this state exists exactly
        // so a background poll cannot make a 2FA prompt appear unbidden.
        setStatus(tr("Session lost — press Connect to reauthenticate"), false,
                  detail);
        appendLog(tr("--- %1 ---\n")
                      .arg(detail.isEmpty()
                               ? tr("the SSH session was lost")
                               : detail),
                  true);
        break;
    }
}

void HpcPanel::onProbeFinished(bool ok, const QString& message,
                                        const QString& schedulerFound)
{
    if (!ok) {
        setStatus(tr("Connection check failed"), false, message);
        appendLog(message, true);
        return;
    }
    QString status = tr("Connected — home: %1").arg(message);
    if (schedulerFound == QLatin1String("slurm")) {
        status += tr(" — SLURM detected");
        schedulerCombo_->setCurrentIndex(0);
    } else if (schedulerFound == QLatin1String("qsub")) {
        status += tr(" — qsub detected (PBS/SGE)");
    } else {
        status += tr(" — no scheduler found");
    }
    setStatus(status, true);
}

void HpcPanel::submitStagedJob(const QString& localJobDir,
                                        const QString& jobName)
{
    const remote::SshConfig config = configFromUi();
    if (config.host.isEmpty() || config.username.isEmpty()) {
        setStatus(tr("Enter host and user first"), false);
        return;
    }
    saveSettings();
    client_->setConfig(config);

    // Wrapper script generated from the Scheduler tab settings.
    const core::RemoteJobSpec spec = specFromUi(jobName);
    QFile wrapper(localJobDir + QStringLiteral("/job.sh"));
    if (!wrapper.open(QIODevice::WriteOnly | QIODevice::Text)) {
        appendLog(tr("could not write job.sh\n"), true);
        return;
    }
    QTextStream(&wrapper)
        << QString::fromStdString(core::SchedulerScript::generate(spec));
    wrapper.close();

    localJobDir_ = localJobDir;
    remoteJobDir_ = config.remoteDir + QStringLiteral("/")
        + QFileInfo(localJobDir).fileName();
    jobId_.clear();
    cancelPending_ = false;
    logView_->clear();
    jobLabel_->setText(tr("Uploading to %1…").arg(remoteJobDir_));
    stateLabel_->clear();
    downloadButton_->setEnabled(false);

    QStringList files;
    const QDir dir(localJobDir);
    for (const QFileInfo& info : dir.entryInfoList(QDir::Files))
        files << info.absoluteFilePath();

    // upload -> submit -> monitor, each step chained on success.
    auto* uploadConnection = new QMetaObject::Connection;
    *uploadConnection = connect(
        client_, &remote::RemoteClient::uploadFinished, this,
        [this, uploadConnection](bool ok, const QString& error) {
            QObject::disconnect(*uploadConnection);
            delete uploadConnection;
            if (!ok) {
                appendLog(tr("upload failed: %1\n").arg(error), true);
                jobLabel_->setText(tr("Upload failed"));
                return;
            }
            jobLabel_->setText(tr("Submitting…"));
            client_->submit(
                remoteJobDir_,
                QString::fromStdString(
                    core::SchedulerScript::submitCommand(scheduler())));
        });
    client_->upload(remoteJobDir_, files);
}

void HpcPanel::onSubmitFinished(bool ok, const QString& jobId,
                                         const QString& message)
{
    if (cancelPending_) {
        // This was the scancel/qdel acknowledgment, not a submission.
        cancelPending_ = false;
        appendLog(tr("cancel requested: %1\n").arg(message));
        return;
    }
    if (!ok) {
        appendLog(tr("submission failed: %1\n").arg(message), true);
        jobLabel_->setText(tr("Submission failed"));
        return;
    }
    jobId_ = jobId;
    jobLabel_->setText(jobId.isEmpty()
                           ? tr("Submitted (id unknown)")
                           : tr("Job %1 @ %2").arg(jobId, remoteJobDir_));
    appendLog(message + QStringLiteral("\n"));
    cancelButton_->setEnabled(!jobId.isEmpty());
    if (!jobId.isEmpty())
        client_->startMonitor(
            remoteJobDir_,
            QString::fromStdString(core::SchedulerScript::schedulerKey(scheduler())),
            jobId_);
}

void HpcPanel::onMonitorFinished(const QString& finalState)
{
    // A monitor that stopped because the SESSION died says nothing about the
    // job, which is still running on the cluster. Downloading "results" then
    // would fetch a half-written directory and look like completion.
    if (finalState.startsWith(QLatin1String("MONITOR-FAILED"))) {
        appendLog(tr("--- stopped watching the job: %1 ---\n").arg(finalState),
                  true);
        cancelButton_->setEnabled(false);
        downloadButton_->setEnabled(true);
        return;
    }
    stateLabel_->setText(finalState.isEmpty() ? tr("FINISHED") : finalState);
    cancelButton_->setEnabled(false);
    downloadButton_->setEnabled(true);
    appendLog(tr("--- job left the queue (%1) — downloading results ---\n")
                  .arg(finalState));
    downloadResults();
}

void HpcPanel::downloadResults()
{
    if (remoteJobDir_.isEmpty() || localJobDir_.isEmpty())
        return;
    auto* downloadConnection = new QMetaObject::Connection;
    *downloadConnection = connect(
        client_, &remote::RemoteClient::downloadFinished, this,
        [this, downloadConnection](bool ok, const QString& localDir,
                                   const QStringList& files, const QString& error) {
            QObject::disconnect(*downloadConnection);
            delete downloadConnection;
            if (!ok) {
                appendLog(tr("download failed: %1\n").arg(error), true);
                return;
            }
            appendLog(tr("--- %n file(s) downloaded to %1 ---\n", nullptr,
                         static_cast<int>(files.size()))
                          .arg(localDir));
            Q_EMIT resultsReady(localDir);
        });
    client_->download(remoteJobDir_, localJobDir_, kResultPatterns);
}

void HpcPanel::cancelRemoteJob()
{
    if (jobId_.isEmpty())
        return;
    cancelPending_ = true;
    client_->submit(remoteJobDir_,
                    QString::fromStdString(core::SchedulerScript::cancelCommand(
                        scheduler(), jobId_.toStdString())));
}

void HpcPanel::appendLog(const QString& text, bool isError)
{
    if (isError)
        logView_->appendHtml(
            QStringLiteral("<span style=\"color:#e06c60;\">%1</span>")
                .arg(text.toHtmlEscaped().replace(QStringLiteral("\n"),
                                                  QStringLiteral("<br>"))));
    else
        logView_->insertPlainText(text);
    logView_->verticalScrollBar()->setValue(
        logView_->verticalScrollBar()->maximum());
}

void HpcPanel::setStatus(const QString& text, bool ok, const QString& detail)
{
    statusLabel_->setText(text);
    statusLabel_->setToolTip(detail.isEmpty() ? text : detail);
    statusLabel_->setStyleSheet(ok ? QStringLiteral("color: #4caf50;")
                                   : QStringLiteral("color: #e06c60;"));
}

QString HpcPanel::shortConnectionError(
    const QString& raw, remote::RemoteClient::ErrorKind kind) const
{
    using Kind = remote::RemoteClient::ErrorKind;
    // Checked ahead of `kind`: a timeout can surface through more than one
    // ErrorKind depending on which step of the handshake it interrupted, but
    // the raw text always says "timed out" — a cause worth naming precisely
    // when it's there, rather than folding into the vaguer kind below it.
    if (raw.contains(QLatin1String("timed out"), Qt::CaseInsensitive)
        || raw.contains(QLatin1String("timeout"), Qt::CaseInsensitive))
        return tr("Timeout");
    switch (kind) {
    case Kind::Auth:
    case Kind::AuthRequired:
        return tr("Authentication failed");
    case Kind::HostKey:
        return tr("Host key changed");
    case Kind::Network:
        return tr("Host unreachable");
    case Kind::Remote:
        return tr("Remote error");
    case Kind::Internal:
        return tr("Internal error");
    case Kind::None:
        break;
    }
    return tr("Connection failed");
}

void HpcPanel::updateConnectionButton()
{
    using State = remote::RemoteClient::SessionState;
    switch (connectionState_) {
    case State::Disconnected:
    case State::NeedsReauth:
        connectionButton_->setText(tr("Connect"));
        connectionButton_->setEnabled(!connectionBusy_);
        break;
    case State::Connecting:
    case State::Authenticating:
        connectionButton_->setText(tr("Connecting…"));
        // RemoteClient::abort() does not interrupt an in-flight Op::Connect
        // on the helper side, so there is no real cancel to wire up here —
        // disabling the button is the documented fallback for that case.
        connectionButton_->setEnabled(false);
        break;
    case State::Connected:
    case State::Reconnecting:
        connectionButton_->setText(tr("Disconnect"));
        // Always clickable, even mid-job: closing the session is the escape
        // hatch, and a monitored job keeps running on the cluster regardless.
        connectionButton_->setEnabled(true);
        break;
    }
}

void HpcPanel::saveSettings() const
{
    QSettings settings;
    settings.beginGroup(QStringLiteral("remote"));
    settings.setValue(QStringLiteral("host"), hostEdit_->text());
    settings.setValue(QStringLiteral("port"), portSpin_->value());
    settings.setValue(QStringLiteral("user"), userEdit_->text());
    settings.setValue(QStringLiteral("keyPath"), keyPathEdit_->text());
    settings.setValue(QStringLiteral("remoteDir"), remoteDirEdit_->text());
    settings.setValue(QStringLiteral("scheduler"), schedulerCombo_->currentIndex());
    settings.setValue(QStringLiteral("queue"), queueEdit_->text());
    settings.setValue(QStringLiteral("nodes"), nodesSpin_->value());
    settings.setValue(QStringLiteral("tasks"), tasksSpin_->value());
    settings.setValue(QStringLiteral("memoryMb"), memorySpin_->value());
    settings.setValue(QStringLiteral("parallelEnv"), peEdit_->text());
    settings.setValue(QStringLiteral("walltime"), walltimeEdit_->text());
    settings.setValue(QStringLiteral("setup"), setupEdit_->toPlainText());
    settings.endGroup();
    if (presetCombo_)
        settings.setValue(QStringLiteral("hpc/lastCluster"),
                          presetCombo_->currentText().trimmed());
    // Deliberately no password/passphrase — memory only.
}

void HpcPanel::restoreSettings()
{
    QSettings settings;
    settings.beginGroup(QStringLiteral("remote"));
    hostEdit_->setText(settings.value(QStringLiteral("host")).toString());
    portSpin_->setValue(settings.value(QStringLiteral("port"), 22).toInt());
    userEdit_->setText(settings.value(QStringLiteral("user")).toString());
    // The old "auth" key (which method was picked) is left unread: there is
    // no more picker to seed, and the method is now inferred fresh from
    // keyPath/password every time a connection is attempted.
    keyPathEdit_->setText(settings.value(QStringLiteral("keyPath")).toString());
    remoteDirEdit_->setText(settings.value(QStringLiteral("remoteDir")).toString());
    schedulerCombo_->setCurrentIndex(
        settings.value(QStringLiteral("scheduler"), 0).toInt());
    queueEdit_->setText(settings.value(QStringLiteral("queue")).toString());
    nodesSpin_->setValue(settings.value(QStringLiteral("nodes"), 1).toInt());
    tasksSpin_->setValue(settings.value(QStringLiteral("tasks"), 1).toInt());
    memorySpin_->setValue(settings.value(QStringLiteral("memoryMb"), 0).toInt());
    peEdit_->setText(settings.value(QStringLiteral("parallelEnv"),
                                    QStringLiteral("smp")).toString());
    walltimeEdit_->setText(
        settings.value(QStringLiteral("walltime"), QStringLiteral("01:00:00"))
            .toString());
    setupEdit_->setPlainText(settings.value(QStringLiteral("setup")).toString());
    settings.endGroup();
    updateSchedulerRows();
}

// ---------------------------------------------------------------------------
// Cluster presets
// ---------------------------------------------------------------------------

ClusterPreset HpcPanel::presetFromUi(const QString& name) const
{
    ClusterPreset preset;
    preset.name = name.trimmed();
    preset.host = hostEdit_->text().trimmed();
    preset.port = portSpin_->value();
    preset.username = userEdit_->text().trimmed();
    // preset.auth is left at its default: there is no picker to read it
    // from any more, and ClusterPreset::auth exists only so a preset file
    // saved before this change still deserializes without error.
    preset.keyPath = keyPathEdit_->text().trimmed();
    preset.remoteDir = remoteDirEdit_->text().trimmed();
    preset.scheduler = schedulerCombo_->currentIndex();
    preset.queue = queueEdit_->text().trimmed();
    preset.nodes = nodesSpin_->value();
    preset.tasksPerNode = tasksSpin_->value();
    preset.memoryMbPerNode = memorySpin_->value();
    preset.walltime = walltimeEdit_->text().trimmed();
    preset.parallelEnvironment = peEdit_->text().trimmed();
    preset.setupLines = setupEdit_->toPlainText();
    // The password is deliberately absent — see ClusterPreset.
    return preset;
}

void HpcPanel::applyPreset(const ClusterPreset& preset)
{
    hostEdit_->setText(preset.host);
    portSpin_->setValue(preset.port);
    userEdit_->setText(preset.username);
    // preset.auth (a saved-before-this-change picker selection, if present)
    // is deliberately not applied — the method is inferred fresh from
    // keyPath/password below and whatever gets typed into Password next.
    keyPathEdit_->setText(preset.keyPath);
    remoteDirEdit_->setText(preset.remoteDir);
    schedulerCombo_->setCurrentIndex(preset.scheduler);
    queueEdit_->setText(preset.queue);
    nodesSpin_->setValue(preset.nodes);
    tasksSpin_->setValue(preset.tasksPerNode);
    memorySpin_->setValue(preset.memoryMbPerNode);
    walltimeEdit_->setText(preset.walltime);
    peEdit_->setText(preset.parallelEnvironment);
    setupEdit_->setPlainText(preset.setupLines);
    // Switching cluster must not carry the previous one's password across:
    // it would be silently wrong, and on a locked-out account it costs the
    // user a failed login they cannot explain.
    passwordEdit_->clear();
    setStatus(tr("Not connected"), true);
    updateSchedulerRows();
}

void HpcPanel::refreshPresetCombo(const QString& select)
{
    const QSignalBlocker blocker(presetCombo_);
    const QString keep = select.isEmpty() ? presetCombo_->currentText() : select;
    presetCombo_->clear();
    for (const ClusterPreset& preset : presets_)
        presetCombo_->addItem(preset.name);
    const int at = ClusterPresets::indexOf(presets_, keep);
    if (at >= 0)
        presetCombo_->setCurrentIndex(at);
    else
        presetCombo_->setCurrentText(keep);
    presetDeleteButton_->setEnabled(at >= 0);
}

void HpcPanel::saveCurrentPreset()
{
    const QString name = presetCombo_->currentText().trimmed();
    if (name.isEmpty()) {
        setStatus(tr("Name the cluster before saving it."), false);
        return;
    }
    ClusterPresets::upsert(presets_, presetFromUi(name));
    ClusterPresets::save(presets_);
    refreshPresetCombo(name);
    setStatus(tr("Saved \"%1\".").arg(name), true);
}

void HpcPanel::deleteCurrentPreset()
{
    const QString name = presetCombo_->currentText().trimmed();
    if (!ClusterPresets::remove(presets_, name)) {
        setStatus(tr("No saved cluster called \"%1\".").arg(name), false);
        return;
    }
    ClusterPresets::save(presets_);
    refreshPresetCombo(presets_.isEmpty() ? QString() : presets_.first().name);
    if (!presets_.isEmpty())
        applyPreset(presets_.first());
    setStatus(tr("Deleted \"%1\".").arg(name), true);
}

void HpcPanel::applySelectedPreset()
{
    const int at = ClusterPresets::indexOf(presets_,
                                           presetCombo_->currentText().trimmed());
    if (at >= 0)
        applyPreset(presets_[at]);
    presetDeleteButton_->setEnabled(at >= 0);
}

void HpcPanel::updateSchedulerRows()
{
    // The parallel environment is an SGE concept; SLURM and PBS describe the
    // layout directly, so the row would be a control their scripts ignore.
    if (schedulerForm_ && peRow_ >= 0)
        schedulerForm_->setRowVisible(
            peRow_, schedulerCombo_->currentIndex()
                        == static_cast<int>(core::Scheduler::Sge));
}

} // namespace calango::gui
