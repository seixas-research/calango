#pragma once

#include <QDialog>
#include <QString>

class QLabel;
class QTableWidget;

namespace calango::gui {

/// Results -> "Wavefunctions": the dedicated read-out for a completed
/// Wavefunctions run, reading `wavefunctions.json`. A summary table of
/// every state the job wrote (band, k-point, spin, quantity, energy,
/// occupation, cube file name) — the RENDERING itself stays entirely in
/// the Volumetric Data dock (VolumetricPanel), which
/// MainWindow::openWavefunctionsResults() already registers every cube
/// into (unchecked, like the Wannier orbitals it mirrors that convention
/// from); this viewer is the "what did this job actually produce, at a
/// glance, with the physics that named each row" companion to it, the
/// same role MlwfViewer's centres/spreads table plays for a Wannier run,
/// scaled down: no per-row viewport toggle here, since the Volumetric
/// Data dock already IS that toggle surface and duplicating it would be
/// two controls for the same thing.
class WavefunctionsResultsViewer : public QDialog {
    Q_OBJECT

public:
    explicit WavefunctionsResultsViewer(QWidget* parent = nullptr);

    /// Loads `path` (wavefunctions.json). Returns false (dialog left
    /// visibly empty, with a status message) on a missing or unparsable
    /// file.
    bool loadResults(const QString& path);
    bool hasData() const { return hasData_; }

private:
    QLabel* summaryLabel_ = nullptr;
    QTableWidget* table_ = nullptr;
    bool hasData_ = false;
};

} // namespace calango::gui
