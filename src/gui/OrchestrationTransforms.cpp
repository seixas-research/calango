#include "gui/OrchestrationTransforms.hpp"

#include "core/CalphadModel.hpp"
#include "core/Element.hpp"
#include "core/Structure.hpp"
#include "core/TdbWriter.hpp"
#include "gui/GuiUtils.hpp"
#include "python_bridge/AseBridge.hpp"

#include <QJsonArray>
#include <QJsonDocument>
#include <QObject>
#include <QStringList>

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

} // namespace calango::gui
