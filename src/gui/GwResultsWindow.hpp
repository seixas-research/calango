#pragma once

#include <QDialog>
#include <QJsonObject>
#include <QString>

class QLabel;
class QTableWidget;

namespace calango::gui {

/// Results → "GW Viewer": the read-out for a completed G₀W₀ job. Parses the
/// `gw.json` written by GwScriptGenerator — identical schema for both engines,
/// so a GPAW and a Yambo run are read the same way — and shows the DFT and
/// quasiparticle band edges side by side.
///
/// The headline is the gap renormalization ΔE = E_gap^GW − E_gap^DFT. It is
/// reported with its sign made obvious because the sign is diagnostic: G₀W₀ on
/// a semiconductor essentially always OPENS the gap (typically 0.5–2 eV), so a
/// negative or near-zero renormalization is far more likely to mean an
/// unconverged screening cutoff or too few empty bands than a physical result.
/// The viewer says so rather than presenting the number neutrally.
///
/// The per-state table shows ε_DFT, E_qp and their difference, which is where
/// an unconverged run shows up first: the corrections should vary smoothly
/// across bands, not jump erratically between neighboring states.
class GwResultsWindow : public QDialog {
    Q_OBJECT

public:
    explicit GwResultsWindow(QWidget* parent = nullptr);

    /// Parse a `gw.json` and fill the read-outs. Returns false when the file is
    /// missing or malformed.
    bool loadResults(const QString& jsonPath);

private Q_SLOTS:
    void copyToClipboard();
    void exportCsv();

private:
    /// Compact multi-line rendering of the summary (also the clipboard payload).
    QString plainTextSummary() const;

    QJsonObject data_;
    QString sourcePath_;

    QLabel* engineLabel_ = nullptr;
    QLabel* dftGapLabel_ = nullptr;
    QLabel* gwGapLabel_ = nullptr;
    QLabel* renormLabel_ = nullptr;
    QLabel* edgesLabel_ = nullptr;
    QLabel* warningLabel_ = nullptr;
    QTableWidget* statesTable_ = nullptr;
};

} // namespace calango::gui
