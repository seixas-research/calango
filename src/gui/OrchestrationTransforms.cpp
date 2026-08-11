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

} // namespace calango::gui
