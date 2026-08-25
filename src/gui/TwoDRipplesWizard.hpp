#pragma once

#include "core/Ripples.hpp"
#include "core/Structure.hpp"

#include <QDialog>

#include <memory>
#include <vector>

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QGroupBox;
class QLabel;
class QPushButton;
class QSpinBox;

namespace calango::gui {

/// Modules → 2D Materials → "2D Ripples…": impose a sinusoidal out-of-plane
/// corrugation on a monolayer supercell.
///
/// A pure generator, native throughout — like Random Noise Setup there is no
/// script and no job, because there is nothing to run: the transform is
/// core::applyRipples, evaluated in process, and the result is either one
/// structure or a whole amplitude series opened as a scrubbable trajectory.
///
/// WHY IT IS IN THE 2D MATERIALS GROUP AND FIRST IN IT. Every other entry on
/// that menu READS a sheet (bands, optics, work function, charged defects);
/// this one MAKES one, and a builder goes ahead of the readouts that consume
/// it — the same ordering the Graphene Oxide menu uses, with a separator
/// marking the change of kind.
///
/// WHAT THE USER HAS TO DECIDE, and what this class refuses to decide for
/// them. The out-of-plane axis is SEEDED from gui::guessVacuumAxis() and
/// left editable, because a thick slab in a modest cell and a thin one in a
/// huge cell are not reliably distinguishable from coordinates — and getting
/// it wrong here does not produce a slightly wrong sheet, it ripples the
/// structure sideways. Everything else (profile, amplitude, periods,
/// whether to contract) is a modelling choice with no defensible automatic
/// answer.
///
/// The physics — the profile, the arc-length contraction and why it is not
/// optional — lives on core::RippleOptions. This class owns the controls,
/// the live read-out of what they imply for the cell, and the hand-off to
/// the host.
class TwoDRipplesWizard : public QDialog {
    Q_OBJECT

public:
    /// `sheet` is the monolayer to ripple; it is never modified.
    explicit TwoDRipplesWizard(std::shared_ptr<const core::Structure> sheet,
                               QWidget* parent = nullptr);

    /// The generated frames. One entry for a single build, `count` entries
    /// for an amplitude series. Empty until "Generate" has been pressed.
    const std::vector<std::shared_ptr<core::Structure>>& frames() const
    {
        return frames_;
    }

Q_SIGNALS:
    /// A fresh build arrived. One frame is a plain structure to the host;
    /// several are a trajectory. ONE signal for both, because the two differ
    /// only in how the host presents them and a second signal would be a
    /// second thing to keep in step.
    void structuresGenerated(
        const std::vector<std::shared_ptr<core::Structure>>& frames);

private Q_SLOTS:
    void generateStructures();

private:
    core::RippleOptions rippleOptions() const;
    /// Grey out the period control for a direction the profile does not vary
    /// along, and keep the series group's own enablement honest.
    void syncControls();
    /// The live read-out: what the current amplitude does to the cell, in Å
    /// and in percent, from the SAME solver the build uses. Also where an
    /// impossible amplitude is reported, before Generate is pressed rather
    /// than after.
    void refreshSummary();

    std::shared_ptr<const core::Structure> sheet_;
    std::vector<std::shared_ptr<core::Structure>> frames_;

    QComboBox* axisCombo_ = nullptr;
    QComboBox* directionCombo_ = nullptr;
    QDoubleSpinBox* amplitudeSpin_ = nullptr;
    QSpinBox* periodsFirstSpin_ = nullptr;
    QSpinBox* periodsSecondSpin_ = nullptr;
    QCheckBox* contractCheck_ = nullptr;
    QLabel* cellSummary_ = nullptr;

    QGroupBox* seriesGroup_ = nullptr;
    QDoubleSpinBox* minAmplitudeSpin_ = nullptr;
    QDoubleSpinBox* maxAmplitudeSpin_ = nullptr;
    QSpinBox* seriesCountSpin_ = nullptr;

    QPushButton* generateButton_ = nullptr;
    QLabel* generationStatus_ = nullptr;
};

} // namespace calango::gui
