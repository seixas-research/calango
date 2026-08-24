#pragma once

#include <QObject>
#include <QString>

#include <vector>

namespace calango::gui {

/// WHICH VIEWPORT TABS A GO/MCMD RUN OPENS — the whole answer, in one place.
///
/// Not a detail of MainWindow, because the answer used to be wrong in three
/// separate places at once and nothing could see the total. A single local run
/// opened FOUR tabs:
///
///   1. `openGoMcmd()` opened its decoupled working copy of the chosen
///      Graphene Oxide Build, purely so `stageJob()` — which stages whatever
///      document is current — would find it. Fixed by staging the copy
///      directly (`MainWindow::stagedRunStructure_`); the copy still exists,
///      it just no longer needs a tab to be reachable.
///   2. `launchJob()` opened the generic stdout-streamed trajectory tab that
///      every frame-producing run gets. Fixed by `opensStreamedTrajectoryTab()`
///      below: GO/MCMD's *All Structures* tab already carries every geometry
///      that stream would have shown, and carries it WITH the per-atom
///      functional-group columns the stdout wire format cannot express
///      (DECISIONS.md D4), so the streamed tab was a strictly worse duplicate
///      of a tab that was opened anyway.
///   3. + 4. `setUpGoMcmdLiveFiles()` opened the two tabs S4 actually asks
///      for — `goMcmdLiveViews()` below. These are the two that stay.
///
/// A tab the user opened themselves before the run is none of this function's
/// business and is never touched.
///
/// The translations go through `QObject::tr` rather than a class's own `tr()`
/// because these strings have no class any more. The repo ships no translation
/// catalogs, so this is about keeping the strings marked, not about a lookup.

/// One of the two viewport tabs a GO/MCMD run opens, and the file under the
/// run's `proc_<id>/` directory it follows as the script appends to it.
struct GoMcmdLiveView {
    /// File name relative to the run directory. A fixed literal here because
    /// it is a fixed literal in the generated script too (DECISIONS.md D7) —
    /// nothing outside that script chooses either name.
    QString fileName;
    /// Tab title, before `MainWindow` appends the " (live)" marker it drops
    /// again when the run finishes.
    QString title;
};

/// The task label a GO/MCMD run is launched under — the key `launchJob()`
/// matches on to recognize one, and the name its row in the Processes panel
/// carries.
inline QString goMcmdTaskLabel()
{
    return QObject::tr("GO/MCMD");
}

/// The same, for GO/MC-Opt — the sibling module that relaxes each proposal to
/// a local minimum instead of running a burst of dynamics.
///
/// A SEPARATE label, not a variant of the one above, because everything that
/// keys off it has to tell the two apart: the Processes row, the Summary
/// window's subtitle, and the viewport tab titles. What they SHARE is the
/// output-file layout — both write mcmd_all_structures.extxyz and
/// accepted_structures.extxyz into their own proc_<id>/ directory — so every
/// consumer downstream (the live tabs, the Summary window, the analysis
/// modules) works for both without knowing which produced the run.
inline QString goMcOptTaskLabel()
{
    return QObject::tr("GO/MC-Opt");
}

/// The task label a GO Grand Canonical MC run is launched under.
///
/// A third distinct label, for the same reason the second one is distinct:
/// the Processes row, the Summary window's title and the viewport tab
/// titles all have to say which module produced the run. The output-file
/// layout is shared with the other two, so every consumer downstream reads
/// all three without knowing which.
inline QString goGcmcTaskLabel()
{
    return QObject::tr("GO/GCMC");
}

/// Is `taskLabel` one of the graphene-oxide Monte Carlo modules?
///
/// The question every tab, dialog and dispatch site actually wants: the two
/// behave identically in all of them, and asking it once here is what keeps a
/// new sibling from needing a fourth `||` in five different files.
inline bool isGoMonteCarloTask(const QString& taskLabel)
{
    return taskLabel == goMcmdTaskLabel()
        || taskLabel == goMcOptTaskLabel()
        || taskLabel == goGcmcTaskLabel();
}

/// Which of the three trajectory files a run should open a live tab for.
///
/// THE FILES ARE ALWAYS WRITTEN — all three of them, on every run. This
/// selects viewing only, which is why the wizard's own tooltip says so: a
/// user unchecking a box is choosing not to watch a file, never to lose it.
struct GoMcmdLiveTabSelection {
    /// One frame per MC cycle: the configuration the Metropolis test was
    /// handed. On by default — it is the per-cycle record, and the one that
    /// answers "what is it proposing?".
    bool candidates = true;
    /// The distinct accepted configurations. On by default — the ensemble.
    bool accepted = true;
    /// Every frame of every inner phase. Off by default: it is the longest
    /// file by far (one frame per MD or optimizer step) and watching it is a
    /// deliberate choice, not a starting point.
    bool allStructures = false;
};

/// Every viewport tab a run of `taskLabel` creates, in creation order,
/// filtered by `selection`. Empty for a label that is not a GO Monte Carlo
/// module — and empty is also a legitimate answer for one, if every box was
/// unchecked.
inline std::vector<GoMcmdLiveView> goMcmdLiveViews(
    const QString& taskLabel, const GoMcmdLiveTabSelection& selection = {})
{
    if (!isGoMonteCarloTask(taskLabel))
        return {};
    // The FILE names are shared by both modules; only the titles say which
    // module produced the run. Two modules never share a proc_<id>/, so there
    // is nothing for a shared name to collide with, and one set of names is
    // what lets the Summary window and the analysis modules read either.
    //
    // Creation order is the reading order: candidates (one per cycle), then
    // the accepted subset, then the full step-by-step record.
    std::vector<GoMcmdLiveView> views;
    if (selection.candidates)
        views.push_back({QStringLiteral("candidates_structures.extxyz"),
                         QObject::tr("%1 — Candidates").arg(taskLabel)});
    if (selection.accepted)
        views.push_back({QStringLiteral("accepted_structures.extxyz"),
                         QObject::tr("%1 — Accepted").arg(taskLabel)});
    if (selection.allStructures)
        views.push_back({QStringLiteral("mcmd_all_structures.extxyz"),
                         QObject::tr("%1 — All Structures").arg(taskLabel)});
    return views;
}

/// Every tab the module CAN open, selection ignored — for the wizard's own
/// checkbox list and for tests that enumerate the full set.
inline std::vector<GoMcmdLiveView> goMcmdAllLiveViews(const QString& taskLabel)
{
    return goMcmdLiveViews(taskLabel, GoMcmdLiveTabSelection{true, true, true});
}

/// GO/MCMD's two tabs — the no-argument form, kept for callers that mean that
/// module specifically.
inline std::vector<GoMcmdLiveView> goMcmdLiveViews()
{
    return goMcmdLiveViews(goMcmdTaskLabel());
}

/// Whether a frame-producing run under `taskLabel` ALSO opens the generic
/// stdout-streamed trajectory tab.
///
/// True for every run but the two Monte Carlo modules — see the note at the
/// top of this file for why those are the exception. Phrased as a question
/// about the label rather than as an `if` buried in `launchJob()` so the
/// exception is a thing a test can ask about.
inline bool opensStreamedTrajectoryTab(const QString& taskLabel)
{
    return !isGoMonteCarloTask(taskLabel);
}

} // namespace calango::gui
