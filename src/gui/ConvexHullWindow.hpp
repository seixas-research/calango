#pragma once

#include <QDialog>
#include <QString>

namespace calango::gui {

class ConvexHullPlotWidget;

/// Modeless standalone viewer for a finished Cluster Expansion Calculation:
/// loads cluster_expansion.json and shows the formation-energy convex hull
/// (E_form vs concentration x) with stable-vertex markers, hover read-outs and
/// a data-export table. Replaces the former Zone-10 "Results" dock tab so the
/// analysis gets its own resizable window.
class ConvexHullWindow : public QDialog {
    Q_OBJECT

public:
    explicit ConvexHullWindow(const QString& directory, QWidget* parent = nullptr);

    /// True when cluster_expansion.json was found and parsed.
    bool hasData() const { return hasData_; }

Q_SIGNALS:
    /// Re-emitted from the embedded plot: the user double-clicked a
    /// configuration — the controller jumps the viewport to that frame.
    void frameActivated(int frameIndex);

private:
    ConvexHullPlotWidget* plot_;
    bool hasData_ = false;
};

} // namespace calango::gui
