#pragma once

#include "core/Structure.hpp"
#include "core/WannierScriptGenerator.hpp"

#include <QDialog>

#include <memory>

class QCheckBox;
class QDoubleSpinBox;
class QSpinBox;

namespace calango::gui {

class EmbeddedKPathEditor;

/// "Wannier Interpolation…" setup dialog, launched from the MLWF Viewer. It
/// configures a fast real-space→reciprocal-space interpolation (H(R) → H(k))
/// from a completed MLWF run:
///   * a high-symmetry k-path for the interpolated band structure E_n(k)
///     (interactive Brillouin-zone builder, reused from the wizards);
///   * a dense Monkhorst-Pack k-mesh for a high-resolution Wannier-projected
///     PDOS;
///   * a frozen energy window (E_f) and inner/outer disentanglement windows.
///
/// The dialog only collects settings; on accept the MLWF Viewer generates the
/// script (core::generateWannierInterpolationScript) and runs it, after which
/// the interactive Band Structure + PDOS viewer opens on the results.
class WannierInterpolationDialog : public QDialog {
    Q_OBJECT

public:
    explicit WannierInterpolationDialog(
        std::shared_ptr<const core::Structure> structure,
        QWidget* parent = nullptr);

    /// The interpolation settings the user configured.
    core::WannierInterpolationConfig config() const;

private Q_SLOTS:
    void updateEnabled();

private:
    EmbeddedKPathEditor* kpath_ = nullptr;
    QSpinBox* bandPointsSpin_ = nullptr;
    QSpinBox* kmeshSpins_[3] = {nullptr, nullptr, nullptr};
    QDoubleSpinBox* pdosWidthSpin_ = nullptr;
    QCheckBox* frozenCheck_ = nullptr;
    QDoubleSpinBox* frozenSpin_ = nullptr;
    QCheckBox* disentangleCheck_ = nullptr;
    QDoubleSpinBox* innerSpin_ = nullptr;
    QDoubleSpinBox* outerSpin_ = nullptr;
};

} // namespace calango::gui
