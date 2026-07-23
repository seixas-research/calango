#pragma once

#include <QList>
#include <QString>

namespace calango::gui {

/// One discovered Conda environment: its short name and the absolute env
/// directory (which contains bin/python), suitable to hand to
/// CondaEnvs::resolvePython.
struct CondaEnv {
    QString name;
    QString path;
};

/// Conda environment auto-detection driven by the "Conda Directory Path"
/// preference (QSettings key "jobs/condaDir"). Populates the environment
/// dropdowns in the simulation wizards.
class CondaEnvs {
public:
    /// The resolved envs directory: the configured path (accepting either a
    /// conda root that contains `envs/`, or the envs dir itself), else the
    /// first of the common install locations that exists ("" if none).
    static QString envsDirectory();

    /// Environments found under envsDirectory() that contain a python
    /// interpreter, sorted by name. Empty when nothing is configured/found.
    static QList<CondaEnv> discover();

    /// Accepts a python executable path, a conda environment root, or its
    /// bin/ directory; returns the interpreter path, or "" if none is found
    /// there (also for empty input, which every caller reads as "use the
    /// embedded interpreter"). Shared by every job-launching dialog — it used
    /// to live on the removed CalculatorDialog.
    static QString resolvePython(const QString& input);
};

} // namespace calango::gui
