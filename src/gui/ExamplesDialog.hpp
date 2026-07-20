#pragma once

#include "core/Structure.hpp"

#include <QDialog>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>

#include <memory>

namespace calango::gui {

/// "Database & Preset Browser" (Build → By Examples…):
///   - Presets tab: the bundled benchmark structures with recommended
///     potentials.
///   - Materials Project tab: fetch crystal structures by mp-id through
///     the user's API key (persisted in QSettings).
/// The dialog only *selects/fetches*; the controller (MainWindow) opens
/// the resulting documents via the emitted signals.
class ExamplesDialog : public QDialog {
    Q_OBJECT

public:
    explicit ExamplesDialog(QWidget* parent = nullptr);

Q_SIGNALS:
    void presetChosen(const QString& resourcePath, const QString& recommendation);
    void structureFetched(std::shared_ptr<core::Structure> structure,
                          const QString& name);

private Q_SLOTS:
    void loadSelectedPreset();
    void fetchFromMaterialsProject();

private:
    QListWidget* presetList_;
    QLineEdit* apiKeyEdit_;
    QLineEdit* materialIdEdit_;
    QPushButton* fetchButton_;
    QLabel* fetchStatus_;
};

} // namespace calango::gui
