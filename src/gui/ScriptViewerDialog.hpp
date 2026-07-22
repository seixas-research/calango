#pragma once

#include <QDialog>

namespace calango::gui {

/// Read-only, syntax-highlighted viewer for the exact Python/ASE script that a
/// process executed (its run.py). Opened from the Processes panel's "View ASE
/// Script" action; offers a "Copy to Clipboard" button.
class ScriptViewerDialog : public QDialog {
    Q_OBJECT

public:
    explicit ScriptViewerDialog(const QString& title, const QString& script,
                                QWidget* parent = nullptr);
};

} // namespace calango::gui
