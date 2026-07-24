#pragma once

#include <QDialog>
#include <QString>

namespace calango::gui {

class SpectralHeatmapWidget;

/// Modeless standalone viewer for a finished Effective Bands (Popescu-Zunger
/// unfolding) job: loads effective_bands.json and shows the unfolded spectral
/// function A(k, E) as an interactive heatmap with colormap, intensity
/// threshold, Gaussian σ, an E − E_F Fermi-shift toggle, and image/data
/// export. Replaces the former Zone-10 "Results" dock tab so the analysis
/// gets its own resizable window.
class EffectiveBandsWindow : public QDialog {
    Q_OBJECT

public:
    explicit EffectiveBandsWindow(const QString& directory,
                                  QWidget* parent = nullptr);

    /// True when effective_bands.json was found and parsed.
    bool hasData() const { return hasData_; }

private:
    SpectralHeatmapWidget* plot_;
    bool hasData_ = false;
};

} // namespace calango::gui
