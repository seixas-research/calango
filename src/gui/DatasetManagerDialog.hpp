#pragma once

#include <QDialog>
#include <QStringList>

class QComboBox;
class QLabel;
class QListWidget;
class QPushButton;
class QSpinBox;

namespace calango::gui {

/// Simulation → "Dataset Manager": assembles MLIP training datasets from
/// heterogeneous multi-frame trajectory files. Provides deterministic
/// train/validation/test splits (percentages + seed), Query-by-Committee
/// sub-dataset generation (independent splits or bootstrap resampling),
/// and export to Extended XYZ (MACE-ready train/valid/test files) or an
/// ASE SQLite database. Frames are read and written entirely through
/// ase.io so attached energies/forces/stresses survive into the export.
class DatasetManagerDialog : public QDialog {
    Q_OBJECT

public:
    explicit DatasetManagerDialog(QWidget* parent = nullptr);

private Q_SLOTS:
    void addFiles();
    void clearFiles();
    void exportDatasets();

private:
    void refreshSummary();
    int totalFrames() const;

    QStringList files_;
    std::vector<int> frameCounts_; ///< frames per file (ase.io.read count)

    QListWidget* fileList_;
    QLabel* summaryLabel_;
    QSpinBox* trainSpin_;
    QSpinBox* validationSpin_;
    QLabel* testLabel_;
    QSpinBox* seedSpin_;
    QSpinBox* committeeSpin_;
    QComboBox* committeeModeCombo_;
    QComboBox* formatCombo_;
    QPushButton* exportButton_;
};

} // namespace calango::gui
