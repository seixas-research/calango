#pragma once

#include <QColor>
#include <QPen>
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

    /// Everything the "Customize Appearance…" dialog exposes. Defaults
    /// reproduce the plot's previous hardcoded look, so an untouched viewer
    /// is unchanged.
    struct Style {
        // -- Typography (points) -------------------------------------------
        /// Axis titles match the tick labels by default: a title set larger
        /// than the numbers it labels reads as a mismatch, and both are
        /// already scaled up for presentation legibility.
        double tickPointSize = 15.0;
        double axisTitlePointSize = 15.0;
        double annotationPointSize = 13.0; ///< high-symmetry labels, gap notes

        // -- Dispersion curves ---------------------------------------------
        /// One color per spin channel (electronic) or a two-tone palette for
        /// phonon branches.
        QColor bandColors[2] = {QColor(102, 163, 255), QColor(235, 110, 96)};
        Qt::PenStyle bandPenStyle = Qt::SolidLine;
        double bandLineWidth = 1.4;

        // -- Reference line (E_F for electrons, omega = 0 for phonons) ------
        bool showFermi = true;
        QColor fermiColor{255, 199, 88};
        Qt::PenStyle fermiPenStyle = Qt::DashLine;
        double fermiLineWidth = 1.4;

        // -- Plot chrome ----------------------------------------------------
        QColor background{24, 26, 30};
        QColor spineColor{120, 124, 134};
        double spineWidth = 1.2;
        double tickWidth = 1.0;
        QColor gridColor{70, 74, 84};
        QColor textColor{210, 213, 220};

        /// Fill under the DOS curves (phonon PhDOS reads better filled).
        bool fillDos = false;
        int dosFillAlpha = 70;
    };

    explicit BandPdosView(QWidget* parent = nullptr);

    const Style& style() const { return style_; }
    void setStyle(const Style& style);

    /// Render the whole figure (both panels) at an arbitrary size onto any
    /// paint device — used by paintEvent and by the image exporters, so a
    /// PNG/PDF/SVG is pixel-identical to what is on screen.
    void renderTo(QPainter& painter, const QSizeF& size);

    /// Save the figure as PNG / JPEG / PDF / SVG. Raster formats are rendered
    /// at `scale` times the on-screen size for print resolution.
    void exportImage(QWidget* dialogParent);

    void setBandData(BandData data);
    void setPdosData(PdosData data);
    const BandData& bandData() const { return bands_; }
    const PdosData& pdosData() const { return pdos_; }

    /// Reference energy: plots show E − reference (default: file E_F).
    void setReference(double referenceEv);
    /// Whether the vertical axis is captioned as an E_F-relative energy
    /// ("E − E_F (eV)") or an absolute one ("E (eV)"). Purely a label
    /// concern — the shift itself is setReference(). Ignored in phonon mode,
    /// where the axis is a frequency with no Fermi level.
    void setReferenceIsFermi(bool fermiRelative);
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
    /// Caption the energy axis relative to E_F (true) or absolute (false).
    bool referenceIsFermi_ = true;
    Style style_;
};

} // namespace calango::gui
