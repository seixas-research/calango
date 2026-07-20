#pragma once

#include <QLabel>
#include <QWidget>

namespace calango::core {
class Structure;
}

namespace calango::gui {

/// Read-only side panel summarizing the current structure
/// (formula, atom count, bonds, cell). A pure View: no model mutation.
class StructureInfoWidget : public QWidget {
    Q_OBJECT

public:
    explicit StructureInfoWidget(QWidget* parent = nullptr);

public Q_SLOTS:
    void updateFromStructure(const core::Structure* structure);

private:
    QLabel* formulaLabel_;
    QLabel* atomCountLabel_;
    QLabel* bondCountLabel_;
    QLabel* cellLabel_;
    QLabel* pbcLabel_;
};

} // namespace calango::gui
