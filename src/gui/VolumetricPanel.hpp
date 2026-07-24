#pragma once

#include "core/MarchingCubes.hpp"
#include "core/VolumetricData.hpp"
#include "gui/VolumetricStyle.hpp"

#include <QFutureWatcher>
#include <QString>
#include <QWidget>

#include <memory>
#include <vector>

class QCheckBox;
class QTreeWidget;

namespace calango::gui {

class ViewportWidget;
class EditVolumetricRenderDialog;

/// Zone 13 — the "Volumetric Data" dock: a single-view registry of 3D scalar
/// volumetric grids (.cube / .xsf / CHGCAR-family, MLWF orbitals) visualized as
/// overlays on the MAIN 3D viewport, aligned with the atoms.
///
/// Layout: the tree registry of active fields over a compact icon-only action
/// bar (RemixIcon glyphs) —
///   * "Load External Files…" — import a scalar volumetric dataset from disk;
///   * "Show Metadata…"       — the read-only VolumetricMetadataDialog;
///   * "Edit Render…"         — the multi-tab EditVolumetricRenderDialog
///     (Isosurfaces / Color Slice / Potential Map), which also selects the
///     active render mode.
///
/// Isosurface extraction runs off the GUI thread (coalesced). The panel owns no
/// canvas; it drives the injected ViewportWidget through
/// setCustomOverlay()/setLatticePlane().
class VolumetricPanel : public QWidget {
    Q_OBJECT

public:
    explicit VolumetricPanel(ViewportWidget* viewport,
                             QWidget* parent = nullptr);
    ~VolumetricPanel() override;

    /// Register a volumetric file produced by a calculation (e.g. elf.cube,
    /// wannier_<n>.cube). Loads it, adds a registry row, and selects it.
    /// `structureLabel` names the associated atomic structure. No-op on a load
    /// failure (callers fire-and-forget).
    void registerResultFile(const QString& path, const QString& label,
                            const QString& structureLabel = QString());

public Q_SLOTS:
    /// Detach any volumetric overlay from the viewport.
    void clearViewportOverlay();

private Q_SLOTS:
    void loadExternalFile();
    void showMetadata();
    void openEditDialog();
    void onSelectionChanged();
    void onShowToggled(bool on);
    void onIsoExtractionFinished();

private:
    struct Entry {
        std::shared_ptr<const core::VolumetricData> field;
        QString label;
        QString path;
        QString structureLabel;
    };
    /// Positive- and negative-phase isosurfaces for a signed field (the
    /// negative mesh is empty for an all-positive field).
    struct PhaseMeshes {
        core::IsoMesh positive;
        core::IsoMesh negative;
    };

    void addEntry(std::shared_ptr<const core::VolumetricData> field,
                  const QString& label, const QString& path,
                  const QString& structureLabel);
    const Entry* currentEntry() const;
    const core::VolumetricData* currentField() const;
    int currentRow() const;
    void defaultIsovalueForField();

    void render();               ///< dispatch by mode for the selection
    void renderSlice(bool potentialRamp); ///< build + push a color slice
    void requestIsosurface();    ///< queue an off-thread isosurface extraction
    void pumpIsoExtraction();
    void pushPhaseMeshes(const PhaseMeshes& meshes);

    ViewportWidget* viewport_ = nullptr;
    std::vector<Entry> entries_;
    VolumetricStyle style_;
    VolumetricRenderMode mode_ = VolumetricRenderMode::Isosurface;
    EditVolumetricRenderDialog* editDialog_ = nullptr; ///< modeless, lazy

    QTreeWidget* registry_ = nullptr;
    QCheckBox* showCheck_ = nullptr;

    // Off-thread isosurface extraction (coalesced, generation-tagged).
    QFutureWatcher<PhaseMeshes> isoWatcher_;
    bool isoPending_ = false;
    unsigned isoGeneration_ = 0;
    unsigned isoRunningGeneration_ = 0;
};

} // namespace calango::gui
