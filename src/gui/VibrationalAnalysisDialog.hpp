#pragma once

#include "core/Structure.hpp"
#include "core/Vec3.hpp"

#include <QDialog>
#include <QString>

#include <memory>
#include <vector>

class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QPushButton;
class QSlider;
class QTimer;

namespace calango::gui {

class ViewportWidget;

/// "Phonon Viewer → Vibrational Analysis…": pick a phonon mode and watch it.
///
/// A dispersion plot says a branch exists at 480 cm⁻¹; it does not say whether
/// that is a bond stretch, a librational mode or a soft mode about to drive a
/// transition. Animating the eigenvector is what answers that, so this dialog
/// pairs a (q-point, branch) selector with a real-time animation on the MAIN
/// 3D viewport — the structure is inspected with the full representation and
/// measurement tooling rather than in a thumbnail.
///
/// Eigenvectors come from `phonon_modes.json` when the run wrote one. Without
/// it only the frequencies are known (phonon_band.json carries no
/// eigenvectors), and the dialog says so rather than animating a fabricated
/// displacement pattern — a plausible-looking wrong animation is worse than
/// none, because nothing about it looks wrong.
class VibrationalAnalysisDialog : public QDialog {
    Q_OBJECT

public:
    VibrationalAnalysisDialog(const QString& directory,
                              std::shared_ptr<const core::Structure> structure,
                              ViewportWidget* viewport,
                              QWidget* parent = nullptr);
    ~VibrationalAnalysisDialog() override;

Q_SIGNALS:
    /// "Create Mode Trajectory Tab": one full vibrational period of the
    /// selected mode, as frames carrying the harmonic restoring forces. The
    /// host opens it as a new workspace tab (this dialog owns no documents).
    void modeTrajectoryRequested(
        const std::vector<std::shared_ptr<core::Structure>>& frames,
        const QString& label);

private Q_SLOTS:
    void createModeTrajectory();
    void onQPointChanged(int index);
    void onModeChanged(int index);
    void togglePlay();
    /// One animation tick: advance the phase and push the displaced geometry.
    void advance();

private:
    /// Parse phonon_modes.json (q-points, frequencies, eigenvectors) and, as a
    /// fallback, the frequencies alone from phonon_band.json.
    void load(const QString& directory);
    /// The reference structure displaced by the selected mode at `phase`,
    /// with the harmonic restoring forces of that instant attached as the
    /// "forces" vector field when `withForces` is set. Null when no mode /
    /// eigenvector is available.
    std::shared_ptr<core::Structure> displacedAt(double phase,
                                                 bool withForces) const;
    /// Displace the reference structure by the current mode at the current
    /// phase and show it.
    void applyDisplacement();
    /// Put the undisplaced structure back on the viewport.
    void restoreStructure();
    void updateModeLabel();

    /// One q-point's modes: frequencies (cm⁻¹) and, when available, the
    /// per-atom complex eigenvectors of each branch.
    struct QPointModes {
        QString label;                 ///< "Γ", "X", or the raw coordinates
        double q[3] = {0.0, 0.0, 0.0}; ///< fractional reciprocal coordinates
        std::vector<double> frequenciesCm;
        /// eigenvectors[branch][atom] — real and imaginary parts of e_{n,α}(q).
        /// Empty when the run did not export them.
        std::vector<std::vector<core::Vec3>> eigenvectorsReal;
        std::vector<std::vector<core::Vec3>> eigenvectorsImag;
    };

    std::shared_ptr<const core::Structure> reference_;
    ViewportWidget* viewport_ = nullptr;
    std::vector<QPointModes> qpoints_;
    bool hasEigenvectors_ = false;

    QComboBox* qpointCombo_ = nullptr;
    QComboBox* modeCombo_ = nullptr;
    QLabel* modeLabel_ = nullptr;
    QLabel* noticeLabel_ = nullptr;
    QSlider* amplitudeSlider_ = nullptr;
    QSlider* speedSlider_ = nullptr;
    QPushButton* playButton_ = nullptr;
    QPushButton* trajectoryButton_ = nullptr;
    QTimer* timer_ = nullptr;
    double phase_ = 0.0;
};

} // namespace calango::gui
