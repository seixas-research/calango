#pragma once

#include "core/Structure.hpp"

#include <QDialog>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QLabel>
#include <QSpinBox>
#include <QTimer>

#include <memory>

namespace calango::gui {

class ViewportWidget;

/// "Build → Cleave Surface": surface slab generator (ase.build.surface)
/// with a live 3D preview. Parameter edits rebuild the slab after a short
/// debounce; alongside the preview the dialog reports the surface unit
/// cell vectors u and v (lengths and angle), the slab thickness, and the
/// atom count — all before anything lands in the workspace.
class SlabBuilderDialog : public QDialog {
    Q_OBJECT

public:
    explicit SlabBuilderDialog(std::shared_ptr<const core::Structure> bulk,
                               QWidget* parent = nullptr);

    /// The slab accepted by the user (null if none was built).
    std::shared_ptr<core::Structure> result() const { return result_; }
    QString resultLabel() const; ///< e.g. "(1 1 1) slab"

private Q_SLOTS:
    void scheduleRebuild();
    void rebuildPreview();

private:
    std::shared_ptr<const core::Structure> bulk_;
    std::shared_ptr<core::Structure> result_;

    QSpinBox* millerSpins_[3];
    QSpinBox* layersSpin_;
    QDoubleSpinBox* vacuumSpin_;
    QLabel* infoLabel_;
    QLabel* statusLabel_;
    ViewportWidget* preview_;
    QDialogButtonBox* buttons_;
    QTimer debounce_;
};

} // namespace calango::gui
