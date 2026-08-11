#include "gui/OrchestrationTransforms.hpp"

#include "core/Element.hpp"
#include "core/Structure.hpp"
#include "gui/GuiUtils.hpp"
#include "python_bridge/AseBridge.hpp"

#include <QJsonArray>
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

QString SupercellSpec::describe() const
{
    return QStringLiteral("%1 x %2 x %3").arg(na).arg(nb).arg(nc);
}

QJsonObject SupercellSpec::toJson() const
{
    return QJsonObject{{QStringLiteral("na"), na},
                       {QStringLiteral("nb"), nb},
                       {QStringLiteral("nc"), nc}};
}

SupercellSpec SupercellSpec::fromJson(const QJsonObject& object)
{
    SupercellSpec spec;
    spec.na = object.value(QStringLiteral("na")).toInt(spec.na);
    spec.nb = object.value(QStringLiteral("nb")).toInt(spec.nb);
    spec.nc = object.value(QStringLiteral("nc")).toInt(spec.nc);
    return spec;
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

QJsonObject DefectSpec::toJson() const
{
    QJsonArray array;
    for (const DefectOperation& operation : operations)
        array.append(operation.toJson());
    return QJsonObject{{QStringLiteral("operations"), array}};
}

DefectSpec DefectSpec::fromJson(const QJsonObject& object)
{
    DefectSpec spec;
    for (const QJsonValue& value :
         object.value(QStringLiteral("operations")).toArray())
        spec.operations.append(DefectOperation::fromJson(value.toObject()));
    return spec;
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
        fail(QObject::tr("Supercell repetitions must be 1 or more (got %1).")
                 .arg(spec.describe()));
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
        return pybridge::AseBridge::makeSupercell(structure, spec.na, spec.nb,
                                                  spec.nc);
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

} // namespace calango::gui
