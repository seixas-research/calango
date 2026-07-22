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
class QTableWidget;

namespace calango::gui {

/// Analysis → "Adsorption & Catalysis": detects high-symmetry adsorption
/// sites (top / bridge / fcc / hcp hollows) on the current slab, places
/// common or custom adsorbates on selected sites at a chosen height, and
/// generates systematic coverage series (0.25–1.0 ML over one site
/// family). Accepting exposes the generated structures via outputs().
class AdsorptionDialog : public QDialog {
    Q_OBJECT

public:
    struct Output {
        QString name;
        std::shared_ptr<core::Structure> structure;
    };

    explicit AdsorptionDialog(std::shared_ptr<const core::Structure> slab,
                              QWidget* parent = nullptr);

    const std::vector<Output>& outputs() const { return outputs_; }

private Q_SLOTS:
    void placeOnSelection();
    void generateCoverageSeries();

private:
    void refreshTable();
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
};

} // namespace calango::gui
