#pragma once

#include "core/Structure.hpp"

#include <QDialog>
#include <QList>
#include <QPair>
#include <QString>
#include <QVector>

#include <memory>
#include <vector>

class QCheckBox;
class QComboBox;
class QLabel;
class QPushButton;
class QRadioButton;
class QTableWidget;

namespace calango::gui {

class ViewportWidget;

/// Analysis → "Partial Charge Analysis…": estimate per-atom net charges with a
/// choice of partitioning scheme, tabulate them (index, element, q, atomic
/// volume) and colour-map the 3D canvas by charge.
///
/// THE ENGINE MATTERS, AND ONLY THROUGH THE DENSITY
///
/// All three partitioning schemes here are native: they operate on a
/// standardized 3D density grid and know nothing about the code that produced
/// it. What differs per engine is only how that grid is OBTAINED —
///
///   GPAW    calc.get_all_electron_density() from the run's `.gpw`
///   VASP    CHGCAR (valence) or AECCAR0 + AECCAR2 (all-electron), read
///           through ase.calculators.vasp.VaspChargeDensity
///   QE      pp.x (plot_num = 0) exports the density to a Gaussian cube,
///           which is then read like any other cube
///
/// so the engine selector picks an acquisition path and nothing else. Bader in
/// particular NEEDS the all-electron density: partitioned on the pseudo-valence
/// density alone, the basins are placed by the wrong topology and the charges
/// come out systematically small. For VASP that means the run must have written
/// AECCAR0/AECCAR2 (`LAECHG = .TRUE.`), which the VASP settings group exposes.
///
/// SCOPE
///
/// A partitioning can be run for the displayed frame alone or for every frame
/// of the active trajectory. The second is not a loop over the first: it needs
/// one converged density PER FRAME, so it is only meaningful against a source
/// that produced them (an MD run with periodic density dumps). The dialog says
/// so rather than silently reusing one frame's density for all of them.
///
/// The results can be written back onto the structure as `initial_charges` —
/// the name ASE reads and writes — so saving the trajectory as extended XYZ
/// carries the charges as a column of the file.
class PartialChargeDialog : public QDialog {
    Q_OBJECT

public:
    PartialChargeDialog(std::shared_ptr<core::Structure> structure,
                        ViewportWidget* viewport, QWidget* parent = nullptr);

    /// Populate the density-source selector with completed processes that hold
    /// a calculated charge density. Each entry is (display label, absolute path
    /// to the origin process directory).
    void setDensityBaselines(const QList<QPair<QString, QString>>& baselines);

    /// Tell the dialog how many trajectory frames the active document has, so
    /// the "all frames" scope can say what it would cover — and disable itself
    /// when there is only one frame to cover.
    void setTrajectoryFrameCount(int frames);

Q_SIGNALS:
    /// Generated analysis script + a task label; the controller stages and
    /// runs it through the normal local-job path.
    void runRequested(const QString& script, const QString& label);

    /// Loaded charges, ready to be written onto the document. `wholeTrajectory`
    /// repeats the same array across every frame — legitimate only when the
    /// charges describe a geometry that does not change between them, which is
    /// why the dialog asks rather than assuming.
    ///
    /// The host owns the undo stack and the file, so it does the writing; this
    /// dialog holds a working structure and has no business saving anything.
    void chargesApplied(const QVector<double>& charges, bool wholeTrajectory);

private Q_SLOTS:
    void runAnalysis();
    void loadResults();
    /// "Write into the trajectory": push the loaded charges onto the document
    /// as `initial_charges`, so a save carries them as an extended-XYZ column.
    void applyToTrajectory();

private:
    enum class Method { Bader, Voronoi, Hirshfeld };
    /// Which acquisition path the script takes. Auto reads the process
    /// directory and decides; the explicit entries are for a directory holding
    /// output from more than one engine, where guessing would be a coin toss.
    enum class Engine { Auto, Gpaw, Vasp, Espresso };

    Method currentMethod() const;
    Engine currentEngine() const;
    bool wholeTrajectory() const;

    /// Method-specific ASE script writing partial_charges.json into the job
    /// directory.
    QString generateScript(Method method) const;
    /// The Python that defines `atoms` and a 3D `rho` grid for the selected
    /// engine, plus (for the trajectory scope) the frame loop around it.
    QString densityAcquisition() const;
    /// Populate the table from parsed results and, when requested, push the
    /// charges onto the structure as a scalar field and colour the viewport.
    void loadResultsFile(const QString& path);
    /// Restate the loaded charges as the numbers a reader needs: the net
    /// charge (which must be ~0 for a neutral cell, and is the first sign that
    /// a partitioning went wrong), the range, and the per-element means.
    void refreshChargeSummary();

    std::shared_ptr<core::Structure> structure_;
    ViewportWidget* viewport_;

    QComboBox* baselineCombo_ = nullptr; ///< density-source (origin process)
    QComboBox* engineCombo_ = nullptr;
    QComboBox* methodCombo_ = nullptr;
    QRadioButton* scopeCurrentRadio_ = nullptr;
    QRadioButton* scopeTrajectoryRadio_ = nullptr;
    QLabel* scopeNote_ = nullptr;
    QLabel* infoLabel_ = nullptr;
    QTableWidget* table_ = nullptr;
    QLabel* chargeSummary_ = nullptr;
    QCheckBox* colorCheck_ = nullptr;
    QPushButton* applyButton_ = nullptr;

    /// The most recently loaded per-atom charges, index-aligned with the
    /// structure. Empty until results are read.
    std::vector<double> charges_;
    int frameCount_ = 1;
};

} // namespace calango::gui
