#pragma once

#include <QDialog>
#include <QString>

namespace calango::core {
class VolumetricData;
}

namespace calango::gui {

/// "Volumetric Metadata" — a read-only report for one loaded scalar grid,
/// opened from the Volumetric Data panel's toolbar. Shows the spatial
/// dimensions, the Cartesian grid origin, the per-axis voxel spacing
/// (Δx, Δy, Δz), the min/max scalar values and the associated atomic structure.
class VolumetricMetadataDialog : public QDialog {
    Q_OBJECT

public:
    VolumetricMetadataDialog(const core::VolumetricData& field,
                             const QString& source,
                             const QString& structureLabel,
                             QWidget* parent = nullptr);
};

} // namespace calango::gui
