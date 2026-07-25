#pragma once

#include <QDialog>

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QTableWidget;

namespace calango::gui {

class ViewportWidget;

/// "Edit Polyhedral Setup" — the appearance and perception controls for the
/// coordination polyhedra drawn in Polyhedral representation mode.
///
/// Face opacity and the edge wireframe are presentation; the per-cation
/// coordination cutoff is not. Bond perception from covalent radii gets some
/// coordination shells wrong (an octahedral cation picking up four or eight
/// neighbours instead of six), and no single global tolerance fixes one element
/// without breaking the rest — so the cutoff is overridable per central
/// element, which is the only place in the app where that distinction exists.
///
/// Modeless and live: edits apply to the viewport as they are made.
class PolyhedralSettingsDialog : public QDialog {
    Q_OBJECT

public:
    explicit PolyhedralSettingsDialog(ViewportWidget* viewport,
                                      QWidget* parent = nullptr);

private Q_SLOTS:
    void addCutoffOverride();
    void removeCutoffOverride();

private:
    void refreshOverrides();
    /// Elements present in the current structure, as (Z, symbol).
    void populateElementCombo();

    ViewportWidget* viewport_;
    QDoubleSpinBox* opacitySpin_ = nullptr;
    QCheckBox* edgesCheck_ = nullptr;
    QDoubleSpinBox* edgeWidthSpin_ = nullptr;
    QComboBox* elementCombo_ = nullptr;
    QDoubleSpinBox* cutoffSpin_ = nullptr;
    QTableWidget* overrideTable_ = nullptr;
    QLabel* modeNoteLabel_ = nullptr;
};

} // namespace calango::gui
