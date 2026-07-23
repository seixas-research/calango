#pragma once

#include "core/BrillouinZone.hpp"
#include "core/KPath.hpp"
#include "python_bridge/AseBridge.hpp"

#include <QDialog>

namespace calango::gui {

class BrillouinZoneWidget;

/// Standalone host for BrillouinZoneWidget (Build → Brillouin Zone Builder),
/// adding the export actions the wizards do not need. The interactive zone,
/// path table and action bar all live in the widget, which the Electronic
/// Structure and Phonon wizards embed directly in Stage 1.
///
/// Paths may contain discontinuous sections ("Break", e.g. Γ→X | M→R) and export to
/// VASP KPOINTS (line mode), Quantum ESPRESSO K_POINTS (crystal_b), CASTEP
/// SPECTRAL_KPOINT_PATH, SIESTA BandLines, and standalone ASE/Python
/// scripts; the annotated 3D figure exports to PNG or SVG.
class BrillouinZoneDialog : public QDialog {
    Q_OBJECT

public:
    BrillouinZoneDialog(const core::BrillouinZoneData& zone,
                        const pybridge::AseBridge::BandPathInfo& bandPath,
                        QWidget* parent = nullptr);

    /// The currently built k-path as an ASE path string. Retained for
    /// callers that open the builder standalone; the wizards embed
    /// BrillouinZoneWidget directly instead.
    QString asePathString() const;

private Q_SLOTS:
    /// One entry point for every k-path format: pops a format-selection
    /// dialog (VASP / QE / CASTEP / SIESTA / ASE script) and dispatches.
    void exportKPath();
    void exportFigure();

private:
    /// Calango's native k-path interchange format (kpath.json): the
    /// high-symmetry point coordinates plus the explicit sequential path
    /// segments (labels + fractional coordinates + cumulative distance).
    /// This is the primary export format and the one the Phonon / Electronic
    /// Structure wizards load.
    void exportKpathJson();
    void exportVaspKpoints();
    void exportQeKpoints();
    void exportCastepPath();
    void exportSiestaBands();
    void exportAseScript();

private:
    void saveTextFile(const QString& text, const QString& caption,
                      const QString& defaultName);

    /// All zone rendering, path state and editing lives here; the dialog adds
    /// only the export actions and a Close button.
    BrillouinZoneWidget* builder_;
};

} // namespace calango::gui
