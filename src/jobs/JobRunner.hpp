#pragma once

#include "core/Structure.hpp"

#include <QObject>
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

public Q_SLOTS:
    /// Launch `pythonExe scriptPath` with `workDir` as the working directory.
    ///
    /// The interpreter's directory is prepended to PATH so that solver
    /// binaries installed alongside it (pw.x, siesta, ... in a conda env's
    /// bin/) are found without global PATH conflicts; for conda-style
    /// layouts CONDA_PREFIX is set to the environment root as well.
    void start(const QString& pythonExe, const QString& scriptPath, const QString& workDir);

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
    QString stdoutBuffer_;
    QString stderrBuffer_;

    // Frame-streaming state machine (CALANGO_CELL / CALANGO_FRAME).
    std::unique_ptr<core::Structure> pendingFrame_;
    int pendingAtoms_ = 0; ///< atom lines still expected for the frame
    bool pendingCellValid_ = false;
    double pendingCell_[9] = {};
};

} // namespace calango::jobs
