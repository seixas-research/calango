#include "gui/RemoteAccessPanel.hpp"

#include <QComboBox>
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

RemoteAccessPanel::RemoteAccessPanel(const QString& pythonExe, QWidget* parent)
    : QWidget(parent)
    , client_(new remote::RemoteClient(pythonExe, this))
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(4, 4, 4, 4);
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

    authCombo_ = new QComboBox(connectionPage);
    authCombo_->addItems({tr("SSH key"), tr("Password")});
    form->addRow(tr("Auth:"), authCombo_);

    auto* keyRow = new QWidget(connectionPage);
    auto* keyLayout = new QHBoxLayout(keyRow);
    keyLayout->setContentsMargins(0, 0, 0, 0);
    keyPathEdit_ = new QLineEdit(connectionPage);
    keyPathEdit_->setPlaceholderText(tr("~/.ssh/id_rsa (empty = agent/default keys)"));
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
    form->addRow(tr("Password:"), passwordEdit_);

    remoteDirEdit_ = new QLineEdit(connectionPage);
    remoteDirEdit_->setPlaceholderText(tr("calango_jobs (relative to $HOME)"));
    form->addRow(tr("Remote dir:"), remoteDirEdit_);

    auto* testRow = new QWidget(connectionPage);
    auto* testLayout = new QHBoxLayout(testRow);
    testLayout->setContentsMargins(0, 0, 0, 0);
    testButton_ = new QPushButton(tr("Test Connection"), connectionPage);
    statusLabel_ = new QLabel(tr("Not connected"), connectionPage);
    statusLabel_->setStyleSheet(QStringLiteral("color: gray;"));
    testLayout->addWidget(testButton_);
    testLayout->addWidget(statusLabel_, 1);
    form->addRow(QString(), testRow);
    connect(testButton_, &QPushButton::clicked,
            this, &RemoteAccessPanel::testConnection);

    const auto syncAuthMode = [this](int index) {
        const bool key = index == 0;
        keyPathEdit_->setEnabled(key);
        keyBrowseButton_->setEnabled(key);
    };
    connect(authCombo_, &QComboBox::currentIndexChanged, this, syncAuthMode);
    syncAuthMode(authCombo_->currentIndex());

    tabs->addTab(connectionPage, tr("Connection"));

    // ---- Scheduler tab ----------------------------------------------------
    auto* schedulerPage = new QWidget(tabs);
    auto* schedulerForm = new QFormLayout(schedulerPage);
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

    tasksSpin_ = new QSpinBox(schedulerPage);
    tasksSpin_->setRange(1, 4096);
    tasksSpin_->setValue(1);
    schedulerForm->addRow(tr("Tasks / cores:"), tasksSpin_);

    walltimeEdit_ = new QLineEdit(QStringLiteral("01:00:00"), schedulerPage);
    schedulerForm->addRow(tr("Walltime:"), walltimeEdit_);

    setupEdit_ = new QPlainTextEdit(schedulerPage);
    setupEdit_->setPlaceholderText(
        tr("# environment setup, e.g.\nmodule load python\nsource ~/venv/bin/activate"));
    setupEdit_->setMaximumHeight(64);
    schedulerForm->addRow(tr("Setup:"), setupEdit_);

    submitButton_ = new QPushButton(tr("Submit Calculation…"), schedulerPage);
    submitButton_->setToolTip(
        tr("Configure an ASE calculation and run it on the cluster\n"
           "(uploads run.py, the structure and a job.sh wrapper, then submits)"));
    schedulerForm->addRow(QString(), submitButton_);
    connect(submitButton_, &QPushButton::clicked,
            this, &RemoteAccessPanel::submitCalculationRequested);

    tabs->addTab(schedulerPage, tr("Scheduler"));

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
            this, &RemoteAccessPanel::cancelRemoteJob);
    connect(downloadButton_, &QPushButton::clicked,
            this, &RemoteAccessPanel::downloadResults);

    // ---- Client wiring ----------------------------------------------------
    connect(client_, &remote::RemoteClient::probeFinished,
            this, &RemoteAccessPanel::onProbeFinished);
    connect(client_, &remote::RemoteClient::busyChanged, this, [this](bool busy) {
        testButton_->setEnabled(!busy);
        submitButton_->setEnabled(!busy);
    });
    connect(client_, &remote::RemoteClient::fileUploaded, this, [this](const QString& f) {
        appendLog(tr("uploaded %1\n").arg(f));
    });
    connect(client_, &remote::RemoteClient::submitFinished,
            this, &RemoteAccessPanel::onSubmitFinished);
    connect(client_, &remote::RemoteClient::jobStateChanged, this,
            [this](const QString& state) { stateLabel_->setText(state); });
    connect(client_, &remote::RemoteClient::remoteLog, this,
            [this](const QString& stream, const QString& text) {
                appendLog(text, stream == QLatin1String("err"));
            });
    connect(client_, &remote::RemoteClient::monitorFinished,
            this, &RemoteAccessPanel::onMonitorFinished);
    connect(client_, &remote::RemoteClient::fileDownloaded, this, [this](const QString& f) {
        appendLog(tr("downloaded %1\n").arg(f));
    });

    restoreSettings();
}

core::Scheduler RemoteAccessPanel::scheduler() const
{
    return static_cast<core::Scheduler>(schedulerCombo_->currentIndex());
}

remote::SshConfig RemoteAccessPanel::configFromUi() const
{
    remote::SshConfig config;
    config.host = hostEdit_->text().trimmed();
    config.port = portSpin_->value();
    config.username = userEdit_->text().trimmed();
    config.auth = authCombo_->currentIndex() == 1
        ? remote::SshConfig::Auth::Password
        : remote::SshConfig::Auth::Key;
    config.keyPath = keyPathEdit_->text().trimmed();
    config.password = passwordEdit_->text();
    const QString dir = remoteDirEdit_->text().trimmed();
    config.remoteDir = dir.isEmpty() ? QStringLiteral("calango_jobs") : dir;
    return config;
}

core::RemoteJobSpec RemoteAccessPanel::specFromUi(const QString& jobName) const
{
    core::RemoteJobSpec spec;
    spec.scheduler = scheduler();
    spec.jobName = jobName.toStdString();
    spec.queue = queueEdit_->text().trimmed().toStdString();
    spec.tasks = tasksSpin_->value();
    spec.walltime = walltimeEdit_->text().trimmed().toStdString();
    spec.setupLines = setupEdit_->toPlainText().toStdString();
    return spec;
}

void RemoteAccessPanel::testConnection()
{
    const remote::SshConfig config = configFromUi();
    if (config.host.isEmpty() || config.username.isEmpty()) {
        setStatus(tr("Enter host and user first"), false);
        return;
    }
    saveSettings();
    client_->setConfig(config);
    setStatus(tr("Connecting…"));
    client_->probe();
}

void RemoteAccessPanel::onProbeFinished(bool ok, const QString& message,
                                        const QString& schedulerFound)
{
    if (!ok) {
        setStatus(message, false);
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

void RemoteAccessPanel::submitStagedJob(const QString& localJobDir,
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

void RemoteAccessPanel::onSubmitFinished(bool ok, const QString& jobId,
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

void RemoteAccessPanel::onMonitorFinished(const QString& finalState)
{
    stateLabel_->setText(finalState.isEmpty() ? tr("FINISHED") : finalState);
    cancelButton_->setEnabled(false);
    downloadButton_->setEnabled(true);
    appendLog(tr("--- job left the queue (%1) — downloading results ---\n")
                  .arg(finalState));
    downloadResults();
}

void RemoteAccessPanel::downloadResults()
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

void RemoteAccessPanel::cancelRemoteJob()
{
    if (jobId_.isEmpty())
        return;
    cancelPending_ = true;
    client_->submit(remoteJobDir_,
                    QString::fromStdString(core::SchedulerScript::cancelCommand(
                        scheduler(), jobId_.toStdString())));
}

void RemoteAccessPanel::appendLog(const QString& text, bool isError)
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

void RemoteAccessPanel::setStatus(const QString& text, bool ok)
{
    statusLabel_->setText(text);
    statusLabel_->setStyleSheet(ok ? QStringLiteral("color: #4caf50;")
                                   : QStringLiteral("color: #e06c60;"));
}

void RemoteAccessPanel::saveSettings() const
{
    QSettings settings;
    settings.beginGroup(QStringLiteral("remote"));
    settings.setValue(QStringLiteral("host"), hostEdit_->text());
    settings.setValue(QStringLiteral("port"), portSpin_->value());
    settings.setValue(QStringLiteral("user"), userEdit_->text());
    settings.setValue(QStringLiteral("auth"), authCombo_->currentIndex());
    settings.setValue(QStringLiteral("keyPath"), keyPathEdit_->text());
    settings.setValue(QStringLiteral("remoteDir"), remoteDirEdit_->text());
    settings.setValue(QStringLiteral("scheduler"), schedulerCombo_->currentIndex());
    settings.setValue(QStringLiteral("queue"), queueEdit_->text());
    settings.setValue(QStringLiteral("tasks"), tasksSpin_->value());
    settings.setValue(QStringLiteral("walltime"), walltimeEdit_->text());
    settings.setValue(QStringLiteral("setup"), setupEdit_->toPlainText());
    settings.endGroup();
    // Deliberately no password/passphrase — memory only.
}

void RemoteAccessPanel::restoreSettings()
{
    QSettings settings;
    settings.beginGroup(QStringLiteral("remote"));
    hostEdit_->setText(settings.value(QStringLiteral("host")).toString());
    portSpin_->setValue(settings.value(QStringLiteral("port"), 22).toInt());
    userEdit_->setText(settings.value(QStringLiteral("user")).toString());
    authCombo_->setCurrentIndex(settings.value(QStringLiteral("auth"), 0).toInt());
    keyPathEdit_->setText(settings.value(QStringLiteral("keyPath")).toString());
    remoteDirEdit_->setText(settings.value(QStringLiteral("remoteDir")).toString());
    schedulerCombo_->setCurrentIndex(
        settings.value(QStringLiteral("scheduler"), 0).toInt());
    queueEdit_->setText(settings.value(QStringLiteral("queue")).toString());
    tasksSpin_->setValue(settings.value(QStringLiteral("tasks"), 1).toInt());
    walltimeEdit_->setText(
        settings.value(QStringLiteral("walltime"), QStringLiteral("01:00:00"))
            .toString());
    setupEdit_->setPlainText(settings.value(QStringLiteral("setup")).toString());
    settings.endGroup();
}

} // namespace calango::gui
