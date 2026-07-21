#pragma once

#include <QDialog>

class QDoubleSpinBox;
class QListWidget;

namespace calango::gui {

class BandPdosView;

/// Modeless viewer for a finished electronic-structure job directory:
/// loads bands.json (+ pdos.json when present) and shows the band
/// structure and PDOS with an adjustable Fermi reference, energy window,
/// per-projection visibility toggles, and CSV/.dat export.
class BandPdosWindow : public QDialog {
    Q_OBJECT

public:
    explicit BandPdosWindow(const QString& directory, QWidget* parent = nullptr);

    /// True when bands.json was found and parsed.
    bool hasData() const { return hasData_; }

private Q_SLOTS:
    void exportBands();
    void exportPdos();

private:
    void loadDirectory(const QString& directory);

    BandPdosView* view_;
    QDoubleSpinBox* fermiSpin_;
    QDoubleSpinBox* minSpin_;
    QDoubleSpinBox* maxSpin_;
    QListWidget* projectionList_;
    bool hasData_ = false;
};

} // namespace calango::gui
