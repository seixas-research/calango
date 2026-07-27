#pragma once

#include "gui/OpticsPlotStyleDialog.hpp"

#include <QDialog>
#include <QJsonObject>

class QCheckBox;
class QLabel;

namespace calango::gui {

class SpectrumPlotWidget;

/// The X-ray absorption spectrum written by an XAS run (`xas.json`).
///
/// Shows the isotropic spectrum — the average over the three Cartesian
/// polarizations, which is what a powder or solution measurement sees — with
/// the individual polarizations available alongside it. For an oriented sample
/// the difference between them IS the result, so they are one checkbox away
/// rather than hidden.
class XasResultsWindow : public QDialog {
    Q_OBJECT

public:
    explicit XasResultsWindow(QWidget* parent = nullptr);

    /// Load `xas.json`. False (with nothing shown) when it is missing or
    /// unreadable.
    bool loadResults(const QString& path);

private:
    void refreshPlot();

    QJsonObject data_;
    SpectrumPlotWidget* plot_ = nullptr;
    QCheckBox* polarizationCheck_ = nullptr;
    QCheckBox* sticksCheck_ = nullptr;
    QLabel* summaryLabel_ = nullptr;
    OpticsPlotStyle style_;
};

} // namespace calango::gui
