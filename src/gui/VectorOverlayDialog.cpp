#include "gui/VectorOverlayDialog.hpp"

#include "core/Structure.hpp"
#include "gui/GuiUtils.hpp"
#include "gui/ViewportWidget.hpp"

#include <QColorDialog>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSlider>
#include <QStandardItemModel>
#include <QVBoxLayout>

#include <cmath>

namespace calango::gui {

VectorOverlayDialog::VectorOverlayDialog(ViewportWidget* viewport,
                                         QWidget* parent)
    : QDialog(parent), viewport_(viewport)
{
    setWindowTitle(tr("Edit Vector Overlay Setup"));
    resize(420, 250);

    auto* layout = new QVBoxLayout(this);
    auto* form = new QFormLayout;
    layout->addLayout(form);

    overlayCombo_ = new QComboBox(this);
    // Order matches render::VectorOverlay.
    overlayCombo_->addItem(tr("None"));
    overlayCombo_->addItem(tr("Velocity"));
    overlayCombo_->addItem(tr("Force"));
    overlayCombo_->addItem(tr("Magnetic moment"));
    overlayCombo_->setCurrentIndex(
        static_cast<int>(viewport_->style().vectorOverlay));
    form->addRow(tr("Vector overlay:"), overlayCombo_);
    connect(overlayCombo_, &QComboBox::currentIndexChanged, this, [this](int index) {
        viewport_->style().vectorOverlay =
            static_cast<render::VectorOverlay>(index);
        syncColorButton();
        viewport_->styleChanged(true);
    });

    // Slider (coarse) + spin box (exact), bidirectionally synced.
    auto* scaleRow = new QWidget(this);
    auto* scaleLayout = new QHBoxLayout(scaleRow);
    scaleLayout->setContentsMargins(0, 0, 0, 0);
    scaleSlider_ = new QSlider(Qt::Horizontal, scaleRow);
    scaleSlider_->setRange(10, 1000); // x0.1 .. x10.0 in hundredths
    scaleSpin_ = new QDoubleSpinBox(scaleRow);
    scaleSpin_->setRange(0.1, 10.0);
    scaleSpin_->setDecimals(2);
    scaleSpin_->setSingleStep(0.1);
    scaleSpin_->setSuffix(QStringLiteral("×"));
    const double current = viewport_->style().vectorScale;
    scaleSlider_->setValue(static_cast<int>(std::lround(current * 100.0)));
    scaleSpin_->setValue(current);
    scaleSpin_->setToolTip(
        tr("Arrow length relative to the calibrated baseline (1.0×), which is "
           "half an Å of arrow per field unit\n"
           "(eV/Å for forces, Å/fs·√(amu) for velocities, μB for magnetic "
           "moments). Velocities keep an extra 20× so they stay visible."));
    scaleLayout->addWidget(scaleSlider_, 1);
    scaleLayout->addWidget(scaleSpin_);
    form->addRow(tr("Vector scale:"), scaleRow);

    connect(scaleSlider_, &QSlider::valueChanged, this, [this](int hundredths) {
        const float factor = static_cast<float>(hundredths) / 100.0f;
        {
            const QSignalBlocker blocker(scaleSpin_);
            scaleSpin_->setValue(factor);
        }
        viewport_->style().vectorScale = factor;
        viewport_->styleChanged(true);
    });
    connect(scaleSpin_, &QDoubleSpinBox::valueChanged, this, [this](double factor) {
        {
            const QSignalBlocker blocker(scaleSlider_);
            scaleSlider_->setValue(static_cast<int>(std::lround(factor * 100.0)));
        }
        viewport_->style().vectorScale = static_cast<float>(factor);
        viewport_->styleChanged(true);
    });

    colorButton_ = new QPushButton(this);
    colorButton_->setToolTip(
        tr("Arrow colour for the selected overlay. Each property (velocity, "
           "force, magnetic moment) remembers its own."));
    form->addRow(tr("Vector color:"), colorButton_);
    connect(colorButton_, &QPushButton::clicked, this, [this] {
        QColor* target = overlayColor();
        if (!target)
            return;
        const QColor chosen =
            QColorDialog::getColor(*target, this, tr("Vector Overlay Color"));
        if (!chosen.isValid())
            return;
        *target = chosen;
        setButtonColor(colorButton_, chosen);
        viewport_->styleChanged(true); // arrow colours live in the instance buffer
    });

    layout->addStretch(1);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);

    // Re-check availability when the frame changes: scrubbing a trajectory can
    // move to a frame that carries different per-atom columns.
    connect(viewport_, &ViewportWidget::structureReplaced, this,
            [this] { refreshAvailability(); });

    refreshAvailability();
    syncColorButton();
}

QColor* VectorOverlayDialog::overlayColor()
{
    auto& style = viewport_->style();
    switch (style.vectorOverlay) {
    case render::VectorOverlay::Velocity: return &style.velocityColor;
    case render::VectorOverlay::Force: return &style.forceColor;
    case render::VectorOverlay::MagneticMoment: return &style.magmomColor;
    case render::VectorOverlay::None: break;
    }
    return nullptr; // nothing is drawn, so there is no colour to edit
}

void VectorOverlayDialog::syncColorButton()
{
    const QColor* color = overlayColor();
    colorButton_->setEnabled(color != nullptr);
    setButtonColor(colorButton_, color ? *color : palette().color(QPalette::Button));
    if (!color)
        colorButton_->setToolTip(
            tr("Select a vector overlay above to choose its arrow colour."));
}

void VectorOverlayDialog::refreshAvailability()
{
    const auto held = viewport_->structure();
    const core::Structure* structure = held ? held.get() : nullptr;

    // Grey out entries the frame has no data for rather than hiding them: a
    // fixed list keeps the indices aligned with render::VectorOverlay, and the
    // disabled tooltip explains what is missing instead of silently offering
    // nothing.
    const QSignalBlocker blocker(overlayCombo_);
    auto* model = qobject_cast<QStandardItemModel*>(overlayCombo_->model());
    bool currentStillValid = true;
    for (int i = 1; i < overlayCombo_->count(); ++i) {
        const auto overlay = static_cast<render::VectorOverlay>(i);
        const std::string field = render::vectorFieldName(overlay);
        const bool available =
            structure && structure->vectorFields().count(field) > 0;
        if (model) {
            if (QStandardItem* item = model->item(i)) {
                item->setEnabled(available);
                item->setToolTip(
                    available ? QString()
                              : tr("This frame carries no per-atom \"%1\" data.")
                                    .arg(QString::fromStdString(field)));
            }
        }
        if (!available && overlayCombo_->currentIndex() == i)
            currentStillValid = false;
    }
    if (!currentStillValid) {
        overlayCombo_->setCurrentIndex(0);
        viewport_->style().vectorOverlay = render::VectorOverlay::None;
        syncColorButton();
        viewport_->styleChanged(true);
    }
}

} // namespace calango::gui
