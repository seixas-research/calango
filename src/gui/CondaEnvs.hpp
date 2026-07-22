#pragma once

#include <QList>
#include <QString>

namespace calango::gui {

/// One discovered Conda environment: its short name and the absolute env
/// directory (which contains bin/python), suitable to hand to
/// CalculatorDialog::resolveEnvironmentPython.
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
};

} // namespace calango::gui
