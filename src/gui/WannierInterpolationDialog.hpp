#pragma once

#include "core/Structure.hpp"
#include "core/WannierScriptGenerator.hpp"

#include <QDialog>
#include <QList>
#include <QPair>
#include <QString>

#include <memory>

class QCheckBox;
class QDoubleSpinBox;
class QSpinBox;

namespace calango::gui {

class EmbeddedKPathEditor;
class MlwfSourceSelector;

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
    /// `mlwfRuns` are the completed MLWF processes to offer as sources —
    /// (label, job directory) pairs; the user may also browse to one.
    WannierInterpolationDialog(
        const QList<QPair<QString, QString>>& mlwfRuns,
        std::shared_ptr<const core::Structure> structure,
        QWidget* parent = nullptr);

    /// The interpolation settings the user configured.
    core::WannierInterpolationConfig config() const;

    /// Directory of the selected MLWF run — the localization being
    /// interpolated.
    QString mlwfDirectory() const;

private Q_SLOTS:
    void updateEnabled();

private:
    MlwfSourceSelector* source_ = nullptr;
    EmbeddedKPathEditor* kpath_ = nullptr;
    QSpinBox* bandPointsSpin_ = nullptr;
    QSpinBox* kmeshSpins_[3] = {nullptr, nullptr, nullptr};
    QDoubleSpinBox* pdosWidthSpin_ = nullptr;
    // One control per ASE parameter. The old "frozen window" and "inner
    // window" pair were two names for fixedenergy, and only the first of them
    // was passed.
    QCheckBox* innerCheck_ = nullptr;
    QDoubleSpinBox* innerSpin_ = nullptr;
    QCheckBox* outerCheck_ = nullptr;
    QDoubleSpinBox* outerSpin_ = nullptr;
};

} // namespace calango::gui
