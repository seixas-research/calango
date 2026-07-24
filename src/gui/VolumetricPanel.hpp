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
/// bar (RemixIcon glyphs) — Load External Files, Show Metadata, Edit Render and
/// Remove Dataset. A selected dataset can also be removed with Delete /
/// Backspace. "Edit Render…" opens the EditVolumetricRenderDialog whose mode
/// dropdown (Isosurfaces / Color Slice / Potential Map) drives the viewport.
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
    void onShowToggled(bool on);
    void onIsoExtractionFinished();

private:
    struct Entry {
        std::shared_ptr<const core::VolumetricData> field;
        QString label;
        QString path;
        QString structureLabel;
    };
    /// Result of one off-thread extraction. For an isosurface, `positive` /
    /// `negative` are the ± phase lobes. For a potential map, `positive` is the
    /// base isosurface with its `colorValues` sampled from the secondary field.
    struct ExtractResult {
        core::IsoMesh positive;
        core::IsoMesh negative;
        bool potential = false;
    };

    void addEntry(std::shared_ptr<const core::VolumetricData> field,
                  const QString& label, const QString& path,
                  const QString& structureLabel);
    const Entry* currentEntry() const;
    const core::VolumetricData* currentField() const;
    int currentRow() const;
    /// Labels of every registered dataset, in registry order (for the dialog's
    /// base/secondary field selectors).
    QStringList datasetLabels() const;
    /// The field a style dataset index refers to (-1 ⇒ the current selection);
    /// null when out of range.
    std::shared_ptr<const core::VolumetricData> fieldForIndex(int index) const;
    void defaultIsovalueForField();
    void syncEditDialogDatasets();

    void render();               ///< dispatch by mode for the selection
    void renderSlice();          ///< build + push a color slice (synchronous)
    void requestExtraction();    ///< queue an off-thread isosurface extraction
    void pumpIsoExtraction();
    void pushResult(const ExtractResult& result);

    ViewportWidget* viewport_ = nullptr;
    std::vector<Entry> entries_;
    VolumetricStyle style_;
    VolumetricRenderMode mode_ = VolumetricRenderMode::Isosurface;
    EditVolumetricRenderDialog* editDialog_ = nullptr; ///< modeless, lazy

    QTreeWidget* registry_ = nullptr;
    QCheckBox* showCheck_ = nullptr;

    // Off-thread extraction (coalesced, generation-tagged).
    QFutureWatcher<ExtractResult> isoWatcher_;
    bool isoPending_ = false;
    unsigned isoGeneration_ = 0;
    unsigned isoRunningGeneration_ = 0;
};

} // namespace calango::gui
