#include "gui/ProjectSerializer.hpp"

#include "core/Structure.hpp"

#include <QJsonArray>

#include <algorithm>
#include <utility>

namespace calango::gui::ProjectSerializer {

namespace {

QJsonArray vec3ListToJson(const std::vector<core::Vec3>& values)
{
    QJsonArray flat; // x0,y0,z0,x1,y1,z1,... — compact and index-aligned
    for (const core::Vec3& v : values) {
        flat.append(v.x);
        flat.append(v.y);
        flat.append(v.z);
    }
    return flat;
}

std::vector<core::Vec3> vec3ListFromJson(const QJsonArray& flat)
{
    std::vector<core::Vec3> values;
    values.reserve(static_cast<std::size_t>(flat.size() / 3));
    for (qsizetype i = 0; i + 2 < flat.size(); i += 3)
        values.push_back({flat[i].toDouble(), flat[i + 1].toDouble(),
                          flat[i + 2].toDouble()});
    return values;
}

QJsonArray bondPairsToJson(const std::vector<std::pair<int, int>>& pairs)
{
    QJsonArray array;
    for (const auto& [i, j] : pairs)
        array.append(QJsonArray{i, j});
    return array;
}

std::vector<std::pair<int, int>> bondPairsFromJson(const QJsonArray& array)
{
    std::vector<std::pair<int, int>> pairs;
    pairs.reserve(static_cast<std::size_t>(array.size()));
    for (const auto& entry : array) {
        const QJsonArray pair = entry.toArray();
        if (pair.size() == 2)
            pairs.emplace_back(pair[0].toInt(), pair[1].toInt());
    }
    return pairs;
}

} // namespace

QJsonObject structureToJson(const core::Structure& structure)
{
    QJsonObject json;

    QJsonArray numbers;
    std::vector<core::Vec3> positions;
    positions.reserve(structure.size());
    for (const core::Atom& atom : structure.atoms()) {
        numbers.append(atom.atomicNumber);
        positions.push_back(atom.position);
    }
    json[QStringLiteral("atomicNumbers")] = numbers;
    json[QStringLiteral("positions")] = vec3ListToJson(positions);

    if (structure.cell().isDefined()) {
        const auto& vectors = structure.cell().vectors();
        QJsonObject cell;
        cell[QStringLiteral("vectors")] = vec3ListToJson(
            {vectors[0], vectors[1], vectors[2]});
        const auto pbc = structure.cell().pbc();
        cell[QStringLiteral("pbc")] = QJsonArray{pbc[0], pbc[1], pbc[2]};
        json[QStringLiteral("cell")] = cell;
    }

    if (!structure.scalarFields().empty()) {
        QJsonObject fields;
        for (const auto& [name, values] : structure.scalarFields()) {
            QJsonArray array;
            for (const double value : values)
                array.append(value);
            fields[QString::fromStdString(name)] = array;
        }
        json[QStringLiteral("scalarFields")] = fields;
    }

    if (!structure.vectorFields().empty()) {
        QJsonObject fields;
        for (const auto& [name, values] : structure.vectorFields())
            fields[QString::fromStdString(name)] = vec3ListToJson(values);
        json[QStringLiteral("vectorFields")] = fields;
    }

    if (!structure.addedBonds().empty())
        json[QStringLiteral("addedBonds")] = bondPairsToJson(structure.addedBonds());
    if (!structure.removedBonds().empty())
        json[QStringLiteral("removedBonds")]
            = bondPairsToJson(structure.removedBonds());

    return json;
}

std::shared_ptr<core::Structure> structureFromJson(const QJsonObject& json)
{
    auto structure = std::make_shared<core::Structure>();

    const QJsonArray numbers = json[QStringLiteral("atomicNumbers")].toArray();
    const auto positions
        = vec3ListFromJson(json[QStringLiteral("positions")].toArray());
    const auto atomCount
        = std::min(static_cast<std::size_t>(numbers.size()), positions.size());
    for (std::size_t i = 0; i < atomCount; ++i)
        structure->addAtom({numbers[static_cast<qsizetype>(i)].toInt(6),
                            positions[i]});

    if (const QJsonObject cell = json[QStringLiteral("cell")].toObject();
        !cell.isEmpty()) {
        const auto vectors
            = vec3ListFromJson(cell[QStringLiteral("vectors")].toArray());
        const QJsonArray pbc = cell[QStringLiteral("pbc")].toArray();
        if (vectors.size() == 3)
            structure->setCell(core::UnitCell(
                vectors[0], vectors[1], vectors[2],
                {pbc.size() == 3 ? pbc[0].toBool(true) : true,
                 pbc.size() == 3 ? pbc[1].toBool(true) : true,
                 pbc.size() == 3 ? pbc[2].toBool(true) : true}));
    }

    const QJsonObject scalarFields = json[QStringLiteral("scalarFields")].toObject();
    for (auto it = scalarFields.begin(); it != scalarFields.end(); ++it) {
        const QJsonArray array = it.value().toArray();
        std::vector<double> values;
        values.reserve(static_cast<std::size_t>(array.size()));
        for (const auto& value : array)
            values.push_back(value.toDouble());
        structure->setScalarField(it.key().toStdString(), std::move(values));
    }

    const QJsonObject vectorFields = json[QStringLiteral("vectorFields")].toObject();
    for (auto it = vectorFields.begin(); it != vectorFields.end(); ++it)
        structure->setVectorField(it.key().toStdString(),
                                  vec3ListFromJson(it.value().toArray()));

    for (const auto& [i, j] :
         bondPairsFromJson(json[QStringLiteral("addedBonds")].toArray()))
        structure->addBondOverride(i, j);
    for (const auto& [i, j] :
         bondPairsFromJson(json[QStringLiteral("removedBonds")].toArray()))
        structure->removeBondOverride(i, j);

    return structure;
}

} // namespace calango::gui::ProjectSerializer
