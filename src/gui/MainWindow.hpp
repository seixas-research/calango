#pragma once

#include <QMainWindow>

#include <deque>
#include <memory>
#include <vector>

class QDockWidget;

namespace calango::core {
class Structure;
}
namespace calango::jobs {
class JobRunner;
}

namespace calango::gui {

class EnergyPlotWidget;
class JobLogWidget;
class StructureInfoWidget;
class TimelineWidget;
class ViewportWidget;

/// Application shell and MVC "Controller": owns the Structure model, the
/// trajectory frames, the undo history and the JobRunner. All mutations
/// flow through here (via AseBridge or direct model edits) and are pushed
/// into the views with notifyStructureChanged().
class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow() override;

    /// Load any ASE-readable structure file (also used for CLI arguments).
    void loadFile(const QString& path);

private Q_SLOTS:
    void openStructure();
    void openTrajectory();
    void saveStructureAs();
    void exportImage();
    void exportAnimation();

    void createSupercell();
    void cleaveSurface();
    void addAtom();
    void changeElementOfSelection();
    void translateSelection();
    void deleteSelectedAtoms();
    void undo();
    void redo();

    void newCalculation();
    void onJobFinished(int exitCode, bool crashed);
    void showFrame(int index);

    void about();

private:
    void createMenusAndDocks();
    /// Replace the model (clears any loaded trajectory).
    void setStructure(std::shared_ptr<core::Structure> structure, const QString& sourceName);
    void notifyStructureChanged(bool frameCamera = true);
    void pushUndo();
    void updateUndoActions();
    void runScript(const QString& script);
    bool ensureAseAvailable();

    std::shared_ptr<core::Structure> structure_;
    std::vector<std::shared_ptr<core::Structure>> frames_; ///< trajectory playback
    std::deque<std::shared_ptr<core::Structure>> undoStack_;
    std::deque<std::shared_ptr<core::Structure>> redoStack_;
    QString currentFileName_;
    QString lastJobDir_;

    ViewportWidget* viewport_ = nullptr;
    StructureInfoWidget* infoWidget_ = nullptr;
    JobLogWidget* jobLogWidget_ = nullptr;
    EnergyPlotWidget* energyPlot_ = nullptr;
    TimelineWidget* timeline_ = nullptr;
    QDockWidget* jobDock_ = nullptr;
    jobs::JobRunner* jobRunner_ = nullptr;
    QAction* undoAction_ = nullptr;
    QAction* redoAction_ = nullptr;
};

} // namespace calango::gui
