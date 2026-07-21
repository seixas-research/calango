#pragma once

#include "core/Structure.hpp"

#include <QDialog>

#include <memory>

class QLabel;
class QTableWidget;

namespace calango::gui {

/// Analysis → "Raman Modes": Γ-point factor-group classification of the
/// vibrational modes — which optical phonons are Raman-active, IR-active
/// or silent, per irreducible representation of the crystal's point
/// group (computed by pybridge::RamanAnalysis via spglib).
class RamanDialog : public QDialog {
    Q_OBJECT

public:
    explicit RamanDialog(std::shared_ptr<const core::Structure> structure,
                         QWidget* parent = nullptr);

private:
    void compute();

    std::shared_ptr<const core::Structure> structure_;
    QLabel* summaryLabel_;
    QTableWidget* table_;
};

} // namespace calango::gui
