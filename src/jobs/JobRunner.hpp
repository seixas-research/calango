#pragma once

#include "core/Structure.hpp"

#include <QObject>
#include <QMap>
#include <QProcess>
#include <QString>

#include <memory>

namespace calango::jobs {

/// Runs one generated ASE script as a local subprocess (QProcess) and
/// streams its output back to the GUI, line by line, in real time.
///
/// Lines matching "CALANGO_PROGRESS <step> <total>" additionally emit
/// progress() so views can drive a progress bar. Running out-of-process
/// (instead of via the embedded interpreter) keeps the GUI responsive and
/// isolates simulation crashes from the application.
class JobRunner : public QObject {
    Q_OBJECT

public:
    explicit JobRunner(QObject* parent = nullptr);

    bool isRunning() const;

    /// Process id of the running job, or 0 when nothing is running.
    ///
    /// The status bar samples this and everything below it. The pid is the
    /// SHELL the command was launched through — the compute usually lives in
    /// its children (`mpirun -n 4 gpaw …`), so a sampler that stops at this
    /// process alone would report near-zero for a machine that is fully busy.
    qint64 processId() const;
    /// Human label of the running job (the task name the host registered), or
    /// an empty string when idle.
    QString description() const { return description_; }

public Q_SLOTS:
    /// Launch `commandLine` through the system shell with `workDir` as the
    /// working directory.
    ///
    /// A shell rather than a direct exec because the command comes from a
    /// user-editable template (Preferences → "Run", and the wizard's
    /// "Running:" field): those legitimately carry leading environment
    /// assignments ("OMP_NUM_THREADS=1 gpaw …") and redirections
    /// ("> pw.out"), which only a shell interprets. `pythonExe` is not
    /// executed — it locates the environment whose bin/ is prepended to PATH,
    /// so solver binaries installed beside the interpreter (pw.x, siesta) win
    /// over globally installed ones; for conda-style layouts CONDA_PREFIX is
    /// set to the environment root as well. `extraEnv` carries per-engine
    /// hand-offs such as ASE_ESPRESSO_COMMAND.
    void start(const QString& commandLine, const QString& pythonExe,
               const QString& workDir,
               const QMap<QString, QString>& extraEnv = {});

    /// Politely terminate; escalates to kill() after a grace period.
    void terminate();

Q_SIGNALS:
    void started(const QString& description);
    void outputLine(const QString& line);
    void errorLine(const QString& line);
    void progress(int step, int total);
    /// One sample per "CALANGO_ENERGY <step> <energy_eV>" marker.
    void energySample(int step, double energyEv);
    /// One sample per "CALANGO_TEMP <step> <temperature_K>" marker.
    void temperatureSample(int step, double temperatureK);
    /// One sample per "CALANGO_FMAX <step> <fmax_eV_per_A>" marker: the
    /// maximum atomic force component during optimizations and MD runs.
    void maxForceSample(int step, double fmaxEvPerA);
    /// One sample per "CALANGO_PRESSURE <step> <pressure_GPa>" marker:
    /// the scalar pressure -tr(stress)/3, emitted by barostatted MD only.
    void pressureSample(int step, double pressureGPa);
    /// "CALANGO_TARGET_TEMP <K>": the thermostat setpoint of a
    /// constant-temperature MD run (never emitted for NVE).
    void targetTemperature(double temperatureK);
    /// "CALANGO_TARGET_PRESSURE <GPa>": the barostat setpoint of a
    /// constant-pressure MD run (NPT/NPH only).
    void targetPressure(double pressureGPa);
    /// One complete streamed geometry per "CALANGO_CELL … / CALANGO_FRAME
    /// <n> / n atom lines" block: live trajectory frames during MD and
    /// relaxations. Atom lines are consumed here (not echoed to the log).
    void frameStreamed(const std::shared_ptr<core::Structure>& frame);
    void finished(int exitCode, bool crashed);

private:
    void flushChannel(QString& buffer, const QByteArray& chunk, bool isStderr);
    void handleLine(const QString& line, bool isStderr);

    QProcess process_;
    QString description_;
    QString stdoutBuffer_;
    QString stderrBuffer_;

    // Frame-streaming state machine (CALANGO_CELL / CALANGO_FRAME).
    std::unique_ptr<core::Structure> pendingFrame_;
    int pendingAtoms_ = 0;
    /// The in-flight frame declared "FV": its atom lines carry force and
    /// velocity components after the position, accumulated here and attached
    /// to the structure once the last atom arrives.
    bool pendingVectors_ = false;
    std::vector<core::Vec3> pendingForces_;
    std::vector<core::Vec3> pendingVelocities_; ///< atom lines still expected for the frame
    bool pendingCellValid_ = false;
    /// Incremented every time a process actually starts.
    ///
    /// terminate() escalates to SIGKILL on a timer, and the timer has to know
    /// WHICH process it was armed for: a runner that starts a new job inside
    /// the escalation window would otherwise have the previous job's timer
    /// kill it. That is not hypothetical — both the Orchestration canvas
    /// (abort, then Resume) and the Processes panel (delete a running job
    /// while others are queued) restart this runner within seconds.
    int startGeneration_ = 0;
    double pendingCell_[9] = {};
};

} // namespace calango::jobs
