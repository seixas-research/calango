#pragma once

#include <QColor>
#include <QPen>
#include <QWidget>

#include <functional>
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

    /// Orbital weights carried alongside the band energies — the "fatband"
    /// data. One entry per projection channel; `weights[spin][kpoint][band]`
    /// is index-aligned with BandData::energies, so a band and its weight are
    /// read from the same three indices.
    struct FatbandData {
        std::vector<std::pair<QString,
                              std::vector<std::vector<std::vector<double>>>>>
            projections;
        /// Largest weight anywhere, used to normalize the width/opacity
        /// mapping. Shared across channels on purpose: normalizing each
        /// channel to its own maximum would make a channel that contributes
        /// 2% of the state look exactly as strong as one contributing 90%.
        double maxWeight = 1.0;
        bool valid() const { return !projections.empty(); }
    };

    /// How an orbital weight is turned into ink.
    enum class FatbandMode {
        Off,      ///< plain dispersion curves
        Width,    ///< line thickness ∝ weight (the classic fatband)
        Color,    ///< channel colour, opacity ∝ weight
        Both,     ///< thickness AND opacity
    };

    /// One irreducible-representation label to draw beside a high-symmetry
    /// tick: which multiplet, where it sits, and what it is called.
    struct SymmetryLabel {
        double x = 0.0;         ///< k-path coordinate of the point
        double energy = 0.0;    ///< absolute eV
        QString text;           ///< Mulliken symbol, e.g. "E2g", "A1'"
        int degeneracy = 1;
        bool onLine = false;    ///< a symmetry LINE rather than a point
    };
    struct SymmetryData {
        std::vector<SymmetryLabel> labels;
        QString spaceGroup;
        bool valid() const { return !labels.empty(); }
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

        // -- Orbital projections (fatbands) --------------------------------
        /// Extra line width, in pixels, at the maximum orbital weight. The
        /// base `bandLineWidth` is what a zero-weight band keeps, so a band
        /// never disappears — it just stops being fat.
        double fatbandScale = 7.0;
        /// Opacity floor in Color/Both mode. ZERO by design: the colormaps
        /// below run from fully transparent at zero weight to opaque at the
        /// maximum, which is what lets several channels be superimposed
        /// without the first one painted hiding the rest. Raise it only to
        /// make a very weak contribution visible at the cost of that.
        int fatbandMinAlpha = 0;

        // -- Symmetry labels ------------------------------------------------
        QColor symmetryLabelColor{235, 220, 150};
        double symmetryLabelPointSize = 11.0;
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

    void setFatbandData(FatbandData data);
    const FatbandData& fatbandData() const { return fatbands_; }
    void setFatbandMode(FatbandMode mode);
    FatbandMode fatbandMode() const { return fatbandMode_; }
    /// Which projection channels are drawn on top of the dispersion. Several
    /// at once is the point: seeing metal d and ligand p on the same plot is
    /// how hybridization becomes visible.
    void setFatbandChannelVisible(const QString& label, bool visible);

    void setSymmetryData(SymmetryData data);
    const SymmetryData& symmetryData() const { return symmetry_; }
    void setSymmetryLabelsVisible(bool visible);
    /// Draw the labels on the symmetry LINES as well as at the points. Off by
    /// default — the line labels are numerous and the points are what a reader
    /// looks at first.
    void setSymmetryLineLabelsVisible(bool visible);

    /// Opaque, legible stand-in colour for fatband channel `index` — the
    /// swatch its entry gets in the viewer's channel list, and the ink used in
    /// Width mode, where there is no weight-to-colour mapping to sample. Taken
    /// from the middle of the channel's colormap, which is the one stop that
    /// reads clearly on both a light and a dark plot background.
    static QColor fatbandColor(int index);

    /// Channel `index`'s SEQUENTIAL COLORMAP sampled at `t` ∈ [0, 1], where t
    /// is the orbital weight normalized to FatbandData::maxWeight.
    ///
    /// The maps are the ColorBrewer sequentials matplotlib ships — Greens,
    /// Blues, Reds, Oranges, Greys, Purples, in that order — with one
    /// deliberate modification: the alpha channel ramps linearly with t, so
    /// the lowest value is not white but INVISIBLE. That is what makes the
    /// channels superimposable. A plain white low end would paint over every
    /// channel drawn before it and over the dispersion itself, turning a
    /// six-orbital plot into whichever orbital happened to be drawn last.
    ///
    /// `darkBackground` mirrors the ramp's lightness. A sequential colormap is
    /// a luminance ramp AWAY from the page: on white it has to run dark, on a
    /// dark plot background it has to run light, or maximum weight is drawn in
    /// near-black on near-black. Hue and saturation are untouched, so Blues
    /// stays blue either way.
    static QColor fatbandColorAt(int index, double t, bool darkBackground);

    /// Name of that colormap ("Greens", "Blues", …). Shown in the viewer so a
    /// reader can name the mapping in a figure caption.
    static QString fatbandColormapName(int index);

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
    /// Orbital-weight overlay on top of the dispersion already drawn.
    void paintFatbands(QPainter& painter, const QRectF& rect,
                       const std::function<double(double)>& mapX,
                       const std::function<double(double)>& mapY);
    /// Irrep symbols beside the high-symmetry ticks.
    void paintSymmetryLabels(QPainter& painter, const QRectF& rect,
                             const std::function<double(double)>& mapX,
                             const std::function<double(double)>& mapY);

    BandData bands_;
    PdosData pdos_;
    FatbandData fatbands_;
    SymmetryData symmetry_;
    /// Width AND colour by default: the width is the classic fatband, and the
    /// colormap's transparent low end is what keeps several channels legible
    /// on the same axes. Either one alone gives up half of that.
    FatbandMode fatbandMode_ = FatbandMode::Both;
    /// Channel label -> drawn. Absent means visible (matches `visible_`).
    std::map<QString, bool> fatbandVisible_;
    bool symmetryVisible_ = true;
    bool symmetryLineLabels_ = false;
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
