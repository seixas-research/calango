#pragma once

#include "core/CalculatorConfig.hpp"

#include <QDialog>

#include <vector>

class QCheckBox;
class QLabel;
class QPushButton;
class QTableWidget;

namespace calango::gui {

/// GPAW DFT+U editor: which elements carry a Hubbard correction, on which
/// orbital shell, and how large.
///
/// The result is emitted as GPAW's `setups` dictionary, e.g.
/// `setups={"Fe": ":d,3.5"}`. The leading colon matters — it means "the
/// default PAW dataset for this element, plus this correction"; without it
/// GPAW looks for a differently named dataset instead of correcting the
/// standard one.
///
/// Nothing here validates the numbers. A Hubbard U is a property of an
/// element's shell in a particular chemical environment, not a constant of the
/// element: the same Fe 3d takes different values in an oxide and in a metal,
/// and the accepted value depends on how it was determined (linear response,
/// fitting to a measured gap, matching a formation energy). Choosing and
/// justifying it is the user's job, so the dialog reports what will be written
/// rather than second-guessing it.
class HubbardParametersDialog : public QDialog {
    Q_OBJECT

public:
    /// `elements` seeds the element column's completer with the species
    /// actually present in the structure — a U on an element the cell does not
    /// contain is silently inert, which is hard to spot in a generated script.
    explicit HubbardParametersDialog(bool enabled,
                                     const std::vector<core::HubbardU>& initial,
                                     const QStringList& elements,
                                     QWidget* parent = nullptr);

    /// True when the correction block should be written at all.
    bool isEnabled() const;
    /// The edited table, with blank and zero-U rows dropped.
    std::vector<core::HubbardU> parameters() const;

private Q_SLOTS:
    void addRow();
    void removeSelectedRows();
    /// Enable/disable the table with the master checkbox and refresh the
    /// preview of the dictionary that will be generated.
    void updateState();

private:
    void appendRow(const core::HubbardU& entry);

    QCheckBox* enabledCheck_ = nullptr;
    QTableWidget* table_ = nullptr;
    QPushButton* removeButton_ = nullptr;
    QLabel* previewLabel_ = nullptr;
    QStringList elements_;
};

} // namespace calango::gui
