// VASP POTCAR pre-flight: the LOCAL, C++-side approximation of the check the
// generated script performs at runtime (AseScriptGenerator.cpp, emitVasp()).
//
// Every fixture here is a SYNTHETIC POTCAR tree — a directory named POTCAR
// holding one line of placeholder text — never real POTCAR content, which is
// licensed material Calango must never bundle, generate, or even approximate
// convincingly. The check only cares that a file named POTCAR exists at the
// expected path; it never reads what is inside one.

#include "gui/VaspPotcarPreflight.hpp"

#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QTextStream>

#include <cstdio>
#include <cstdlib>

using namespace calango::gui;

namespace {

int failures = 0;

void check(bool ok, const char* label)
{
    std::printf("  %-4s %s\n", ok ? "ok" : "FAIL", label);
    if (!ok)
        ++failures;
}

/// Writes a synthetic (obviously not real) POTCAR at
/// `root/<variantDir>/<element>/POTCAR`, or `root/<element>/POTCAR` when
/// `variantDir` is empty (the flat layout).
void writeFakePotcar(const QString& root, const QString& variantDir,
                     const QString& element)
{
    const QString dirPath = variantDir.isEmpty()
        ? QDir(root).filePath(element)
        : QDir(root).filePath(variantDir + QLatin1Char('/') + element);
    QDir().mkpath(dirPath);
    QFile file(QDir(dirPath).filePath(QStringLiteral("POTCAR")));
    file.open(QIODevice::WriteOnly | QIODevice::Text);
    QTextStream(&file) << "SYNTHETIC TEST FIXTURE -- NOT A REAL POTCAR\n";
}

} // namespace

int main()
{
    std::printf("Not configured:\n");
    {
        const auto result = checkVaspPotcar(QString());
        check(!result.ok, "an empty path is not ok");
        check(result.errorMessage.contains(QStringLiteral("No POTCAR directory")),
              "and says so, not a path-not-found message");
        check(result.searchedPath.isEmpty(), "with nothing to report as searched");

        const auto whitespace = checkVaspPotcar(QStringLiteral("   "));
        check(!whitespace.ok, "whitespace-only is treated the same as empty");
    }

    std::printf("Directory does not exist:\n");
    {
        const auto result =
            checkVaspPotcar(QStringLiteral("/no/such/potcar/directory"));
        check(!result.ok, "a non-existent directory is not ok");
        check(result.errorMessage.contains(QStringLiteral("not found")),
              "and the message says not found");
        check(result.errorMessage.contains(
                  QStringLiteral("/no/such/potcar/directory")),
              "naming the exact path that was searched");
    }

    QTemporaryDir tmp;
    check(tmp.isValid(), "scratch directory created");

    std::printf("Nested layout (potpaw_PBE/<El>/POTCAR):\n");
    {
        const QString root = tmp.filePath(QStringLiteral("nested"));
        writeFakePotcar(root, QStringLiteral("potpaw_PBE"), QStringLiteral("Si"));
        writeFakePotcar(root, QStringLiteral("potpaw_PBE"), QStringLiteral("O"));

        const auto pathOnly = checkVaspPotcar(root);
        check(pathOnly.ok,
              "with no element list, only the directory itself is checked");
        check(pathOnly.searchedPath == QDir(root).filePath(QStringLiteral("potpaw_PBE")),
              "and the resolved variant directory is reported");

        const auto complete =
            checkVaspPotcar(root, {QStringLiteral("Si"), QStringLiteral("O")});
        check(complete.ok, "every requested element present -> ok");
        check(complete.missingElements.isEmpty(), "with nothing missing");

        const auto oneMissing = checkVaspPotcar(
            root, {QStringLiteral("Si"), QStringLiteral("O"), QStringLiteral("Fe")});
        check(!oneMissing.ok, "a library missing ONE element's POTCAR is not ok");
        check(oneMissing.missingElements.size() == 1
                  && oneMissing.missingElements.first() == QStringLiteral("Fe"),
              "and names exactly that element, not the ones that ARE present");
        check(oneMissing.errorMessage.contains(QStringLiteral("Fe"))
                  && !oneMissing.errorMessage.contains(QStringLiteral("No POTCAR for Si"))
                  && !oneMissing.errorMessage.contains(QStringLiteral("No POTCAR for O")),
              "the message itself names only the missing element");

        const auto twoMissing = checkVaspPotcar(
            root, {QStringLiteral("Si"), QStringLiteral("Fe"), QStringLiteral("Pt")});
        check(!twoMissing.ok && twoMissing.missingElements.size() == 2,
              "multiple missing elements are all reported, not just the first");
        check(twoMissing.missingElements.contains(QStringLiteral("Fe"))
                  && twoMissing.missingElements.contains(QStringLiteral("Pt")),
              "specifically Fe and Pt");
    }

    std::printf("Flat layout (element folders directly under the root):\n");
    {
        const QString root = tmp.filePath(QStringLiteral("flat"));
        writeFakePotcar(root, QString(), QStringLiteral("Si"));
        writeFakePotcar(root, QString(), QStringLiteral("O"));

        const auto result =
            checkVaspPotcar(root, {QStringLiteral("Si"), QStringLiteral("O")});
        check(result.ok, "a flat layout with every element present is ok — "
                         "the same shim the generated script applies at "
                         "runtime, recognised here without mutating anything");
        check(result.searchedPath == root,
              "the flat root itself is reported as searched, no potpaw_* "
              "subdirectory exists to descend into");

        const auto missing =
            checkVaspPotcar(root, {QStringLiteral("Si"), QStringLiteral("Mg")});
        check(!missing.ok && missing.missingElements == QStringList{QStringLiteral("Mg")},
              "and a missing element is still caught in the flat layout");
    }

    std::printf("Preference order among variant directories:\n");
    {
        // PBE takes precedence over LDA when both happen to exist, matching
        // the exact search order the generated script uses.
        const QString root = tmp.filePath(QStringLiteral("both_variants"));
        writeFakePotcar(root, QStringLiteral("potpaw_LDA"), QStringLiteral("Si"));
        writeFakePotcar(root, QStringLiteral("potpaw_PBE"), QStringLiteral("Si"));
        const auto result = checkVaspPotcar(root);
        check(result.searchedPath == QDir(root).filePath(QStringLiteral("potpaw_PBE")),
              "potpaw_PBE is preferred over potpaw_LDA when both are present");
    }

    if (failures == 0) {
        std::printf("\nAll VASP POTCAR pre-flight checks passed.\n");
        return EXIT_SUCCESS;
    }
    std::printf("\n%d VASP POTCAR pre-flight check(s) FAILED.\n", failures);
    return EXIT_FAILURE;
}
