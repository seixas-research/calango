#include "gui/EnginePresets.hpp"

#include "gui/SettingsManager.hpp"

#include <QJsonDocument>
#include <QJsonObject>
#include <QSettings>

namespace calango::gui {

namespace EnginePresets {

QString presetName(core::CalculatorKind kind)
{
    switch (kind) {
    case core::CalculatorKind::Gpaw: return QStringLiteral("GPAW");
    case core::CalculatorKind::Mace: return QStringLiteral("MACE");
    case core::CalculatorKind::QuantumEspresso:
        return QStringLiteral("QuantumEspresso");
    case core::CalculatorKind::Siesta: return QStringLiteral("SIESTA");
    case core::CalculatorKind::Orca: return QStringLiteral("ORCA");
    case core::CalculatorKind::Vasp: return QStringLiteral("VASP");
    case core::CalculatorKind::EMT: return QStringLiteral("EMT");
    case core::CalculatorKind::Asap: return QStringLiteral("ASAP");
    case core::CalculatorKind::LennardJones:
        return QStringLiteral("LennardJones");
    }
    return QStringLiteral("default");
}

QString displayName(core::CalculatorKind kind)
{
    switch (kind) {
    case core::CalculatorKind::Gpaw: return QStringLiteral("GPAW");
    case core::CalculatorKind::Mace: return QStringLiteral("MACE");
    case core::CalculatorKind::QuantumEspresso:
        return QStringLiteral("Quantum ESPRESSO");
    case core::CalculatorKind::Siesta: return QStringLiteral("SIESTA");
    case core::CalculatorKind::Orca: return QStringLiteral("ORCA");
    case core::CalculatorKind::Vasp: return QStringLiteral("VASP");
    case core::CalculatorKind::EMT: return QStringLiteral("EMT");
    case core::CalculatorKind::Asap: return QStringLiteral("ASAP");
    case core::CalculatorKind::LennardJones:
        return QStringLiteral("Lennard-Jones");
    }
    return QStringLiteral("Default");
}

const QVector<core::CalculatorKind>& configurableEngines()
{
    static const QVector<core::CalculatorKind> kEngines = {
        core::CalculatorKind::Gpaw,
        core::CalculatorKind::Mace,
        core::CalculatorKind::QuantumEspresso,
        core::CalculatorKind::Siesta,
        core::CalculatorKind::Orca,
        core::CalculatorKind::Vasp,
    };
    return kEngines;
}

QJsonObject readMap()
{
    const QString raw =
        QSettings().value(SettingsManager::kEnvironmentPresets).toString();
    return QJsonDocument::fromJson(raw.toUtf8()).object();
}

void writeMap(const QJsonObject& map)
{
    QSettings().setValue(
        SettingsManager::kEnvironmentPresets,
        QString::fromUtf8(QJsonDocument(map).toJson(QJsonDocument::Compact)));
}

QString envFor(core::CalculatorKind kind)
{
    return readMap().value(presetName(kind)).toString();
}

void setEnvFor(core::CalculatorKind kind, const QString& env)
{
    QJsonObject obj = readMap();
    if (env.trimmed().isEmpty())
        obj.remove(presetName(kind));
    else
        obj[presetName(kind)] = env;
    writeMap(obj);
}

} // namespace EnginePresets

} // namespace calango::gui
