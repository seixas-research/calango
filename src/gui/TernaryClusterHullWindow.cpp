#include "gui/TernaryClusterHullWindow.hpp"

#include "core/TernaryConvexHull.hpp"
#include "gui/GuiUtils.hpp"
#include "gui/TernaryClusterHullWidget.hpp"

#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonObject>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

namespace calango::gui {

TernaryClusterHullWindow::TernaryClusterHullWindow(const QString& directory,
                                                   QWidget* parent)
    : QDialog(parent), plot_(new TernaryClusterHullWidget(this))
{
    setWindowTitle(tr("Ternary Cluster Expansion — Ground-State Map"));
    resize(820, 660);

    auto* layout = new QVBoxLayout(this);
    layout->addWidget(plot_, 1);

    const QJsonObject root =
        readJsonObject(directory + QStringLiteral("/cluster_expansion.json"));
    QStringList elements;
    if (!root.isEmpty()) {
        for (const QJsonValue& sp : root[QStringLiteral("species")].toArray())
            elements << sp.toString();
    }

    QLabel* note = nullptr;
    if (elements.size() >= 4) {
        note = new QLabel(
            tr("This ensemble has %1 species; only the first three (%2) are "
               "plotted here.")
                .arg(elements.size())
                .arg(elements.mid(0, 3).join(QStringLiteral("-"))),
            this);
        note->setWordWrap(true);
        layout->insertWidget(0, note);
    }

    if (elements.size() >= 3) {
        std::vector<core::TernaryHullPoint> points;
        const QJsonArray configs = root[QStringLiteral("configurations")].toArray();
        points.reserve(static_cast<std::size_t>(configs.size()));
        const QString speciesB = elements.at(1);
        const QString speciesC = elements.at(2);
        for (const QJsonValue& entry : configs) {
            const QJsonObject config = entry.toObject();
            const QJsonValue formation = config[QStringLiteral("formation_energy")];
            if (formation.isNull() || formation.isUndefined())
                continue;
            const QJsonObject composition =
                config[QStringLiteral("composition")].toObject();
            core::TernaryHullPoint point;
            point.xB = composition[speciesB].toDouble();
            point.xC = composition[speciesC].toDouble();
            point.formationEnergy = formation.toDouble();
            point.frameIndex = config[QStringLiteral("frame")].toInt(-1);
            point.label = config[QStringLiteral("formula")].toString().toStdString();
            points.push_back(std::move(point));
        }
        if (!points.empty()) {
            plot_->setData(core::computeTernaryConvexHull(std::move(points)),
                           elements.mid(0, 3));
            hasData_ = true;
        }
    }

    auto* row = new QHBoxLayout;
    row->addStretch(1);
    auto* exportButton = new QPushButton(tr("Export Data…"), this);
    row->addWidget(exportButton);
    layout->addLayout(row);
    connect(exportButton, &QPushButton::clicked, plot_,
            &TernaryClusterHullWidget::exportData);
}

} // namespace calango::gui
