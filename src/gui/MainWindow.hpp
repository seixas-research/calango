#pragma once

#include <QMainWindow>

#include <deque>
#include <memory>
#include <vector>

class QDockWidget;
class QTabBar;

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

/// Application shell and MVC "Controller" with a tabbed multi-document
/// workspace: each tab is a Document (structure + optional trajectory +
/// its own undo history). All documents share the single accelerated
/// viewport — the tab bar switches which document the views observe —
/// so display settings apply consistently and no GL context churn occurs.
class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow() override;

    /// Load any ASE-readable structure file into a new tab.
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
    void showBrillouinZone();
    void showRdf();
    void openNanoBuilder();
    void addRandomNoise();
    void loadExample(const QString& resourcePath, const QString& recommendation);

    void onTabChanged(int index);
    void onTabCloseRequested(int index);

    void about();

private:
    struct Document {
        std::shared_ptr<core::Structure> structure;
        std::vector<std::shared_ptr<core::Structure>> frames; ///< trajectory
        std::deque<std::shared_ptr<core::Structure>> undoStack;
        std::deque<std::shared_ptr<core::Structure>> redoStack;
        QString fileName;
    };

    void createMenusAndDocks();
    Document* currentDocument();
    /// Creates an empty "Untitled" tab when none exists (for Add Atom).
    Document& ensureDocument();
    int addDocument(std::shared_ptr<core::Structure> structure, const QString& name,
                    std::vector<std::shared_ptr<core::Structure>> frames = {});
    /// Push the current document's state into all views.
    void syncViewsToCurrent(bool frameCamera);
    /// Replace the current document's structure (supercell, slab, undo...).
    void replaceCurrentStructure(std::shared_ptr<core::Structure> structure,
                                 const QString& name);
    void notifyStructureChanged(bool frameCamera = true);
    void pushUndo();
    void updateUndoActions();
    void runScript(const QString& script, const QString& pythonExe);
    bool ensureAseAvailable();

    std::vector<std::unique_ptr<Document>> documents_;
    QString lastJobDir_;

    QTabBar* tabBar_ = nullptr;
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
