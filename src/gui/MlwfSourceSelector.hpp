#pragma once

#include <QList>
#include <QPair>
#include <QString>
#include <QWidget>

class QComboBox;
class QLabel;

namespace calango::gui {

/// "Source MLWF process" — the first step of every module that post-processes
/// a Maximally Localized Wannier Functions run (Wannier Interpolation, Fermi
/// Surface, Topological Charge).
///
/// All three diagonalize the localized Hamiltonian H(R) an MLWF run produced,
/// so none of them can be configured meaningfully until that run is chosen:
/// the number of Wannier functions, the trial projections and the wavefunctions
/// themselves all come from it. It used to be asked for in a separate
/// QInputDialog raised BEFORE the settings dialog, which put the one decision
/// everything else depends on outside the window that shows the consequences —
/// and made it unrevisable without cancelling.
///
/// Two ways in, because there are two situations. The combo lists the runs
/// Calango tracked this session, which covers the normal flow; Browse… takes
/// any directory, which covers a job from an earlier session, one copied back
/// from a cluster, or one the process list has since lost.
///
/// The widget validates as it goes and says what it found — the Wannier count,
/// the projection seed, and whether the wavefunctions are still on disk. That
/// last one matters: an MLWF started from a single-point baseline reads that
/// baseline's `.gpw` and writes none of its own, so a run can be complete and
/// still be unusable because the file it borrowed has been deleted. Catching it
/// here costs a label; catching it after staging costs a queued job that dies
/// on its first line.
class MlwfSourceSelector : public QWidget {
    Q_OBJECT

public:
    /// `candidates` are (display label, absolute job directory) pairs — the
    /// completed MLWF runs the host knows about. May be empty, in which case
    /// only Browse… is offered.
    explicit MlwfSourceSelector(
        const QList<QPair<QString, QString>>& candidates,
        QWidget* parent = nullptr);

    /// Absolute directory of the selected MLWF job, or empty when nothing
    /// usable is selected.
    QString directory() const;

    /// True when the selection is a completed MLWF run whose wavefunctions are
    /// still reachable — i.e. when a job staged against it would actually run.
    /// Dialogs gate their OK button on this.
    bool isValid() const { return valid_; }

    /// Human-readable reason the current selection is unusable; empty when it
    /// is usable. Suitable for a message box on a blocked accept.
    QString invalidReason() const { return invalidReason_; }

Q_SIGNALS:
    /// The selected directory changed (including to an invalid one).
    void changed();

private Q_SLOTS:
    void browse();

private:
    /// Re-read the selection's wannier.json, refresh the status line and the
    /// validity flags.
    void revalidate();

    QComboBox* combo_ = nullptr;
    QLabel* status_ = nullptr;
    bool valid_ = false;
    QString invalidReason_;
};

} // namespace calango::gui
