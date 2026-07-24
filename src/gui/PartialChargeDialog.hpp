#pragma once

#include "core/Structure.hpp"

#include <QDialog>

#include <memory>

class QCheckBox;
class QComboBox;
class QLabel;
class QTableWidget;

namespace calango::gui {

class ViewportWidget;

/// Analysis → "Partial Charge Analysis…": estimate per-atom net charges with a
/// choice of partitioning scheme, tabulate them (index, element, q, atomic
/// volume) and colour-map the 3D canvas by charge.
///
/// The partitioning is a DFT post-process, so it runs as a background job: the
/// dialog generates the ASE/GPAW script and emits runRequested() for the
/// controller to launch; when the job's partial_charges.json is ready the user
/// loads it back in ("Load Results…") to populate the table and (optionally)
/// tint the atoms by charge.
class PartialChargeDialog : public QDialog {
    Q_OBJECT

public:
    PartialChargeDialog(std::shared_ptr<core::Structure> structure,
                        ViewportWidget* viewport, QWidget* parent = nullptr);

Q_SIGNALS:
    /// Generated analysis script + a task label; the controller stages and
    /// runs it through the normal local-job path.
    void runRequested(const QString& script, const QString& label);

private Q_SLOTS:
    void runAnalysis();
    void loadResults();

private:
    enum class Method { Bader, Voronoi, Hirshfeld };
    Method currentMethod() const;
    /// Method-specific ASE/GPAW script writing partial_charges.json into the
    /// job directory.
    QString generateScript(Method method) const;
    /// Populate the table from parsed results and, when requested, push the
    /// charges onto the structure as a scalar field and colour the viewport.
    void loadResultsFile(const QString& path);

    std::shared_ptr<core::Structure> structure_;
    ViewportWidget* viewport_;

    QComboBox* methodCombo_ = nullptr;
    QLabel* infoLabel_ = nullptr;
    QTableWidget* table_ = nullptr;
    QCheckBox* colorCheck_ = nullptr;
};

} // namespace calango::gui
