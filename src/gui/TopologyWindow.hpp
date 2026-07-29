#pragma once

#include <QDialog>
#include <QJsonObject>
#include <QString>
#include <QWidget>

#include <vector>

class QLabel;
class QCheckBox;

namespace calango::gui {

/// The hybrid Wannier centre flow: one marker per centre at each point of the
/// Wilson loop, drawn on a circle-valued vertical axis.
///
/// The plot is the evidence for the integer, not decoration. A Chern number is
/// the net drift of the centres from one side to the other; a non-trivial Z₂
/// is the largest gap between them being crossed an odd number of times. Both
/// are visible at a glance in the flow and in neither case is the integer
/// alone falsifiable — a badly sampled loop produces a confident wrong answer
/// that only the flow reveals.
class WccFlowWidget : public QWidget {
    Q_OBJECT

public:
    explicit WccFlowWidget(QWidget* parent = nullptr);

    /// `wcc[k][m]` are the centres in [0, 1); `gapMid[k]` the largest-gap
    /// reference the Z₂ crossings are counted against (empty when Z₂ was not
    /// requested).
    void setFlow(std::vector<std::vector<double>> wcc,
                 std::vector<double> gapMid);
    void setShowGapMidpoint(bool on);

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    std::vector<std::vector<double>> wcc_;
    std::vector<double> gapMid_;
    bool showGap_ = true;
};

/// Results viewer for a topological-invariant run: the invariants themselves
/// and the Wilson-loop flow they were read off.
class TopologyWindow : public QDialog {
    Q_OBJECT

public:
    explicit TopologyWindow(QWidget* parent = nullptr);

    /// Parse a `topology.json`. False (showing nothing) when it is missing or
    /// malformed.
    bool loadResults(const QString& jsonPath);

private:
    void exportImage();

    QJsonObject data_;
    WccFlowWidget* flow_ = nullptr;
    QLabel* invariants_ = nullptr;
    QLabel* caveats_ = nullptr;
    QCheckBox* gapCheck_ = nullptr;
};

} // namespace calango::gui
