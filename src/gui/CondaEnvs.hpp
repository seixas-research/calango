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

    // -- Solver binaries ----------------------------------------------------
    //
    // discover() answers "which environments can run a Python script", which
    // is why it insists on an interpreter. That is the wrong question for a
    // compiled solver: `conda create -n siesta -c conda-forge siesta` installs
    // a binary and no Python at all, so the environment holding the solver is
    // frequently one discover() will never list. These look for the BINARY.

    /// Absolute path of `executable` inside a Conda environment, or "" when no
    /// environment provides it (the caller then falls back to whatever $PATH
    /// resolves at run time).
    ///
    /// Search order: `preferredEnv` — the environment already configured for
    /// this engine, accepted in any of the forms resolvePython() takes — then
    /// $CONDA_PREFIX, then every environment under envsDirectory() in name
    /// order. First match wins, so an engine pointed at a specific environment
    /// keeps using that one even when several provide the binary.
    static QString findExecutable(const QString& executable,
                                  const QString& preferredEnv = QString());

    /// The environment ROOT that provides `executable` (the prefix, not the
    /// bin directory), or "". Same search order as findExecutable.
    ///
    /// Callers need the prefix rather than just the binary because the rest of
    /// the environment matters: whether it also ships an MPI launcher decides
    /// whether the solver may be launched under one at all.
    static QString environmentProviding(const QString& executable,
                                        const QString& preferredEnv = QString());

    /// Absolute path of `executable` inside the environment rooted at
    /// `envDir`, or "" — the per-environment lookup the two above are built
    /// from. Handles the platform's bin layout and executable suffix.
    static QString executableIn(const QString& envDir, const QString& executable);
};

} // namespace calango::gui
