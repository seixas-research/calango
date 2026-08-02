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
    // MLIP engines. These keys are what lands in ~/.calango/settings.json, so
    // they must stay stable once shipped.
    case core::CalculatorKind::DeepMd: return QStringLiteral("DeepMD");
    case core::CalculatorKind::NequIp: return QStringLiteral("NequIP");
    case core::CalculatorKind::Allegro: return QStringLiteral("Allegro");
    case core::CalculatorKind::ChgNet: return QStringLiteral("CHGNet");
    case core::CalculatorKind::MatterSim: return QStringLiteral("MatterSim");
    case core::CalculatorKind::FairChem: return QStringLiteral("FAIRChem");
    case core::CalculatorKind::Lammps: return QStringLiteral("LAMMPS");
    case core::CalculatorKind::Xtb: return QStringLiteral("xTB");
    // No '+' in the stored key: it is a JSON object key in settings.json and
    // stays friendlier to hand-editing without punctuation.
    case core::CalculatorKind::DftbPlus: return QStringLiteral("DFTBPlus");
    case core::CalculatorKind::Gromacs: return QStringLiteral("GROMACS");
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
    case core::CalculatorKind::DeepMd: return QStringLiteral("DeepMD-kit");
    case core::CalculatorKind::NequIp: return QStringLiteral("NequIP");
    case core::CalculatorKind::Allegro: return QStringLiteral("Allegro");
    case core::CalculatorKind::ChgNet: return QStringLiteral("CHGNet");
    case core::CalculatorKind::MatterSim: return QStringLiteral("MatterSim");
    case core::CalculatorKind::FairChem:
        return QStringLiteral("FAIRChem / OCP");
    case core::CalculatorKind::Lammps: return QStringLiteral("LAMMPS");
    case core::CalculatorKind::Xtb: return QStringLiteral("xTB");
    case core::CalculatorKind::DftbPlus: return QStringLiteral("DFTB+");
    case core::CalculatorKind::Gromacs: return QStringLiteral("GROMACS");
    }
    return QStringLiteral("Default");
}

const QVector<core::CalculatorKind>& configurableEngines()
{
    // Every engine that needs a package the embedded interpreter does not
    // ship. The MLIP entries each pull a different (and mutually hostile)
    // deep-learning stack — deepmd-kit, nequip, chgnet, mattersim and
    // fairchem-core routinely pin conflicting torch builds — so each gets its
    // own environment slot rather than sharing one "ML" preset.
    static const QVector<core::CalculatorKind> kEngines = {
        core::CalculatorKind::Gpaw,
        core::CalculatorKind::Mace,
        core::CalculatorKind::QuantumEspresso,
        core::CalculatorKind::Siesta,
        core::CalculatorKind::Orca,
        core::CalculatorKind::Vasp,
        core::CalculatorKind::DeepMd,
        core::CalculatorKind::NequIp,
        core::CalculatorKind::Allegro,
        core::CalculatorKind::ChgNet,
        core::CalculatorKind::MatterSim,
        core::CalculatorKind::FairChem,
        // LAMMPS gets its own slot for the same reason the MLIP engines do:
        // conda-forge's `lammps` pulls a complete MPI stack, and sharing an
        // environment with GPAW (which pulls its own) is how both stop working.
        core::CalculatorKind::Lammps,
        // xTB runs in-process through xtb-python, so the env IS the engine;
        // DFTB+ and GROMACS need only ASE in the interpreter, but conda-forge
        // is also how their binaries (dftbplus, gromacs) are usually
        // installed, and pointing the engine at that env keeps interpreter
        // and binary from drifting apart.
        core::CalculatorKind::Xtb,
        core::CalculatorKind::DftbPlus,
        core::CalculatorKind::Gromacs,
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
