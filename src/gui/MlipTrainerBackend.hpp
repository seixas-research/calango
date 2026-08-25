#pragma once

#include "core/CalculatorConfig.hpp"

#include <QList>
#include <QObject>
#include <QString>

#include <memory>
#include <vector>

class QWidget;
class QWizardPage;

namespace calango::gui {

/// One machine-learning-potential framework the Trainer's first page lists,
/// and what Calango can currently do with it.
///
/// THE LIST IS NOT WRITTEN HERE BY HAND. It is every engine in
/// core::CalculatorFamily::MachineLearning — the calculators Calango already
/// knows how to RUN — because a trainer for a framework the application
/// cannot then load the model into would be a dead end. The registry below
/// walks CalculatorKind and keeps whatever that family holds, so a future
/// MLIP engine appears on this page the moment it is added to the family,
/// marked unsupported until somebody writes its backend.
///
/// WHY EVERY ENTRY CARRIES A STATUS. Exactly one framework has an
/// implemented trainer (MACE). The other six are listed rather than hidden
/// because hiding them answers the user's question — "can Calango train a
/// NequIP model?" — with silence, and silence reads as "look harder". Each
/// carries the two facts that decide what its backend would cost: the
/// CONFIG FORMAT its trainer reads, and the ENTRY POINT that reads it. They
/// are not interchangeable — DeePMD wants a JSON with a `model`/`training`
/// split and its own descriptor block, NequIP a YAML keyed to its module
/// graph — which is exactly why they are not half-built here.
struct MlipTrainerFramework {
    core::CalculatorKind kind;
    /// Display name, as the calculator library already spells it.
    QString name;
    /// Whether makeMlipTrainerBackend(kind) returns a backend. When false
    /// the wizard lists the entry, shows `status`, and refuses to advance.
    bool implemented = false;
    /// The config file the framework's own trainer reads ("YAML",
    /// "JSON", …) and the file name it conventionally has.
    QString configFormat;
    QString configFileName;
    /// The command or Python entry point a training run goes through.
    QString entryPoint;
    /// What a backend for it would need — the text the framework page shows
    /// under an unimplemented entry, and the same text FUTURE.md carries.
    /// Empty for an implemented framework.
    QString status;
};

/// Every MLIP framework, in the calculator library's own family order.
const std::vector<MlipTrainerFramework>& mlipTrainerFrameworks();

/// The entry for `kind`, or nullptr when it is not an MLIP at all.
const MlipTrainerFramework* mlipTrainerFramework(core::CalculatorKind kind);

/// What the Trainer wizard needs from a framework, and the whole of it.
///
/// THE EXTENSION POINT. Adding a framework means writing one subclass and
/// one line in makeMlipTrainerBackend(); nothing in the wizard itself is
/// framework-specific. The division of labour is deliberate and is the part
/// worth reading before writing a second one:
///
///   * the BACKEND owns its own parameter pages. It is not handed a struct
///     of settings to translate, because a settings struct is a third place
///     for every knob to live and it would have to be the UNION of every
///     framework's knobs — DeePMD's descriptor cut-offs and NequIP's
///     irreps have nothing to say to each other, and a MACE-shaped struct
///     would make the second backend a series of unused fields.
///   * the WIZARD owns the framework page, the config page, the interpreter,
///     the pre-flight and the launch. Those are the same for every
///     framework, and a backend that re-implemented them would be a second
///     place for the "mace-torch is not installed" message to drift.
///
/// So: a backend supplies pages, reads its own widgets, and emits a config
/// and a launcher. Everything else is already written.
class MlipTrainerBackend : public QObject {
    Q_OBJECT

public:
    ~MlipTrainerBackend() override;

    virtual core::CalculatorKind kind() const = 0;

    /// The parameter pages, in order, inserted between the framework page
    /// and the config page. Ownership passes to the caller (QWizard takes
    /// it). Called ONCE, at construction.
    virtual QList<QWizardPage*> createParameterPages(QWidget* parent) = 0;

    /// The config text for whatever the pages currently hold. Called on
    /// every settings change and by "Regenerate from settings".
    ///
    /// `interpreterNote` is a line the wizard has to contribute and the
    /// backend has to place: which build of the framework's package the
    /// config was generated against, from the last successful environment
    /// check. Empty when nothing has been checked yet — the backend must
    /// then emit no version line rather than an unknown one.
    virtual QString buildConfig(const QString& interpreterNote) const = 0;

    /// A SELF-CONTAINED launcher that writes `config` and runs the training.
    /// Self-contained in the same sense every generated script in this
    /// application is: it imports the framework and nothing from Calango, so
    /// it can be copied to a cluster and run as it stands.
    virtual QString runnerScript(const QString& config) const = 0;

    /// Pre-wire the dataset fields from an Orchestration Dataset Manager
    /// node's manifest. Empty arguments leave the existing values alone.
    virtual void prefillFromDatasetManifest(const QString& trainPath,
                                            const QString& validPath,
                                            const QString& energyKey,
                                            const QString& forcesKey) = 0;

    /// Offer a framework that runs on PyTorch the device availability the
    /// wizard has just probed, so it can DEFAULT its device control to the
    /// best one present. Default: ignored, for a framework with no such
    /// control.
    virtual void applyTorchDevices(bool cuda, bool mps, bool probeSucceeded);
    /// The compute device the user currently has selected, so the wizard
    /// can warn when the probe did not find it. Empty when the framework
    /// offers no device choice.
    virtual QString selectedDevice() const;

    /// The Python module the pre-flight must find importable, and the pip
    /// name to suggest when it cannot. Both come from the calculator
    /// library's own tables rather than being repeated here.
    QString pythonModule() const;
    QString pipPackage() const;
    /// The config file's name and format, from the registry entry.
    QString configFileName() const;

Q_SIGNALS:
    /// Something on a parameter page changed, so the config text the wizard
    /// shows is stale. The wizard decides what to do about it — which is
    /// NOT always "regenerate": a config the user has hand-edited is theirs,
    /// and this signal must never silently overwrite it.
    void settingsChanged();
};

/// A backend for `kind`, or nullptr when none is implemented. The ONE place
/// a framework becomes supported.
std::unique_ptr<MlipTrainerBackend> makeMlipTrainerBackend(
    core::CalculatorKind kind);

} // namespace calango::gui
