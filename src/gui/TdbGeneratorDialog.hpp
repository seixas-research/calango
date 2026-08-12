#pragma once

#include "core/CalphadModel.hpp"

#include <QDialog>
#include <QString>

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QPushButton;
class QSpinBox;
class QTableWidget;

namespace calango::gui {

/// "From DFT…" in the CALPHAD dialog: turn Calango's own first-principles
/// results into a thermodynamic database.
///
/// The pipeline is
///
///   formation energies  ->  Redlich-Kister excess Gibbs energy  ->  .tdb
///
/// and each arrow is a place the physics can be lost quietly, so each is
/// visible here: the configurations are shown with their formation energies
/// and their distance above the convex hull, the fitted coefficients are shown
/// with the residual of the fit, and the emitted file is shown in full before
/// it is written.
///
/// The energies come from a finished cluster-expansion run's
/// `cluster_expansion.json` — the same file the convex-hull viewer reads, so
/// the two always describe the same ensemble. The optional vibrational term
/// comes from `phonon_dos.json` files, run through the project's existing
/// harmonic thermodynamics (core::computePhononThermodynamics) rather than a
/// second implementation of the same integrals.
///
/// No pycalphad anywhere: fitting a polynomial and writing text are not things
/// that need a solver, and requiring one here would make the module unusable
/// on the machines it is meant for.
class TdbGeneratorDialog : public QDialog {
    Q_OBJECT

public:
    explicit TdbGeneratorDialog(QWidget* parent = nullptr);

    /// Load an ensemble from a cluster-expansion results file. Returns false
    /// and explains in the status line when the file carries no usable
    /// configuration. Public so a test can drive it without a file dialog.
    bool loadEnsemble(const QString& path);
    /// Same, from text, for the same reason.
    bool loadEnsembleJson(const QString& json, const QString& label);

    /// Attach a harmonic free energy to one table row, read from a phonon
    /// run's `phonon_dos.json`. `row` follows the table: 0 is the x = 0
    /// endpoint, 1..N the configurations, N+1 the x = 1 endpoint.
    ///
    /// The free energy itself comes from core::computePhononThermodynamics —
    /// the project's existing harmonic integrals — divided by the atom count
    /// the DOS integrates to, since that function reports per cell and the
    /// assessment works per atom.
    bool loadPhononDos(int row, const QString& path);

    /// The assessment as it currently stands. Recomputed by refresh().
    const core::CalphadAssessment& assessment() const { return assessment_; }
    /// What is fed to it: the ensemble as loaded, plus whatever vibrational
    /// free energies have been attached. Exposed so the file-reading layer can
    /// be checked against an independent evaluation of the same integrals —
    /// the place a silent unit factor hides.
    const core::CalphadAssessmentInput& input() const { return input_; }
    /// The `.tdb` text the dialog would write.
    QString databaseText() const;

private Q_SLOTS:
    void browseForEnsemble();
    void refresh();
    void saveDatabase();

private:
    void rebuildTable();
    void setStatus(const QString& text, bool ok);
    core::CalphadAssessmentInput buildInput() const;

    core::CalphadAssessmentInput input_;
    core::CalphadAssessment assessment_;
    bool loaded_ = false;

    QLineEdit* pathEdit_ = nullptr;
    QLabel* statusLabel_ = nullptr;
    QTableWidget* table_ = nullptr;
    QLineEdit* elementAEdit_ = nullptr;
    QLineEdit* elementBEdit_ = nullptr;
    QLineEdit* phaseEdit_ = nullptr;
    QSpinBox* orderSpin_ = nullptr;
    QDoubleSpinBox* minTemperature_ = nullptr;
    QDoubleSpinBox* maxTemperature_ = nullptr;
    QSpinBox* temperatureSteps_ = nullptr;
    QCheckBox* temperatureDependent_ = nullptr;
    QPlainTextEdit* preview_ = nullptr;
    QLabel* fitLabel_ = nullptr;
    QPushButton* saveButton_ = nullptr;
};

} // namespace calango::gui
