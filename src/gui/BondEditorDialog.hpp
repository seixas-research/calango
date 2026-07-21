#pragma once

#include "core/Structure.hpp"

#include <QCheckBox>
#include <QDialog>
#include <QDoubleSpinBox>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QSpinBox>

#include <memory>

namespace calango::gui {

class ViewportWidget;

/// "Edit → Bond Editor": interactive control over bond perception.
///   - toggle automatic distance-based bond detection
///   - tune the covalent cutoff multiplier (live)
///   - manually add or suppress individual bonds between atom pairs,
///     picked by index or from the current two-atom viewport selection
/// Manual overrides live on the Structure (so they survive re-rendering
/// and are captured by undo); edits apply immediately via bondsEdited().
class BondEditorDialog : public QDialog {
    Q_OBJECT

public:
    BondEditorDialog(std::shared_ptr<core::Structure> structure,
                     ViewportWidget* viewport, QWidget* parent = nullptr);

Q_SIGNALS:
    /// Emitted after every structure-level bond edit (add/suppress/clear).
    void bondsEdited();

private Q_SLOTS:
    void addBond();
    void suppressBond();
    void clearSelectedOverride();
    void clearAllOverrides();
    void useSelection();

private:
    void refreshOverrideList();
    std::pair<int, int> currentPair() const; ///< 0-based indices

    std::shared_ptr<core::Structure> structure_;
    ViewportWidget* viewport_;

    QCheckBox* autoBondsCheck_;
    QDoubleSpinBox* toleranceSpin_;
    QSpinBox* atomISpin_;
    QSpinBox* atomJSpin_;
    QPushButton* useSelectionButton_;
    QLabel* pairInfoLabel_;
    QListWidget* overrideList_;
};

} // namespace calango::gui
