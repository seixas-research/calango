#pragma once

#include "core/Structure.hpp"
#include "core/XasScriptGenerator.hpp"
#include "gui/GpawElectronicWizard.hpp"

#include <memory>

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QFormLayout;
class QLabel;
class QSpinBox;

namespace calango::gui {

/// Electronics → "X-ray Absorption Spectroscopy (XAS)…".
///
/// Two stages: the XAS settings (which atom absorbs, what kind of core hole,
/// how the spectrum is broadened), then the standard Calculator & Convergence
/// page and the script review.
///
/// The core-hole configuration is the substance of the module rather than a
/// detail. A core-level spectrum needs a PAW dataset with a hole in the core
/// level, none ships with GPAW, and which hole you make changes the answer:
/// half a hole is the transition-potential approximation most published XAS
/// uses, a full hole is the excited final state proper, and no hole shows what
/// the spectrum would be if the core hole did not pull the excited states
/// down. The generated script builds the dataset for whichever is chosen.
///
/// GPAW only: `gpaw.xas` is the implementation, and it additionally requires
/// GPAW's LEGACY engine — the script clears GPAW_NEW and asks for
/// `legacy_gpaw=True`, which no other Calango script does.
class XasWizard : public GpawElectronicWizard {
    Q_OBJECT

public:
    explicit XasWizard(std::shared_ptr<const core::Structure> structure,
                       QWidget* parent = nullptr);

protected:
    QString wizardTitle() const override;
    QString settingsHeader() const override
    {
        return tr("Absorbing Site & Core Hole");
    }
    QWidget* buildSettingsPage() override;
    QString calculatorSettingsHeader() const override
    {
        return tr("Calculator & Convergence Settings");
    }
    bool calculatorAllowed(core::CalculatorKind kind) const override
    {
        // gpaw.xas is the implementation; nothing else here can make a
        // core-hole dataset, let alone evaluate the dipole matrix elements.
        return kind == core::CalculatorKind::Gpaw;
    }
    QStringList calculatorElements() const override;


    QString generateScript() const override;
    QString exportFileName() const override { return QStringLiteral("xas.py"); }

private Q_SLOTS:
    /// The absorbing element changed: refill the atom selector with the sites
    /// of that species only.
    void onElementChanged();

private:
    core::XasRunConfig runConfig() const;
    /// Enable the broadening-ramp and delta-Kohn-Sham rows only when they mean
    /// something.
    void updateEnabled();

    std::shared_ptr<const core::Structure> structure_;

    QComboBox* elementCombo_ = nullptr;
    QComboBox* atomCombo_ = nullptr;
    QComboBox* levelCombo_ = nullptr;
    QComboBox* holeCombo_ = nullptr;
    QSpinBox* bandsSpin_ = nullptr;
    QDoubleSpinBox* fwhmSpin_ = nullptr;
    QCheckBox* linBroadCheck_ = nullptr;
    QDoubleSpinBox* linBroadFwhm_ = nullptr;
    QDoubleSpinBox* linBroadStart_ = nullptr;
    QDoubleSpinBox* linBroadStop_ = nullptr;
    QCheckBox* dksCheck_ = nullptr;
    QDoubleSpinBox* dksSpin_ = nullptr;
    QLabel* note_ = nullptr;

};

} // namespace calango::gui
