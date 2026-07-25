#pragma once

#include "core/Structure.hpp"

#include <QDialog>
#include <QJsonObject>
#include <QString>

#include <memory>
#include <vector>

class QLabel;
class QPushButton;
class QSlider;

namespace calango::gui {

class ViewportWidget;

/// Results → "Geometry Optimization Viewer": the read-out for a completed
/// relaxation. It parses the `geometry_optimization.json` the generated ASE
/// script writes (AseScriptGenerator, TaskKind::GeometryOptimization) and
/// shows the physical summary — final total energy and the change over the
/// run, the maximum and RMS atomic force against the convergence criterion,
/// the optimizer used and how many steps it took.
///
/// It also loads `opt.traj` so the intermediate geometries can be scrubbed: a
/// relaxation that "converged" through an unphysical intermediate is a real
/// failure mode, and the only way to see it is to step through the path. The
/// scrubber drives the MAIN viewport, so the structure is inspected with the
/// full representation and measurement tooling rather than in a thumbnail.
///
/// Opened automatically when a relaxation finishes (MainWindow::onJobFinished),
/// on demand from the Processes panel, and from the Results menu. Purely a
/// viewer: it never launches a calculation.
class GeometryOptimizationViewer : public QDialog {
    Q_OBJECT

public:
    /// `viewport` receives the scrubbed trajectory frames; null disables the
    /// scrubber (the summary still works).
    explicit GeometryOptimizationViewer(ViewportWidget* viewport,
                                        QWidget* parent = nullptr);
    ~GeometryOptimizationViewer() override;

    /// Parse a `geometry_optimization.json` summary and fill the read-outs.
    /// Returns false (showing nothing) when the file is missing or malformed.
    /// The JSON's directory is remembered for the volumetric export and is
    /// where `opt.traj` is looked for.
    bool loadResults(const QString& jsonPath);

Q_SIGNALS:
    /// "Get Volumetric Data": the host should export the final electron
    /// density / potential from `directory` into the Volumetric Data dock.
    void getVolumetricDataRequested(const QString& directory);

private Q_SLOTS:
    void showFrame(int index);
    void copyToClipboard();

private:
    /// Read `opt.traj` (falling back to optimized.extxyz) into frames_.
    void loadTrajectory();
    QString plainTextSummary() const;
    /// Restore whatever the viewport showed before scrubbing started, so
    /// closing the viewer does not leave the workspace on an intermediate
    /// relaxation frame the user never chose.
    void restoreViewport();

    ViewportWidget* viewport_ = nullptr;
    QJsonObject data_;
    QString directory_;
    std::vector<std::shared_ptr<core::Structure>> frames_;
    std::shared_ptr<const core::Structure> viewportStructureBefore_;

    QLabel* energyLabel_ = nullptr;
    QLabel* energyChangeLabel_ = nullptr;
    QLabel* forceLabel_ = nullptr;
    QLabel* rmsForceLabel_ = nullptr;
    QLabel* optimizerLabel_ = nullptr;
    QLabel* convergedLabel_ = nullptr;
    QLabel* frameLabel_ = nullptr;
    QSlider* frameSlider_ = nullptr;
    QPushButton* volumetricButton_ = nullptr;
};

} // namespace calango::gui
