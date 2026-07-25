#pragma once

#include "core/MarchingCubes.hpp"
#include "core/VolumetricData.hpp"
#include "gui/VolumetricStyle.hpp"

#include <QFutureWatcher>
#include <QString>
#include <QStringList>
#include <QWidget>

#include <memory>
#include <vector>

class QTreeWidget;
class QTreeWidgetItem;

namespace calango::gui {

class ViewportWidget;
class EditVolumetricRenderDialog;

/// Zone 13 — the "Volumetric Data" dock: a single-view registry of 3D scalar
/// volumetric grids (.cube / .xsf / CHGCAR-family, MLWF orbitals) visualized as
/// overlays on the MAIN 3D viewport, aligned with the atoms.
///
/// Layout: the tree registry of active fields over a compact icon-only action
/// bar (RemixIcon glyphs) — Load External Files, Show Metadata, Edit Render and
/// Remove Dataset. A selected dataset can also be removed with Delete /
/// Backspace. "Edit Render…" opens the EditVolumetricRenderDialog whose mode
/// dropdown (Isosurfaces / Color Slice / Potential Map) drives the viewport.
///
/// Visibility is per dataset, not global: every registry row carries its own
/// check box that toggles just that field's 3D rendering, so several
/// isosurfaces can be shown at once (the color slice stays single-plane and
/// follows the selected row).
///
/// Every dataset is bound to the workspace tab that was active when it was
/// registered. setActiveWorkspace() — called by MainWindow on every tab change
/// — filters the registry to that tab's records and re-renders, so overlays
/// belonging to another structure never leak onto the one on screen.
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
    /// `structureLabel` names the associated atomic structure; `workspaceId`
    /// binds the record to a workspace tab (-1 = the active one). No-op on a
    /// load failure (callers fire-and-forget).
    void registerResultFile(const QString& path, const QString& label,
                            const QString& structureLabel = QString(),
                            int workspaceId = -1);

public Q_SLOTS:
    /// Bind the panel to the workspace tab `id` now on screen: rows registered
    /// under other tabs are hidden and their overlays dropped, and only this
    /// tab's records are listed and rendered.
    void setActiveWorkspace(int id);

    /// Detach any volumetric overlay from the viewport.
    void clearViewportOverlay();

protected:
    /// Delete / Backspace on the focused registry tree removes the selection.
    bool eventFilter(QObject* watched, QEvent* event) override;

private Q_SLOTS:
    void loadExternalFile();
    void showMetadata();
    void openEditDialog();
    /// Remove the selected dataset from memory and unload its viewport overlay.
    void removeCurrentDataset();
    void onSelectionChanged();
    /// A registry row's check box was toggled — update that entry's visibility.
    void onItemChanged(QTreeWidgetItem* item, int column);
    void onIsoExtractionFinished();

private:
    struct Entry {
        std::shared_ptr<const core::VolumetricData> field;
        QString label;
        QString path;
        QString structureLabel;
        /// Workspace tab this record belongs to; -1 for a record registered
        /// before any tab existed (shown in every tab, never orphaned).
        int workspaceId = -1;
        /// Per-dataset render toggle, driven by the row's check box.
        bool visible = true;
    };
    /// Result of one off-thread extraction of a single dataset. For an
    /// isosurface, `positive` / `negative` are the ± phase lobes. For a
    /// potential map, `positive` is the base isosurface with its `colorValues`
    /// sampled from the secondary field.
    struct ExtractResult {
        core::IsoMesh positive;
        core::IsoMesh negative;
        bool potential = false;
    };

    void addEntry(std::shared_ptr<const core::VolumetricData> field,
                  const QString& label, const QString& path,
                  const QString& structureLabel, int workspaceId);
    const Entry* currentEntry() const;
    const core::VolumetricData* currentField() const;
    int currentRow() const;
    /// Whether `entry` belongs to the workspace tab currently on screen.
    bool inActiveWorkspace(const Entry& entry) const;
    /// Registry indices of every visible (checked) dataset bound to the active
    /// workspace — exactly what gets rendered.
    std::vector<int> renderableRows() const;
    /// Show only the active workspace's rows, moving the selection off a row
    /// that just went hidden. Callers re-render afterwards.
    void applyWorkspaceFilter();
    /// Labels of every registered dataset, in registry order (for the dialog's
    /// base/secondary field selectors).
    QStringList datasetLabels() const;
    /// The field a style dataset index refers to (-1 ⇒ the current selection);
    /// null when out of range.
    std::shared_ptr<const core::VolumetricData> fieldForIndex(int index) const;
    void defaultIsovalueForField();
    void syncEditDialogDatasets();

    void render();               ///< dispatch by mode for the visible datasets
    void renderSlice();          ///< build + push a color slice (synchronous)
    void requestExtraction();    ///< queue an off-thread isosurface extraction
    void pumpIsoExtraction();
    void pushResults(const std::vector<ExtractResult>& results);

    ViewportWidget* viewport_ = nullptr;
    std::vector<Entry> entries_;
    VolumetricStyle style_;
    VolumetricRenderMode mode_ = VolumetricRenderMode::Isosurface;
    EditVolumetricRenderDialog* editDialog_ = nullptr; ///< modeless, lazy
    /// Workspace tab whose datasets are listed and rendered (-1 = none yet).
    int activeWorkspace_ = -1;

    QTreeWidget* registry_ = nullptr;

    // Off-thread extraction (coalesced, generation-tagged). One future covers
    // every visible dataset of the active tab, so a batch lands atomically.
    QFutureWatcher<std::vector<ExtractResult>> isoWatcher_;
    bool isoPending_ = false;
    unsigned isoGeneration_ = 0;
    unsigned isoRunningGeneration_ = 0;
};

} // namespace calango::gui
