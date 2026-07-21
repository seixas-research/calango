#pragma once

#include "core/PhononScriptGenerator.hpp"
#include "core/Structure.hpp"

#include <QComboBox>
#include <QDialog>
#include <QDoubleSpinBox>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSpinBox>

#include <memory>
#include <vector>

namespace calango::gui {

/// "Build → Phonon Builder": finite-displacement vibrational analysis.
///
/// Two outputs, selected by the mode combo:
///   - Generate displaced structures: builds the supercell and applies the
///     ±δ displacements in-app (reference + 6N frames), opening them as a
///     trajectory tab — ready for export to external DFT codes.
///   - Run calculation: generates an ASE script (ase.phonons for periodic
///     systems, ase.vibrations for molecules) that computes forces with
///     the chosen potential (EMT / Lennard-Jones / MACE), assembles force
///     constants and the dynamical matrix, and reports frequencies, band
///     structure and DOS. The script pane is editable, like the
///     calculator dialog, and the job runs through the standard runner.
class PhononBuilderDialog : public QDialog {
    Q_OBJECT

public:
    explicit PhononBuilderDialog(std::shared_ptr<const core::Structure> structure,
                                 QWidget* parent = nullptr);

    /// True when the user chose to only generate displaced structures.
    bool generateDisplacementsOnly() const;

    /// Reference supercell + one frame per (atom, axis, ±δ) displacement.
    /// Requires ASE (supercell construction); throws std::runtime_error.
    std::vector<std::shared_ptr<core::Structure>> buildDisplacedFrames() const;

    core::PhononConfig config() const;
    QString script() const;
    QString pythonExecutable() const;

private Q_SLOTS:
    void refreshPreview();
    void regenerateScript();
    void saveScript();
    void browseMaceModel();

private:
    std::shared_ptr<const core::Structure> structure_;
    bool periodic_ = false;

    QComboBox* modeCombo_;
    QSpinBox* supercellSpins_[3];
    QDoubleSpinBox* deltaSpin_;
    QLabel* countLabel_;
    QComboBox* calculatorCombo_;
    QComboBox* maceModelCombo_;
    QComboBox* maceSizeCombo_;
    QLineEdit* maceModelPathEdit_;
    QPushButton* maceBrowseButton_;
    QComboBox* maceDeviceCombo_;
    QSpinBox* bandPointsSpin_;
    QSpinBox* dosGridSpin_;
    QLineEdit* envPathEdit_;
    QLabel* envStatusLabel_;
    QLabel* editedNotice_;
    QPlainTextEdit* preview_;
    QPushButton* runButton_;
    bool updatingPreview_ = false;
    bool manuallyEdited_ = false;
};

} // namespace calango::gui
