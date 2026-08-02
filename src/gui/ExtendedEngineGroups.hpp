#pragma once

#include "core/CalculatorConfig.hpp"

#include <QObject>

#include <functional>

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QFormLayout;
class QGroupBox;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QSpinBox;
class QVBoxLayout;
class QWidget;

namespace calango::gui {

/// The per-engine settings groups for ABINIT, FHI-aims, NWChem, OpenMX, FLEUR,
/// CP2K and Amber — one group box each, shown when that engine is selected.
///
/// Held here rather than added to SimulationWizardBase (already ~3000 lines and
/// carrying eight engine groups of its own). The base owns the SHARED
/// controls — the plane-wave cutoff, the k-grid, the smearing and spin rows —
/// which several of these engines do not take at all; what belongs in this
/// class is exactly the part that is specific to one code.
///
/// Each group is built once, hidden, and revealed by updateVisibility(). That
/// is the same contract the base's own groups follow, and it is what lets
/// applyTo() read every field unconditionally: the widgets exist whether or not
/// their engine was ever selected.
class ExtendedEngineGroups : public QObject {
    Q_OBJECT

public:
    explicit ExtendedEngineGroups(QObject* parent = nullptr);

    /// Build every group and append it to `layout`. `onChanged` fires whenever
    /// a control that alters the generated script moves — the wizard passes
    /// its refreshPreview().
    void build(QWidget* parent, QVBoxLayout* layout,
               std::function<void()> onChanged);

    /// Show only the group belonging to `kind` (or none).
    void updateVisibility(core::CalculatorKind kind);
    /// Hide every group, for a wizard that suppresses the whole calculator
    /// chrome (Electronic Structure inherits its engine from a baseline).
    void hideAll();

    /// Copy the widget state into `config`. Unconditional by design: reading
    /// only the selected engine's fields would leave the others at their
    /// struct defaults, and a saved project that switches engine would then
    /// silently lose settings the user had entered.
    void applyTo(core::CalculatorConfig& config) const;

private:
    void buildAbinit(QWidget* parent, QVBoxLayout* layout);
    void buildAims(QWidget* parent, QVBoxLayout* layout);
    void buildNwChem(QWidget* parent, QVBoxLayout* layout);
    void buildOpenMx(QWidget* parent, QVBoxLayout* layout);
    void buildFleur(QWidget* parent, QVBoxLayout* layout);
    void buildCp2k(QWidget* parent, QVBoxLayout* layout);
    void buildAmber(QWidget* parent, QVBoxLayout* layout);

    /// Show only the NWChem rows the selected `theory` actually reads: the
    /// molecular modules take a Gaussian basis and ignore the cell, the
    /// plane-wave ones take k-points and ignore the basis.
    void updateNwChemRows();

    /// A directory row: line edit + Browse, persisted in QSettings under `key`.
    /// Every one of these paths is per-INSTALLATION state (a pseudopotential
    /// library, a species-defaults tree, a data path), so it is remembered
    /// across sessions the way the DFTB+ Slater-Koster directory is.
    QLineEdit* directoryRow(QWidget* parent, QFormLayout* form,
                            const QString& label, const QString& settingsKey,
                            const QString& placeholder, const QString& tooltip);

    std::function<void()> onChanged_;

    // -- ABINIT -------------------------------------------------------------
    QGroupBox* abinitGroup_ = nullptr;
    QComboBox* abinitXcCombo_ = nullptr;
    QComboBox* abinitPpsCombo_ = nullptr;
    QLineEdit* abinitPseudoEdit_ = nullptr;
    QLineEdit* abinitToldfeEdit_ = nullptr;
    QSpinBox* abinitNstepSpin_ = nullptr;
    QPlainTextEdit* abinitExtraEdit_ = nullptr;

    // -- FHI-aims -----------------------------------------------------------
    QGroupBox* aimsGroup_ = nullptr;
    QComboBox* aimsXcCombo_ = nullptr;
    QLineEdit* aimsSpeciesEdit_ = nullptr;
    QComboBox* aimsTierCombo_ = nullptr;
    QComboBox* aimsRelativisticCombo_ = nullptr;
    QLineEdit* aimsAccuracyEdit_ = nullptr;
    QPlainTextEdit* aimsExtraEdit_ = nullptr;

    // -- NWChem -------------------------------------------------------------
    QGroupBox* nwchemGroup_ = nullptr;
    QComboBox* nwchemTheoryCombo_ = nullptr;
    QComboBox* nwchemXcCombo_ = nullptr;
    QComboBox* nwchemBasisCombo_ = nullptr;
    QLineEdit* nwchemMemoryEdit_ = nullptr;
    QLabel* nwchemPeriodicNote_ = nullptr;
    QPlainTextEdit* nwchemExtraEdit_ = nullptr;

    // -- OpenMX -------------------------------------------------------------
    QGroupBox* openmxGroup_ = nullptr;
    QComboBox* openmxXcCombo_ = nullptr;
    QLineEdit* openmxDataEdit_ = nullptr;
    QDoubleSpinBox* openmxCutoffSpin_ = nullptr;
    QDoubleSpinBox* openmxCriterionSpin_ = nullptr;
    QSpinBox* openmxMaxIterSpin_ = nullptr;
    QComboBox* openmxSolverCombo_ = nullptr;

    // -- FLEUR --------------------------------------------------------------
    QGroupBox* fleurGroup_ = nullptr;
    QComboBox* fleurXcCombo_ = nullptr;
    QDoubleSpinBox* fleurKmaxSpin_ = nullptr;
    QLineEdit* fleurRootEdit_ = nullptr;
    QLineEdit* fleurConvEdit_ = nullptr;
    QSpinBox* fleurMaxIterSpin_ = nullptr;

    // -- CP2K ---------------------------------------------------------------
    QGroupBox* cp2kGroup_ = nullptr;
    QComboBox* cp2kXcCombo_ = nullptr;
    QDoubleSpinBox* cp2kCutoffSpin_ = nullptr;
    QDoubleSpinBox* cp2kRelCutoffSpin_ = nullptr;
    QComboBox* cp2kBasisCombo_ = nullptr;
    QLineEdit* cp2kBasisFileEdit_ = nullptr;
    QComboBox* cp2kPseudoCombo_ = nullptr;
    QLineEdit* cp2kPotentialFileEdit_ = nullptr;
    QSpinBox* cp2kMaxScfSpin_ = nullptr;
    QLineEdit* cp2kCommandEdit_ = nullptr;
    QPlainTextEdit* cp2kExtraEdit_ = nullptr;

    // -- Amber --------------------------------------------------------------
    QGroupBox* amberGroup_ = nullptr;
    QLineEdit* amberExeEdit_ = nullptr;
    QLineEdit* amberTopologyEdit_ = nullptr;
    QLineEdit* amberInputEdit_ = nullptr;
};

} // namespace calango::gui
