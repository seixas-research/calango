#include "gui/TopologyDialog.hpp"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLabel>
#include <QSpinBox>
#include <QVBoxLayout>

namespace calango::gui {

TopologyDialog::TopologyDialog(QWidget* parent) : QDialog(parent)
{
    setWindowTitle(tr("Topological Invariants"));

    auto* layout = new QVBoxLayout(this);
    auto* intro = new QLabel(
        tr("Both invariants are read off the <b>hybrid Wannier centre "
           "flow</b>: the Berry phases of the occupied manifold accumulated "
           "along one reciprocal direction, resolved against the "
           "perpendicular k. They differ only in what is counted — the net "
           "winding for the Chern number, the parity of the largest-gap "
           "crossings for Z₂."),
        this);
    intro->setWordWrap(true);
    intro->setTextFormat(Qt::RichText);
    layout->addWidget(intro);

    auto* form = new QFormLayout;

    invariantCombo_ = new QComboBox(this);
    invariantCombo_->addItem(tr("Chern number and Z₂"),
                             static_cast<int>(core::TopologicalInvariant::Both));
    invariantCombo_->addItem(tr("Chern number only"),
                             static_cast<int>(core::TopologicalInvariant::Chern));
    invariantCombo_->addItem(tr("Z₂ index only"),
                             static_cast<int>(core::TopologicalInvariant::Z2));
    form->addRow(tr("Compute:"), invariantCombo_);
    connect(invariantCombo_, &QComboBox::currentIndexChanged, this,
            [this] { refreshApplicabilityNote(); });

    directionCombo_ = new QComboBox(this);
    directionCombo_->addItem(tr("along b₁ (k_x)"), 0);
    directionCombo_->addItem(tr("along b₂ (k_y)"), 1);
    directionCombo_->addItem(tr("along b₃ (k_z)"), 2);
    directionCombo_->setCurrentIndex(2);
    directionCombo_->setToolTip(
        tr("The reciprocal direction the Berry phase is accumulated along. "
           "The flow is then resolved against the remaining two.\n\n"
           "For a 2D sheet in the xy-plane this should be an in-plane "
           "direction: the out-of-plane one has no dispersion to transport "
           "through."));
    form->addRow(tr("Wilson loop:"), directionCombo_);

    occupiedSpin_ = new QSpinBox(this);
    occupiedSpin_->setRange(0, 100000);
    occupiedSpin_->setValue(0);
    occupiedSpin_->setSpecialValueText(tr("from the electron count"));
    occupiedSpin_->setToolTip(
        tr("Bands in the occupied manifold.\n\n"
           "The invariant belongs to a GAPPED set of bands, so this has to be "
           "a filling where the gap is actually open. Derived from the "
           "electron count by default, which is right whenever the gap sits "
           "at the nominal filling — and the generated script warns when it "
           "finds no gap there."));
    form->addRow(tr("Occupied bands:"), occupiedSpin_);

    loopSpin_ = new QSpinBox(this);
    loopSpin_->setRange(5, 2001);
    loopSpin_->setValue(51);
    loopSpin_->setToolTip(
        tr("Samples along the flow coordinate. Too few and the centres jump "
           "further than half a period between steps, which breaks the "
           "branch tracking and gives an integer that is a rounding of noise "
           "— the reported residual is what exposes that."));
    form->addRow(tr("Loop samples:"), loopSpin_);

    socCheck_ = new QCheckBox(tr("Spin-orbit coupling"), this);
    socCheck_->setChecked(true);
    socCheck_->setToolTip(
        tr("For most candidate materials SOC is what opens the inverted gap "
           "that makes the phase non-trivial in the first place — a Z₂ "
           "computed without it usually just says the setting was off."));
    form->addRow(QString(), socCheck_);
    connect(socCheck_, &QCheckBox::toggled, this,
            [this] { refreshApplicabilityNote(); });
    layout->addLayout(form);

    note_ = new QLabel(this);
    note_->setWordWrap(true);
    note_->setTextFormat(Qt::RichText);
    layout->addWidget(note_);

    auto* buttons = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);

    refreshApplicabilityNote();
}

void TopologyDialog::refreshApplicabilityNote()
{
    const auto invariant = static_cast<core::TopologicalInvariant>(
        invariantCombo_->currentData().toInt());
    QStringList notes;
    if (invariant != core::TopologicalInvariant::Chern)
        notes << tr("<b>Z₂ requires time-reversal symmetry</b> and is not "
                    "defined for a magnetic system — use the Chern number "
                    "there.");
    if (invariant != core::TopologicalInvariant::Z2)
        notes << tr("<b>A non-zero Chern number requires broken "
                    "time-reversal symmetry.</b> In a non-magnetic material it "
                    "is zero by symmetry, and computing it is a consistency "
                    "check rather than a result.");
    if (!socCheck_->isChecked() && invariant != core::TopologicalInvariant::Chern)
        notes << tr("Spin-orbit coupling is off, so a trivial Z₂ here is "
                    "expected regardless of the material.");
    note_->setText(QStringLiteral("<i>%1</i>")
                       .arg(notes.join(QStringLiteral("<br>"))));
}

core::TopologyConfig TopologyDialog::config() const
{
    core::TopologyConfig cfg;
    cfg.invariant = static_cast<core::TopologicalInvariant>(
        invariantCombo_->currentData().toInt());
    cfg.direction = directionCombo_->currentData().toInt();
    cfg.occupiedBands = occupiedSpin_->value();
    cfg.loopSamples = loopSpin_->value();
    cfg.spinOrbit = socCheck_->isChecked();
    return cfg;
}

} // namespace calango::gui
