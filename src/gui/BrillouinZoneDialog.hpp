#pragma once

#include "core/BrillouinZone.hpp"
#include "core/KPath.hpp"
#include "python_bridge/AseBridge.hpp"

#include <QDialog>
#include <QListWidget>
#include <QSpinBox>

#include <vector>

namespace calango::gui {

class BrillouinZoneView;

/// Reciprocal-space analytics: renders the first Brillouin zone with its
/// high-symmetry points and lets the user build a band-structure k-path by
/// clicking points in order (or loading ASE's suggested path). Paths may
/// contain discontinuous sections ("Break", e.g. Γ→X | M→R) and export to
/// VASP KPOINTS (line mode), Quantum ESPRESSO K_POINTS (crystal_b), CASTEP
/// SPECTRAL_KPOINT_PATH, SIESTA BandLines, and standalone ASE/Python
/// scripts; the annotated 3D figure exports to PNG or SVG.
class BrillouinZoneDialog : public QDialog {
    Q_OBJECT

public:
    BrillouinZoneDialog(const core::BrillouinZoneData& zone,
                        const pybridge::AseBridge::BandPathInfo& bandPath,
                        QWidget* parent = nullptr);

private Q_SLOTS:
    void appendPoint(int index);
    void addBreak();
    void undoLastPoint();
    void clearPath();
    void useSuggestedPath();
    /// One entry point for every k-path format: pops a format-selection
    /// dialog (VASP / QE / CASTEP / SIESTA / ASE script) and dispatches.
    void exportKPath();
    void exportFigure();

private:
    void exportVaspKpoints();
    void exportQeKpoints();
    void exportCastepPath();
    void exportSiestaBands();
    void exportAseScript();

private:
    void syncPathViews();
    /// Continuous sections of the current path (breaks split; sections
    /// with < 2 points are kept for display but skipped by exporters).
    core::KPathSegments segments() const;
    bool hasExportablePath() const;
    void saveTextFile(const QString& text, const QString& caption,
                      const QString& defaultName);

    core::BrillouinZoneData zone_;
    std::vector<core::KPathPoint> specialPoints_;
    QString suggestedPath_;
    std::vector<int> path_; ///< indices into specialPoints_; -1 = break

    BrillouinZoneView* view_;
    QListWidget* pathList_;
    QSpinBox* divisionsSpin_;
};

} // namespace calango::gui
