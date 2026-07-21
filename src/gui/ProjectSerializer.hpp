#pragma once

#include <QJsonObject>

#include <memory>

namespace calango::core {
class Structure;
}

namespace calango::gui {

/// JSON codec for the .calproj project format (version 1).
///
/// A project file is one self-describing JSON document that embeds every
/// structure verbatim (atoms, cell, per-atom scalar/vector fields, manual
/// bond overrides) — saving and restoring a workspace therefore needs no
/// Python/ASE round-trip and no sidecar files. Trajectories store every
/// frame; analysis overlays that live on the structure (CN/GCN/charges
/// pushed into scalar fields) ride along automatically.
///
/// The workspace-level schema (documents, tabs, job console and metric
/// series) is assembled in MainWindow; this header owns only the
/// structure-level encoding so core stays Qt-JSON-free.
namespace ProjectSerializer {

/// Format magic — refuse files that are not Calango projects.
inline constexpr int kFormatVersion = 1;

QJsonObject structureToJson(const core::Structure& structure);
std::shared_ptr<core::Structure> structureFromJson(const QJsonObject& json);

} // namespace ProjectSerializer
} // namespace calango::gui
