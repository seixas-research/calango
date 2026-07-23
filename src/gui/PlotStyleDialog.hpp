#pragma once

#include "gui/BandPdosView.hpp"

#include <QDialog>

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QPushButton;
class QSpinBox;

namespace calango::gui {

/// "Customize Appearance…" for the band-structure / DOS plots.
///
/// One dialog serves both viewers because both drive the same BandPdosView;
/// `phonon` only re-labels the controls (dispersion *branches* and PhDOS
/// rather than bands and PDOS, ω = 0 rather than E_F) and exposes the
/// frequency-axis bounds, which have no electronic counterpart.
///
/// Edits apply live via styleChanged() so the user judges the result against
/// the actual plot rather than a preview.
class PlotStyleDialog : public QDialog {
    Q_OBJECT

public:
    PlotStyleDialog(const BandPdosView::Style& style, bool phonon,
                    QWidget* parent = nullptr);

    /// Seed the axis-bounds row (phonon only); emits boundsChanged on edit.
    void setBounds(double minimum, double maximum);

Q_SIGNALS:
    void styleChanged(const BandPdosView::Style& style);
    /// Frequency/energy axis window, phonon dialog only.
    void boundsChanged(double minimum, double maximum);

private:
    /// Collect every widget into a Style and emit it.
    void emitStyle();
    /// Color button that opens a picker and writes into `target`.
    QPushButton* colorButton(QColor* target);

    BandPdosView::Style style_;
    bool phonon_;

    QDoubleSpinBox* tickSizeSpin_;
    QDoubleSpinBox* titleSizeSpin_;
    QDoubleSpinBox* annotationSizeSpin_;
    QComboBox* bandPenCombo_;
    QDoubleSpinBox* bandWidthSpin_;
    QCheckBox* showFermiCheck_;
    QComboBox* fermiPenCombo_;
    QDoubleSpinBox* fermiWidthSpin_;
    QDoubleSpinBox* spineWidthSpin_;
    QDoubleSpinBox* tickWidthSpin_;
    QCheckBox* fillDosCheck_;
    QSpinBox* fillAlphaSpin_;
    QDoubleSpinBox* minBoundSpin_ = nullptr;
    QDoubleSpinBox* maxBoundSpin_ = nullptr;
};

} // namespace calango::gui
