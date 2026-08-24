#include "gui/VaspPotcarPreflight.hpp"

#include "core/CalculatorConfig.hpp"

#include <QDir>
#include <QFileInfo>

namespace calango::gui {

VaspPotcarPreflightResult checkVaspPotcar(const QString& potcarPath,
                                          const QStringList& elements,
                                          const QString& xc)
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

    // The family ASE will ACTUALLY use for this run's functional, from the
    // one shared mapping the generated script reads too. Searching a fixed
    // list instead is what let a PBE-only library pass here under xc=LDA and
    // then fail inside ASE looking for `potpaw`.
    const QString family =
        QString::fromLatin1(core::vaspPotcarFamilyDir(xc.toStdString()));
    const QDir root(trimmed);

    // Both documented layouts, in the order the script tries them.
    const QStringList candidates{root.filePath(family), trimmed};

    const auto missingUnder = [&elements](const QString& base) {
        QStringList missing;
        for (const QString& element : elements) {
            const QString file =
                QDir(base).filePath(element + QLatin1String("/POTCAR"));
            if (!QFileInfo::exists(file))
                missing << element;
        }
        return missing;
    };

    for (const QString& base : candidates) {
        if (!QFileInfo(base).isDir())
            continue;
        // With no element list the directory existing is all that can be
        // checked; the family dir being present is the stronger signal, so
        // it wins when both exist.
        const QStringList missing = missingUnder(base);
        if (missing.isEmpty()) {
            result.searchedPath = base;
            result.ok = true;
            return result;
        }
    }

    // Nothing resolved completely. Report against the layout that got
    // CLOSEST -- the one with the fewest missing elements, preferring the
    // family dir on a tie. Reporting against the family dir unconditionally
    // named every element as missing on a flat library that was merely
    // short one, which turns "you are missing Mg" into "none of this
    // works".
    result.searchedPath = candidates.front();
    result.missingElements = missingUnder(candidates.front());
    for (const QString& base : candidates) {
        if (!QFileInfo(base).isDir())
            continue;
        const QStringList missing = missingUnder(base);
        if (missing.size() < result.missingElements.size()) {
            result.searchedPath = base;
            result.missingElements = missing;
        }
    }
    // An element missing from BOTH layouts is genuinely missing; one that
    // resolves under the other layout would have returned above.
    QStringList searchedLines;
    for (const QString& base : candidates)
        searchedLines << QStringLiteral("  %1/<element>/POTCAR").arg(base);
    result.errorMessage =
        QObject::tr("No POTCAR for %1.\n\nSearched:\n%2\n\n"
                    "Expected one of two layouts under the configured "
                    "directory:\n"
                    "  %3/<element>/POTCAR   (the library parent), or\n"
                    "  %4/<element>/POTCAR   (the directory IS the library)\n"
                    "The family is %5 because this run uses xc=%6: the "
                    "PBE-based functionals use potpaw_PBE, LDA uses potpaw.")
            .arg(result.missingElements.isEmpty()
                     ? QObject::tr("the requested elements")
                     : result.missingElements.join(QStringLiteral(", ")),
                 searchedLines.join(QLatin1Char('\n')),
                 root.filePath(family), trimmed, family,
                 xc.trimmed().isEmpty() ? QObject::tr("(unset)") : xc);
    return result;
}

} // namespace calango::gui
