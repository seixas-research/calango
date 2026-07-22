#pragma once

#include "core/Structure.hpp"

#include <QDialog>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>

#include <memory>

class QComboBox;

namespace calango::gui {

/// "Database Browser" (Build → From Database…):
///   - Materials Project tab: fetch crystal structures by mp-id through the
///     user's API key (persisted in QSettings).
///   - PubChem tab: fetch a 3D molecular conformer by name, SMILES or CID
///     (no key required).
/// The dialog only *fetches*; the controller (MainWindow) opens the resulting
/// document via the emitted structureFetched signal.
class ExamplesDialog : public QDialog {
    Q_OBJECT

public:
    explicit ExamplesDialog(QWidget* parent = nullptr);

Q_SIGNALS:
    void structureFetched(std::shared_ptr<core::Structure> structure,
                          const QString& name);

private Q_SLOTS:
    void fetchFromMaterialsProject();
    void fetchFromPubChem();

private:
    QLineEdit* apiKeyEdit_;
    QLineEdit* materialIdEdit_;
    QPushButton* fetchButton_;
    QLabel* fetchStatus_;

    QComboBox* pubchemFieldCombo_;
    QLineEdit* pubchemQueryEdit_;
    QPushButton* pubchemButton_;
    QLabel* pubchemStatus_;
};

} // namespace calango::gui
