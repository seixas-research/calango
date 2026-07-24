#include "gui/ConvexHullWindow.hpp"

#include "gui/ConvexHullPlotWidget.hpp"

#include <QHBoxLayout>
#include <QPushButton>
#include <QVBoxLayout>

namespace calango::gui {

ConvexHullWindow::ConvexHullWindow(const QString& directory, QWidget* parent)
    : QDialog(parent), plot_(new ConvexHullPlotWidget(this))
{
    setWindowTitle(tr("Convex Hull Analytics"));
    resize(760, 620);

    hasData_ = plot_->loadFromJson(directory + QStringLiteral("/cluster_expansion.json"));

    auto* layout = new QVBoxLayout(this);
    layout->addWidget(plot_, 1);

    auto* row = new QHBoxLayout;
    row->addStretch(1);
    auto* exportButton = new QPushButton(tr("Export Data…"), this);
    row->addWidget(exportButton);
    layout->addLayout(row);
    connect(exportButton, &QPushButton::clicked, plot_,
            &ConvexHullPlotWidget::exportData);

    // Bubble the plot's double-click-to-jump signal up to the controller.
    connect(plot_, &ConvexHullPlotWidget::frameActivated, this,
            &ConvexHullWindow::frameActivated);
}

} // namespace calango::gui
