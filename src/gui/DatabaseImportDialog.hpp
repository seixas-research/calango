#pragma once

#include "core/Structure.hpp"

#include <QDialog>
#include <QList>
#include <QPair>
#include <QString>

#include <memory>

class QComboBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QSpinBox;
class QTableWidget;

namespace calango::gui {

/// Search a structure database and COLLECT structures, rather than open them.
///
/// This is deliberately not ExamplesDialog. That dialog exists to put
/// documents in tabs: its actions are called "Open Selected Separately" and
/// "Group Selected into Single Trajectory File", it reports "Opened 3
/// structure(s) in separate tabs", and it hands results out through
/// fire-and-forget signals. Driving it as a picker meant scraping those
/// signals for side effects, showing the user buttons that described something
/// that was not going to happen, and stacking its window-modal progress dialog
/// on top of two modal parents — which is what made the window stop
/// responding.
///
/// So: one job, done directly. Search, select, Add. The chosen structures
/// accumulate across several searches, because building a sweep across
/// chemistries is exactly what a container is for, and they are returned by
/// value rather than emitted.
class DatabaseImportDialog : public QDialog {
    Q_OBJECT

public:
    /// (display name, structure) — the shape a Container node holds.
    using Entry = QPair<QString, std::shared_ptr<const core::Structure>>;

    explicit DatabaseImportDialog(QWidget* parent = nullptr);

    /// What the user collected. Empty when they cancelled.
    const QList<Entry>& entries() const { return entries_; }

    /// Run the dialog and return what was collected — the whole interface the
    /// orchestration canvas needs.
    static QList<Entry> pick(QWidget* parent);

private Q_SLOTS:
    void search();
    void addSelected();

private:
    /// mp-ids of the selected rows, in table order.
    QStringList selectedIds() const;
    void setBusy(bool busy, const QString& message = QString());
    void refreshBasket();

    QLineEdit* apiKeyEdit_ = nullptr;
    QLineEdit* queryEdit_ = nullptr;
    QComboBox* modeCombo_ = nullptr;
    QSpinBox* limitSpin_ = nullptr;
    QPushButton* searchButton_ = nullptr;
    QPushButton* addButton_ = nullptr;
    QTableWidget* results_ = nullptr;
    QLabel* status_ = nullptr;
    QLabel* basket_ = nullptr;

    QList<Entry> entries_;
};

} // namespace calango::gui
