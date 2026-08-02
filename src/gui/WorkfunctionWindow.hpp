#pragma once

#include <QDialog>
#include <QString>

#include <vector>

class QLabel;

namespace calango::gui {

class SpectrumPlotWidget;

/// Results → "2D Workfunction Viewer": the read-out for a completed
/// work-function job. Parses the `workfunction.json` written by
/// WorkfunctionScriptGenerator and shows V̄(z) with the Fermi level and the
/// vacuum level(s) as dashed reference lines.
///
/// The headline is Φ = E_vac − E_F — BOTH faces when they differ. Two
/// different values are physics (a dipole-corrected asymmetric slab has one
/// work function per face); two equal values on an asymmetric slab are an
/// artifact of a missing dipole correction, and the viewer says which case
/// it is showing rather than presenting one number neutrally.
///
/// The plateau-flatness diagnostic |dV̄/dz| at the cell edges is the trust
/// test: the vacuum level is only defined where V̄(z) is flat, so a slope
/// beyond ~5 meV/Å gets the red warning treatment — that Φ would quietly
/// change with the cell size, which is the signature of a vacuum region too
/// thin to be converged.
class WorkfunctionWindow : public QDialog {
    Q_OBJECT

public:
    explicit WorkfunctionWindow(QWidget* parent = nullptr);

    /// Parse a `workfunction.json` and fill the read-outs. Returns false when
    /// the file is missing or malformed.
    bool loadResults(const QString& jsonPath);

private Q_SLOTS:
    void exportCsv();
    void exportImage();

private:
    std::vector<double> z_;
    std::vector<double> potential_;

    QLabel* headlineLabel_ = nullptr;
    QLabel* levelsLabel_ = nullptr;
    QLabel* dipoleNote_ = nullptr;
    QLabel* warningLabel_ = nullptr;
    SpectrumPlotWidget* plot_ = nullptr;
};

} // namespace calango::gui
