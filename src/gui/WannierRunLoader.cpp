#include "gui/WannierRunLoader.hpp"

#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QObject>

namespace calango::gui {

bool loadWannierRun(const QString& runDir, WannierRunData* out, QString* error)
{
    const auto fail = [error](const QString& message) {
        if (error)
            *error = message;
        return false;
    };
    if (!out)
        return fail(QObject::tr("No destination for the loaded run."));

    const QString jsonPath = runDir + QStringLiteral("/wannier.json");
    QFile file(jsonPath);
    if (!file.open(QIODevice::ReadOnly))
        return fail(QObject::tr("%1 holds no wannier.json, so it is not a "
                                "completed Wannier run.")
                        .arg(runDir));
    const QJsonObject root =
        QJsonDocument::fromJson(file.readAll()).object();
    if (root.isEmpty())
        return fail(QObject::tr("%1 could not be read.").arg(jsonPath));

    // `hr` is recorded rather than assumed by name, so a run that predates
    // H(R) output is distinguishable from one whose file went missing — and
    // the two need different instructions.
    const QString hrName = root.value(QStringLiteral("hr")).toString();
    if (hrName.isEmpty())
        return fail(QObject::tr(
            "This Wannier run recorded no Hamiltonian H(R).\n\n"
            "Runs from before Calango wrote one only stored the centres, "
            "spreads and orbital cubes. Re-run the Wannierization on the same "
            "baseline to produce it — the settings are already in its "
            "run.py."));

    const QString hrPath = QFileInfo(hrName).isAbsolute()
        ? hrName
        : runDir + QLatin1Char('/') + hrName;
    if (!QFile::exists(hrPath))
        return fail(QObject::tr("%1 is missing from %2, though the run "
                                "recorded it. It may have been moved or "
                                "deleted.")
                        .arg(hrName, runDir));

    WannierRunData data;
    data.hrPath = hrPath;
    data.nWannier = root.value(QStringLiteral("nwannier")).toInt();

    const QJsonArray cell = root.value(QStringLiteral("cell")).toArray();
    if (cell.size() < 3)
        return fail(QObject::tr(
            "This Wannier run recorded no cell, so the integer lattice "
            "vectors of its Hamiltonian cannot be turned into distances. "
            "Re-run the Wannierization to produce one."));
    for (int i = 0; i < 3; ++i) {
        const QJsonArray row = cell.at(i).toArray();
        for (int j = 0; j < 3 && j < row.size(); ++j)
            data.cell[static_cast<std::size_t>(i)][static_cast<std::size_t>(j)] =
                row.at(j).toDouble();
    }

    for (const QJsonValue& value :
         root.value(QStringLiteral("centers")).toArray()) {
        const QJsonArray c = value.toArray();
        if (c.size() >= 3)
            data.centres.append({c.at(0).toDouble(), c.at(1).toDouble(),
                                 c.at(2).toDouble()});
    }
    for (const QJsonValue& value :
         root.value(QStringLiteral("spreads")).toArray())
        data.spreads.append(value.toDouble());

    std::string parseError;
    data.hamiltonian = core::WannierHamiltonian::fromHrDat(
        hrPath.toStdString(), data.cell, &parseError);
    if (!parseError.empty())
        return fail(QObject::tr("Could not read %1: %2")
                        .arg(hrName, QString::fromStdString(parseError)));
    if (data.hamiltonian.orbitals() == 0)
        return fail(QObject::tr("%1 carries no orbitals.").arg(hrName));

    *out = std::move(data);
    return true;
}

} // namespace calango::gui
