#include "gui/LatticePlaneDialog.hpp"

#include "core/UnitCell.hpp"
#include "gui/ViewportWidget.hpp"
#include "render/ColorMap.hpp"

#include <QCheckBox>
#include <QColorDialog>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSlider>
#include <QSpinBox>
#include <QVBoxLayout>

#include <array>
#include <cmath>

namespace calango::gui {

namespace {
constexpr int kGridDivisions = 96;   ///< slice tessellation (N×N quads)
constexpr int kSliderSteps = 1000;   ///< offset-slider resolution

void appendVertex(std::vector<float>& out, const core::Vec3& p, const QColor& c)
{
    out.push_back(static_cast<float>(p.x));
    out.push_back(static_cast<float>(p.y));
    out.push_back(static_cast<float>(p.z));
    out.push_back(static_cast<float>(c.redF()));
    out.push_back(static_cast<float>(c.greenF()));
    out.push_back(static_cast<float>(c.blueF()));
}
} // namespace

LatticePlaneDialog::LatticePlaneDialog(
    std::shared_ptr<const core::Structure> structure, ViewportWidget* viewport,
    QWidget* parent)
    : QDialog(parent)
    , structure_(std::move(structure))
    , viewport_(viewport)
{
    setWindowTitle(tr("Lattice Plane Settings"));

    // Size the plane quad to the cell so it spans the whole structure.
    if (structure_ && structure_->cell().isDefined()) {
        const auto& v = structure_->cell().vectors();
        planeExtent_ = 0.5 * (v[0] + v[1] + v[2]).norm();
        if (planeExtent_ < 1.0)
            planeExtent_ = 1.0;
    }

    auto* layout = new QVBoxLayout(this);

    // --- Miller indices ----------------------------------------------------
    auto* orientGroup = new QGroupBox(tr("Orientation — Miller indices (h k l)"),
                                      this);
    auto* millerRow = new QHBoxLayout(orientGroup);
    const auto makeMiller = [this, millerRow](const QString& label, int def) {
        millerRow->addWidget(new QLabel(label, this));
        auto* spin = new QSpinBox(this);
        spin->setRange(-12, 12);
        spin->setValue(def);
        millerRow->addWidget(spin);
        connect(spin, &QSpinBox::valueChanged, this, &LatticePlaneDialog::rebuild);
        return spin;
    };
    hSpin_ = makeMiller(QStringLiteral("h"), 0);
    kSpin_ = makeMiller(QStringLiteral("k"), 0);
    lSpin_ = makeMiller(QStringLiteral("l"), 1);
    millerRow->addStretch(1);
    layout->addWidget(orientGroup);

    // --- Offset along the normal ------------------------------------------
    auto* offsetGroup = new QGroupBox(tr("Position — offset along normal"), this);
    auto* offsetRow = new QHBoxLayout(offsetGroup);
    offsetSlider_ = new QSlider(Qt::Horizontal, offsetGroup);
    offsetSlider_->setRange(0, kSliderSteps);
    offsetSlider_->setValue(kSliderSteps / 2);
    offsetSpin_ = new QDoubleSpinBox(offsetGroup);
    offsetSpin_->setRange(-planeExtent_, planeExtent_);
    offsetSpin_->setDecimals(3);
    offsetSpin_->setSingleStep(planeExtent_ / 50.0);
    offsetSpin_->setSuffix(tr(" Å"));
    offsetSpin_->setValue(0.0);
    offsetRow->addWidget(offsetSlider_, 1);
    offsetRow->addWidget(offsetSpin_);
    layout->addWidget(offsetGroup);
    // Slider ↔ spin (map [0,steps] ↔ [-extent, extent]).
    connect(offsetSlider_, &QSlider::valueChanged, this, [this](int v) {
        const double off =
            (double(v) / kSliderSteps * 2.0 - 1.0) * planeExtent_;
        const QSignalBlocker block(offsetSpin_);
        offsetSpin_->setValue(off);
        rebuild();
    });
    connect(offsetSpin_, &QDoubleSpinBox::valueChanged, this, [this](double off) {
        const int v = static_cast<int>(std::lround(
            (off / planeExtent_ * 0.5 + 0.5) * kSliderSteps));
        const QSignalBlocker block(offsetSlider_);
        offsetSlider_->setValue(std::clamp(v, 0, kSliderSteps));
        rebuild();
    });

    // --- Appearance --------------------------------------------------------
    auto* styleGroup = new QGroupBox(tr("Appearance"), this);
    auto* styleForm = new QFormLayout(styleGroup);
    colorButton_ = new QPushButton(styleGroup);
    connect(colorButton_, &QPushButton::clicked, this,
            &LatticePlaneDialog::chooseColor);
    styleForm->addRow(tr("Plane color:"), colorButton_);

    opacitySlider_ = new QSlider(Qt::Horizontal, styleGroup);
    opacitySlider_->setRange(0, 100);
    opacitySlider_->setValue(40);
    connect(opacitySlider_, &QSlider::valueChanged, this,
            [this] { rebuild(); });
    styleForm->addRow(tr("Opacity:"), opacitySlider_);

    edgesCheck_ = new QCheckBox(tr("Show edge outline"), styleGroup);
    edgesCheck_->setChecked(true);
    connect(edgesCheck_, &QCheckBox::toggled, this, [this] { rebuild(); });
    styleForm->addRow(QString(), edgesCheck_);

    visibleCheck_ = new QCheckBox(tr("Show plane in viewport"), styleGroup);
    visibleCheck_->setChecked(true);
    connect(visibleCheck_, &QCheckBox::toggled, this, [this] { rebuild(); });
    styleForm->addRow(QString(), visibleCheck_);
    layout->addWidget(styleGroup);

    // --- Volumetric color-slice -------------------------------------------
    auto* sliceGroup =
        new QGroupBox(tr("Volumetric color-slice (charge density / ELF)"), this);
    auto* sliceLayout = new QVBoxLayout(sliceGroup);
    sliceCheck_ = new QCheckBox(tr("Color the plane by a volumetric field"),
                                sliceGroup);
    connect(sliceCheck_, &QCheckBox::toggled, this, [this] { rebuild(); });
    sliceLayout->addWidget(sliceCheck_);

    auto* fieldRow = new QHBoxLayout;
    loadFieldButton_ = new QPushButton(tr("Load field…"), sliceGroup);
    connect(loadFieldButton_, &QPushButton::clicked, this,
            &LatticePlaneDialog::loadField);
    fieldRow->addWidget(loadFieldButton_);
    gradientCombo_ = new QComboBox(sliceGroup);
    gradientCombo_->addItem(tr("Viridis"),
                            int(render::ColorGradient::Viridis));
    gradientCombo_->addItem(tr("Plasma"), int(render::ColorGradient::Plasma));
    gradientCombo_->addItem(tr("Coolwarm"),
                            int(render::ColorGradient::Coolwarm));
    gradientCombo_->addItem(tr("Rainbow"), int(render::ColorGradient::Rainbow));
    connect(gradientCombo_, &QComboBox::currentIndexChanged, this,
            [this] { rebuild(); });
    fieldRow->addWidget(gradientCombo_, 1);
    sliceLayout->addLayout(fieldRow);
    fieldLabel_ = new QLabel(tr("No field loaded."), sliceGroup);
    fieldLabel_->setWordWrap(true);
    sliceLayout->addWidget(fieldLabel_);
    layout->addWidget(sliceGroup);

    auto* closeButton = new QPushButton(tr("Close"), this);
    connect(closeButton, &QPushButton::clicked, this, &QDialog::close);
    auto* buttonRow = new QHBoxLayout;
    buttonRow->addStretch(1);
    buttonRow->addWidget(closeButton);
    layout->addLayout(buttonRow);

    if (!structure_ || !structure_->cell().isDefined()) {
        orientGroup->setEnabled(false);
        offsetGroup->setEnabled(false);
        styleGroup->setEnabled(false);
        sliceGroup->setEnabled(false);
        fieldLabel_->setText(
            tr("This structure has no periodic unit cell, so no lattice plane "
               "can be defined."));
    }

    updateColorButton();
    rebuild();
}

void LatticePlaneDialog::closeEvent(QCloseEvent* event)
{
    if (viewport_)
        viewport_->clearLatticePlane();
    QDialog::closeEvent(event);
}

void LatticePlaneDialog::chooseColor()
{
    const QColor chosen = QColorDialog::getColor(planeColor_, this,
                                                 tr("Lattice Plane Color"));
    if (chosen.isValid()) {
        planeColor_ = chosen;
        updateColorButton();
        rebuild();
    }
}

void LatticePlaneDialog::updateColorButton()
{
    if (!colorButton_)
        return;
    colorButton_->setText(planeColor_.name());
    colorButton_->setStyleSheet(
        QStringLiteral("background-color: %1; color: %2;")
            .arg(planeColor_.name(),
                 planeColor_.lightnessF() > 0.5 ? QStringLiteral("#000")
                                                : QStringLiteral("#fff")));
}

void LatticePlaneDialog::loadField()
{
    const QString path = QFileDialog::getOpenFileName(
        this, tr("Load Volumetric Field"), QString(),
        tr("Volumetric grids (*.cube *.xsf CHGCAR* LOCPOT* PARCHG* ELFCAR*);;"
           "All files (*)"));
    if (path.isEmpty())
        return;
    try {
        field_ = std::make_shared<core::VolumetricData>(
            core::VolumetricData::load(path.toStdString()));
        fieldLabel_->setText(
            tr("Loaded: %1  (%2×%3×%4 grid)")
                .arg(QString::fromStdString(field_->label))
                .arg(field_->nx)
                .arg(field_->ny)
                .arg(field_->nz));
        sliceCheck_->setChecked(true);
        rebuild();
    } catch (const std::exception& e) {
        QMessageBox::warning(this, tr("Load Volumetric Field"),
                             tr("Could not read the field:\n%1")
                                 .arg(QString::fromLocal8Bit(e.what())));
    }
}

void LatticePlaneDialog::buildGeometry(std::vector<float>& faceTris,
                                       std::vector<float>& edgeLines) const
{
    faceTris.clear();
    edgeLines.clear();
    if (!structure_ || !structure_->cell().isDefined())
        return;

    const auto& v = structure_->cell().vectors();
    const core::Vec3 a = v[0], b = v[1], c = v[2];

    // (hkl) plane normal = h·(b×c) + k·(c×a) + l·(a×b) (reciprocal vectors).
    core::Vec3 normal = a.cross(b) * double(lSpin_->value())
        + b.cross(c) * double(hSpin_->value())
        + c.cross(a) * double(kSpin_->value());
    if (normal.norm() < 1e-9)
        normal = c; // degenerate (0 0 0) → fall back to the c-axis normal
    normal = normal.normalized();

    // In-plane orthonormal basis.
    const core::Vec3 helper =
        std::abs(normal.x) < 0.9 ? core::Vec3{1, 0, 0} : core::Vec3{0, 1, 0};
    const core::Vec3 uAxis = helper.cross(normal).normalized();
    const core::Vec3 vAxis = normal.cross(uAxis).normalized();

    const core::Vec3 center = (a + b + c) * 0.5;
    const core::Vec3 planeOrigin = center + normal * offsetSpin_->value();
    const double R = planeExtent_;

    // Optional field slice: solve Cartesian → field-box fractional via Cramer.
    const bool useField = sliceCheck_->isChecked() && field_ && !field_->empty();
    const auto gradient = static_cast<render::ColorGradient>(
        gradientCombo_->currentData().toInt());
    double lo = 0.0, hi = 1.0, invDet = 0.0;
    core::Vec3 fBC, fCA, fAB;
    if (useField) {
        lo = field_->minValue();
        hi = field_->maxValue();
        if (hi <= lo)
            hi = lo + 1.0;
        fBC = field_->spanB.cross(field_->spanC);
        fCA = field_->spanC.cross(field_->spanA);
        fAB = field_->spanA.cross(field_->spanB);
        const double det = field_->spanA.dot(fBC);
        invDet = std::abs(det) > 1e-12 ? 1.0 / det : 0.0;
    }

    const auto colorAt = [&](const core::Vec3& p) -> QColor {
        if (!useField || invDet == 0.0)
            return planeColor_;
        const core::Vec3 dp = p - field_->origin;
        const double uf = dp.dot(fBC) * invDet;
        const double vf = dp.dot(fCA) * invDet;
        const double wf = dp.dot(fAB) * invDet;
        const double value = field_->samplePeriodic(uf * field_->nx,
                                                     vf * field_->ny,
                                                     wf * field_->nz);
        const float t = static_cast<float>((value - lo) / (hi - lo));
        return render::ColorMap::sample(gradient, t);
    };

    const auto point = [&](int i, int j) {
        const double s = (double(i) / kGridDivisions * 2.0 - 1.0) * R;
        const double t = (double(j) / kGridDivisions * 2.0 - 1.0) * R;
        return planeOrigin + uAxis * s + vAxis * t;
    };

    // Tessellated quad (two triangles per cell), per-vertex colored.
    for (int i = 0; i < kGridDivisions; ++i) {
        for (int j = 0; j < kGridDivisions; ++j) {
            const core::Vec3 p00 = point(i, j), p10 = point(i + 1, j),
                             p11 = point(i + 1, j + 1), p01 = point(i, j + 1);
            const QColor c00 = colorAt(p00), c10 = colorAt(p10),
                         c11 = colorAt(p11), c01 = colorAt(p01);
            appendVertex(faceTris, p00, c00);
            appendVertex(faceTris, p10, c10);
            appendVertex(faceTris, p11, c11);
            appendVertex(faceTris, p00, c00);
            appendVertex(faceTris, p11, c11);
            appendVertex(faceTris, p01, c01);
        }
    }

    // Border outline (4 segments), drawn opaque in the plane color.
    if (edgesCheck_->isChecked()) {
        const std::array<core::Vec3, 4> corners = {
            point(0, 0), point(kGridDivisions, 0),
            point(kGridDivisions, kGridDivisions), point(0, kGridDivisions)};
        for (int e = 0; e < 4; ++e) {
            appendVertex(edgeLines, corners[e], planeColor_);
            appendVertex(edgeLines, corners[(e + 1) % 4], planeColor_);
        }
    }
}

void LatticePlaneDialog::rebuild()
{
    if (!viewport_)
        return;
    std::vector<float> faces, edges;
    buildGeometry(faces, edges);
    const float alpha = opacitySlider_->value() / 100.0f;
    const bool visible = visibleCheck_->isChecked() && !faces.empty();
    viewport_->setLatticePlane(std::move(faces), std::move(edges), alpha, visible,
                               edgesCheck_->isChecked());
}

} // namespace calango::gui
