#pragma once

#include "core/Structure.hpp"
#include "python_bridge/SurfaceScience.hpp"

#include <QDialog>

#include <memory>
#include <vector>

class QSlider;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QRadioButton;
class QTableWidget;

namespace calango::gui {

/// Modules → "Adsorption & Catalysis": two tabs over one slab.
///
///   1. Site Identification & Geometry Generation — detect the high-symmetry
///      sites (top / bridge / fcc / hcp hollows), place an adsorbate on the
///      chosen ones, and decide whether each site becomes its own workspace
///      tab or all of them become frames of one trajectory.
///   2. Coverage — populate a site family to a target fractional coverage,
///      subject to a minimum adsorbate-adsorbate separation.
///
/// Accepting exposes the generated structures via outputs(); outputMode()
/// says how the caller should present them.
class AdsorptionDialog : public QDialog {
    Q_OBJECT

public:
    struct Output {
        QString name;
        std::shared_ptr<core::Structure> structure;
    };

    /// How the caller should surface `outputs()`.
    enum class OutputMode {
        IndividualTabs,   ///< one workspace tab per structure
        SingleTrajectory, ///< all structures as frames of one tab
    };

    explicit AdsorptionDialog(std::shared_ptr<const core::Structure> slab,
                              QWidget* parent = nullptr);

    const std::vector<Output>& outputs() const { return outputs_; }
    OutputMode outputMode() const { return outputMode_; }

private Q_SLOTS:
    void placeOnSelection();
    void generateCoverageSeries();

private:
    void refreshTable();
    /// Greedy subset of `pool` of size `want` in which no two chosen sites are
    /// closer than `minSeparation` (minimum-image, in the slab plane).
    /// Returns fewer than `want` when the constraint cannot be met.
    std::vector<pybridge::SurfaceScience::AdsorptionSite> spreadSites(
        const std::vector<pybridge::SurfaceScience::AdsorptionSite>& pool,
        std::size_t want, double minSeparation) const;
    std::vector<pybridge::SurfaceScience::AdsorptionSite>
    sitesOfType(const std::string& type) const;
    QString adsorbateName() const;

    std::shared_ptr<const core::Structure> slab_;
    std::vector<pybridge::SurfaceScience::AdsorptionSite> sites_;
    std::vector<Output> outputs_;

    QLabel* summaryLabel_;
    QComboBox* filterCombo_;
    QTableWidget* table_;
    QComboBox* adsorbateCombo_;
    QDoubleSpinBox* heightSpin_;
    QComboBox* coverageSiteCombo_;
    QDoubleSpinBox* coverageSpin_;
    QSlider* coverageSlider_;
    QLabel* statusLabel_;
    QDoubleSpinBox* minSeparationSpin_ = nullptr;
    QRadioButton* individualTabsRadio_ = nullptr;
    QRadioButton* trajectoryRadio_ = nullptr;
    OutputMode outputMode_ = OutputMode::IndividualTabs;
};

} // namespace calango::gui
