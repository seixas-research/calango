#include "gui/OrchestrationTransforms.hpp"

#include "core/ClusterVariation.hpp"
#include "core/ClusterExpansionFit.hpp"
#include "core/CalphadModel.hpp"
#include "core/Element.hpp"
#include "core/SqsGenerator.hpp"
#include "core/Structure.hpp"
#include "core/TdbWriter.hpp"
#include "gui/GuiUtils.hpp"
#include "python_bridge/AseBridge.hpp"

#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QObject>
#include <QStringList>

#include <algorithm>
#include <cmath>
#include <map>
#include <set>

namespace calango::gui {

namespace {

QString joinShort(const QStringList& parts)
{
    return parts.join(QStringLiteral("; "));
}

} // namespace

// ---------------------------------------------------------------------------
// SupercellSpec
// ---------------------------------------------------------------------------

long SupercellSpec::determinant() const
{
    const auto l = [this](int i, int j) { return static_cast<long>(p[i][j]); };
    return l(0, 0) * (l(1, 1) * l(2, 2) - l(1, 2) * l(2, 1))
        - l(0, 1) * (l(1, 0) * l(2, 2) - l(1, 2) * l(2, 0))
        + l(0, 2) * (l(1, 0) * l(2, 1) - l(1, 1) * l(2, 0));
}

bool SupercellSpec::isIdentity() const
{
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 3; ++j)
            if (p[i][j] != (i == j ? 1 : 0))
                return false;
    return true;
}

bool SupercellSpec::isDiagonal() const
{
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 3; ++j)
            if (i != j && p[i][j] != 0)
                return false;
    return p[0][0] >= 1 && p[1][1] >= 1 && p[2][2] >= 1;
}

SupercellSpec SupercellSpec::diagonal(int na, int nb, int nc)
{
    SupercellSpec spec;
    spec.p[0][0] = na;
    spec.p[1][1] = nb;
    spec.p[2][2] = nc;
    return spec;
}

QString SupercellSpec::describe() const
{
    if (isDiagonal())
        return QStringLiteral("%1 x %2 x %3").arg(na()).arg(nb()).arg(nc());
    QStringList rows;
    for (const auto& row : p)
        rows << QStringLiteral("[%1,%2,%3]")
                    .arg(row[0])
                    .arg(row[1])
                    .arg(row[2]);
    // The determinant is the cell-count multiplier, and it is the number a
    // reader of a non-diagonal matrix actually wants: nine integers do not say
    // "eight times bigger" at a glance.
    return QObject::tr("[%1] (x%2)")
        .arg(rows.join(QStringLiteral(",")))
        .arg(std::labs(determinant()));
}

QJsonObject SupercellSpec::toJson() const
{
    QJsonArray matrix;
    for (const auto& row : p)
        matrix.append(QJsonArray{row[0], row[1], row[2]});
    QJsonObject object{{QStringLiteral("matrix"), matrix}};
    // The diagonal form is written alongside, for readers that understand only
    // repetitions. It is emitted ONLY when the matrix really is diagonal — a
    // reader that fell back to na/nb/nc on a non-diagonal cell would build a
    // different structure and report success, which is the one outcome this
    // format exists to prevent.
    if (isDiagonal()) {
        object.insert(QStringLiteral("na"), na());
        object.insert(QStringLiteral("nb"), nb());
        object.insert(QStringLiteral("nc"), nc());
    }
    return object;
}

SupercellSpec SupercellSpec::fromJson(const QJsonObject& object)
{
    const QJsonArray matrix = object.value(QStringLiteral("matrix")).toArray();
    if (matrix.size() == 3) {
        SupercellSpec spec;
        for (int i = 0; i < 3; ++i) {
            const QJsonArray row = matrix[i].toArray();
            for (int j = 0; j < 3 && j < row.size(); ++j)
                spec.p[i][j] = row[j].toInt();
        }
        return spec;
    }
    // A document written before the matrix existed.
    return diagonal(object.value(QStringLiteral("na")).toInt(2),
                    object.value(QStringLiteral("nb")).toInt(2),
                    object.value(QStringLiteral("nc")).toInt(2));
}

// ---------------------------------------------------------------------------
// DefectOperation
// ---------------------------------------------------------------------------

QString DefectOperation::kindName(Kind kind)
{
    switch (kind) {
    case Kind::Remove:
        return QStringLiteral("remove");
    case Kind::Add:
        return QStringLiteral("add");
    case Kind::Substitute:
        break;
    }
    return QStringLiteral("substitute");
}

QString DefectOperation::describe() const
{
    switch (kind) {
    case Kind::Remove:
        return QObject::tr("remove %1").arg(indices);
    case Kind::Add:
        return QObject::tr("add %1 at %2(%3, %4, %5)")
            .arg(element,
                 fractional ? QObject::tr("frac ") : QString())
            .arg(x, 0, 'g', 4)
            .arg(y, 0, 'g', 4)
            .arg(z, 0, 'g', 4);
    case Kind::Substitute:
        break;
    }
    return QObject::tr("substitute %1 with %2").arg(indices, element);
}

QJsonObject DefectOperation::toJson() const
{
    QJsonObject object{{QStringLiteral("kind"), kindName(kind)}};
    switch (kind) {
    case Kind::Remove:
        object.insert(QStringLiteral("indices"), indices);
        break;
    case Kind::Add:
        object.insert(QStringLiteral("element"), element);
        object.insert(QStringLiteral("position"),
                      QJsonArray{x, y, z});
        object.insert(QStringLiteral("fractional"), fractional);
        break;
    case Kind::Substitute:
        object.insert(QStringLiteral("indices"), indices);
        object.insert(QStringLiteral("element"), element);
        break;
    }
    return object;
}

DefectOperation DefectOperation::fromJson(const QJsonObject& object)
{
    DefectOperation operation;
    const QString kind = object.value(QStringLiteral("kind")).toString();
    if (kind == QLatin1String("remove"))
        operation.kind = Kind::Remove;
    else if (kind == QLatin1String("add"))
        operation.kind = Kind::Add;
    operation.indices = object.value(QStringLiteral("indices")).toString();
    operation.element = object.value(QStringLiteral("element"))
                            .toString(operation.element);
    const QJsonArray position =
        object.value(QStringLiteral("position")).toArray();
    if (position.size() == 3) {
        operation.x = position[0].toDouble();
        operation.y = position[1].toDouble();
        operation.z = position[2].toDouble();
    }
    operation.fractional =
        object.value(QStringLiteral("fractional")).toBool(false);
    return operation;
}

// ---------------------------------------------------------------------------
// DefectSpec
// ---------------------------------------------------------------------------

QString DefectSpec::describe() const
{
    QStringList parts;
    for (const DefectOperation& operation : operations)
        parts << operation.describe();
    return joinShort(parts);
}

QString DefectSpec::modeName(Mode mode)
{
    return mode == Mode::Separate
        ? QObject::tr("one material per defect")
        : QObject::tr("one material with every defect");
}

QJsonObject DefectSpec::toJson() const
{
    QJsonArray array;
    for (const DefectOperation& operation : operations)
        array.append(operation.toJson());
    return QJsonObject{
        {QStringLiteral("operations"), array},
        // Written as a word rather than an enum index: a document is read by
        // calango-cli and by people, and "separate" survives a reordering of
        // the enum that "1" does not.
        {QStringLiteral("mode"),
         mode == Mode::Separate ? QStringLiteral("separate")
                                : QStringLiteral("combined")}};
}

DefectSpec DefectSpec::fromJson(const QJsonObject& object)
{
    DefectSpec spec;
    for (const QJsonValue& value :
         object.value(QStringLiteral("operations")).toArray())
        spec.operations.append(DefectOperation::fromJson(value.toObject()));
    // Absent in documents written before the mode existed, and those all meant
    // the combined recipe — which is the default, so they load unchanged.
    spec.mode = object.value(QStringLiteral("mode")).toString()
            == QLatin1String("separate")
        ? Mode::Separate
        : Mode::Combined;
    return spec;
}

// ---------------------------------------------------------------------------
// RandomNoiseSpec
// ---------------------------------------------------------------------------

QString RandomNoiseSpec::describe() const
{
    if (!isValid())
        return QObject::tr("not configured");
    QStringList what;
    if (options.perturbPositions)
        what << QObject::tr("positions");
    if (options.perturbCell)
        what << QObject::tr("cell");
    return QObject::tr("%1 frames (%2 perturbed + reference), %3 %4 = %5 Å%6")
        .arg(variantCount())
        .arg(count)
        .arg(options.distribution
                     == core::NoiseOptions::Distribution::Gaussian
                 ? QObject::tr("Gaussian σ")
                 : QObject::tr("uniform ±"),
             what.join(QObject::tr(" & ")))
        .arg(options.amplitude, 0, 'g', 3)
        .arg(ramped ? QObject::tr(", ramped") : QString());
}

QJsonObject RandomNoiseSpec::toJson() const
{
    return QJsonObject{
        {QStringLiteral("distribution"),
         options.distribution == core::NoiseOptions::Distribution::Gaussian
             ? QStringLiteral("gaussian")
             : QStringLiteral("uniform")},
        {QStringLiteral("amplitude"), options.amplitude},
        {QStringLiteral("seed"), static_cast<int>(options.seed)},
        {QStringLiteral("perturb_positions"), options.perturbPositions},
        {QStringLiteral("perturb_cell"), options.perturbCell},
        {QStringLiteral("count"), count},
        {QStringLiteral("cumulative"), cumulative},
        {QStringLiteral("ramped"), ramped},
    };
}

RandomNoiseSpec RandomNoiseSpec::fromJson(const QJsonObject& object)
{
    RandomNoiseSpec spec;
    spec.options.distribution =
        object.value(QStringLiteral("distribution")).toString()
                == QLatin1String("uniform")
            ? core::NoiseOptions::Distribution::Uniform
            : core::NoiseOptions::Distribution::Gaussian;
    spec.options.amplitude =
        object.value(QStringLiteral("amplitude")).toDouble(spec.options.amplitude);
    spec.options.seed = static_cast<unsigned int>(
        object.value(QStringLiteral("seed")).toInt(static_cast<int>(spec.options.seed)));
    spec.options.perturbPositions =
        object.value(QStringLiteral("perturb_positions"))
            .toBool(spec.options.perturbPositions);
    spec.options.perturbCell =
        object.value(QStringLiteral("perturb_cell")).toBool(spec.options.perturbCell);
    spec.count = object.value(QStringLiteral("count")).toInt(spec.count);
    spec.cumulative =
        object.value(QStringLiteral("cumulative")).toBool(spec.cumulative);
    spec.ramped = object.value(QStringLiteral("ramped")).toBool(spec.ramped);
    return spec;
}

// ---------------------------------------------------------------------------
// SingleAtomContainerSpec
// ---------------------------------------------------------------------------

QString SingleAtomContainerSpec::describe() const
{
    return QObject::tr("%1 Å box, periodic")
        .arg(boxSizeAngstrom, 0, 'g', 6);
}

QJsonObject SingleAtomContainerSpec::toJson() const
{
    return QJsonObject{{QStringLiteral("box_size_angstrom"), boxSizeAngstrom}};
}

SingleAtomContainerSpec SingleAtomContainerSpec::fromJson(const QJsonObject& object)
{
    SingleAtomContainerSpec spec;
    spec.boxSizeAngstrom = object.value(QStringLiteral("box_size_angstrom"))
                               .toDouble(spec.boxSizeAngstrom);
    return spec;
}

QList<NamedStructure> buildSingleAtomBatch(const QList<NamedStructure>& sources,
                                           const SingleAtomContainerSpec& spec)
{
    // First-appearance order: walk every source structure's atoms in order,
    // recording each atomic number the first time it is seen. A std::set
    // would sort by Z instead, which reads as an arbitrary reshuffling of
    // the elements the source list actually names them in.
    std::vector<int> uniqueZ;
    for (const NamedStructure& source : sources) {
        if (!source.second)
            continue;
        for (const core::Atom& atom : source.second->atoms()) {
            if (std::find(uniqueZ.begin(), uniqueZ.end(), atom.atomicNumber)
                == uniqueZ.end())
                uniqueZ.push_back(atom.atomicNumber);
        }
    }

    QList<NamedStructure> result;
    const double side = spec.boxSizeAngstrom;
    for (int z : uniqueZ) {
        auto single = std::make_shared<core::Structure>();
        // UnitCell's own default is periodic on all three axes -- exactly
        // the "large periodic cubic box" convention this node commits to;
        // nothing here needs to ask for it separately.
        single->setCell(core::UnitCell({side, 0.0, 0.0}, {0.0, side, 0.0},
                                       {0.0, 0.0, side}));
        core::Atom atom;
        atom.atomicNumber = z;
        atom.position = {side / 2.0, side / 2.0, side / 2.0};
        single->addAtom(atom);
        result.append(
            {QString::fromUtf8(core::Elements::data(z).symbol), single});
    }
    return result;
}

QList<DefectVariant> applyDefectSet(const core::Structure& structure,
                                    const DefectSpec& spec, QString* error)
{
    if (spec.isEmpty()) {
        if (error)
            *error = QObject::tr("No defect operations are configured.");
        return {};
    }

    if (spec.mode == DefectSpec::Mode::Combined) {
        QString problem;
        core::Structure result = applyDefects(structure, spec, &problem);
        if (!problem.isEmpty()) {
            if (error)
                *error = problem;
            return {};
        }
        return {DefectVariant{spec.describe(), std::move(result)}};
    }

    // One material per operation, each applied to the PRISTINE input. Built by
    // handing applyDefects a one-operation recipe rather than by reimplementing
    // the edits: index resolution, the empty-match refusal and the ordering
    // rules are subtle enough that a second copy of them would drift.
    QList<DefectVariant> variants;
    variants.reserve(spec.operations.size());
    for (const DefectOperation& operation : spec.operations) {
        DefectSpec single;
        single.operations.append(operation);
        QString problem;
        core::Structure result = applyDefects(structure, single, &problem);
        if (!problem.isEmpty()) {
            // All or nothing: a screening set missing the one dopant that could
            // not be placed draws its conclusion from the wrong set, and does
            // it silently.
            if (error)
                *error = problem;
            return {};
        }
        variants.append(DefectVariant{operation.describe(), std::move(result)});
    }
    return variants;
}

// ---------------------------------------------------------------------------
// The transforms themselves
// ---------------------------------------------------------------------------

core::Structure applySupercell(const core::Structure& structure,
                               const SupercellSpec& spec, QString* error)
{
    const auto fail = [error](const QString& message) {
        if (error)
            *error = message;
    };
    if (!spec.isValid()) {
        fail(QObject::tr(
            "The supercell matrix is singular (det P = 0): the three "
            "transformed lattice vectors are coplanar or collinear, which is "
            "not a cell."));
        return structure;
    }
    if (spec.isIdentity())
        return structure; // nothing to do, and nothing to complain about
    if (!structure.cell().isDefined()) {
        fail(QObject::tr(
            "The incoming structure has no periodic cell, so it cannot be "
            "repeated. Give it a cell upstream, or drop the Supercell node."));
        return structure;
    }
    try {
        // The diagonal case goes through Atoms.repeat rather than
        // make_supercell: same result, but repeat preserves the atom ORDER of
        // the original cell, which is what makes an index list written against
        // the input still address the same atoms in a downstream Defect
        // Generator.
        if (spec.isDiagonal())
            return pybridge::AseBridge::makeSupercell(structure, spec.na(),
                                                      spec.nb(), spec.nc());
        return pybridge::AseBridge::makeSupercellMatrix(structure, spec.p);
    } catch (const std::exception& e) {
        fail(QObject::tr("Supercell %1 failed: %2")
                 .arg(spec.describe(), QString::fromUtf8(e.what())));
        return structure;
    }
}

core::Structure applyDefects(const core::Structure& structure,
                             const DefectSpec& spec, QString* error)
{
    const auto fail = [error](const QString& message) {
        if (error)
            *error = message;
    };
    if (spec.isEmpty()) {
        fail(QObject::tr("No defect operations are configured."));
        return structure;
    }

    const int count = static_cast<int>(structure.size());
    // Both index-based kinds are resolved against the ORIGINAL numbering and
    // collected here, so a recipe reads the way the user wrote it rather than
    // the way the indices happen to shift as it is applied.
    std::set<int> removals;
    QList<QPair<int, int>> substitutions; // index -> atomic number
    QList<core::Atom> additions;

    for (const DefectOperation& operation : spec.operations) {
        if (operation.kind == DefectOperation::Kind::Add) {
            const int z = core::Elements::atomicNumber(operation.element.toStdString());
            if (z <= 0) {
                fail(QObject::tr("\"%1\" is not a chemical element.")
                         .arg(operation.element));
                return structure;
            }
            core::Atom atom;
            atom.atomicNumber = z;
            const core::Vec3 raw{operation.x, operation.y, operation.z};
            if (operation.fractional) {
                if (!structure.cell().isDefined()) {
                    fail(QObject::tr(
                        "\"%1\" uses cell coordinates, but the incoming "
                        "structure has no cell.")
                             .arg(operation.describe()));
                    return structure;
                }
                atom.position = structure.cell().fractionalToCartesian(raw);
            } else {
                atom.position = raw;
            }
            additions.append(atom);
            continue;
        }

        const std::vector<int> targets =
            parseAtomIndexList(operation.indices, count);
        if (targets.empty()) {
            // Deliberately an error, not a no-op. See the header: a recipe
            // that addresses nothing is a recipe written for a different
            // structure, and the pristine cell is exactly the wrong answer to
            // hand a defect study.
            fail(QObject::tr(
                     "\"%1\" matches no atom in a structure of %2 — check the "
                     "index list against the geometry that reaches this node.")
                     .arg(operation.describe())
                     .arg(count));
            return structure;
        }
        if (operation.kind == DefectOperation::Kind::Remove) {
            removals.insert(targets.begin(), targets.end());
        } else {
            const int z = core::Elements::atomicNumber(operation.element.toStdString());
            if (z <= 0) {
                fail(QObject::tr("\"%1\" is not a chemical element.")
                         .arg(operation.element));
                return structure;
            }
            for (int index : targets)
                substitutions.append({index, z});
        }
    }

    core::Structure result;
    result.setCell(structure.cell());
    std::vector<core::Atom> kept;
    kept.reserve(structure.atoms().size());
    for (int index = 0; index < count; ++index) {
        if (removals.count(index) != 0)
            continue;
        core::Atom atom = structure.atoms()[static_cast<std::size_t>(index)];
        // Last substitution for an index wins, matching how the list reads
        // top to bottom.
        for (const auto& [target, z] : substitutions)
            if (target == index)
                atom.atomicNumber = z;
        kept.push_back(atom);
    }
    for (const core::Atom& atom : kept)
        result.addAtom(atom);
    for (const core::Atom& atom : additions)
        result.addAtom(atom);

    if (result.empty()) {
        fail(QObject::tr(
            "The defect recipe removed every atom — nothing would be left to "
            "compute."));
        return structure;
    }
    return result;
}

// ---------------------------------------------------------------------------
// TdbGeneratorSpec
// ---------------------------------------------------------------------------

QString TdbGeneratorSpec::describe() const
{
    QString systemName;
    if (!elementA.isEmpty() && !elementB.isEmpty())
        systemName = QStringLiteral("%1-%2 ").arg(elementA, elementB);
    return QObject::tr("%1%2, Redlich-Kister order %3")
        .arg(systemName, phaseName)
        .arg(order);
}

QJsonObject TdbGeneratorSpec::toJson() const
{
    QJsonObject object;
    object.insert(QStringLiteral("element_a"), elementA);
    object.insert(QStringLiteral("element_b"), elementB);
    object.insert(QStringLiteral("phase"), phaseName);
    object.insert(QStringLiteral("order"), order);
    object.insert(QStringLiteral("t_low"), lowTemperatureK);
    object.insert(QStringLiteral("t_high"), highTemperatureK);
    return object;
}

TdbGeneratorSpec TdbGeneratorSpec::fromJson(const QJsonObject& object)
{
    TdbGeneratorSpec spec;
    spec.elementA = object.value(QStringLiteral("element_a")).toString();
    spec.elementB = object.value(QStringLiteral("element_b")).toString();
    spec.phaseName = object.value(QStringLiteral("phase"))
                         .toString(spec.phaseName);
    spec.order = object.value(QStringLiteral("order")).toInt(spec.order);
    spec.lowTemperatureK =
        object.value(QStringLiteral("t_low")).toDouble(spec.lowTemperatureK);
    spec.highTemperatureK =
        object.value(QStringLiteral("t_high")).toDouble(spec.highTemperatureK);
    return spec;
}

namespace {

/// The leading element symbol of a formula ("Ag3Au1" -> "AG").
QString leadingElementUpper(const QString& formula)
{
    QString out;
    for (const QChar c : formula) {
        if (c.isDigit())
            break;
        if (c.isUpper() && !out.isEmpty())
            break;
        out.append(c);
    }
    return out.toUpper();
}

} // namespace

bool runTdbAssessment(const QString& ensembleJson, const TdbGeneratorSpec& spec,
                      TdbGeneratorOutput* output, QString* error)
{
    const auto fail = [error](const QString& message) {
        if (error)
            *error = message;
        return false;
    };
    if (!output)
        return fail(QObject::tr("no output was requested"));
    if (!spec.isValid())
        return fail(QObject::tr("its settings are out of range (%1)")
                        .arg(spec.describe()));

    const QJsonObject root =
        QJsonDocument::fromJson(ensembleJson.toUtf8()).object();
    const QJsonArray configurations =
        root.value(QStringLiteral("configurations")).toArray();
    if (configurations.isEmpty())
        return fail(QObject::tr(
            "its input carries no configurations — the upstream run is not a "
            "cluster-expansion ensemble"));

    core::CalphadAssessmentInput input;
    QString formulaAtZero;
    QString formulaAtOne;
    bool haveZero = false;
    bool haveOne = false;
    for (const QJsonValue& value : configurations) {
        const QJsonObject entry = value.toObject();
        // A failed relaxation carries a null formation energy. Dropped rather
        // than read as zero, which would enter the fit as a perfectly ideal
        // alloy at that composition and pull every coefficient towards it.
        const QJsonValue formation =
            entry.value(QStringLiteral("formation_energy"));
        if (formation.isNull() || formation.isUndefined())
            continue;
        const double x = entry.value(QStringLiteral("concentration")).toDouble();
        const double energy =
            entry.value(QStringLiteral("energy_per_atom")).toDouble();
        const QString formula =
            entry.value(QStringLiteral("formula")).toString();
        if (x <= 1e-9) {
            if (!haveZero || energy < input.referenceEnergyAEvPerAtom) {
                input.referenceEnergyAEvPerAtom = energy;
                formulaAtZero = formula;
                haveZero = true;
            }
            continue;
        }
        if (x >= 1.0 - 1e-9) {
            if (!haveOne || energy < input.referenceEnergyBEvPerAtom) {
                input.referenceEnergyBEvPerAtom = energy;
                formulaAtOne = formula;
                haveOne = true;
            }
            continue;
        }
        core::CalphadConfiguration config;
        config.label = formula.isEmpty()
            ? QStringLiteral("x = %1").arg(x, 0, 'f', 3).toStdString()
            : formula.toStdString();
        config.moleFractionB = x;
        config.energyEvPerAtom = energy;
        input.configurations.push_back(config);
    }
    if (!haveZero || !haveOne)
        return fail(QObject::tr(
            "its ensemble is missing a pure endpoint. Formation energies are "
            "referenced to x = 0 and x = 1, and without one of them every "
            "interaction parameter absorbs the missing endpoint's energy"));
    if (input.configurations.empty())
        return fail(QObject::tr(
            "its ensemble has no alloy between the endpoints — two pure "
            "elements determine the reference and nothing else"));

    input.elementA = spec.elementA.isEmpty()
        ? leadingElementUpper(formulaAtZero).toStdString()
        : spec.elementA.toUpper().toStdString();
    const QString axis =
        root.value(QStringLiteral("concentration_element")).toString();
    input.elementB = !spec.elementB.isEmpty()
        ? spec.elementB.toUpper().toStdString()
        : (axis.isEmpty() ? leadingElementUpper(formulaAtOne).toStdString()
                          : axis.toUpper().toStdString());
    if (input.elementA.empty())
        input.elementA = "A";
    if (input.elementB.empty() || input.elementB == input.elementA)
        input.elementB = "B";
    input.phaseName = spec.phaseName.toUpper().toStdString();
    input.order = spec.order;
    // A cluster-expansion ensemble carries no vibrational free energy, so the
    // temperature dependence is not merely unfitted — it is undetermined, and
    // asking for it would be the constant-temperature rank deficiency in
    // disguise. The assessment refuses it on its own; saying so here keeps the
    // two ends from disagreeing about why.
    input.temperatureDependent = false;
    input.temperaturesK = {spec.lowTemperatureK};

    const core::CalphadAssessment assessment =
        core::assessBinaryFromFirstPrinciples(input);
    if (!assessment.ok)
        return fail(QString::fromStdString(assessment.note));

    core::TdbWriteOptions options = core::tdbOptionsForAssessment(input, assessment);
    options.lowTemperatureK = spec.lowTemperatureK;
    options.highTemperatureK = spec.highTemperatureK;
    output->databaseText = QString::fromStdString(core::writeTdb(options));

    // The summary carries what the .tdb cannot: the residual of the fit, the
    // samples it was made from and which configurations are on the static
    // hull. A database that fits badly is still a valid database, so the only
    // place the quality of the fit can be recorded is beside it.
    QJsonObject summary;
    summary.insert(QStringLiteral("schema"),
                   QStringLiteral("calango.calphad.assessment/1"));
    summary.insert(QStringLiteral("element_a"),
                   QString::fromStdString(input.elementA));
    summary.insert(QStringLiteral("element_b"),
                   QString::fromStdString(input.elementB));
    summary.insert(QStringLiteral("phase"),
                   QString::fromStdString(input.phaseName));
    summary.insert(QStringLiteral("order"), input.order);
    summary.insert(QStringLiteral("vibrational"), assessment.vibrational);
    summary.insert(QStringLiteral("note"),
                   QString::fromStdString(assessment.note));
    summary.insert(QStringLiteral("rms_residual_J_per_mol"),
                   assessment.fit.rmsResidualJPerMol);
    summary.insert(QStringLiteral("max_residual_J_per_mol"),
                   assessment.fit.maxResidualJPerMol);
    summary.insert(QStringLiteral("samples"), assessment.fit.usedSamples);
    QJsonArray terms;
    for (const core::RedlichKisterTerm& term : assessment.fit.terms) {
        QJsonObject entry;
        entry.insert(QStringLiteral("a_J_per_mol"), term.a);
        entry.insert(QStringLiteral("b_J_per_mol_K"), term.b);
        terms.append(entry);
    }
    summary.insert(QStringLiteral("redlich_kister"), terms);
    QJsonArray points;
    for (const core::HullPoint& point : assessment.staticHull.points) {
        QJsonObject entry;
        entry.insert(QStringLiteral("label"),
                     QString::fromStdString(point.label));
        entry.insert(QStringLiteral("x"), point.concentration);
        entry.insert(QStringLiteral("formation_energy_eV_per_atom"),
                     point.formationEnergy);
        entry.insert(QStringLiteral("energy_above_hull_eV_per_atom"),
                     point.energyAboveHull);
        entry.insert(QStringLiteral("on_hull"), point.onHull);
        points.append(entry);
    }
    summary.insert(QStringLiteral("hull"), points);
    output->summaryJson =
        QString::fromUtf8(QJsonDocument(summary).toJson(QJsonDocument::Indented));

    output->headline =
        QObject::tr("%1-%2 %3, order %4, RMS %5 J/mol over %6 sample(s)")
            .arg(QString::fromStdString(input.elementA),
                 QString::fromStdString(input.elementB),
                 QString::fromStdString(input.phaseName))
            .arg(input.order)
            .arg(assessment.fit.rmsResidualJPerMol, 0, 'f', 1)
            .arg(assessment.fit.usedSamples);
    return true;
}

// ---------------------------------------------------------------------------
// AlloyComposition / SqsGeneratorSpec
// ---------------------------------------------------------------------------

QString AlloyComposition::describe() const
{
    QStringList parts;
    for (const auto& [symbol, fraction] : species)
        parts << QStringLiteral("%1 %2").arg(symbol).arg(fraction, 0, 'g', 3);
    return parts.join(QStringLiteral(" / "));
}

QString AlloyComposition::name() const
{
    if (!label.isEmpty())
        return label;
    // "Cu75Au25": percentages of the NORMALIZED fractions, so a list typed as
    // 3:1 and one typed as 0.75:0.25 name the same material. Two compositions
    // that round to the same percentages would collide, which is exactly when
    // they are the same alloy to three digits and want telling apart by hand.
    double total = 0.0;
    for (const auto& [symbol, fraction] : species)
        total += fraction;
    if (total <= 0.0)
        return QObject::tr("alloy");
    QString out;
    for (const auto& [symbol, fraction] : species)
        out += QStringLiteral("%1%2").arg(symbol).arg(
            qRound(100.0 * fraction / total));
    return out;
}

QJsonObject AlloyComposition::toJson() const
{
    QJsonArray entries;
    for (const auto& [symbol, fraction] : species) {
        QJsonObject entry;
        entry.insert(QStringLiteral("element"), symbol);
        entry.insert(QStringLiteral("fraction"), fraction);
        entries.append(entry);
    }
    QJsonObject object;
    object.insert(QStringLiteral("label"), label);
    object.insert(QStringLiteral("species"), entries);
    return object;
}

AlloyComposition AlloyComposition::fromJson(const QJsonObject& object)
{
    AlloyComposition composition;
    composition.label = object.value(QStringLiteral("label")).toString();
    for (const QJsonValue& value :
         object.value(QStringLiteral("species")).toArray()) {
        const QJsonObject entry = value.toObject();
        composition.species.append(
            {entry.value(QStringLiteral("element")).toString(),
             entry.value(QStringLiteral("fraction")).toDouble()});
    }
    return composition;
}

bool SqsGeneratorSpec::isValid() const
{
    if (compositions.isEmpty() || na < 1 || nb < 1 || nc < 1 || steps < 1)
        return false;
    if (shell1 <= 0.0)
        return false;
    for (const AlloyComposition& composition : compositions) {
        if (!composition.isValid())
            return false;
        for (const auto& [symbol, fraction] : composition.species)
            if (fraction <= 0.0
                || core::Elements::atomicNumber(symbol.toStdString()) == 0)
                return false;
    }
    return true;
}

QString SqsGeneratorSpec::describe() const
{
    if (compositions.isEmpty())
        return QObject::tr("no compositions");
    QStringList names;
    for (const AlloyComposition& composition : compositions)
        names << composition.name();
    QString clusters = QObject::tr("pairs to %1 Å")
                           .arg(std::max(shell1, shell2), 0, 'g', 3);
    if (tripletCutoff > 0.0)
        clusters += QObject::tr(" + triplets to %1 Å").arg(tripletCutoff, 0, 'g', 3);
    if (quadrupletCutoff > 0.0)
        clusters += QObject::tr(" + quadruplets to %1 Å")
                        .arg(quadrupletCutoff, 0, 'g', 3);
    return QObject::tr("%1 · %2x%3x%4 · %5")
        .arg(names.join(QStringLiteral(", ")))
        .arg(na)
        .arg(nb)
        .arg(nc)
        .arg(clusters);
}

QJsonObject SqsGeneratorSpec::toJson() const
{
    QJsonArray entries;
    for (const AlloyComposition& composition : compositions)
        entries.append(composition.toJson());
    QJsonObject object;
    object.insert(QStringLiteral("supercell"),
                  QJsonArray{na, nb, nc});
    object.insert(QStringLiteral("replace_element"), replaceElement);
    object.insert(QStringLiteral("compositions"), entries);
    object.insert(QStringLiteral("shell1"), shell1);
    object.insert(QStringLiteral("shell2"), shell2);
    object.insert(QStringLiteral("triplet_cutoff"), tripletCutoff);
    object.insert(QStringLiteral("quadruplet_cutoff"), quadrupletCutoff);
    object.insert(QStringLiteral("triplet_weight"), tripletWeight);
    object.insert(QStringLiteral("quadruplet_weight"), quadrupletWeight);
    object.insert(QStringLiteral("steps"), steps);
    object.insert(QStringLiteral("seed"), seed);
    return object;
}

SqsGeneratorSpec SqsGeneratorSpec::fromJson(const QJsonObject& object)
{
    SqsGeneratorSpec spec;
    const QJsonArray cell = object.value(QStringLiteral("supercell")).toArray();
    if (cell.size() == 3) {
        spec.na = cell[0].toInt(spec.na);
        spec.nb = cell[1].toInt(spec.nb);
        spec.nc = cell[2].toInt(spec.nc);
    }
    spec.replaceElement =
        object.value(QStringLiteral("replace_element")).toString();
    for (const QJsonValue& value :
         object.value(QStringLiteral("compositions")).toArray())
        spec.compositions.append(AlloyComposition::fromJson(value.toObject()));
    spec.shell1 = object.value(QStringLiteral("shell1")).toDouble(spec.shell1);
    spec.shell2 = object.value(QStringLiteral("shell2")).toDouble(spec.shell2);
    spec.tripletCutoff = object.value(QStringLiteral("triplet_cutoff"))
                             .toDouble(spec.tripletCutoff);
    spec.quadrupletCutoff = object.value(QStringLiteral("quadruplet_cutoff"))
                                .toDouble(spec.quadrupletCutoff);
    spec.tripletWeight = object.value(QStringLiteral("triplet_weight"))
                             .toDouble(spec.tripletWeight);
    spec.quadrupletWeight = object.value(QStringLiteral("quadruplet_weight"))
                                .toDouble(spec.quadrupletWeight);
    spec.steps = object.value(QStringLiteral("steps")).toInt(spec.steps);
    spec.seed = object.value(QStringLiteral("seed")).toInt(spec.seed);
    return spec;
}

bool runSqsGeneration(const core::Structure& parent,
                      const SqsGeneratorSpec& spec, int index,
                      SqsGeneratorOutput* output, QString* error)
{
    const auto fail = [error](const QString& message) {
        if (error)
            *error = message;
        return false;
    };
    if (!output)
        return fail(QObject::tr("no output was requested"));
    if (!spec.isValid())
        return fail(QObject::tr("its settings cannot be used (%1)")
                        .arg(spec.describe()));

    const int clamped =
        std::clamp(index, 0, static_cast<int>(spec.compositions.size()) - 1);
    const AlloyComposition& composition =
        spec.compositions[static_cast<qsizetype>(clamped)];

    core::SqsGenerator::Params params;
    params.nx = spec.na;
    params.ny = spec.nb;
    params.nz = spec.nc;
    params.shell1 = spec.shell1;
    params.shell2 = spec.shell2;
    params.tripletCutoff = spec.tripletCutoff;
    params.quadrupletCutoff = spec.quadrupletCutoff;
    params.tripletWeight = spec.tripletWeight;
    params.quadrupletWeight = spec.quadrupletWeight;
    params.steps = spec.steps;
    params.seed = static_cast<unsigned>(spec.seed);
    for (const auto& [symbol, fraction] : composition.species)
        params.composition.emplace_back(symbol.toStdString(), fraction);

    // An unset sublattice element means "whatever this structure is mostly
    // made of". Resolved HERE rather than in the editor because the node is
    // normally configured before anything has run, when there is no structure
    // to resolve it against.
    QString replace = spec.replaceElement.trimmed();
    if (replace.isEmpty()) {
        std::map<int, int> population;
        for (const core::Atom& atom : parent.atoms())
            ++population[atom.atomicNumber];
        int best = 0;
        int bestCount = 0;
        for (const auto& [z, count] : population)
            if (count > bestCount) {
                best = z;
                bestCount = count;
            }
        if (best == 0)
            return fail(QObject::tr("its input structure has no atoms"));
        replace = QString::fromUtf8(core::Elements::data(best).symbol);
    }
    params.replaceElement = replace.toStdString();

    core::SqsGenerator::Result result;
    try {
        result = core::SqsGenerator::generate(parent, params);
    } catch (const std::exception& e) {
        return fail(QObject::tr("the SQS could not be generated (%1)")
                        .arg(QString::fromUtf8(e.what())));
    }

    output->structure = std::move(result.structure);
    output->label = composition.name();

    QJsonObject summary;
    summary.insert(QStringLiteral("schema"), QStringLiteral("calango.sqs/1"));
    summary.insert(QStringLiteral("composition"), composition.toJson());
    summary.insert(QStringLiteral("replace_element"), replace);
    summary.insert(QStringLiteral("supercell"),
                   QJsonArray{spec.na, spec.nb, spec.nc});
    summary.insert(QStringLiteral("sublattice_sites"), result.sublatticeSites);
    summary.insert(QStringLiteral("objective"), result.objective);
    summary.insert(QStringLiteral("initial_objective"), result.initialObjective);
    summary.insert(
        QStringLiteral("deviation"),
        QJsonObject{{QStringLiteral("pair"), result.deviation.pair},
                    {QStringLiteral("triplet"), result.deviation.triplet},
                    {QStringLiteral("quadruplet"), result.deviation.quadruplet}});
    summary.insert(QStringLiteral("clusters"),
                   QJsonObject{{QStringLiteral("pairs"), result.pairs},
                               {QStringLiteral("triplets"), result.triplets},
                               {QStringLiteral("quadruplets"),
                                result.quadruplets}});
    summary.insert(QStringLiteral("steps"), result.steps);
    summary.insert(QStringLiteral("accepted"), result.accepted);
    // The short-range order of what was produced, in the units alloy people
    // read. ΔΠ is internal to the optimizer; α = 0 is the ideal random alloy
    // to anyone, and it is what the downstream CVM curve is compared against.
    QJsonArray shells;
    double worstAlpha = 0.0;
    for (const core::WarrenCowleyShell& shell : result.shortRangeOrder.shells) {
        QJsonArray rows;
        for (const std::vector<double>& row : shell.alpha) {
            QJsonArray columns;
            for (const double alpha : row) {
                columns.append(std::isnan(alpha) ? QJsonValue()
                                                 : QJsonValue(alpha));
                if (!std::isnan(alpha))
                    worstAlpha = std::max(worstAlpha, std::abs(alpha));
            }
            rows.append(columns);
        }
        QJsonObject entry;
        entry.insert(QStringLiteral("r_min"), shell.rMin);
        entry.insert(QStringLiteral("r_max"), shell.rMax);
        entry.insert(QStringLiteral("mean_neighbors"), shell.meanNeighbors);
        entry.insert(QStringLiteral("alpha"), rows);
        shells.append(entry);
    }
    QJsonArray speciesArray;
    for (const int z : result.shortRangeOrder.species)
        speciesArray.append(QString::fromUtf8(core::Elements::data(z).symbol));
    summary.insert(QStringLiteral("warren_cowley"),
                   QJsonObject{{QStringLiteral("species"), speciesArray},
                               {QStringLiteral("shells"), shells}});
    output->summaryJson =
        QString::fromUtf8(QJsonDocument(summary).toJson(QJsonDocument::Indented));

    output->headline =
        QObject::tr("%1 on %2 sites: ΔΠ %3 (from %4), max |α| %5")
            .arg(output->label)
            .arg(result.sublatticeSites)
            .arg(result.objective, 0, 'g', 3)
            .arg(result.initialObjective, 0, 'g', 3)
            .arg(worstAlpha, 0, 'f', 4);
    return true;
}

// ---------------------------------------------------------------------------
// ClusterExpansionFitSpec
// ---------------------------------------------------------------------------

QString ClusterExpansionFitSpec::methodName(Method method)
{
    switch (method) {
    case Method::Ridge:
        return QObject::tr("ridge");
    case Method::Ard:
        return QObject::tr("ARD");
    case Method::Lasso:
        break;
    }
    return QObject::tr("lasso");
}

QString ClusterExpansionFitSpec::describe() const
{
    QStringList parts;
    parts << methodName(method);
    parts << QObject::tr("pairs %1 Å").arg(pairCutoff, 0, 'g', 3);
    if (tripletCutoff > 0.0)
        parts << QObject::tr("triplets %1 Å").arg(tripletCutoff, 0, 'g', 3);
    if (quadrupletCutoff > 0.0)
        parts << QObject::tr("quadruplets %1 Å").arg(quadrupletCutoff, 0, 'g', 3);
    parts << (crossValidationFolds > 0
                  ? QObject::tr("%1-fold CV").arg(crossValidationFolds)
                  : QObject::tr("leave-one-out"));
    if (oneStandardError)
        parts << QObject::tr("1-SE rule");
    return parts.join(QStringLiteral(", "));
}

QJsonObject ClusterExpansionFitSpec::toJson() const
{
    QJsonObject object;
    object.insert(QStringLiteral("method"), static_cast<int>(method));
    object.insert(QStringLiteral("pair_cutoff"), pairCutoff);
    object.insert(QStringLiteral("triplet_cutoff"), tripletCutoff);
    object.insert(QStringLiteral("quadruplet_cutoff"), quadrupletCutoff);
    object.insert(QStringLiteral("lambda_count"), lambdaCount);
    object.insert(QStringLiteral("cv_folds"), crossValidationFolds);
    object.insert(QStringLiteral("one_standard_error"), oneStandardError);
    object.insert(QStringLiteral("standardize"), standardize);
    return object;
}

ClusterExpansionFitSpec
ClusterExpansionFitSpec::fromJson(const QJsonObject& object)
{
    ClusterExpansionFitSpec spec;
    // Clamped, not cast blindly: a document from a newer Calango could name a
    // method this build has never heard of, and an out-of-range enum is
    // undefined behaviour the first time it reaches a switch.
    spec.method = static_cast<Method>(
        std::clamp(object.value(QStringLiteral("method")).toInt(1), 0, 2));
    spec.pairCutoff =
        object.value(QStringLiteral("pair_cutoff")).toDouble(spec.pairCutoff);
    spec.tripletCutoff = object.value(QStringLiteral("triplet_cutoff"))
                             .toDouble(spec.tripletCutoff);
    spec.quadrupletCutoff = object.value(QStringLiteral("quadruplet_cutoff"))
                                .toDouble(spec.quadrupletCutoff);
    spec.lambdaCount =
        object.value(QStringLiteral("lambda_count")).toInt(spec.lambdaCount);
    spec.crossValidationFolds = object.value(QStringLiteral("cv_folds"))
                                    .toInt(spec.crossValidationFolds);
    spec.oneStandardError = object.value(QStringLiteral("one_standard_error"))
                                .toBool(spec.oneStandardError);
    spec.standardize =
        object.value(QStringLiteral("standardize")).toBool(spec.standardize);
    return spec;
}

bool runClusterExpansionFit(const QString& ensembleJson,
                            const ClusterExpansionFitSpec& spec,
                            ClusterExpansionFitOutput* output, QString* error)
{
    const auto fail = [error](const QString& message) {
        if (error)
            *error = message;
        return false;
    };
    if (!output)
        return fail(QObject::tr("no output was requested"));
    if (!spec.isValid())
        return fail(QObject::tr("its settings are out of range (%1)")
                        .arg(spec.describe()));

    // The input is validated BEFORE the refusal below, so that a mis-wired
    // graph is reported as a mis-wired graph rather than as the missing
    // solver. These two failures look identical from the canvas and have
    // nothing to do with each other.
    const QJsonObject root =
        QJsonDocument::fromJson(ensembleJson.toUtf8()).object();
    const QJsonArray configurations =
        root.value(QStringLiteral("configurations")).toArray();
    if (configurations.isEmpty())
        return fail(QObject::tr(
            "its input carries no configurations — the upstream run is not a "
            "cluster-expansion ensemble"));
    int usable = 0;
    for (const QJsonValue& value : configurations) {
        const QJsonValue energy =
            value.toObject().value(QStringLiteral("energy_per_atom"));
        if (!energy.isNull() && !energy.isUndefined())
            ++usable;
    }
    if (usable < 2)
        return fail(QObject::tr(
            "its ensemble holds %1 configuration(s) with an energy. A cluster "
            "expansion is a regression: it needs more samples than it has "
            "clusters, and it has at least two.")
                        .arg(usable));

    // The design matrix. cluster_expansion.json now carries one correlation
    // row per configuration (ClusterExpansionScriptGenerator emits it, and the
    // builder hands it to the run), so the fit has both halves at last. A file
    // written before that carries energies and no matrix, and is refused
    // below rather than fitted against whatever else is present — a cluster
    // expansion regressed on the wrong columns still reproduces its training
    // energies.
    std::vector<std::vector<double>> correlations;
    std::vector<double> energies;
    for (const QJsonValue& value : configurations) {
        const QJsonObject record = value.toObject();
        const QJsonArray row =
            record.value(QStringLiteral("correlation")).toArray();
        const QJsonValue energy =
            record.value(QStringLiteral("energy_per_atom"));
        if (row.isEmpty() || !energy.isDouble())
            continue;
        const double e = energy.toDouble();
        if (!std::isfinite(e))
            continue;
        std::vector<double> correlation;
        correlation.reserve(row.size());
        for (const QJsonValue& entry : row)
            correlation.push_back(entry.toDouble());
        correlations.push_back(std::move(correlation));
        energies.push_back(e);
    }
    if (correlations.size() < 2)
        return fail(QObject::tr(
            "its ensemble carries %1 configuration(s) with energies but no "
            "cluster correlations, so there is no design matrix to regress "
            "against. The run predates correlation output: rebuild the "
            "ensemble with the Cluster Expansion builder and run it again. "
            "This node refuses rather than emitting an ECI file of zeros that "
            "the CVM node downstream would turn into a plausible and entirely "
            "wrong entropy curve.")
                        .arg(usable));
    const std::size_t columns = correlations.front().size();
    for (const auto& row : correlations)
        if (row.size() != columns)
            return fail(QObject::tr(
                "its design matrix is ragged — the configurations do not "
                "share one cluster basis"));

    core::EciFitOptions options;
    options.method = spec.method == ClusterExpansionFitSpec::Method::Ridge
        ? core::EciMethod::Ridge
        : spec.method == ClusterExpansionFitSpec::Method::Ard
            ? core::EciMethod::Ard
            : core::EciMethod::Lasso;
    options.lambdaCount = spec.lambdaCount;
    options.cvFolds = spec.crossValidationFolds;
    options.oneStandardError = spec.oneStandardError;
    options.standardize = spec.standardize;

    // Column labels, when the run recorded them. Presentation only — a label
    // this does not recognise costs a blank name, never a wrong fit.
    std::vector<core::EciColumn> columnsInfo;
    const QJsonArray labels = root.value(QStringLiteral("orbit_labels")).toArray();
    for (int i = 0; i < labels.size(); ++i) {
        core::EciColumn column;
        const QString text = labels.at(i).toString();
        column.label = text.toStdString();
        column.order = text.startsWith(QStringLiteral("pair"))      ? 2
            : text.startsWith(QStringLiteral("triplet"))            ? 3
            : text.startsWith(QStringLiteral("quad"))               ? 4
                                                                    : 0;
        columnsInfo.push_back(column);
    }

    const core::EciFitResult fit = core::fitEffectiveClusterInteractions(
        correlations, energies, options, columnsInfo);
    if (!fit.ok)
        return fail(QObject::tr("its ECI fit failed: %1")
                        .arg(QString::fromStdString(fit.note)));

    // The nearest-neighbour pair ECI is the one number the CVM node can use;
    // it is written at the top level so the downstream node does not have to
    // rediscover which term that is.
    double pairEci = 0.0;
    double pairRadius = 0.0;
    double tripletEci = 0.0;
    double tripletRadius = 0.0;
    QJsonArray eciArray;
    for (const core::EciTerm& term : fit.terms) {
        QJsonObject entry;
        entry[QStringLiteral("label")] = QString::fromStdString(term.label);
        entry[QStringLiteral("order")] = term.order;
        entry[QStringLiteral("radius")] = term.radius;
        entry[QStringLiteral("multiplicity")] = term.multiplicity;
        entry[QStringLiteral("value_eV")] = term.eci;
        entry[QStringLiteral("weighted_eV")] = term.weightedEci;
        eciArray.append(entry);
        if (term.order == 2 && term.eci != 0.0
            && (pairEci == 0.0 || term.radius < pairRadius)) {
            pairEci = term.eci;
            pairRadius = term.radius;
        }
        // The nearest-neighbour TRIPLET too. It is what breaks the A <-> B
        // symmetry a pair-only model has, so a downstream CVM given only the
        // pair term cannot tell A3B from AB3.
        if (term.order == 3 && term.eci != 0.0
            && (tripletEci == 0.0 || term.radius < tripletRadius)) {
            tripletEci = term.eci;
            tripletRadius = term.radius;
        }
    }

    QJsonObject out;
    out[QStringLiteral("schema")] =
        QStringLiteral("calango.cluster_expansion.eci/1");
    out[QStringLiteral("method")] = ClusterExpansionFitSpec::methodName(spec.method);
    out[QStringLiteral("configurations")] = static_cast<int>(correlations.size());
    out[QStringLiteral("columns")] = static_cast<int>(columns);
    out[QStringLiteral("intercept_eV")] = fit.intercept;
    out[QStringLiteral("lambda")] = fit.lambda;
    out[QStringLiteral("active_terms")] = fit.activeTerms;
    // BOTH errors, always. A cluster expansion that reproduces its training
    // set and predicts nothing is the classic failure, and reporting one
    // number hides it.
    out[QStringLiteral("cv_score")] = fit.cvScore;
    out[QStringLiteral("rmse")] = fit.rmse;
    out[QStringLiteral("nearest_neighbour_pair_eci_eV")] = pairEci;
    out[QStringLiteral("nearest_neighbour_triplet_eci_eV")] = tripletEci;
    out[QStringLiteral("eci")] = eciArray;
    if (const QJsonValue species = root.value(QStringLiteral("species"));
        !species.isUndefined())
        out[QStringLiteral("species")] = species;

    output->eciJson =
        QString::fromUtf8(QJsonDocument(out).toJson(QJsonDocument::Indented));
    output->headline =
        QObject::tr("%1 ECIs from %2 configurations; CV %3, RMSE %4 eV/atom")
            .arg(fit.activeTerms)
            .arg(correlations.size())
            .arg(fit.cvScore, 0, 'g', 3)
            .arg(fit.rmse, 0, 'g', 3);
    return true;
}

// ---------------------------------------------------------------------------
// CvmEntropySpec
// ---------------------------------------------------------------------------

QString CvmEntropySpec::latticeName(Lattice lattice)
{
    switch (lattice) {
    case Lattice::Bcc:
        return QObject::tr("bcc");
    case Lattice::Chain:
        return QObject::tr("chain");
    case Lattice::Fcc:
        break;
    }
    return QObject::tr("fcc");
}

QString CvmEntropySpec::approximationName(Approximation approximation)
{
    switch (approximation) {
    case Approximation::Point:
        return QObject::tr("point (ideal)");
    case Approximation::Pair:
        return QObject::tr("pair");
    case Approximation::Tetrahedron:
        break;
    }
    return QObject::tr("tetrahedron");
}

QString CvmEntropySpec::describe() const
{
    return QObject::tr("%1, %2, %3–%4 K")
        .arg(latticeName(lattice), approximationName(approximation))
        .arg(minTemperatureK, 0, 'g', 4)
        .arg(maxTemperatureK, 0, 'g', 4);
}

QJsonObject CvmEntropySpec::toJson() const
{
    QJsonObject object;
    object.insert(QStringLiteral("lattice"), static_cast<int>(lattice));
    object.insert(QStringLiteral("approximation"),
                  static_cast<int>(approximation));
    object.insert(QStringLiteral("t_min"), minTemperatureK);
    object.insert(QStringLiteral("t_max"), maxTemperatureK);
    object.insert(QStringLiteral("t_steps"), temperatureSteps);
    return object;
}

CvmEntropySpec CvmEntropySpec::fromJson(const QJsonObject& object)
{
    CvmEntropySpec spec;
    // Clamped rather than cast blindly: a document written by a newer Calango
    // could name a lattice this build has never heard of, and an out-of-range
    // enum would be undefined behaviour the first time it reached a switch.
    const int lattice = std::clamp(
        object.value(QStringLiteral("lattice")).toInt(0), 0, 2);
    const int approximation = std::clamp(
        object.value(QStringLiteral("approximation")).toInt(2), 0, 2);
    spec.lattice = static_cast<Lattice>(lattice);
    spec.approximation = static_cast<Approximation>(approximation);
    spec.minTemperatureK =
        object.value(QStringLiteral("t_min")).toDouble(spec.minTemperatureK);
    spec.maxTemperatureK =
        object.value(QStringLiteral("t_max")).toDouble(spec.maxTemperatureK);
    spec.temperatureSteps =
        object.value(QStringLiteral("t_steps")).toInt(spec.temperatureSteps);
    return spec;
}

bool runCvmEntropy(const QString& eciJson, const CvmEntropySpec& spec,
                   CvmEntropyOutput* output, QString* error)
{
    const auto fail = [error](const QString& message) {
        if (error)
            *error = message;
        return false;
    };
    if (!output)
        return fail(QObject::tr("no output was requested"));
    if (!spec.isValid())
        return fail(QObject::tr("its temperature range is not usable (%1)")
                        .arg(spec.describe()));

    const QJsonObject root = QJsonDocument::fromJson(eciJson.toUtf8()).object();
    if (root.value(QStringLiteral("eci")).toArray().isEmpty())
        return fail(QObject::tr(
            "its input carries no ECIs — the node feeding it is not a "
            "Cluster Expansion (ECI Fitter)"));

    // The nearest-neighbour pair ECI is the one term this CVM can consume:
    // the solver is a nearest-neighbour model. Longer-range pairs and
    // multi-body terms are in the file and are deliberately NOT folded in —
    // silently absorbing them would report a coupling the model never used.
    const double pairEci =
        root.value(QStringLiteral("nearest_neighbour_pair_eci_eV")).toDouble(0.0);
    if (pairEci == 0.0)
        return fail(QObject::tr(
            "the fit upstream kept no nearest-neighbour pair interaction, so "
            "there is nothing for a nearest-neighbour CVM to use. That is a "
            "result about the alloy rather than an error: its ordering is "
            "carried by longer-range or multi-body clusters, which this "
            "solver does not model."));

    core::CvmInput input;
    input.lattice = spec.lattice == CvmEntropySpec::Lattice::Bcc
        ? core::CvmLattice::Bcc
        : spec.lattice == CvmEntropySpec::Lattice::Chain
            ? core::CvmLattice::Chain
            : core::CvmLattice::Fcc;
    input.approximation =
        spec.approximation == CvmEntropySpec::Approximation::Point
        ? core::CvmApproximation::Point
        : spec.approximation == CvmEntropySpec::Approximation::Pair
            ? core::CvmApproximation::Pair
            : core::CvmApproximation::Tetrahedron;

    // Species and composition from the ECI file when it carries them,
    // otherwise an equiatomic binary — stated in the output so a default is
    // never mistaken for the ensemble's actual composition.
    bool defaulted = true;
    const QJsonArray speciesArray = root.value(QStringLiteral("species")).toArray();
    if (speciesArray.size() == 2) {
        for (const QJsonValue& v : speciesArray)
            input.species.push_back(v.toString().toStdString());
        input.composition = {0.5, 0.5};
    } else {
        input.species = {"A", "B"};
        input.composition = {0.5, 0.5};
    }
    defaulted = speciesArray.size() != 2;

    // pairEnergiesFromEci, NOT a hand-written transform: the ECI is in the
    // ±1 correlation basis and the CVM wants bond energies, and that
    // conversion is a factor of two and a sign — precisely the mistake that
    // turns an ordering alloy into a clustering one with nothing failing.
    bool ok = false;
    input.pairEnergiesEv = core::pairEnergiesFromEci(pairEci, &ok);
    if (!ok || input.pairEnergiesEv.size() != 4)
        return fail(QObject::tr("its pair ECI could not be converted into "
                                "bond energies"));

    // The triplet, when the fit found one and the approximation can hold it.
    const double tripletEciEv =
        root.value(QStringLiteral("nearest_neighbour_triplet_eci_eV"))
            .toDouble(0.0);
    if (tripletEciEv != 0.0
        && spec.approximation == CvmEntropySpec::Approximation::Tetrahedron) {
        bool okTriplet = false;
        input.tripletEnergiesEv =
            core::tripletEnergiesFromEci(tripletEciEv, &okTriplet);
    }

    input.minTemperatureK = spec.minTemperatureK;
    input.maxTemperatureK = spec.maxTemperatureK;
    input.temperatureSteps = spec.temperatureSteps;

    const core::CvmResult solved = core::solveClusterVariation(input);
    if (!solved.ok)
        return fail(QObject::tr("its CVM solve failed: %1")
                        .arg(solved.warnings.empty()
                                 ? QObject::tr("no usable solution")
                                 : QString::fromStdString(
                                       solved.warnings.front())));

    QJsonArray points;
    for (const core::CvmPoint& point : solved.points) {
        QJsonObject entry;
        entry[QStringLiteral("temperature_K")] = point.temperatureK;
        entry[QStringLiteral("entropy_kB")] = point.entropyPerSiteKb;
        entry[QStringLiteral("energy_eV")] = point.energyPerSiteEv;
        entry[QStringLiteral("free_energy_eV")] = point.freeEnergyPerSiteEv;
        QJsonArray alpha;
        for (const double a : point.warrenCowley)
            alpha.append(a);
        entry[QStringLiteral("warren_cowley")] = alpha;
        points.append(entry);
    }

    QJsonObject out;
    out[QStringLiteral("schema")] = QStringLiteral("calango.cvm.entropy/1");
    out[QStringLiteral("lattice")] = CvmEntropySpec::latticeName(spec.lattice);
    out[QStringLiteral("approximation")] =
        CvmEntropySpec::approximationName(spec.approximation);
    out[QStringLiteral("pair_eci_eV")] = pairEci;
    out[QStringLiteral("triplet_eci_eV")] = tripletEciEv;
    out[QStringLiteral("triplet_used")] = !input.tripletEnergiesEv.empty();
    out[QStringLiteral("ideal_entropy_kB")] = solved.idealEntropyKb;
    out[QStringLiteral("sro_vanishing_temperature_K")] =
        solved.sroVanishingTemperatureK;
    out[QStringLiteral("composition_defaulted")] = defaulted;
    QJsonArray warnings;
    for (const std::string& w : solved.warnings)
        warnings.append(QString::fromStdString(w));
    // The warnings travel WITH the curve. The headline one — that a
    // homogeneous CVM cannot produce an order-disorder transition — decides
    // whether these numbers can be compared with the alloy literature at all.
    out[QStringLiteral("warnings")] = warnings;
    out[QStringLiteral("points")] = points;

    output->entropyJson =
        QString::fromUtf8(QJsonDocument(out).toJson(QJsonDocument::Indented));
    const double coldest = solved.points.empty()
        ? 0.0
        : solved.points.front().entropyPerSiteKb;
    output->headline =
        QObject::tr("S = %1 k_B at %2 K against an ideal %3 k_B")
            .arg(coldest, 0, 'f', 4)
            .arg(solved.points.empty() ? 0.0
                                       : solved.points.front().temperatureK,
                 0, 'f', 0)
            .arg(solved.idealEntropyKb, 0, 'f', 4);
    return true;
}

QString DumpSpec::describe() const
{
    QStringList keys{energyKey, forcesKey};
    if (!stressKey.isEmpty())
        keys << stressKey;
    const QString name =
        outputPath.isEmpty() ? QObject::tr("(no path set)")
                             : QFileInfo(outputPath).fileName();
    return QObject::tr("%1 -> %2").arg(keys.join(QStringLiteral("/")), name);
}

QJsonObject DumpSpec::toJson() const
{
    QJsonObject object;
    object.insert(QStringLiteral("output_path"), outputPath);
    object.insert(QStringLiteral("energy_key"), energyKey);
    object.insert(QStringLiteral("forces_key"), forcesKey);
    object.insert(QStringLiteral("stress_key"), stressKey);
    object.insert(QStringLiteral("config_type"), configType);
    object.insert(QStringLiteral("include_failed_frames"), includeFailedFrames);
    object.insert(QStringLiteral("append"), appendToExistingFile);
    return object;
}

DumpSpec DumpSpec::fromJson(const QJsonObject& object)
{
    DumpSpec spec;
    spec.outputPath = object.value(QStringLiteral("output_path")).toString();
    spec.energyKey = object.value(QStringLiteral("energy_key"))
                         .toString(spec.energyKey);
    spec.forcesKey = object.value(QStringLiteral("forces_key"))
                         .toString(spec.forcesKey);
    spec.stressKey = object.value(QStringLiteral("stress_key")).toString();
    spec.configType = object.value(QStringLiteral("config_type")).toString();
    spec.includeFailedFrames =
        object.value(QStringLiteral("include_failed_frames")).toBool(false);
    spec.appendToExistingFile =
        object.value(QStringLiteral("append")).toBool(false);
    return spec;
}

void applyMaceTrainingPreset(DumpSpec* spec)
{
    if (!spec)
        return;
    // mace.tools.default_keys.DefaultKeys, mace 0.3.15 (verified against the
    // installed package, not assumed): ENERGY = "REF_energy",
    // FORCES = "REF_forces", STRESS = "REF_stress" — the same values
    // mace_run_train's --energy_key/--forces_key/--stress_key default to.
    // The REF_ prefix is deliberate on MACE's side, not a Calango
    // convention: its data loader refuses a bare "stress" key outright,
    // since ASE 3.23.0b1 made that name unsafe to round-trip between ASE and
    // MACE, and names this prefix as the fix.
    spec->energyKey = QStringLiteral("REF_energy");
    spec->forcesKey = QStringLiteral("REF_forces");
    spec->stressKey = QStringLiteral("REF_stress");
}

bool runDump(const QList<DumpSourceFrame>& sources, const DumpSpec& spec,
            DumpOutput* output, QString* error)
{
    const auto fail = [error](const QString& message) {
        if (error)
            *error = message;
        return false;
    };
    if (!output)
        return fail(QObject::tr("no output was requested"));
    if (!spec.isValid())
        return fail(QObject::tr(
            "it has no output path, or no energy/forces key names (%1)")
                        .arg(spec.describe()));

    std::vector<pybridge::AseBridge::DumpSourceFile> files;
    files.reserve(static_cast<std::size_t>(sources.size()));
    for (const DumpSourceFrame& source : sources)
        files.push_back({source.path.toStdString(), source.label.toStdString(),
                         source.configTypeOverride.toStdString()});

    pybridge::AseBridge::DumpWriteResult written;
    try {
        written = pybridge::AseBridge::writeDumpTrainingSet(
            files, spec.outputPath.toStdString(), spec.energyKey.toStdString(),
            spec.forcesKey.toStdString(), spec.stressKey.toStdString(),
            spec.configType.toStdString(), spec.includeFailedFrames,
            spec.appendToExistingFile);
    } catch (const std::exception& e) {
        return fail(QObject::tr("its training set could not be written (%1)")
                        .arg(QString::fromUtf8(e.what())));
    }

    output->framesWritten = written.framesWritten;
    output->framesExcluded = written.framesExcluded;
    for (const std::string& reason : written.excludedReasons)
        output->excludedReasons << QString::fromStdString(reason);
    output->headline =
        output->framesExcluded > 0
            ? QObject::tr("%1 frame(s) written to %2 (%3 excluded)")
                  .arg(output->framesWritten)
                  .arg(QFileInfo(spec.outputPath).fileName())
                  .arg(output->framesExcluded)
            : QObject::tr("%1 frame(s) written to %2")
                  .arg(output->framesWritten)
                  .arg(QFileInfo(spec.outputPath).fileName());
    return true;
}

} // namespace calango::gui
