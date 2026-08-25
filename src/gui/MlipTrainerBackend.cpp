#include "gui/MlipTrainerBackend.hpp"

#include "gui/MaceTrainerBackend.hpp"

#include <QCoreApplication>

namespace calango::gui {

MlipTrainerBackend::~MlipTrainerBackend() = default;

void MlipTrainerBackend::applyTorchDevices(bool, bool, bool) {}

QString MlipTrainerBackend::selectedDevice() const { return {}; }

QString MlipTrainerBackend::pythonModule() const
{
    const char* module = core::mlipPythonModule(kind());
    return module ? QString::fromLatin1(module) : QString();
}

QString MlipTrainerBackend::pipPackage() const
{
    const char* package = core::mlipPipPackage(kind());
    return package ? QString::fromLatin1(package) : QString();
}

QString MlipTrainerBackend::configFileName() const
{
    const MlipTrainerFramework* entry = mlipTrainerFramework(kind());
    return entry ? entry->configFileName : QString();
}

namespace {

/// What a framework's own trainer reads and runs, and — for the six with no
/// backend yet — what writing one would take.
///
/// One entry per MachineLearning-family engine, looked up by kind rather
/// than listed positionally, so the family gaining a member is a compile-
/// time-visible gap here rather than a silently missing row.
struct FrameworkFacts {
    const char* configFormat;
    const char* configFileName;
    const char* entryPoint;
    /// Null for an implemented framework.
    const char* status;
};

FrameworkFacts factsFor(core::CalculatorKind kind)
{
    using core::CalculatorKind;
    switch (kind) {
    case CalculatorKind::Mace:
        return {"YAML", "mace_train.yaml",
                "mace.cli.run_train (in-process, via its own main())",
                nullptr};
    case CalculatorKind::DeepMd:
        return {"JSON", "deepmd_input.json", "dp train <input.json>",
                QT_TRANSLATE_NOOP(
                    "calango::gui",
                    "A JSON config split into `model` (descriptor + fitting "
                    "net) and `training` (batching, decay, checkpointing), "
                    "and a dataset in DeePMD's own raw/npy layout rather "
                    "than extended XYZ — so a backend needs a dataset "
                    "CONVERTER as well as a config writer, and the "
                    "descriptor choice (se_e2_a, se_atten, …) is a real "
                    "modelling decision that needs its own page.")};
    case CalculatorKind::NequIp:
        return {"YAML", "nequip_train.yaml", "nequip-train <config.yaml>",
                QT_TRANSLATE_NOOP(
                    "calango::gui",
                    "A YAML keyed to NequIP's module graph: the config names "
                    "the layers and their irreps, so its \"model size\" is "
                    "not one preset but a description of a network. A "
                    "backend needs that vocabulary plus NequIP's own "
                    "dataset block (`dataset: ase`, key mapping, "
                    "train/valid split by index).")};
    case CalculatorKind::Allegro:
        return {"YAML", "allegro_train.yaml", "nequip-train <config.yaml>",
                QT_TRANSLATE_NOOP(
                    "calango::gui",
                    "Trained through NequIP's own trainer with the Allegro "
                    "model builders substituted, so it inherits NequIP's "
                    "config vocabulary and adds its strictly-local layer "
                    "parameters. Closest to reachable of the six — but "
                    "only once the NequIP backend exists to derive from.")};
    case CalculatorKind::ChgNet:
        return {"Python", "chgnet_train.py",
                "chgnet.trainer.Trainer (library API, no CLI)",
                QT_TRANSLATE_NOOP(
                    "calango::gui",
                    "CHGNet has no config file and no command-line trainer: "
                    "training is a Python API (Trainer(model=…).train(…)). "
                    "So its \"config\" is a generated SCRIPT, not a "
                    "document — the final wizard page would be editing "
                    "Python. It also fits MAGNETIC MOMENTS alongside "
                    "energies and forces, which the Dataset Manager does "
                    "not currently carry.")};
    case CalculatorKind::MatterSim:
        return {"CLI flags", "mattersim_finetune.sh",
                "mattersim.training.finetune_mattersim",
                QT_TRANSLATE_NOOP(
                    "calango::gui",
                    "MatterSim is distributed as a pretrained universal "
                    "potential and what it supports is FINE-TUNING, not "
                    "training from scratch — the parameter set is a "
                    "checkpoint to start from plus a small optimizer "
                    "block, which is a different first page from every "
                    "other entry here. Its trainer is driven by command-"
                    "line flags rather than a config file.")};
    case CalculatorKind::FairChem:
        return {"YAML", "fairchem_train.yaml", "fairchem / OCP main.py",
                QT_TRANSLATE_NOOP(
                    "calango::gui",
                    "FAIRChem's configs are composed from included YAML "
                    "fragments (a base per model family, plus dataset and "
                    "optimizer overrides) rather than written flat, and its "
                    "datasets are LMDB. A backend has to either reproduce "
                    "that composition or ship the fragments — and the "
                    "checkpoints worth fine-tuning are large enough that "
                    "the dataset path is the smaller half of the work.")};
    default:
        return {nullptr, nullptr, nullptr, nullptr};
    }
}

std::vector<MlipTrainerFramework> buildRegistry()
{
    std::vector<MlipTrainerFramework> registry;
    // Walk the calculator library rather than listing the frameworks here:
    // the page must show what Calango can RUN, and an MLIP added to the
    // family later appears automatically (unsupported, with no status text,
    // which is a visible gap rather than an invisible one).
    for (int value = 0; core::isValidCalculatorKind(value); ++value) {
        const auto kind = static_cast<core::CalculatorKind>(value);
        if (core::calculatorFamily(kind)
            != core::CalculatorFamily::MachineLearning)
            continue;
        const FrameworkFacts facts = factsFor(kind);
        MlipTrainerFramework entry;
        entry.kind = kind;
        entry.name = QString::fromStdString(toString(kind));
        entry.implemented = facts.status == nullptr && facts.configFormat;
        entry.configFormat = facts.configFormat
            ? QString::fromLatin1(facts.configFormat)
            : QString();
        entry.configFileName = facts.configFileName
            ? QString::fromLatin1(facts.configFileName)
            : QString();
        entry.entryPoint =
            facts.entryPoint ? QString::fromLatin1(facts.entryPoint) : QString();
        entry.status = facts.status
            ? QCoreApplication::translate("calango::gui", facts.status)
            : QString();
        registry.push_back(std::move(entry));
    }
    return registry;
}

} // namespace

const std::vector<MlipTrainerFramework>& mlipTrainerFrameworks()
{
    static const std::vector<MlipTrainerFramework> kRegistry = buildRegistry();
    return kRegistry;
}

const MlipTrainerFramework* mlipTrainerFramework(core::CalculatorKind kind)
{
    for (const MlipTrainerFramework& entry : mlipTrainerFrameworks())
        if (entry.kind == kind)
            return &entry;
    return nullptr;
}

std::unique_ptr<MlipTrainerBackend> makeMlipTrainerBackend(
    core::CalculatorKind kind)
{
    // The one place a framework becomes supported. A second entry here and
    // a subclass beside MaceTrainerBackend is the whole of it.
    if (kind == core::CalculatorKind::Mace)
        return std::make_unique<MaceTrainerBackend>();
    return nullptr;
}

} // namespace calango::gui
