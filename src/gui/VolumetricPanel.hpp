#pragma once

#include "core/MarchingCubes.hpp"
#include "core/VolumetricData.hpp"
#include "gui/VolumetricStyle.hpp"

#include <QFutureWatcher>
#include <QString>
#include <QStringList>
#include <QWidget>

#include <memory>
#include <unordered_map>
#include <vector>

class QTreeWidget;
class QTreeWidgetItem;

namespace calango::gui {

class ViewportWidget;
class EditVolumetricRenderDialog;

/// What a volumetric dataset IS, as opposed to what it contains — facts the
/// panel cannot read off the grid and that change how the field is drawn.
///
/// At namespace scope rather than nested in VolumetricPanel because
/// registerResultFile() defaults it: a default argument may not reach into a
/// nested class's member initializers while the enclosing class is still being
/// defined.
struct DatasetOrigin {
    /// This field is one Wannier function. Two things follow: its isosurface
    /// is continued into the neighbouring periodic images instead of being cut
    /// at the cell faces, and a tab showing one for the first time switches to
    /// the glossy material.
    bool wannier = false;
    /// Wannier centre in Cartesian angstrom, as reported by the
    /// wannierization. Optional: without it the panel derives the centre from
    /// the field itself (core::periodicCentroid), which for a Wannier cube is
    /// the same quantity computed the same way.
    bool hasCentre = false;
    core::Vec3 centre{};
};

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
                            int workspaceId = -1,
                            const DatasetOrigin& origin = {});

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
        /// What this dataset is (see DatasetOrigin). `centre` is resolved here
        /// once at registration — from the wannierization when it reported one,
        /// otherwise from the field — so the extraction thread never has to
        /// walk the whole grid again per isovalue change.
        bool wannier = false;
        core::Vec3 centre{};
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

    /// Everything about HOW a workspace tab's volumetric data is drawn, as
    /// opposed to WHICH datasets it owns (that is `Entry::workspaceId`).
    ///
    /// Kept per tab for the same reason the camera and the atom/bond style
    /// are: these settings are authored against one field of one structure —
    /// an isovalue is a number in that field's own units, and a slice plane is
    /// a direction in that cell. Carrying one tab's values to the next was
    /// only half the problem; the worse half was that switching back
    /// re-derived the isovalue from the field and silently discarded whatever
    /// the user had dialled in.
    struct WorkspaceState {
        VolumetricStyle style;
        VolumetricRenderMode mode = VolumetricRenderMode::Isosurface;
    };
    /// Copy the live style/mode into `workspaceStates_[activeWorkspace_]`.
    void stashWorkspaceState();

    void addEntry(std::shared_ptr<const core::VolumetricData> field,
                  const QString& label, const QString& path,
                  const QString& structureLabel, int workspaceId,
                  const DatasetOrigin& origin);
    /// Switch `workspaceId`'s material to Glossy unless the user has already
    /// picked one. Called once per Wannier dataset registered; see the comment
    /// at the call site for why this is a promotion rather than a default.
    void promoteWannierMaterial(int workspaceId);
    /// True when any dataset bound to the active tab is a Wannier function —
    /// which is what makes the periodic-continuation control in the Edit
    /// Render dialog live rather than dead.
    bool activeWorkspaceHasWannier() const;
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
    /// Upload the selected field as a 3D texture + transfer function for
    /// direct volume rendering. Synchronous: the work is one normalizing
    /// pass over the grid, and the marching itself happens on the GPU.
    void pushDirectVolume();
    void requestExtraction();    ///< queue an off-thread isosurface extraction
    void pumpIsoExtraction();
    void pushResults(const std::vector<ExtractResult>& results);

    ViewportWidget* viewport_ = nullptr;
    std::vector<Entry> entries_;
    /// The live style/mode — always those of `activeWorkspace_`. Saved into
    /// `workspaceStates_` on the way out of a tab and reloaded on the way in.
    VolumetricStyle style_;
    VolumetricRenderMode mode_ = VolumetricRenderMode::Isosurface;
    EditVolumetricRenderDialog* editDialog_ = nullptr; ///< modeless, lazy
    /// Workspace tab whose datasets are listed and rendered (-1 = none yet).
    int activeWorkspace_ = -1;
    /// Per-tab render settings. Workspace ids are handed out monotonically and
    /// never reused, so a closed tab's entry can never be adopted by a new one.
    std::unordered_map<int, WorkspaceState> workspaceStates_;
    /// True while a tab switch is installing a saved style, which suppresses
    /// the automatic isovalue derivation in onSelectionChanged(). Without it
    /// the restore is undone one call later — that WAS the bug.
    bool restoringWorkspace_ = false;

    QTreeWidget* registry_ = nullptr;

    // Off-thread extraction (coalesced, generation-tagged). One future covers
    // every visible dataset of the active tab, so a batch lands atomically.
    QFutureWatcher<std::vector<ExtractResult>> isoWatcher_;
    bool isoPending_ = false;
    unsigned isoGeneration_ = 0;
    unsigned isoRunningGeneration_ = 0;
};

} // namespace calango::gui
