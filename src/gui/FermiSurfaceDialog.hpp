#pragma once

#include "core/FermiSurfaceScriptGenerator.hpp"

#include <QDialog>
#include <QList>
#include <QPair>
#include <QString>

class QDoubleSpinBox;
class QLabel;
class QSpinBox;

namespace calango::gui {

class MlwfSourceSelector;

/// Settings for a Wannier-interpolated Fermi surface. Two steps: which MLWF
/// run to post-process, then how finely to sample k and at what energy to cut.
/// Everything else about the electronic structure is inherited from that run.
class FermiSurfaceDialog : public QDialog {
    Q_OBJECT

public:
    /// `mlwfRuns` are the completed MLWF processes to offer as sources —
    /// (label, job directory) pairs; the user may also browse to one.
    explicit FermiSurfaceDialog(
        const QList<QPair<QString, QString>>& mlwfRuns,
        QWidget* parent = nullptr);

    /// The collected settings, `mlwfDir` included — the dialog now owns that
    /// choice, so the caller no longer has to fill it in afterwards.
    core::FermiSurfaceConfig config() const;

    /// Directory of the selected MLWF run.
    QString mlwfDirectory() const;

private:
    void refreshCostNote();

    MlwfSourceSelector* source_ = nullptr;
    QSpinBox* samplesSpins_[3] = {nullptr, nullptr, nullptr};
    QDoubleSpinBox* offsetSpin_ = nullptr;
    QSpinBox* iterationsSpin_ = nullptr;
    QLabel* costNote_ = nullptr;
};

} // namespace calango::gui
