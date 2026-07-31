#pragma once

#include "core/Structure.hpp"
#include "core/VolumetricData.hpp"

#include <QDialog>
#include <QString>

#include <map>
#include <memory>
#include <vector>

class QLabel;
class QPushButton;
class QTableWidget;
class QTableWidgetItem;

namespace calango::gui {

class ViewportWidget;

/// Results → "MLWF Viewer": the dedicated read-out for a completed Maximally
/// Localized Wannier Functions run. It parses `wannier.json` and shows the
/// Wannier centres r̄_n = (x̄,ȳ,z̄) in Å, the per-orbital spreads Ω_n and the
/// total minimization spread Ω_tot = Ω_I + Ω_D̃, plus the localization
/// functional (the trial-projection overlap metric).
///
/// Per-orbital checkboxes toggle the real-space isosurface ψ_n(r) of each
/// Wannier orbital directly onto the MAIN 3D viewport (each in its own
/// color), so they overlay the atomic structure.
///
/// Purely a read-out: the Wannier post-processes that CONSUME this run
/// (Wannier Interpolation, Fermi Surface, Topological Invariants) are standalone
/// modules under Electronics, each with its own completed-MLWF prerequisite
/// check — they no longer launch from here.
class MlwfViewer : public QDialog {
    Q_OBJECT

public:
    MlwfViewer(std::shared_ptr<const core::Structure> structure,
               ViewportWidget* viewport, QWidget* parent = nullptr);
    ~MlwfViewer() override;

    /// Parse a `wannier.json` results file and fill the table + read-outs. The
    /// per-orbital cubes are resolved relative to the JSON's directory.
    void loadResults(const QString& jsonPath);

private Q_SLOTS:
    void onOrbitalToggled(QTableWidgetItem* item);

private:
    /// Rebuild the combined viewport overlay from the currently checked
    /// orbitals (each colored distinctly, blended translucently).
    void rebuildOverlay();
    /// Load (and cache) one Wannier orbital cube from the results directory.
    std::shared_ptr<const core::VolumetricData> cubeFor(int orbital);

    std::shared_ptr<const core::Structure> structure_;
    ViewportWidget* viewport_ = nullptr;
    QString jobDir_;              ///< directory of the loaded wannier.json
    std::vector<QString> cubes_;  ///< per-orbital cube filenames from the JSON
    std::map<int, std::shared_ptr<const core::VolumetricData>> cubeCache_;

    QTableWidget* table_ = nullptr;
    QLabel* totalSpreadLabel_ = nullptr;
    QLabel* functionalLabel_ = nullptr;
};

} // namespace calango::gui
