#pragma once

#include "core/FermiSurfaceScriptGenerator.hpp"

#include <QDialog>

class QDoubleSpinBox;
class QLabel;
class QSpinBox;

namespace calango::gui {

/// Settings for a Wannier-interpolated Fermi surface, opened from the MLWF
/// viewer. Small on purpose: everything about the electronic structure is
/// inherited from the MLWF run, so the only decisions left are how finely to
/// sample k and at what energy to cut.
class FermiSurfaceDialog : public QDialog {
    Q_OBJECT

public:
    explicit FermiSurfaceDialog(QWidget* parent = nullptr);

    /// The collected settings. `mlwfDir` is left empty for the caller, which
    /// is the only party that knows which job this belongs to.
    core::FermiSurfaceConfig config() const;

private:
    void refreshCostNote();

    QSpinBox* samplesSpin_ = nullptr;
    QDoubleSpinBox* offsetSpin_ = nullptr;
    QSpinBox* iterationsSpin_ = nullptr;
    QLabel* costNote_ = nullptr;
};

} // namespace calango::gui
