#pragma once

#include <QWidget>

#include <utility>
#include <vector>

namespace calango::gui {

/// A weight-binned histogram of a parent calculation's Kohn-Sham eigenvalue
/// spectrum, with TWO draggable vertical handles bounding a shaded energy
/// window — the LDOS module's energy-range selector. Extends the single-
/// handle drag pattern IsovalueHistogramWidget uses for an isovalue to two
/// handles bounding a range.
///
/// The window is always stored and reported in ABSOLUTE eV — "relative to
/// Fermi" is a DISPLAY convention only (axis labels read "E - E_F" and the
/// Fermi line sits at the labelled zero), so a caller never has to guess
/// which convention a signal's payload uses.
class EnergyWindowWidget : public QWidget {
    Q_OBJECT

public:
    /// One Kohn-Sham state, already resolved to a single number the
    /// histogram bins by — the caller has already decided which spin
    /// channel(s) contribute (this widget draws one spectrum, not per-
    /// channel ones).
    struct Level {
        double energyEv = 0.0;
        double weight = 0.0; ///< k-point weight; degenerate/heavy levels
                             ///< bin taller, exactly like a real DOS
    };

    explicit EnergyWindowWidget(QWidget* parent = nullptr);

    /// Replace the spectrum. `efermi` positions the dashed Fermi line and is
    /// the reference `setRelativeToFermi(true)` labels against.
    void setLevels(const std::vector<Level>& levels, double efermiEv);

    /// Axis-label convention only; the stored window stays absolute.
    void setRelativeToFermi(bool relative);
    bool relativeToFermi() const { return relativeToFermi_; }

    /// Programmatic window set (e.g. from the wizard's spin boxes, or a
    /// preset button) — absolute eV. Clamped so min <= max. Does not
    /// re-emit windowChanged (the caller already knows).
    void setWindow(double minEv, double maxEv);
    std::pair<double, double> window() const { return {min_, max_}; }

    double fermiLevel() const { return efermi_; }

    /// While a background "read the baseline's eigenvalues" step is
    /// running, show `message` instead of a (necessarily empty) plot —
    /// there is nothing to drag yet. Cleared by the next setLevels().
    void setLoading(bool loading, const QString& message = QString());

Q_SIGNALS:
    /// A drag moved a handle. Always absolute eV, regardless of
    /// relativeToFermi() — convert at the call site if the bound spin box
    /// displays a Fermi-relative number.
    void windowChanged(double minEv, double maxEv);

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;

private:
    enum class Handle { None, Min, Max };

    QRectF plotRect() const;
    double valueFromX(double x) const;
    double xFromValue(double v) const;
    Handle nearestHandle(double x) const;
    void dragTo(double x);
    void rebuildHistogram();

    std::vector<Level> levels_;
    double efermi_ = 0.0;
    double dataMin_ = -1.0;
    double dataMax_ = 1.0;
    double min_ = -0.5;
    double max_ = 0.5;
    bool relativeToFermi_ = true;
    bool loading_ = false;
    QString loadingMessage_;

    std::vector<double> counts_; ///< weight-binned; see rebuildHistogram()
    double maxCount_ = 0.0;
    Handle activeDrag_ = Handle::None;
};

} // namespace calango::gui
