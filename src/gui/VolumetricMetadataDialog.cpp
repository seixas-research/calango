#include "gui/VolumetricMetadataDialog.hpp"

#include "core/VolumetricData.hpp"

#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLabel>
#include <QVBoxLayout>

#include <cmath>

namespace calango::gui {

namespace {

double norm(const core::Vec3& v)
{
    return std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
}

QString vec(const core::Vec3& v)
{
    return QStringLiteral("(%1, %2, %3) Å")
        .arg(v.x, 0, 'f', 4)
        .arg(v.y, 0, 'f', 4)
        .arg(v.z, 0, 'f', 4);
}

} // namespace

VolumetricMetadataDialog::VolumetricMetadataDialog(
    const core::VolumetricData& field, const QString& source,
    const QString& structureLabel, QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle(tr("Volumetric Metadata"));

    auto* layout = new QVBoxLayout(this);
    auto* form = new QFormLayout;
    layout->addLayout(form);

    const auto addRow = [&](const QString& key, const QString& value) {
        auto* label = new QLabel(value, this);
        label->setTextInteractionFlags(Qt::TextSelectableByMouse);
        label->setWordWrap(true);
        form->addRow(key, label);
    };

    addRow(tr("Field:"),
           QString::fromStdString(field.label).isEmpty()
               ? tr("(unnamed)")
               : QString::fromStdString(field.label));
    addRow(tr("Data source:"), source.isEmpty() ? tr("—") : source);
    addRow(tr("Spatial dimensions:"),
           QStringLiteral("%1 × %2 × %3  (%4 voxels)")
               .arg(field.nx)
               .arg(field.ny)
               .arg(field.nz)
               .arg(static_cast<qlonglong>(field.nx) * field.ny * field.nz));
    addRow(tr("Grid origin:"), vec(field.origin));

    // Per-axis voxel spacing Δ = |span| / dim.
    const double dx = field.nx > 0 ? norm(field.spanA) / field.nx : 0.0;
    const double dy = field.ny > 0 ? norm(field.spanB) / field.ny : 0.0;
    const double dz = field.nz > 0 ? norm(field.spanC) / field.nz : 0.0;
    addRow(tr("Voxel spacing (Δx, Δy, Δz):"),
           QStringLiteral("(%1, %2, %3) Å")
               .arg(dx, 0, 'f', 4)
               .arg(dy, 0, 'f', 4)
               .arg(dz, 0, 'f', 4));
    addRow(tr("Cell vectors:"),
           QStringLiteral("a %1\nb %2\nc %3")
               .arg(vec(field.spanA), vec(field.spanB), vec(field.spanC)));
    addRow(tr("Min / Max scalar:"),
           QStringLiteral("%1  /  %2")
               .arg(field.minValue(), 0, 'g', 6)
               .arg(field.maxValue(), 0, 'g', 6));
    addRow(tr("Associated structure:"),
           structureLabel.isEmpty() ? tr("—") : structureLabel);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);
}

} // namespace calango::gui
