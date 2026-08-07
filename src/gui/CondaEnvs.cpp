#include "gui/CondaEnvs.hpp"

#include <QDir>
#include <QFileInfo>
#include <QProcessEnvironment>
#include <QSettings>

namespace calango::gui {

namespace {

bool hasPython(const QString& envDir)
{
    const QDir dir(envDir);
    for (const auto* candidate : {"bin/python", "bin/python3",
                                  "python.exe", "Scripts/python.exe"}) {
        if (QFileInfo::exists(dir.filePath(QLatin1String(candidate))))
            return true;
    }
    return false;
}

/// Normalize a user-supplied conda path to its `envs` directory: accept either
/// a conda root (…/miniconda3 → …/miniconda3/envs) or an envs dir directly.
QString normalizeToEnvsDir(const QString& path)
{
    if (path.isEmpty())
        return {};
    const QDir dir(path);
    if (dir.exists(QStringLiteral("envs")))
        return dir.filePath(QStringLiteral("envs"));
    return dir.absolutePath();
}

QStringList defaultRoots()
{
    const QString home = QDir::homePath();
    QStringList roots = {
        home + QStringLiteral("/miniconda3/envs"),
        home + QStringLiteral("/anaconda3/envs"),
        home + QStringLiteral("/miniforge3/envs"),
        home + QStringLiteral("/mambaforge/envs"),
        QStringLiteral("/opt/anaconda3/envs"),
        QStringLiteral("/opt/miniconda3/envs"),
        QStringLiteral("/opt/homebrew/Caskroom/miniconda/base/envs"),
    };
    // The env the app itself was launched from, if any.
    const QString prefix = QProcessEnvironment::systemEnvironment().value(
        QStringLiteral("CONDA_PREFIX"));
    if (!prefix.isEmpty()) {
        QDir p(prefix);
        if (p.cdUp() && p.dirName() == QLatin1String("envs"))
            roots.prepend(p.absolutePath());
    }
    return roots;
}

} // namespace

QString CondaEnvs::envsDirectory()
{
    const QString configured = QSettings()
                                   .value(QStringLiteral("jobs/condaDir"))
                                   .toString()
                                   .trimmed();
    if (!configured.isEmpty()) {
        const QString envs = normalizeToEnvsDir(configured);
        if (QFileInfo::exists(envs))
            return envs;
    }
    for (const QString& root : defaultRoots()) {
        if (QFileInfo::exists(root))
            return root;
    }
    return {};
}

QList<CondaEnv> CondaEnvs::discover()
{
    QList<CondaEnv> envs;
    const QString envsDir = envsDirectory();
    if (envsDir.isEmpty())
        return envs;

    const QDir dir(envsDir);
    const auto entries = dir.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
    for (const QString& name : entries) {
        const QString envPath = dir.absoluteFilePath(name);
        if (hasPython(envPath))
            envs.append({name, envPath});
    }
    return envs;
}

QString CondaEnvs::executableIn(const QString& envDir, const QString& executable)
{
    if (envDir.isEmpty() || executable.isEmpty())
        return {};
    const QDir dir(envDir);
    const QStringList candidates = {
#ifdef Q_OS_WIN
        // Conda on Windows scatters binaries across three places: the prefix
        // root, Scripts/ (entry points) and Library/bin/ (anything built
        // against the C toolchain, which is where a compiled solver lands).
        executable + QStringLiteral(".exe"),
        QStringLiteral("Scripts/") + executable + QStringLiteral(".exe"),
        QStringLiteral("Library/bin/") + executable + QStringLiteral(".exe"),
        QStringLiteral("Library/bin/") + executable,
#endif
        QStringLiteral("bin/") + executable,
    };
    for (const QString& candidate : candidates) {
        const QFileInfo info(dir.filePath(candidate));
        if (info.isFile() && info.isExecutable())
            return info.absoluteFilePath();
    }
    return {};
}

namespace {

/// Every environment root worth searching for a solver binary, in the order
/// findExecutable() tries them.
QStringList solverSearchRoots(const QString& preferredEnv)
{
    QStringList roots;
    const auto append = [&roots](const QString& path) {
        if (!path.isEmpty() && !roots.contains(path))
            roots.append(path);
    };

    // The engine's own configured environment, in whatever form it was stored:
    // an env root, its bin/, or the interpreter inside it.
    const QString configured = preferredEnv.trimmed();
    if (!configured.isEmpty()) {
        QFileInfo info(configured);
        QDir dir(info.isFile() ? info.absolutePath() : info.absoluteFilePath());
        if (dir.dirName() == QLatin1String("bin")
            || dir.dirName() == QLatin1String("Scripts"))
            dir.cdUp();
        append(dir.absolutePath());
    }

    append(QProcessEnvironment::systemEnvironment().value(
        QStringLiteral("CONDA_PREFIX")));

    const QString envsDir = CondaEnvs::envsDirectory();
    if (!envsDir.isEmpty()) {
        const QDir dir(envsDir);
        for (const QString& name :
             dir.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name))
            append(dir.absoluteFilePath(name));
        // The base environment sits beside `envs/`, not inside it, and is
        // where a `conda install -n base` lands.
        QDir base(dir);
        if (base.cdUp())
            append(base.absolutePath());
    }
    return roots;
}

} // namespace

QString CondaEnvs::findExecutable(const QString& executable,
                                  const QString& preferredEnv)
{
    for (const QString& root : solverSearchRoots(preferredEnv)) {
        const QString found = executableIn(root, executable);
        if (!found.isEmpty())
            return found;
    }
    return {};
}

QString CondaEnvs::environmentProviding(const QString& executable,
                                        const QString& preferredEnv)
{
    for (const QString& root : solverSearchRoots(preferredEnv)) {
        if (!executableIn(root, executable).isEmpty())
            return root;
    }
    return {};
}

QString CondaEnvs::resolvePython(const QString& input)
{
    const QString trimmed = input.trimmed();
    if (trimmed.isEmpty())
        return {};
    const QFileInfo info(trimmed);
    // A direct path to an interpreter is taken at face value: the user may be
    // pointing at a wrapper script or a differently-named build.
    if (info.isFile())
        return info.absoluteFilePath();
    if (info.isDir()) {
        const QDir dir(trimmed);
        // Ordered by likelihood: a conda/venv root first, then the case where
        // the user picked the bin/ (or Scripts/) directory itself.
        const QStringList candidates = {
#ifdef Q_OS_WIN
            QStringLiteral("python.exe"), QStringLiteral("Scripts/python.exe"),
#endif
            QStringLiteral("bin/python"), QStringLiteral("bin/python3"),
            QStringLiteral("python"), QStringLiteral("python3")};
        for (const QString& candidate : candidates) {
            if (QFileInfo::exists(dir.filePath(candidate)))
                return QFileInfo(dir.filePath(candidate)).absoluteFilePath();
        }
    }
    return {};
}

} // namespace calango::gui
