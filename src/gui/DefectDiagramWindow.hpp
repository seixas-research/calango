#pragma once

#include <QColor>
#include <QDialog>
#include <QJsonObject>
#include <QString>
#include <QWidget>

#include <vector>

class QCheckBox;
class QLabel;
class QTableWidget;

namespace calango::gui {

/// The plot canvas of the charged-defect viewer: E_f versus E_F, one straight
/// line per charge state, the lower envelope emphasized, and a marker at every
/// thermodynamic transition level.
///
/// Painted with QPainter rather than built on the spectrum widget: the axes
/// are unlike any other plot in the application (the abscissa is bounded by
/// the host band gap and both edges are meaningful), and the object that
/// carries the physics — the lower envelope with its kinks — has to be drawn
/// on top of the lines it is made from.
class DefectDiagramWidget : public QWidget {
    Q_OBJECT

public:
    explicit DefectDiagramWidget(QWidget* parent = nullptr);

    struct Line {
        int charge = 0;
        std::vector<double> energies; ///< eV, aligned with fermiLevels()
        QColor color;
    };
    struct Transition {
        int fromCharge = 0;
        int toCharge = 0;
        double levelEv = 0.0;      ///< above the VBM
        double formationEv = 0.0;
    };

    void setData(std::vector<double> fermiLevels, std::vector<Line> lines,
                 std::vector<double> envelope,
                 std::vector<Transition> transitions, double gapEv);
    /// Draw only the lower envelope, which is the physically observable
    /// object — the individual charge-state lines above it describe states
    /// that are never the ground state at that Fermi level.
    void setEnvelopeOnly(bool on);
    void setShowTransitions(bool on);

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    std::vector<double> fermi_;
    std::vector<Line> lines_;
    std::vector<double> envelope_;
    std::vector<Transition> transitions_;
    double gapEv_ = 0.0;
    bool envelopeOnly_ = false;
    bool showTransitions_ = true;
};

/// Results viewer for a Charged Defects run: the formation-energy diagram, the
/// transition levels, and the per-charge-state breakdown including the FNV
/// correction terms.
///
/// The breakdown is shown rather than folded away because the correction is
/// the part a reader has to be able to audit: E_corr enters every formation
/// energy directly, it is the largest single approximation in the method, and
/// its three terms (isolated, periodic, alignment) are what reveal an ε that
/// was left at 1 or an averaging region that never reached bulk-like potential.
class DefectDiagramWindow : public QDialog {
    Q_OBJECT

public:
    explicit DefectDiagramWindow(QWidget* parent = nullptr);

    /// Parse a `charged_defects.json`. False (showing nothing) when the file
    /// is missing or malformed.
    bool loadResults(const QString& jsonPath);

private:
    void populateTables();
    void exportImage();
    void exportData();

    QJsonObject data_;
    QString sourcePath_;

    DefectDiagramWidget* plot_ = nullptr;
    QLabel* summary_ = nullptr;
    QLabel* warning_ = nullptr;
    QTableWidget* chargeTable_ = nullptr;
    QTableWidget* transitionTable_ = nullptr;
    QCheckBox* envelopeCheck_ = nullptr;
    QCheckBox* transitionsCheck_ = nullptr;
};

} // namespace calango::gui
