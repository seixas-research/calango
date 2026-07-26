#pragma once

#include "core/AdsorptionSites.hpp"
#include "core/Structure.hpp"

#include <QDialog>
#include <QString>

#include <memory>
#include <vector>

class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QRadioButton;
class QTabWidget;

namespace calango::gui {

/// Build → "Add adsorbate…": put one adsorbate on the current geometry.
///
/// Two tabs, because adding a single atom and adding a molecule are different
/// questions:
///
///   1. "Single Atom" — pick an element and say where it goes. This is the
///      dopant / vacancy-filler / lone adatom case, where there is nothing to
///      orient.
///   2. "Molecule / Radical" — pick from ASE's molecule database (extended with
///      the open-shell fragments surface science actually binds: OH, OOH, CH3,
///      NH2 …), choose which of its atoms faces the surface, and turn it
///      (tilt / azimuth / roll). CO stands on end and benzene lies flat; an
///      "upright" default that cannot be overridden is wrong half the time.
///
/// Both tabs share the same placement question: a detected high-symmetry site
/// (top / bridge / fcc / hcp hollow) at a chosen height along that site's
/// OUTWARD normal — which follows a curved nanoparticle facet, not a hardcoded
/// +z — or an explicit Cartesian position for full manual control.
///
/// Accepting leaves the combined geometry in result(); the host opens it as a
/// NEW workspace tab rather than mutating the current one, so the clean surface
/// stays available for the next adsorbate.
class AddAdsorbateDialog : public QDialog {
    Q_OBJECT

public:
    explicit AddAdsorbateDialog(std::shared_ptr<const core::Structure> substrate,
                                QWidget* parent = nullptr);

    /// Substrate + placed adsorbate. Null until the dialog is accepted.
    std::shared_ptr<core::Structure> result() const { return result_; }
    /// Tab title for the generated structure, e.g. "Pt36 + CO (fcc)".
    QString resultName() const { return resultName_; }

public Q_SLOTS:
    void accept() override;

private Q_SLOTS:
    /// Resolve the typed/selected molecule name and refresh the anchor combo
    /// and the composition preview.
    void refreshMolecule();
    void updatePreview();

private:
    /// The site / Cartesian placement controls, built once per tab.
    struct Placement {
        QRadioButton* siteRadio = nullptr;
        QRadioButton* cartesianRadio = nullptr;
        QComboBox* siteCombo = nullptr;
        QDoubleSpinBox* heightSpin = nullptr;
        QDoubleSpinBox* coordSpin[3] = {nullptr, nullptr, nullptr};
    };

    QWidget* buildAtomTab();
    QWidget* buildMoleculeTab();
    /// A "Placement" group box wired into `placement`.
    QWidget* buildPlacementGroup(QWidget* parent, Placement& placement);
    /// The site the given placement selects — either a detected one, or a
    /// synthetic +z site at the typed Cartesian position.
    core::AdsorptionSite resolveSite(const Placement& placement) const;
    /// Height along the normal for the given placement (0 for Cartesian, which
    /// already names the exact spot).
    double resolveHeight(const Placement& placement) const;
    /// The adsorbate the active tab describes, plus its anchor. An empty
    /// structure means the molecule name could not be resolved.
    core::Structure adsorbate(int& anchorIndex, QString& label) const;
    /// Whether the molecule tab (rather than the single-atom tab) is showing.
    bool moleculeTabActive() const;

    std::shared_ptr<const core::Structure> substrate_;
    std::vector<core::AdsorptionSite> sites_;
    /// The molecule last resolved from the database, cached so the anchor combo
    /// and the preview do not re-enter Python on every keystroke.
    core::Structure moleculeTemplate_;
    int moleculeAnchor_ = 0;
    QString moleculeName_;

    std::shared_ptr<core::Structure> result_;
    QString resultName_;

    QTabWidget* tabs_ = nullptr;

    // Single-atom tab
    QComboBox* elementCombo_ = nullptr;
    Placement atomPlacement_;

    // Molecule tab
    QComboBox* moleculeCombo_ = nullptr;
    QComboBox* anchorCombo_ = nullptr;
    QDoubleSpinBox* tiltSpin_ = nullptr;
    QDoubleSpinBox* azimuthSpin_ = nullptr;
    QDoubleSpinBox* rollSpin_ = nullptr;
    QLabel* moleculeInfoLabel_ = nullptr;
    Placement moleculePlacement_;

    QLabel* previewLabel_ = nullptr;
};

} // namespace calango::gui
