#include "gui/VaspPotcarPreflight.hpp"

#include <QDir>
#include <QFileInfo>

namespace calango::gui {

VaspPotcarPreflightResult checkVaspPotcar(const QString& potcarPath,
                                          const QStringList& elements)
{
    VaspPotcarPreflightResult result;
    const QString trimmed = potcarPath.trimmed();
    if (trimmed.isEmpty()) {
        result.errorMessage =
            QObject::tr("No POTCAR directory configured. Set it in "
                        "Preferences -> External Files (VASP), or "
                        "per-cluster in the HPC panel (Scheduler -> VASP "
                        "POTCAR directory).");
        return result;
    }

    if (!QFileInfo(trimmed).isDir()) {
        result.searchedPath = trimmed;
        result.errorMessage =
            QObject::tr("POTCAR directory not found: %1").arg(trimmed);
        return result;
    }

    // Same three-name search the generated script performs (and the same
    // preference order: PBE first, matching ASE's own default).
    const QDir root(trimmed);
    QString variantDir;
    for (const char* candidate : {"potpaw_PBE", "potpaw", "potpaw_LDA"}) {
        if (QFileInfo(root.filePath(QLatin1String(candidate))).isDir()) {
            variantDir = QLatin1String(candidate);
            break;
        }
    }
    // A flat layout (element folders directly under potcarPath, no
    // potpaw_PBE/... wrapper) is exactly what the in-script shim exists
    // for — checked here the same way, without creating the shim itself:
    // this is a read-only probe.
    const bool flat = variantDir.isEmpty();
    result.searchedPath = flat ? trimmed : root.filePath(variantDir);

    for (const QString& element : elements) {
        const QString potcarFile =
            QDir(result.searchedPath).filePath(element + QLatin1String("/POTCAR"));
        if (!QFileInfo::exists(potcarFile))
            result.missingElements << element;
    }

    if (!result.missingElements.isEmpty()) {
        result.errorMessage =
            QObject::tr("No POTCAR for %1 under %2")
                .arg(result.missingElements.join(QStringLiteral(", ")),
                     result.searchedPath);
        return result;
    }

    result.ok = true;
    return result;
}

} // namespace calango::gui
