#pragma once

#include <QDialog>
#include <QString>

namespace calango::gui {

class TernaryClusterHullWidget;

/// Modeless standalone viewer for a finished ternary Cluster Expansion
/// Calculation (3+ species): loads cluster_expansion.json's per-species
/// "composition" field (ClusterExpansionScriptGenerator's generalized JSON
/// schema — see its "Formation energies" section) and shows the
/// formation-energy / ground-state map on the composition triangle.
///
/// The K=2 sibling is ConvexHullWindow; MainWindow::onProcessResultRequested()
/// picks between the two by the JSON's own "species" array length. Only the
/// FIRST three species named there are plotted — a quaternary+ ensemble is
/// read (nothing crashes) but projected onto the first three axes, which the
/// window says plainly rather than silently mislabelling a fourth species'
/// share as noise on the other three.
class TernaryClusterHullWindow : public QDialog {
    Q_OBJECT

public:
    explicit TernaryClusterHullWindow(const QString& directory,
                                      QWidget* parent = nullptr);

    /// True when cluster_expansion.json was found, parsed, and named at
    /// least three species.
    bool hasData() const { return hasData_; }

private:
    TernaryClusterHullWidget* plot_;
    bool hasData_ = false;
};

} // namespace calango::gui
