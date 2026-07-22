#pragma once

#include <QWidget>

#include <map>
#include <vector>

namespace calango::gui {

/// Hand-painted two-pane electronic-structure plot: band structure
/// (energy vs. k-path distance, high-symmetry tick labels, dashed Fermi
/// reference at E − E_F = 0) side by side with the projected density of
/// states sharing the energy axis. Follows the LinePlotWidget precedent
/// (QPainter, no external plotting dependency).
class BandPdosView : public QWidget {
    Q_OBJECT

public:
    struct BandData {
        std::vector<double> x;        ///< cumulative k-path distance
        std::vector<double> specialX; ///< positions of high-symmetry points
        QStringList specialLabels;    ///< "G", "X", ... ("G" renders as Γ)
        /// energies[spin][kpoint][band] (eV, absolute)
        std::vector<std::vector<std::vector<double>>> energies;
        double efermi = 0.0;
        bool valid() const { return !x.empty() && !energies.empty(); }
    };
    struct PdosData {
        std::vector<double> energies; ///< eV, absolute
        /// label ("Si p") -> DOS curve, insertion-ordered
        std::vector<std::pair<QString, std::vector<double>>> projections;
        bool valid() const { return !energies.empty() && !projections.empty(); }
    };

    explicit BandPdosView(QWidget* parent = nullptr);

    void setBandData(BandData data);
    void setPdosData(PdosData data);
    const BandData& bandData() const { return bands_; }
    const PdosData& pdosData() const { return pdos_; }

    /// Reference energy: plots show E − reference (default: file E_F).
    void setReference(double referenceEv);
    void setEnergyWindow(double minEv, double maxEv); ///< relative to reference
    void setProjectionVisible(const QString& label, bool visible);

    /// Switch to phonon semantics: the vertical axis is frequency in cm⁻¹
    /// (not energy relative to a Fermi level), the reference is 0, and the
    /// horizontal reference line marks ω = 0 (the acoustic modes). The band
    /// energies are then interpreted as frequencies and the PDOS as PhDOS.
    void setPhononMode(bool on);

    /// Color used for a projection curve (legend checkboxes reuse it).
    static QColor projectionColor(int index);

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    void paintBands(QPainter& painter, const QRectF& rect);
    void paintPdos(QPainter& painter, const QRectF& rect);

    BandData bands_;
    PdosData pdos_;
    std::map<QString, bool> visible_;
    double reference_ = 0.0;
    double eMin_ = -10.0;
    double eMax_ = 10.0;
    bool phonon_ = false; ///< frequency (cm⁻¹) semantics instead of energy/eV
};

} // namespace calango::gui
