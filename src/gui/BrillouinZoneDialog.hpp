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
/// clicking points in order (or loading ASE's suggested path). The path
/// exports to VASP KPOINTS (line mode) and Quantum ESPRESSO K_POINTS
/// (crystal_b) files.
class BrillouinZoneDialog : public QDialog {
    Q_OBJECT

public:
    BrillouinZoneDialog(const core::BrillouinZoneData& zone,
                        const pybridge::AseBridge::BandPathInfo& bandPath,
                        QWidget* parent = nullptr);

private Q_SLOTS:
    void appendPoint(int index);
    void undoLastPoint();
    void clearPath();
    void useSuggestedPath();
    void exportVaspKpoints();
    void exportQeKpoints();

private:
    void syncPathViews();
    std::vector<core::KPathPoint> pathPoints() const;
    void saveTextFile(const QString& text, const QString& caption,
                      const QString& defaultName);

    core::BrillouinZoneData zone_;
    std::vector<core::KPathPoint> specialPoints_;
    QString suggestedPath_;
    std::vector<int> path_; ///< indices into specialPoints_

    BrillouinZoneView* view_;
    QListWidget* pathList_;
    QSpinBox* divisionsSpin_;
};

} // namespace calango::gui
