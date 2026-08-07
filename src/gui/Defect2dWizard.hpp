#pragma once

#include "gui/SimulationWizardBase.hpp"

#include <QList>
#include <QPair>
#include <QString>

#include <memory>

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QSpinBox;
class QTableWidget;

namespace calango::core {
class Structure;
}

namespace calango::gui {

/// Modules → 2D Materials → "Charged Defects in 2D Materials…".
///
/// The same quantity as the bulk DefectWizard — E_f[X^q](E_F), the transition
/// levels, the diagram — inheriting the same two Single-Point runs. What is
/// different is the correction, and it is different in kind rather than in
/// size, which is why this is a separate module rather than a checkbox on the
/// bulk one.
///
/// A charged defect in a SLAB supercell has no scalar ε to divide by: the
/// medium is the sheet inside and vacuum outside. Worse, its energy against the
/// neutralizing background diverges with the vacuum thickness instead of
/// converging, so "add vacuum until it settles" never terminates and the 1/(εL)
/// form FNV removes is not merely inaccurate here — it is the wrong functional
/// form. This wizard therefore collects the SHEET's dielectric profile rather
/// than a dielectric constant, and the run solves Poisson twice in it.
///
/// The inputs that cannot be derived, and so must be asked for: ε_∥ and ε_⊥ of
/// the sheet (not of the slab, which is diluted by whatever vacuum was used),
/// its effective thickness, and the chemical potentials that encode the growth
/// condition.
class Defect2dWizard : public SimulationWizardBase {
    Q_OBJECT

public:
    Defect2dWizard(std::shared_ptr<const core::Structure> structure,
                   QWidget* parent = nullptr);

    /// Completed Single-Point runs that saved a `.gpw`, as (label, absolute
    /// path). Fills both selectors — nothing in a `.gpw` says which one has the
    /// vacancy, so which is the host is the user's to say.
    void setDensityBaselines(const QList<QPair<QString, QString>>& baselines);

protected:
    QString wizardTitle() const override;
    QStringList calculatorElements() const override;
    QString settingsHeader() const override
    {
        return tr("Sheet, Reservoirs && 2D Correction");
    }
    QWidget* buildSettingsPage() override;
    QString generateScript() const override;
    QString exportFileName() const override
    {
        return QStringLiteral("charged_defects_2d.py");
    }
    /// The charged SCFs inherit the neutral defect's calculator wholesale, so
    /// there is nothing for the engine/DFT stage to set.
    bool showsEngineAndDftControls() const override { return false; }
    bool inheritsCalculatorFromBaseline() const override { return true; }
    bool calculatorAllowed(core::CalculatorKind kind) const override
    {
        return kind == core::CalculatorKind::Gpaw;
    }

private:
    void addSpeciesRow(const QString& symbol, int count, double mu);
    /// Offer the open structure's species with count 0, so the common cases
    /// arrive mostly filled in without ever guessing the signed counts — which
    /// is the part that would be wrong.
    void suggestSpecies();
    /// Warn when the two selections look wrong (same run picked twice), and
    /// when the dielectric profile has been left isotropic — for a monolayer
    /// ε_⊥ ≈ ε_∥ is not a default, it is a statement about the material that is
    /// usually false.
    void refreshNotes();

    std::shared_ptr<const core::Structure> structure_;

    QComboBox* pristineCombo_ = nullptr;
    QComboBox* neutralCombo_ = nullptr;
    QLabel* consistencyNote_ = nullptr;
    QLineEdit* chargesEdit_ = nullptr;
    QTableWidget* speciesTable_ = nullptr;
    QComboBox* normalCombo_ = nullptr;
    QDoubleSpinBox* epsParSpin_ = nullptr;
    QDoubleSpinBox* epsPerpSpin_ = nullptr;
    QDoubleSpinBox* thicknessSpin_ = nullptr;
    QDoubleSpinBox* interfaceSpin_ = nullptr;
    QSpinBox* defectIndexSpin_ = nullptr;
    QDoubleSpinBox* sigmaSpin_ = nullptr;
    QSpinBox* zComponentsSpin_ = nullptr;
    QSpinBox* inPlaneSpin_ = nullptr;
    QCheckBox* correctionCheck_ = nullptr;
    QSpinBox* fermiPointsSpin_ = nullptr;
    QLabel* profileNote_ = nullptr;
};

} // namespace calango::gui
