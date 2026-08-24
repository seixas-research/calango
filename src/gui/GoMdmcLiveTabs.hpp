#pragma once

#include <QObject>
#include <QString>

#include <vector>

namespace calango::gui {

/// WHICH VIEWPORT TABS A GO-MDMC RUN OPENS — the whole answer, in one place.
///
/// Not a detail of MainWindow, because the answer used to be wrong in three
/// separate places at once and nothing could see the total. A single local run
/// opened FOUR tabs:
///
///   1. `openGoMdmc()` opened its decoupled working copy of the chosen
///      Graphene Oxide Build, purely so `stageJob()` — which stages whatever
///      document is current — would find it. Fixed by staging the copy
///      directly (`MainWindow::stagedRunStructure_`); the copy still exists,
///      it just no longer needs a tab to be reachable.
///   2. `launchJob()` opened the generic stdout-streamed trajectory tab that
///      every frame-producing run gets. Fixed by `opensStreamedTrajectoryTab()`
///      below: GO-MDMC's *All Structures* tab already carries every geometry
///      that stream would have shown, and carries it WITH the per-atom
///      functional-group columns the stdout wire format cannot express
///      (DECISIONS.md D4), so the streamed tab was a strictly worse duplicate
///      of a tab that was opened anyway.
///   3. + 4. `setUpGoMdmcLiveFiles()` opened the two tabs S4 actually asks
///      for — `goMdmcLiveViews()` below. These are the two that stay.
///
/// A tab the user opened themselves before the run is none of this function's
/// business and is never touched.
///
/// The translations go through `QObject::tr` rather than a class's own `tr()`
/// because these strings have no class any more. The repo ships no translation
/// catalogs, so this is about keeping the strings marked, not about a lookup.

/// One of the two viewport tabs a GO-MDMC run opens, and the file under the
/// run's `proc_<id>/` directory it follows as the script appends to it.
struct GoMdmcLiveView {
    /// File name relative to the run directory. A fixed literal here because
    /// it is a fixed literal in the generated script too (DECISIONS.md D7) —
    /// nothing outside that script chooses either name.
    QString fileName;
    /// Tab title, before `MainWindow` appends the " (live)" marker it drops
    /// again when the run finishes.
    QString title;
};

/// Every viewport tab a GO-MDMC run creates, in creation order. EXACTLY two.
inline std::vector<GoMdmcLiveView> goMdmcLiveViews()
{
    return {
        {QStringLiteral("mdmc_all_structures.extxyz"),
         QObject::tr("GO-MDMC / All Structures")},
        {QStringLiteral("accepted_structures.extxyz"),
         QObject::tr("GO-MDMC / Accepted")},
    };
}

/// The task label a GO-MDMC run is launched under — the key `launchJob()`
/// matches on to recognize one, and the name its row in the Processes panel
/// carries.
inline QString goMdmcTaskLabel()
{
    return QObject::tr("GO-MDMC");
}

/// Whether a frame-producing run under `taskLabel` ALSO opens the generic
/// stdout-streamed trajectory tab.
///
/// True for every run but GO-MDMC — see the note at the top of this file for
/// why that one is the exception. Phrased as a question about the label rather
/// than as an `if` buried in `launchJob()` so the exception is a thing a test
/// can ask about.
inline bool opensStreamedTrajectoryTab(const QString& taskLabel)
{
    return taskLabel != goMdmcTaskLabel();
}

} // namespace calango::gui
