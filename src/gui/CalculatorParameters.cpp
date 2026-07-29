#include "gui/CalculatorParameters.hpp"

#include "gui/EnginePresets.hpp"
#include "gui/SettingsManager.hpp"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>

#include <algorithm>

namespace calango::gui::CalculatorParameters {

namespace {

/// Fold one {"pw": …, "kpts": […]} object into the running suggestion,
/// keeping the strictest value seen so far.
void mergeEntry(const QJsonObject& entry, Suggestion& suggestion)
{
    const QJsonValue pw = entry.value(QStringLiteral("pw"));
    if (pw.isDouble() && pw.toDouble() > 0.0)
        suggestion.planeWaveCutoffEv =
            std::max(suggestion.planeWaveCutoffEv.value_or(0.0),
                     pw.toDouble());

    const QJsonValue kptsValue = entry.value(QStringLiteral("kpts"));
    if (kptsValue.isArray()) {
        const QJsonArray kpts = kptsValue.toArray();
        if (kpts.size() == 3) {
            std::array<int, 3> mesh =
                suggestion.kpts.value_or(std::array<int, 3>{1, 1, 1});
            bool valid = true;
            for (int axis = 0; axis < 3; ++axis) {
                const int k = kpts.at(axis).toInt();
                if (k < 1) {
                    valid = false; // a malformed axis invalidates the triple
                    break;
                }
                mesh[axis] = std::max(mesh[axis], k);
            }
            if (valid)
                suggestion.kpts = mesh;
        }
    }
}

} // namespace

QString filePath()
{
    return SettingsManager::directory()
        + QStringLiteral("/calculator_parameters.json");
}

Suggestion suggestionFor(core::CalculatorKind kind,
                         const QStringList& elements)
{
    ensureFileExists();

    Suggestion suggestion;
    QFile file(filePath());
    if (!file.open(QIODevice::ReadOnly))
        return suggestion; // unreadable — hardcoded defaults stand
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    if (!doc.isObject())
        return suggestion; // malformed — likewise
    const QJsonObject engine =
        doc.object().value(EnginePresets::presetName(kind)).toObject();
    if (engine.isEmpty())
        return suggestion;

    const QJsonObject perElement =
        engine.value(QStringLiteral("elements")).toObject();
    for (const QString& symbol : elements) {
        const QJsonValue entry = perElement.value(symbol);
        if (entry.isObject())
            mergeEntry(entry.toObject(), suggestion);
    }

    // Element entries answered nothing (or the structure is unknown): the
    // engine-wide default speaks, field by field.
    if (!suggestion.planeWaveCutoffEv || !suggestion.kpts) {
        Suggestion fallback;
        mergeEntry(engine.value(QStringLiteral("default")).toObject(),
                   fallback);
        if (!suggestion.planeWaveCutoffEv)
            suggestion.planeWaveCutoffEv = fallback.planeWaveCutoffEv;
        if (!suggestion.kpts)
            suggestion.kpts = fallback.kpts;
    }
    return suggestion;
}

void ensureFileExists()
{
    if (QFile::exists(filePath()))
        return;
    QDir().mkpath(SettingsManager::directory());

    // A worked skeleton rather than "{}": the file is the documentation. The
    // _comment keys ride along unread — the parser only looks at "pw",
    // "kpts", "default" and "elements".
    QJsonObject gpaw;
    gpaw.insert(QStringLiteral("_comment"),
                QStringLiteral("Per-element suggested defaults for this "
                               "engine. pw = plane-wave cutoff in eV; kpts = "
                               "[k1, k2, k3]. The strictest value among the "
                               "structure's elements wins; 'default' applies "
                               "when no element entry matches. Every field "
                               "is optional — anything absent falls back to "
                               "the built-in defaults."));
    gpaw.insert(QStringLiteral("default"), QJsonObject());
    gpaw.insert(QStringLiteral("elements"), QJsonObject());

    QJsonObject root;
    root.insert(QStringLiteral("_comment"),
                QStringLiteral("Suggested calculator parameters, keyed by "
                               "engine name as shown in Preferences → Run "
                               "(e.g. \"GPAW\", \"VASP\"). Read every time a "
                               "simulation wizard opens; edit freely."));
    root.insert(EnginePresets::presetName(core::CalculatorKind::Gpaw), gpaw);

    QSaveFile file(filePath());
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text))
        return; // read-only config dir — the feature simply stays dormant
    file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    file.commit();
}

} // namespace calango::gui::CalculatorParameters
