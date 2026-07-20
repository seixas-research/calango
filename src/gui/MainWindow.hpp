#pragma once

#include <QMainWindow>

#include <memory>

class QDockWidget;

namespace calango::core {
class Structure;
}
namespace calango::jobs {
class JobRunner;
}

namespace calango::gui {

class JobLogWidget;
class StructureInfoWidget;
class ViewportWidget;

/// Application shell and MVC "Controller": owns the Structure model and
/// the JobRunner, wires user actions to model mutations (via AseBridge)
/// and pushes the updated model into the views (viewport, info panel).
class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow() override;

    /// Load any ASE-readable structure file (also used for CLI arguments).
    void loadFile(const QString& path);

private Q_SLOTS:
    void openStructure();
    void saveStructureAs();
    void createSupercell();
    void newCalculation();
    void about();

private:
    void createMenusAndDocks();
    void setStructure(std::shared_ptr<core::Structure> structure, const QString& sourceName);
    void notifyStructureChanged();
    void runScript(const QString& script);
    bool ensureAseAvailable();

    std::shared_ptr<core::Structure> structure_;
    QString currentFileName_;

    ViewportWidget* viewport_ = nullptr;
    StructureInfoWidget* infoWidget_ = nullptr;
    JobLogWidget* jobLogWidget_ = nullptr;
    QDockWidget* jobDock_ = nullptr;
    jobs::JobRunner* jobRunner_ = nullptr;
};

} // namespace calango::gui
