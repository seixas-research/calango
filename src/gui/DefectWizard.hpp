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

/// Electronics → "Charged defects…": formation energies E_f(q, E_F), the
/// thermodynamic transition levels, and the charged-defect diagram.
///
/// Two completed Single-Point Calculations are inherited, and the distinction
/// between them is the whole method:
///
///   • the PRISTINE host supercell — supplies E_tot[host], the valence-band
///     maximum the Fermi level is measured from, and the reference potential
///     the FNV alignment is taken against;
///   • the NEUTRAL DEFECT supercell — supplies the geometry every charge
///     state is evaluated at, and q = 0 itself.
///
/// Everything else is derived: the charged states are run here, at fixed
/// geometry and with the neutral run's own parameters, so that the charge is
/// the only thing that differs between the total energies being subtracted.
///
/// Two inputs cannot be derived and must be supplied. The macroscopic
/// dielectric constant ε sets the size of the FNV correction, and the chemical
/// potentials μ_i encode the growth condition — the same defect has different
/// formation energies under metal-rich and anion-rich growth, and no
/// calculation on the supercell alone can know which the user means.
class DefectWizard : public SimulationWizardBase {
    Q_OBJECT

public:
    DefectWizard(std::shared_ptr<const core::Structure> structure,
                 QWidget* parent = nullptr);

    /// Completed Single-Point runs that saved a `.gpw`, as (label, absolute
    /// path). Fills BOTH selectors — which run is the host and which is the
    /// defect is the user's to say, since nothing in the file distinguishes
    /// them.
    void setDensityBaselines(const QList<QPair<QString, QString>>& baselines);

protected:
    QString wizardTitle() const override;
    QStringList calculatorElements() const override;
    QString settingsHeader() const override
    {
        return tr("Defect, Reservoirs && Correction");
    }
    QWidget* buildSettingsPage() override;
    QString generateScript() const override;
    QString exportFileName() const override
    {
        return QStringLiteral("charged_defects.py");
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
    /// Re-derive the species table from the two selected supercells, so the
    /// common cases (a vacancy, an interstitial, a substitution) arrive
    /// filled in rather than as an empty table the user has to decode.
    void suggestSpecies();
    void addSpeciesRow(const QString& symbol, int count, double mu);
    /// Warn when the two selections look wrong — the same run picked twice, or
    /// cells of different size, which would make E_tot[X] − E_tot[host]
    /// meaningless.
    void refreshConsistencyNote();

    std::shared_ptr<const core::Structure> structure_;

    QComboBox* pristineCombo_ = nullptr;
    QComboBox* neutralCombo_ = nullptr;
    QLabel* consistencyNote_ = nullptr;
    QLineEdit* chargesEdit_ = nullptr;
    QTableWidget* speciesTable_ = nullptr;
    QPushButton* addSpeciesButton_ = nullptr;
    QPushButton* removeSpeciesButton_ = nullptr;
    QDoubleSpinBox* epsilonSpin_ = nullptr;
    QSpinBox* defectIndexSpin_ = nullptr;
    QDoubleSpinBox* rcSpin_ = nullptr;
    QDoubleSpinBox* ravgSpin_ = nullptr;
    QDoubleSpinBox* modelCutoffSpin_ = nullptr;
    QCheckBox* fnvCheck_ = nullptr;
    QSpinBox* fermiPointsSpin_ = nullptr;
};

} // namespace calango::gui
