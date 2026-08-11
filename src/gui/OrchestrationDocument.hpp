#pragma once

#include "gui/OrchestrationWindow.hpp"

#include <QJsonObject>
#include <QString>

namespace calango::gui {

/// The serialized form of an orchestration pipeline: the file the GUI writes
/// and `calango-cli` runs on a cluster.
///
/// Three properties are deliberate and the whole design follows from them.
///
///   SELF-CONTAINED. The structures travel INSIDE the document, as extended
///   XYZ text. A workflow you copy to a login node has to bring its geometry
///   with it — a file of paths into somebody's laptop is not a portable
///   workflow, and "remember to also copy the six CIFs" is the kind of
///   instruction that goes wrong at 2 a.m.
///
///   SELF-DESCRIBING. Each node carries its own family and its own input-slot
///   table, rather than the reader being expected to know that Raman/IR has
///   three slots of which two are optional. That knowledge lives in exactly
///   one place (workflow task metadata, here in the GUI) and is *exported*,
///   so the CLI cannot drift from the panel that wrote the file.
///
///   VERSIONED. `schema` is checked by the reader. A pipeline is a thing
///   people keep; a format that silently changes meaning between releases
///   turns an archived run into a wrong one.
///
/// The document holds no run state — no statuses, no job directories. It is
/// the pipeline, not an execution of it; provenance records describe the
/// latter and live beside the results.
namespace OrchestrationDocument {

/// Schema identifier written into every document and required by readers.
inline constexpr auto kSchema = "calango.workflow/1";

/// Build the document for the pipeline `window` currently holds.
///
/// Serializing the structures needs ASE (extxyz is written through the
/// bridge), so this must run on the GUI thread with the interpreter alive.
/// A structure that cannot be written is reported in `warnings` and its
/// container entry is dropped rather than emitted empty — a container item
/// with no geometry would fail on the cluster, hours later, with nothing to
/// explain it.
QJsonObject build(const OrchestrationWindow& window, QStringList* warnings);

/// Write `document` to `path` as indented JSON. False (with `error` set) on
/// an I/O failure.
bool write(const QJsonObject& document, const QString& path, QString* error);

/// Re-read a document written by build(): reconstructs the nodes, their
/// configuration and the links on `window`, which must be empty.
///
/// Exists so the format is round-trippable rather than write-only — a
/// serializer nothing ever reads back is a serializer whose bugs are found by
/// the CLI, on a cluster. Returns false with `error` set on a schema mismatch
/// or malformed content; on success `window` holds the pipeline.
bool load(OrchestrationWindow& window, const QJsonObject& document,
          QString* error);

} // namespace OrchestrationDocument

} // namespace calango::gui
