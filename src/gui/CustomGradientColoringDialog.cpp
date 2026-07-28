#include "gui/CustomGradientColoringDialog.hpp"

#include "core/Structure.hpp"
#include "gui/GuiUtils.hpp"
#include "gui/ViewportWidget.hpp"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QSignalBlocker>
#include <QVBoxLayout>

#include <algorithm>

namespace calango::gui {

CustomGradientColoringDialog::CustomGradientColoringDialog(
    ViewportWidget* viewport, QWidget* parent)
    : QDialog(parent), viewport_(viewport)
{
    setWindowTitle(tr("Custom Gradient Coloring"));
    resize(430, 340);

    auto* layout = new QVBoxLayout(this);
    auto* form = new QFormLayout;
    layout->addLayout(form);

    auto* intro = new QLabel(
        tr("These control the scalar color mapping. They take effect once "
           "\"Color by\" in the Representation panel is set to something other "
           "than Element."),
        this);
    intro->setWordWrap(true);
    form->addRow(intro);

    gradientCombo_ = new QComboBox(this);
    // Same order as render::ColorGradient.
    gradientCombo_->addItems({tr("Viridis"), tr("Plasma"), tr("Turbo"),
                              tr("Inferno"), tr("Magma"), tr("Cividis"),
                              tr("Hot"), tr("Afmhot"), tr("Coolwarm"),
                              tr("Rainbow"), tr("Greys"), tr("Spectral"),
                              tr("Gnuplot")});
    form->addRow(tr("Gradient:"), gradientCombo_);
    connect(gradientCombo_, &QComboBox::currentIndexChanged, this, [this](int index) {
        viewport_->setColorGradient(static_cast<render::ColorGradient>(index));
    });

    invertGradientCheck_ = new QCheckBox(tr("Invert palette"), this);
    invertGradientCheck_->setToolTip(tr("Reverse the scalar-to-color mapping: "
                                        "minimum values take the high end of the\n"
                                        "gradient and maximum values the low end "
                                        "(matplotlib \"_r\" palettes)"));
    form->addRow(invertGradientCheck_);
    connect(invertGradientCheck_, &QCheckBox::toggled, this, [this](bool on) {
        viewport_->setGradientInverted(on);
    });

    propertyCombo_ = new QComboBox(this);
    propertyCombo_->setToolTip(tr("Per-atom scalar fields of the current structure\n"
                                  "(charges, |forces|, extxyz columns, ...)"));
    form->addRow(tr("Property:"), propertyCombo_);
    connect(propertyCombo_, &QComboBox::currentIndexChanged,
            this, &CustomGradientColoringDialog::applyProperty);

    // --- Color range bounds ------------------------------------------------
    // Editable Min/Max rather than a read-only legend: auto-scaling
    // renormalizes the ramp to whatever the current frame/structure happens to
    // contain, which makes two figures on the same property incomparable.
    // Typing bounds pins the scale so the same color means the same value
    // across frames, structures and exported figures.
    auto* rangeRow = new QWidget(this);
    auto* rangeLayout = new QHBoxLayout(rangeRow);
    rangeLayout->setContentsMargins(0, 0, 0, 0);
    rangeLayout->setSpacing(4);
    const auto makeBoundSpin = [rangeRow] {
        // Compact rendering: property ranges span everything from 1e-5 μB to
        // 1e3 eV/Å, and a fixed-decimal spin box either overflows the field
        // ("0.000012345") or shows a misleading "0.000". CompactDoubleSpinBox
        // formats to 3 significant figures, switching to exponential when the
        // magnitude needs it.
        auto* spin = new CompactDoubleSpinBox(rangeRow);
        spin->setRange(-1.0e9, 1.0e9);
        spin->setKeyboardTracking(false); // apply on commit, not per keystroke
        return spin;
    };
    rangeLayout->addWidget(new QLabel(tr("Min"), rangeRow));
    rangeMinSpin_ = makeBoundSpin();
    rangeLayout->addWidget(rangeMinSpin_, 1);
    rangeLayout->addWidget(new QLabel(tr("Max"), rangeRow));
    rangeMaxSpin_ = makeBoundSpin();
    rangeLayout->addWidget(rangeMaxSpin_, 1);
    form->addRow(tr("Range:"), rangeRow);

    autoRangeCheck_ = new QCheckBox(tr("Auto-scale to data"), this);
    autoRangeCheck_->setChecked(true);
    autoRangeCheck_->setToolTip(
        tr("On: the color ramp spans the property's own minimum and maximum, "
           "and the fields above track it. With a trajectory open that is the "
           "range over EVERY frame, not just the one displayed — a ramp "
           "renormalized frame by frame makes the colors flicker as the "
           "timeline plays and means a different value at every step.\n"
           "Off: the ramp is pinned to the Min/Max you type — values beyond "
           "them clamp to the ramp ends — so several structures or "
           "trajectories can be compared on one fixed scale."));
    form->addRow(autoRangeCheck_);

    // Editing a bound is itself the intent to override, so it switches off
    // auto-scaling rather than being silently discarded on the next refresh.
    const auto applyCustomRange = [this] {
        if (syncingRange_)
            return;
        if (autoRangeCheck_->isChecked()) {
            const QSignalBlocker blocker(autoRangeCheck_);
            autoRangeCheck_->setChecked(false);
        }
        applyColorRange();
    };
    connect(rangeMinSpin_, &QDoubleSpinBox::valueChanged, this, applyCustomRange);
    connect(rangeMaxSpin_, &QDoubleSpinBox::valueChanged, this, applyCustomRange);
    // Re-syncing covers both directions: switching auto back on refills the
    // fields from the data, and it applies the window either way.
    connect(autoRangeCheck_, &QCheckBox::toggled, this,
            &CustomGradientColoringDialog::syncFromViewport);



    layout->addStretch(1);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);

    // Observers live with the controls they update, so the dialog stays in
    // step with a trajectory scrub or a mode change made behind it.
    connect(viewport_, &ViewportWidget::structureReplaced,
            this, &CustomGradientColoringDialog::refreshPropertyList);
    connect(viewport_, &ViewportWidget::colorMappingChanged,
            this, &CustomGradientColoringDialog::syncFromViewport);

    refreshPropertyList();
    syncFromViewport();
}

void CustomGradientColoringDialog::applyProperty()
{
    // Selecting a property implies mapping it: switching the viewport to
    // CustomScalar here means the user does not have to set "Color by" in a
    // second place before anything happens.
    if (propertyCombo_->currentText().isEmpty())
        return;
    viewport_->setColorMode(render::ColorMode::CustomScalar,
                            propertyCombo_->currentText());
}

void CustomGradientColoringDialog::refreshPropertyList()
{
    const QSignalBlocker blocker(propertyCombo_);
    const QString previous = propertyCombo_->currentText();
    propertyCombo_->clear();
    const core::Structure* structure = nullptr;
    const auto held = viewport_->structure();
    if (held) {
        structure = held.get();
        for (const auto& [name, values] : structure->scalarFields()) {
            (void)values;
            propertyCombo_->addItem(QString::fromStdString(name));
        }
    }
    const int index = propertyCombo_->findText(previous);
    if (index >= 0)
        propertyCombo_->setCurrentIndex(index);

    // Overlay availability is now the Vector Overlay dialog's own concern: it
    // re-checks on structureReplaced, so a frame that drops a per-atom column
    // still resets the selection — without this panel holding widgets for it.
}

void CustomGradientColoringDialog::syncFromViewport()
{
    // The mapping can also be driven from outside (Coordination Analysis
    // dialog) — mirror the viewport state without re-triggering it.
    const auto mode = viewport_->colorMode();
    {
        const QSignalBlocker blocker(gradientCombo_);
        gradientCombo_->setCurrentIndex(static_cast<int>(viewport_->style().gradient));
    }
    {
        const QSignalBlocker blocker(invertGradientCheck_);
        invertGradientCheck_->setChecked(viewport_->style().invertGradient);
    }
    if (mode == render::ColorMode::CustomScalar) {
        const QSignalBlocker blocker(propertyCombo_);
        const int index = propertyCombo_->findText(viewport_->customScalarField());
        if (index >= 0)
            propertyCombo_->setCurrentIndex(index);
    }

    const bool scalarMode = mode != render::ColorMode::Element;
    gradientCombo_->setEnabled(scalarMode);
    invertGradientCheck_->setEnabled(scalarMode);
    propertyCombo_->setEnabled(mode == render::ColorMode::CustomScalar);

    // The bounds fields stay editable in any scalar mode; they only go dead in
    // Element mode, where no scalar is being mapped at all.
    const bool autoScale = autoRangeCheck_->isChecked();
    autoRangeCheck_->setEnabled(scalarMode);
    rangeMinSpin_->setEnabled(scalarMode);
    rangeMaxSpin_->setEnabled(scalarMode);

    // While auto-scaling, the fields mirror the data's own range so the user
    // starts from the real numbers when they switch to custom bounds. Once
    // pinned, they are the user's values and must not be overwritten.
    if (autoScale) {
        // "The data" for a trajectory is the WHOLE trajectory. Scaling to the
        // displayed frame renormalizes the ramp on every scrub, so a color
        // means a different number at every step and playback flickers —
        // which is exactly what an auto-scaled scale should not do.
        const auto trajectory =
            mode == render::ColorMode::CustomScalar
                ? viewport_->trajectoryScalarRange(propertyCombo_->currentText())
                : ViewportWidget::ScalarRange{};
        trajectoryScaled_ = trajectory.valid;
        const auto range = trajectory.valid ? trajectory : viewport_->scalarRange();
        const QSignalBlocker minBlocker(rangeMinSpin_);
        const QSignalBlocker maxBlocker(rangeMaxSpin_);
        syncingRange_ = true;
        rangeMinSpin_->setValue(range.valid ? range.min : 0.0);
        rangeMaxSpin_->setValue(range.valid ? range.max : 1.0);
        syncingRange_ = false;
    } else {
        trajectoryScaled_ = false;
    }
    applyColorRange();
}

void CustomGradientColoringDialog::applyColorRange()
{
    // A trajectory-wide auto-scale is still a PINNED window at the renderer:
    // "auto" says where the numbers came from, not that they may move. Left
    // un-pinned the renderer would re-normalize to each frame and undo the
    // whole point of spanning the run.
    const bool custom = !autoRangeCheck_->isChecked() || trajectoryScaled_;
    // An inverted or degenerate window would map every atom to one color;
    // order the two bounds rather than silently flattening the figure.
    const auto lo = static_cast<float>(
        std::min(rangeMinSpin_->value(), rangeMaxSpin_->value()));
    const auto hi = static_cast<float>(
        std::max(rangeMinSpin_->value(), rangeMaxSpin_->value()));
    viewport_->setCustomScalarRange(custom, lo, hi);
}


} // namespace calango::gui
