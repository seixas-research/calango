#pragma once

#include "core/HubbardScriptGenerator.hpp"
#include "core/Structure.hpp"
#include "gui/GpawElectronicWizard.hpp"

#include <memory>

class QComboBox;
class QFormLayout;
class QLabel;
class QLineEdit;
class QSpinBox;
class QTableWidget;

namespace calango::gui {

/// Electronics → "Hubbard Parameter Calculation…".
///
/// Computes U from first principles by linear response (Cococcioni & de
/// Gironcoli, Phys. Rev. B 71, 035105 (2005)) rather than asking the user to
/// supply one. The physics is described on core::HubbardRunConfig; what
/// matters for the UI is that the answer depends on three choices the user has
/// to make and that no default can make for them:
///
///   * WHICH SITE. U is a property of a manifold on an atom, not of an
///     element, and two crystallographically inequivalent atoms of the same
///     element have different ones. The site table is therefore explicit.
///   * HOW BIG A SUPERCELL. The perturbation is applied to every periodic
///     image at once, so a cell that is too small measures the response to a
///     lattice of perturbations. This is the convergence parameter of the
///     whole method.
///   * HOW LARGE THE α. Large enough to move the occupation out of the noise,
///     small enough that the response is still linear. The two pull opposite
///     ways, which is why the script reports the fit residual.
///
/// Three stages: the linear-response setup, the standard Calculator &
/// Convergence page, then the script review. The calculator page is the
/// Single-point one — deliberately, because every step of the pipeline IS a
/// single point, and a run whose perturbed and unperturbed SCFs were converged
/// differently would produce a difference dominated by that rather than by the
/// physics.
///
/// VASP and Quantum ESPRESSO only: they are the two engines here that can
/// apply a potential shift to a named site's Hubbard projectors, which is the
/// one primitive the method needs.
class HubbardUWizard : public GpawElectronicWizard {
    Q_OBJECT

public:
    explicit HubbardUWizard(std::shared_ptr<const core::Structure> structure,
                            QWidget* parent = nullptr);

protected:
    QString wizardTitle() const override;
    QString settingsHeader() const override
    {
        return tr("Hubbard Sites & Linear Response");
    }
    QWidget* buildSettingsPage() override;
    QString calculatorSettingsHeader() const override
    {
        return tr("Calculator & Convergence Settings");
    }
    bool calculatorAllowed(core::CalculatorKind kind) const override
    {
        return core::hubbardSupportsCalculator(kind);
    }
    QStringList calculatorElements() const override;


    QString generateScript() const override;
    QString exportFileName() const override
    {
        return QStringLiteral("hubbard_u.py");
    }
    void goNext() override;

private:
    core::HubbardRunConfig config() const;
    /// Parse the α field: comma- or whitespace-separated eV values, zero
    /// dropped (the unperturbed run supplies that point).
    std::vector<double> alphas() const;
    /// Restate what the current setup costs and warns about. Recomputed on
    /// every change, because the run count is the product of three controls
    /// and nobody multiplies them in their head.
    void updateSummary();
    /// Fill the site table with the transition-metal / lanthanide atoms of the
    /// structure, pre-ticking the first of each inequivalent element — the
    /// atoms a U is almost always wanted for.
    void populateSites();

    std::shared_ptr<const core::Structure> structure_;

    QTableWidget* siteTable_ = nullptr;
    QSpinBox* supercellSpins_[3] = {nullptr, nullptr, nullptr};
    QLineEdit* alphaEdit_ = nullptr;
    QComboBox* shellCombo_ = nullptr;
    QLabel* summaryLabel_ = nullptr;

};

} // namespace calango::gui
