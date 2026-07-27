#include "gui/OverlayEditDialog.hpp"

#include "gui/GuiUtils.hpp"

#include <QCheckBox>
#include <QColorDialog>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFontComboBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSlider>
#include <QSpinBox>
#include <QStackedWidget>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>

namespace calango::gui {

namespace {

/// Three spin boxes on one row — the x/y/z vector editor used half a dozen
/// times below.
QWidget* makeVectorRow(QWidget* parent, QDoubleSpinBox* (&spins)[3],
                       double minimum, double maximum, double step)
{
    auto* row = new QWidget(parent);
    auto* layout = new QHBoxLayout(row);
    layout->setContentsMargins(0, 0, 0, 0);
    const char* axes[3] = {"x", "y", "z"};
    for (int i = 0; i < 3; ++i) {
        layout->addWidget(new QLabel(QLatin1String(axes[i]), row));
        spins[i] = new QDoubleSpinBox(row);
        spins[i]->setRange(minimum, maximum);
        spins[i]->setDecimals(3);
        spins[i]->setSingleStep(step);
        layout->addWidget(spins[i]);
    }
    return row;
}

/// Which stacked page a kind uses. Three pages, not nine: the primitives share
/// every control they have.
int pageFor(Overlay::Kind kind)
{
    switch (kind) {
    case Overlay::Kind::LatticePlane:
        return 1;
    case Overlay::Kind::Text:
        return 0;
    default:
        return 2;
    }
}

/// The offset slider is an int control over a continuous quantity: these map
/// the two. 0.01 Å per tick is finer than the plane is drawn, so dragging never
/// feels stepped.
constexpr double kOffsetMin = -200.0;
constexpr double kOffsetMax = 200.0;
constexpr double kOffsetTicksPerAngstrom = 100.0;
constexpr int kOffsetSliderMin =
    static_cast<int>(kOffsetMin * kOffsetTicksPerAngstrom);
constexpr int kOffsetSliderMax =
    static_cast<int>(kOffsetMax * kOffsetTicksPerAngstrom);

int offsetToTicks(double value)
{
    return static_cast<int>(std::lround(
        std::clamp(value, kOffsetMin, kOffsetMax) * kOffsetTicksPerAngstrom));
}

double ticksToOffset(int ticks) { return ticks / kOffsetTicksPerAngstrom; }

} // namespace

OverlayEditDialog::OverlayEditDialog(const Overlay& overlay,
                                     bool structureHasCell, QWidget* parent)
    : QDialog(parent)
    , overlay_(overlay)
    , structureHasCell_(structureHasCell)
{
    setWindowTitle(tr("Overlay Properties"));
    // Resizable, with a grip: the text box grows with the dialog, and a
    // multi-line caption is unworkable in a fixed 3-line well.
    setSizeGripEnabled(true);

    auto* layout = new QVBoxLayout(this);
    auto* header = new QFormLayout;

    kindCombo_ = new QComboBox(this);
    // Order matches Overlay::Kind.
    for (const Overlay::Kind kind :
         {Overlay::Kind::LatticePlane, Overlay::Kind::Text, Overlay::Kind::Box,
          Overlay::Kind::Sphere, Overlay::Kind::Ellipsoid, Overlay::Kind::Tube,
          Overlay::Kind::Cone, Overlay::Kind::Plane, Overlay::Kind::Disk}) {
        kindCombo_->addItem(Overlay::kindName(kind), static_cast<int>(kind));
    }
    header->addRow(tr("Type:"), kindCombo_);

    nameEdit_ = new QLineEdit(this);
    nameEdit_->setPlaceholderText(tr("optional label for the list"));
    header->addRow(tr("Name:"), nameEdit_);

    visibleCheck_ = new QCheckBox(tr("Visible"), this);
    header->addRow(QString(), visibleCheck_);
    layout->addLayout(header);

    pages_ = new QStackedWidget(this);
    pages_->addWidget(buildTextPage());
    pages_->addWidget(buildLatticePlanePage());
    pages_->addWidget(buildPrimitivePage());
    layout->addWidget(pages_);

    // Opacity applies to every kind, so it lives outside the pages rather than
    // being repeated on each of them. For a primitive it is the fill alpha; for
    // text it fades glyphs and background together, which is what you want when
    // an annotation should recede without being deleted.
    opacityRow_ = new QWidget(this);
    auto* opacityLayout = new QHBoxLayout(opacityRow_);
    opacityLayout->setContentsMargins(0, 0, 0, 0);
    opacityLayout->addWidget(new QLabel(tr("Overlay opacity:"), opacityRow_));
    opacitySlider_ = new QSlider(Qt::Horizontal, opacityRow_);
    opacitySlider_->setRange(5, 100);
    opacitySlider_->setToolTip(
        tr("Transparency of the whole overlay. Text fades with its background."));
    opacityLayout->addWidget(opacitySlider_, 1);
    layout->addWidget(opacityRow_);

    auto* buttons = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);

    connect(kindCombo_, &QComboBox::currentIndexChanged, this,
            &OverlayEditDialog::onKindChanged);
    connect(nameEdit_, &QLineEdit::textChanged, this, &OverlayEditDialog::apply);
    connect(visibleCheck_, &QCheckBox::toggled, this, &OverlayEditDialog::apply);
    connect(opacitySlider_, &QSlider::valueChanged, this,
            &OverlayEditDialog::apply);

    load();
}

QWidget* OverlayEditDialog::buildTextPage()
{
    auto* page = new QGroupBox(tr("Text"), this);
    auto* form = new QFormLayout(page);

    textEdit_ = new QPlainTextEdit(page);
    textEdit_->setPlaceholderText(tr("annotation shown in the viewport"));
    textEdit_->setTabChangesFocus(true);
    textEdit_->setMinimumHeight(60);
    // Grows with the dialog, and the dialog is resizable: a caption can run to
    // several lines and the user should be able to see all of them while typing.
    textEdit_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    form->addRow(tr("Text:"), textEdit_);

    fontCombo_ = new QFontComboBox(page);
    form->addRow(tr("Font:"), fontCombo_);

    auto* sizeRow = new QWidget(page);
    auto* sizeLayout = new QHBoxLayout(sizeRow);
    sizeLayout->setContentsMargins(0, 0, 0, 0);
    fontSizeSpin_ = new QSpinBox(sizeRow);
    fontSizeSpin_->setRange(6, 96);
    fontSizeSpin_->setSuffix(tr(" pt"));
    sizeLayout->addWidget(fontSizeSpin_);
    boldCheck_ = new QCheckBox(tr("Bold"), sizeRow);
    italicCheck_ = new QCheckBox(tr("Italic"), sizeRow);
    sizeLayout->addWidget(boldCheck_);
    sizeLayout->addWidget(italicCheck_);
    sizeLayout->addStretch(1);
    form->addRow(tr("Size:"), sizeRow);

    textColorButton_ = new QPushButton(page);
    textColorButton_->setFixedHeight(22);
    form->addRow(tr("Color:"), textColorButton_);

    backgroundCheck_ = new QCheckBox(tr("Draw a background plate"), page);
    backgroundCheck_->setToolTip(
        tr("A filled pill behind the glyphs. Without it a pale label over a "
           "pale structure is unreadable."));
    form->addRow(QString(), backgroundCheck_);

    backgroundColorButton_ = new QPushButton(page);
    backgroundColorButton_->setFixedHeight(22);
    form->addRow(tr("Background:"), backgroundColorButton_);

    auto* bgOpacityRow = new QWidget(page);
    auto* bgOpacityLayout = new QHBoxLayout(bgOpacityRow);
    bgOpacityLayout->setContentsMargins(0, 0, 0, 0);
    backgroundOpacitySlider_ = new QSlider(Qt::Horizontal, bgOpacityRow);
    // From 0: "no plate at all" is a legitimate setting, unlike the overlay
    // opacity below, where a fully invisible overlay is indistinguishable from
    // a broken one.
    backgroundOpacitySlider_->setRange(0, 100);
    bgOpacityLayout->addWidget(backgroundOpacitySlider_, 1);
    form->addRow(tr("Background opacity:"), bgOpacityRow);

    form->addRow(tr("Position (Å):"),
                 makeVectorRow(page, textPosSpin_, -10000.0, 10000.0, 0.5));
    auto* hint = new QLabel(
        tr("<i>The label is anchored to this point in the scene, so it moves "
           "with the structure as you orbit. You can also just drag it in the "
           "viewport — this row follows.</i>"),
        page);
    hint->setWordWrap(true);
    hint->setTextFormat(Qt::RichText);
    form->addRow(hint);

    connect(textEdit_, &QPlainTextEdit::textChanged, this,
            &OverlayEditDialog::apply);
    connect(backgroundCheck_, &QCheckBox::toggled, this,
            &OverlayEditDialog::apply);
    connect(backgroundOpacitySlider_, &QSlider::valueChanged, this,
            &OverlayEditDialog::apply);
    connect(backgroundColorButton_, &QPushButton::clicked, this, [this] {
        const QColor chosen = QColorDialog::getColor(
            overlay_.backgroundColor, this, tr("Background Color"));
        if (!chosen.isValid())
            return;
        overlay_.backgroundColor = chosen;
        updateColorButtons();
        Q_EMIT changed();
    });
    connect(fontCombo_, &QFontComboBox::currentFontChanged, this,
            [this] { apply(); });
    connect(fontSizeSpin_, &QSpinBox::valueChanged, this,
            &OverlayEditDialog::apply);
    connect(boldCheck_, &QCheckBox::toggled, this, &OverlayEditDialog::apply);
    connect(italicCheck_, &QCheckBox::toggled, this, &OverlayEditDialog::apply);
    for (QDoubleSpinBox* spin : textPosSpin_)
        connect(spin, &QDoubleSpinBox::valueChanged, this,
                &OverlayEditDialog::apply);
    connect(textColorButton_, &QPushButton::clicked, this, [this] {
        const QColor chosen =
            QColorDialog::getColor(overlay_.color, this, tr("Text Color"));
        if (!chosen.isValid())
            return;
        overlay_.color = chosen;
        updateColorButtons();
        Q_EMIT changed();
    });
    return page;
}

QWidget* OverlayEditDialog::buildLatticePlanePage()
{
    auto* page = new QGroupBox(tr("Lattice plane"), this);
    auto* form = new QFormLayout(page);

    noCellLabel_ = new QLabel(
        tr("<b>This structure has no unit cell.</b> Miller indices are defined "
           "against a lattice, so this overlay will not draw. Use the plain "
           "<i>Plane</i> type, which is oriented by an explicit normal."),
        page);
    noCellLabel_->setWordWrap(true);
    noCellLabel_->setTextFormat(Qt::RichText);
    form->addRow(noCellLabel_);

    auto* millerRow = new QWidget(page);
    auto* millerLayout = new QHBoxLayout(millerRow);
    millerLayout->setContentsMargins(0, 0, 0, 0);
    const char* labels[3] = {"h", "k", "l"};
    for (int i = 0; i < 3; ++i) {
        millerLayout->addWidget(new QLabel(QLatin1String(labels[i]), millerRow));
        millerSpin_[i] = new QSpinBox(millerRow);
        millerSpin_[i]->setRange(-12, 12);
        millerLayout->addWidget(millerSpin_[i]);
    }
    millerLayout->addStretch(1);
    form->addRow(tr("Miller indices:"), millerRow);

    auto* offsetRow = new QWidget(page);
    auto* offsetLayout = new QHBoxLayout(offsetRow);
    offsetLayout->setContentsMargins(0, 0, 0, 0);
    offsetSlider_ = new QSlider(Qt::Horizontal, offsetRow);
    offsetSlider_->setRange(kOffsetSliderMin, kOffsetSliderMax);
    offsetSlider_->setToolTip(
        tr("Slide the plane along its normal to sweep it through the cell."));
    offsetSpin_ = new QDoubleSpinBox(offsetRow);
    offsetSpin_->setRange(kOffsetMin, kOffsetMax);
    offsetSpin_->setDecimals(3);
    offsetSpin_->setSingleStep(0.25);
    offsetSpin_->setSuffix(tr(" Å"));
    offsetSpin_->setToolTip(
        tr("Displacement along the plane normal from the cell center."));
    offsetLayout->addWidget(offsetSlider_, 1);
    offsetLayout->addWidget(offsetSpin_);
    form->addRow(tr("Position:"), offsetRow);

    auto* sizeRow = new QWidget(page);
    auto* sizeLayout = new QHBoxLayout(sizeRow);
    sizeLayout->setContentsMargins(0, 0, 0, 0);
    const auto makeExtentSpin = [&](const QString& tip) {
        auto* spin = new QDoubleSpinBox(sizeRow);
        spin->setRange(0.5, 500.0);
        spin->setDecimals(2);
        spin->setSingleStep(1.0);
        spin->setSuffix(tr(" Å"));
        spin->setToolTip(tip);
        return spin;
    };
    widthSpin_ = makeExtentSpin(
        tr("Half-width of the drawn quad, along the plane's first in-plane "
           "axis."));
    heightSpin_ = makeExtentSpin(
        tr("Half-height of the drawn quad, along the plane's second in-plane "
           "axis."));
    sizeLayout->addWidget(new QLabel(tr("w"), sizeRow));
    sizeLayout->addWidget(widthSpin_);
    sizeLayout->addWidget(new QLabel(tr("h"), sizeRow));
    sizeLayout->addWidget(heightSpin_);
    sizeLayout->addStretch(1);
    form->addRow(tr("Dimensions:"), sizeRow);

    edgesCheck_ = new QCheckBox(tr("Draw border"), page);
    form->addRow(QString(), edgesCheck_);

    planeColorButton_ = new QPushButton(page);
    planeColorButton_->setFixedHeight(22);
    form->addRow(tr("Plane color:"), planeColorButton_);

    for (QSpinBox* spin : millerSpin_)
        connect(spin, &QSpinBox::valueChanged, this, &OverlayEditDialog::apply);
    // Slider and spin box mirror each other. Both guard on loading_ via
    // apply(), and each setter is a no-op when the value already matches, so
    // the round trip terminates instead of ping-ponging.
    connect(offsetSpin_, &QDoubleSpinBox::valueChanged, this,
            [this](double value) {
                const int ticks = offsetToTicks(value);
                if (offsetSlider_->value() != ticks) {
                    QSignalBlocker block(offsetSlider_);
                    offsetSlider_->setValue(ticks);
                }
                apply();
            });
    connect(offsetSlider_, &QSlider::valueChanged, this, [this](int ticks) {
        const double value = ticksToOffset(ticks);
        if (std::abs(offsetSpin_->value() - value) > 1e-9) {
            QSignalBlocker block(offsetSpin_);
            offsetSpin_->setValue(value);
        }
        apply();
    });
    for (QDoubleSpinBox* spin : {widthSpin_, heightSpin_})
        connect(spin, &QDoubleSpinBox::valueChanged, this,
                &OverlayEditDialog::apply);
    connect(edgesCheck_, &QCheckBox::toggled, this, &OverlayEditDialog::apply);
    connect(planeColorButton_, &QPushButton::clicked, this, [this] {
        const QColor chosen =
            QColorDialog::getColor(overlay_.color, this, tr("Plane Color"));
        if (!chosen.isValid())
            return;
        overlay_.color = chosen;
        updateColorButtons();
        Q_EMIT changed();
    });
    return page;
}

QWidget* OverlayEditDialog::buildPrimitivePage()
{
    auto* page = new QGroupBox(tr("Shape"), this);
    auto* form = new QFormLayout(page);

    form->addRow(tr("Center (Å):"),
                 makeVectorRow(page, centerSpin_, -10000.0, 10000.0, 0.5));
    sizeRow_ = makeVectorRow(page, sizeSpin_, 0.01, 1000.0, 0.5);
    form->addRow(tr("Size (Å):"), sizeRow_);
    endRow_ = makeVectorRow(page, endSpin_, -10000.0, 10000.0, 0.5);
    form->addRow(tr("End point (Å):"), endRow_);
    normalRow_ = makeVectorRow(page, normalSpin_, -100.0, 100.0, 0.1);
    form->addRow(tr("Normal:"), normalRow_);
    rotationRow_ = makeVectorRow(page, rotationSpin_, -360.0, 360.0, 5.0);
    form->addRow(tr("Rotation (°):"), rotationRow_);

    radiusRow_ = new QWidget(page);
    auto* radiusLayout = new QHBoxLayout(radiusRow_);
    radiusLayout->setContentsMargins(0, 0, 0, 0);
    radiusSpin_ = new QDoubleSpinBox(radiusRow_);
    radiusSpin_->setRange(0.01, 1000.0);
    radiusSpin_->setDecimals(3);
    radiusSpin_->setSingleStep(0.25);
    radiusSpin_->setSuffix(tr(" Å"));
    radiusLayout->addWidget(radiusSpin_);
    radiusLayout->addStretch(1);
    form->addRow(tr("Radius / extent:"), radiusRow_);

    textureCombo_ = new QComboBox(page);
    // Order matches Overlay::TextureStyle.
    textureCombo_->addItems({tr("Solid"), tr("Checkerboard"), tr("Wireframe"),
                             tr("Glassy"), tr("Gradient")});
    form->addRow(tr("Texture:"), textureCombo_);

    finishCombo_ = new QComboBox(page);
    finishCombo_->addItems({tr("Smooth"), tr("Corrugated")});
    form->addRow(tr("Surface:"), finishCombo_);

    resolutionSpin_ = new QSpinBox(page);
    resolutionSpin_->setRange(4, 128);
    resolutionSpin_->setToolTip(
        tr("Tessellation density. Higher is smoother and slower; the default "
           "is already smooth at figure scale."));
    form->addRow(tr("Resolution:"), resolutionSpin_);

    colorButton_ = new QPushButton(page);
    colorButton_->setFixedHeight(22);
    form->addRow(tr("Color:"), colorButton_);
    color2Button_ = new QPushButton(page);
    color2Button_->setFixedHeight(22);
    color2Button_->setToolTip(
        tr("Second color, used by the Checkerboard and Gradient textures."));
    form->addRow(tr("Second color:"), color2Button_);

    for (auto* spins : {&centerSpin_, &sizeSpin_, &endSpin_, &normalSpin_,
                        &rotationSpin_}) {
        for (QDoubleSpinBox* spin : *spins)
            connect(spin, &QDoubleSpinBox::valueChanged, this,
                    &OverlayEditDialog::apply);
    }
    connect(radiusSpin_, &QDoubleSpinBox::valueChanged, this,
            &OverlayEditDialog::apply);
    connect(resolutionSpin_, &QSpinBox::valueChanged, this,
            &OverlayEditDialog::apply);
    connect(textureCombo_, &QComboBox::currentIndexChanged, this,
            &OverlayEditDialog::apply);
    connect(finishCombo_, &QComboBox::currentIndexChanged, this,
            &OverlayEditDialog::apply);
    connect(colorButton_, &QPushButton::clicked, this, [this] {
        const QColor chosen =
            QColorDialog::getColor(overlay_.color, this, tr("Overlay Color"));
        if (!chosen.isValid())
            return;
        overlay_.color = chosen;
        updateColorButtons();
        Q_EMIT changed();
    });
    connect(color2Button_, &QPushButton::clicked, this, [this] {
        const QColor chosen =
            QColorDialog::getColor(overlay_.color2, this, tr("Second Color"));
        if (!chosen.isValid())
            return;
        overlay_.color2 = chosen;
        updateColorButtons();
        Q_EMIT changed();
    });
    return page;
}

void OverlayEditDialog::onKindChanged()
{
    if (loading_)
        return;
    overlay_.kind =
        static_cast<Overlay::Kind>(kindCombo_->currentData().toInt());
    pages_->setCurrentIndex(pageFor(overlay_.kind));
    showRelevantPrimitiveRows();
    Q_EMIT changed();
}

void OverlayEditDialog::showRelevantPrimitiveRows()
{
    using K = Overlay::Kind;
    const K kind = overlay_.kind;
    const bool box = kind == K::Box;
    const bool ellipsoid = kind == K::Ellipsoid;
    const bool sphere = kind == K::Sphere;
    const bool tube = kind == K::Tube || kind == K::Cone;
    const bool oriented = kind == K::Plane || kind == K::Disk;

    // A sphere's radius lives in size.x, so it shows the size row; an
    // ellipsoid needs all three. Showing rows a shape ignores would invite the
    // user to set a value that silently does nothing.
    if (sizeRow_)
        sizeRow_->setVisible(box || ellipsoid || sphere);
    if (endRow_)
        endRow_->setVisible(tube);
    if (normalRow_)
        normalRow_->setVisible(oriented);
    if (rotationRow_)
        rotationRow_->setVisible(box);
    if (radiusRow_)
        radiusRow_->setVisible(tube || oriented);
    if (sizeSpin_[1]) {
        // Sphere: only the first component is meaningful.
        sizeSpin_[1]->setEnabled(!sphere);
        sizeSpin_[2]->setEnabled(!sphere);
    }
}

void OverlayEditDialog::load()
{
    loading_ = true;
    kindCombo_->setCurrentIndex(kindCombo_->findData(
        static_cast<int>(overlay_.kind)));
    nameEdit_->setText(overlay_.name);
    visibleCheck_->setChecked(overlay_.visible);
    opacitySlider_->setValue(
        std::clamp(static_cast<int>(overlay_.opacity * 100.0), 5, 100));

    textEdit_->setPlainText(overlay_.text);
    backgroundCheck_->setChecked(overlay_.backgroundOpacity > 0.0);
    backgroundOpacitySlider_->setValue(
        std::clamp(static_cast<int>(overlay_.backgroundOpacity * 100.0), 0, 100));
    backgroundOpacitySlider_->setEnabled(backgroundCheck_->isChecked());
    fontCombo_->setCurrentFont(overlay_.font);
    fontSizeSpin_->setValue(overlay_.font.pointSize() > 0
                                ? overlay_.font.pointSize()
                                : 14);
    boldCheck_->setChecked(overlay_.font.bold());
    italicCheck_->setChecked(overlay_.font.italic());
    const double textPos[3] = {overlay_.center.x, overlay_.center.y,
                               overlay_.center.z};
    for (int i = 0; i < 3; ++i)
        textPosSpin_[i]->setValue(textPos[i]);

    for (int i = 0; i < 3; ++i)
        millerSpin_[i]->setValue(overlay_.miller[i]);
    offsetSpin_->setValue(overlay_.offset);
    offsetSlider_->setValue(offsetToTicks(overlay_.offset));
    widthSpin_->setValue(overlay_.width);
    heightSpin_->setValue(overlay_.height);
    edgesCheck_->setChecked(overlay_.showEdges);
    noCellLabel_->setVisible(!structureHasCell_);

    const double centre[3] = {overlay_.center.x, overlay_.center.y,
                              overlay_.center.z};
    const double size[3] = {overlay_.size.x, overlay_.size.y, overlay_.size.z};
    const double end[3] = {overlay_.endPoint.x, overlay_.endPoint.y,
                           overlay_.endPoint.z};
    const double normal[3] = {overlay_.normal.x, overlay_.normal.y,
                              overlay_.normal.z};
    const double rotation[3] = {overlay_.rotationDeg.x, overlay_.rotationDeg.y,
                                overlay_.rotationDeg.z};
    for (int i = 0; i < 3; ++i) {
        centerSpin_[i]->setValue(centre[i]);
        sizeSpin_[i]->setValue(size[i]);
        endSpin_[i]->setValue(end[i]);
        normalSpin_[i]->setValue(normal[i]);
        rotationSpin_[i]->setValue(rotation[i]);
    }
    radiusSpin_->setValue(overlay_.radius);
    resolutionSpin_->setValue(overlay_.resolution);
    textureCombo_->setCurrentIndex(static_cast<int>(overlay_.texture));
    finishCombo_->setCurrentIndex(static_cast<int>(overlay_.finish));

    pages_->setCurrentIndex(pageFor(overlay_.kind));
    showRelevantPrimitiveRows();
    updateColorButtons();
    loading_ = false;
}

void OverlayEditDialog::updateColorButtons()
{
    // All three show the same overlay colour; only one page is visible at a
    // time, so they never disagree on screen.
    setButtonColor(colorButton_, overlay_.color);
    setButtonColor(planeColorButton_, overlay_.color);
    setButtonColor(color2Button_, overlay_.color2);
    setButtonColor(textColorButton_, overlay_.color);
    setButtonColor(backgroundColorButton_, overlay_.backgroundColor);
}

void OverlayEditDialog::apply()
{
    if (loading_)
        return;

    overlay_.name = nameEdit_->text().trimmed();
    overlay_.visible = visibleCheck_->isChecked();
    overlay_.opacity = opacitySlider_->value() / 100.0;

    if (overlay_.kind == Overlay::Kind::Text) {
        overlay_.text = textEdit_->toPlainText();
        backgroundOpacitySlider_->setEnabled(backgroundCheck_->isChecked());
        overlay_.backgroundOpacity = backgroundCheck_->isChecked()
            ? backgroundOpacitySlider_->value() / 100.0
            : 0.0;
        QFont font = fontCombo_->currentFont();
        font.setPointSize(fontSizeSpin_->value());
        font.setBold(boldCheck_->isChecked());
        font.setItalic(italicCheck_->isChecked());
        overlay_.font = font;
        overlay_.center = {textPosSpin_[0]->value(), textPosSpin_[1]->value(),
                           textPosSpin_[2]->value()};
    } else if (overlay_.kind == Overlay::Kind::LatticePlane) {
        for (int i = 0; i < 3; ++i)
            overlay_.miller[i] = millerSpin_[i]->value();
        overlay_.offset = offsetSpin_->value();
        overlay_.width = widthSpin_->value();
        overlay_.height = heightSpin_->value();
        overlay_.showEdges = edgesCheck_->isChecked();
    } else {
        overlay_.center = {centerSpin_[0]->value(), centerSpin_[1]->value(),
                           centerSpin_[2]->value()};
        overlay_.size = {sizeSpin_[0]->value(), sizeSpin_[1]->value(),
                         sizeSpin_[2]->value()};
        overlay_.endPoint = {endSpin_[0]->value(), endSpin_[1]->value(),
                             endSpin_[2]->value()};
        overlay_.normal = {normalSpin_[0]->value(), normalSpin_[1]->value(),
                           normalSpin_[2]->value()};
        overlay_.rotationDeg = {rotationSpin_[0]->value(),
                                rotationSpin_[1]->value(),
                                rotationSpin_[2]->value()};
        overlay_.radius = radiusSpin_->value();
        overlay_.resolution = resolutionSpin_->value();
        overlay_.texture =
            static_cast<Overlay::TextureStyle>(textureCombo_->currentIndex());
        overlay_.finish =
            static_cast<Overlay::SurfaceFinish>(finishCombo_->currentIndex());
    }
    Q_EMIT changed();
}

} // namespace calango::gui
