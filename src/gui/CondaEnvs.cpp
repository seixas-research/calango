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
