#include "gui/CustomOverlayDialog.hpp"

#include "gui/ViewportWidget.hpp"

#include <QCheckBox>
#include <QColorDialog>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QSlider>
#include <QSpinBox>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>

namespace calango::gui {

using core::Vec3;

namespace {

void appendVertex(std::vector<float>& out, const Vec3& p, const QColor& c)
{
    out.push_back(float(p.x));
    out.push_back(float(p.y));
    out.push_back(float(p.z));
    out.push_back(float(c.redF()));
    out.push_back(float(c.greenF()));
    out.push_back(float(c.blueF()));
}

void appendTri(std::vector<float>& out, const Vec3& a, const Vec3& b,
               const Vec3& c, const QColor& ca, const QColor& cb, const QColor& cc)
{
    appendVertex(out, a, ca);
    appendVertex(out, b, cb);
    appendVertex(out, c, cc);
}

QColor lerpColor(const QColor& a, const QColor& b, double t)
{
    t = std::clamp(t, 0.0, 1.0);
    return QColor::fromRgbF(a.redF() + (b.redF() - a.redF()) * t,
                            a.greenF() + (b.greenF() - a.greenF()) * t,
                            a.blueF() + (b.blueF() - a.blueF()) * t);
}

/// Per-quad color for a parametric surface cell (i,j) of an (ni×nj) grid.
QColor texel(const CustomOverlayDialog::Primitive& p, int i, int j, int ni, int nj)
{
    using T = CustomOverlayDialog::TextureStyle;
    switch (p.texture) {
    case T::Checkerboard: {
        const int su = std::max(1, ni / 8), sv = std::max(1, nj / 8);
        return ((i / su + j / sv) % 2 == 0) ? p.color : p.color2;
    }
    case T::Gradient:
        return lerpColor(p.color, p.color2, double(i) / std::max(1, ni - 1));
    default: // Solid / Glassy / Wireframe all use the base color per-vertex
        return p.color;
    }
}

Vec3 rotateEuler(const Vec3& p, const Vec3& degrees)
{
    const double rx = degrees.x * M_PI / 180.0, ry = degrees.y * M_PI / 180.0,
                 rz = degrees.z * M_PI / 180.0;
    // Rz · Ry · Rx.
    Vec3 v = p;
    v = {v.x, v.y * std::cos(rx) - v.z * std::sin(rx),
         v.y * std::sin(rx) + v.z * std::cos(rx)};
    v = {v.x * std::cos(ry) + v.z * std::sin(ry), v.y,
         -v.x * std::sin(ry) + v.z * std::cos(ry)};
    v = {v.x * std::cos(rz) - v.y * std::sin(rz),
         v.x * std::sin(rz) + v.y * std::cos(rz), v.z};
    return v;
}

// ---- primitive tessellators (append pos+color triangles to `out`) ----------

void genSphere(const CustomOverlayDialog::Primitive& p, std::vector<float>& out)
{
    const bool ellipsoid = p.type == CustomOverlayDialog::PrimitiveType::Ellipsoid;
    const double rx = ellipsoid ? p.size.x : p.size.x;
    const double ry = ellipsoid ? p.size.y : p.size.x;
    const double rz = ellipsoid ? p.size.z : p.size.x;
    const bool corr = p.finish == CustomOverlayDialog::SurfaceFinish::Corrugated;
    const int R = std::clamp(p.resolution, 8, 128);
    const int S = 2 * R;
    const auto pt = [&](int i, int j) {
        const double v = M_PI * i / R;      // 0..π
        const double u = 2.0 * M_PI * j / S; // 0..2π
        double disp = corr ? 1.0 + 0.12 * std::sin(6 * u) * std::sin(6 * v) : 1.0;
        return p.center
            + Vec3{rx * std::sin(v) * std::cos(u) * disp,
                   ry * std::sin(v) * std::sin(u) * disp, rz * std::cos(v) * disp};
    };
    for (int i = 0; i < R; ++i)
        for (int j = 0; j < S; ++j) {
            const QColor c = texel(p, i, j, R, S);
            appendTri(out, pt(i, j), pt(i + 1, j), pt(i + 1, j + 1), c, c, c);
            appendTri(out, pt(i, j), pt(i + 1, j + 1), pt(i, j + 1), c, c, c);
        }
}

void genBox(const CustomOverlayDialog::Primitive& p, std::vector<float>& out)
{
    const Vec3 h = p.size * 0.5;
    const auto corner = [&](int sx, int sy, int sz) {
        return p.center
            + rotateEuler({sx * h.x, sy * h.y, sz * h.z}, p.rotationDeg);
    };
    // 6 faces, each 4 corners (CCW), 2 triangles.
    const int idx[6][4] = {{0, 1, 3, 2}, {4, 6, 7, 5}, {0, 4, 5, 1},
                           {2, 3, 7, 6}, {0, 2, 6, 4}, {1, 5, 7, 3}};
    const Vec3 verts[8] = {corner(-1, -1, -1), corner(1, -1, -1), corner(-1, 1, -1),
                           corner(1, 1, -1),  corner(-1, -1, 1), corner(1, -1, 1),
                           corner(-1, 1, 1),  corner(1, 1, 1)};
    for (int f = 0; f < 6; ++f) {
        const QColor c = texel(p, f, 0, 6, 1);
        const Vec3& a = verts[idx[f][0]];
        const Vec3& b = verts[idx[f][1]];
        const Vec3& d = verts[idx[f][2]];
        const Vec3& e = verts[idx[f][3]];
        appendTri(out, a, b, d, c, c, c);
        appendTri(out, a, d, e, c, c, c);
    }
}

void genCylinderCone(const CustomOverlayDialog::Primitive& p,
                     std::vector<float>& out, bool cone)
{
    const Vec3 axisVec = p.endPoint - p.center;
    const double len = axisVec.norm();
    if (len < 1e-6)
        return;
    const Vec3 axis = axisVec / len;
    const Vec3 helper =
        std::abs(axis.x) < 0.9 ? Vec3{1, 0, 0} : Vec3{0, 1, 0};
    const Vec3 uAxis = helper.cross(axis).normalized();
    const Vec3 vAxis = axis.cross(uAxis).normalized();
    const bool corr = p.finish == CustomOverlayDialog::SurfaceFinish::Corrugated;
    const int S = std::clamp(p.resolution, 6, 128);
    const auto ring = [&](int j, double frac) {
        const double th = 2.0 * M_PI * j / S;
        const double rr = (cone ? p.radius * (1.0 - frac) : p.radius)
            * (corr ? 1.0 + 0.10 * std::sin(8 * th) : 1.0);
        return p.center + axis * (len * frac)
            + (uAxis * std::cos(th) + vAxis * std::sin(th)) * rr;
    };
    for (int j = 0; j < S; ++j) {
        const QColor c = texel(p, j, 0, S, 1);
        const Vec3 b0 = ring(j, 0.0), b1 = ring(j + 1, 0.0);
        const Vec3 t0 = ring(j, 1.0), t1 = ring(j + 1, 1.0);
        appendTri(out, b0, b1, t1, c, c, c);
        if (!cone)
            appendTri(out, b0, t1, t0, c, c, c);
        else
            appendTri(out, b0, b1, p.center + axis * len, c, c, c); // apex
        // Base cap.
        appendTri(out, p.center, b1, b0, c, c, c);
    }
}

void genDisk(const CustomOverlayDialog::Primitive& p, std::vector<float>& out)
{
    const Vec3 n = p.normal.norm() > 1e-9 ? p.normal.normalized() : Vec3{0, 0, 1};
    const Vec3 helper = std::abs(n.x) < 0.9 ? Vec3{1, 0, 0} : Vec3{0, 1, 0};
    const Vec3 uAxis = helper.cross(n).normalized();
    const Vec3 vAxis = n.cross(uAxis).normalized();
    const int S = std::clamp(p.resolution, 6, 256);
    const auto rim = [&](int j) {
        const double th = 2.0 * M_PI * j / S;
        return p.center + (uAxis * std::cos(th) + vAxis * std::sin(th)) * p.radius;
    };
    for (int j = 0; j < S; ++j) {
        const QColor c = texel(p, j, 0, S, 1);
        appendTri(out, p.center, rim(j), rim(j + 1), c, c, c);
    }
}

void genPlane(const CustomOverlayDialog::Primitive& p, std::vector<float>& out)
{
    const Vec3 n = p.normal.norm() > 1e-9 ? p.normal.normalized() : Vec3{0, 0, 1};
    const Vec3 helper = std::abs(n.x) < 0.9 ? Vec3{1, 0, 0} : Vec3{0, 1, 0};
    const Vec3 uAxis = helper.cross(n).normalized();
    const Vec3 vAxis = n.cross(uAxis).normalized();
    const bool corr = p.finish == CustomOverlayDialog::SurfaceFinish::Corrugated;
    const int N = std::clamp(p.resolution, 2, 128);
    const double R = p.radius;
    const auto pt = [&](int i, int j) {
        const double s = (double(i) / N * 2.0 - 1.0) * R;
        const double t = (double(j) / N * 2.0 - 1.0) * R;
        const double d = corr ? 0.15 * R * std::sin(4 * s) * std::sin(4 * t) : 0.0;
        return p.center + uAxis * s + vAxis * t + n * d;
    };
    for (int i = 0; i < N; ++i)
        for (int j = 0; j < N; ++j) {
            const QColor c = texel(p, i, j, N, N);
            appendTri(out, pt(i, j), pt(i + 1, j), pt(i + 1, j + 1), c, c, c);
            appendTri(out, pt(i, j), pt(i + 1, j + 1), pt(i, j + 1), c, c, c);
        }
}

void generate(const CustomOverlayDialog::Primitive& p, std::vector<float>& out)
{
    using PT = CustomOverlayDialog::PrimitiveType;
    switch (p.type) {
    case PT::Sphere:
    case PT::Ellipsoid: genSphere(p, out); break;
    case PT::Box: genBox(p, out); break;
    case PT::Cylinder: genCylinderCone(p, out, /*cone=*/false); break;
    case PT::Cone: genCylinderCone(p, out, /*cone=*/true); break;
    case PT::Disk: genDisk(p, out); break;
    case PT::Plane: genPlane(p, out); break;
    }
}
} // namespace

CustomOverlayDialog::CustomOverlayDialog(ViewportWidget* viewport, QWidget* parent)
    : QDialog(parent)
    , viewport_(viewport)
{
    setWindowTitle(tr("Custom Overlay Manager"));
    resize(560, 520);

    auto* root = new QHBoxLayout(this);

    // --- Left: primitive list + add/remove --------------------------------
    auto* leftCol = new QVBoxLayout;
    list_ = new QListWidget(this);
    leftCol->addWidget(list_, 1);
    connect(list_, &QListWidget::currentRowChanged, this,
            &CustomOverlayDialog::onSelectionChanged);
    auto* addRow = new QHBoxLayout;
    auto* addButton = new QPushButton(tr("Add"), this);
    connect(addButton, &QPushButton::clicked, this,
            &CustomOverlayDialog::addPrimitive);
    auto* removeButton = new QPushButton(tr("Remove"), this);
    connect(removeButton, &QPushButton::clicked, this,
            &CustomOverlayDialog::removePrimitive);
    addRow->addWidget(addButton);
    addRow->addWidget(removeButton);
    leftCol->addLayout(addRow);
    root->addLayout(leftCol);

    // --- Right: editor for the selected primitive -------------------------
    auto* editor = new QGroupBox(tr("Primitive"), this);
    auto* form = new QFormLayout(editor);

    typeCombo_ = new QComboBox(editor);
    typeCombo_->addItem(tr("Sphere"), int(PrimitiveType::Sphere));
    typeCombo_->addItem(tr("Ellipsoid"), int(PrimitiveType::Ellipsoid));
    typeCombo_->addItem(tr("Box / Parallelepiped"), int(PrimitiveType::Box));
    typeCombo_->addItem(tr("Cylinder / Tube"), int(PrimitiveType::Cylinder));
    typeCombo_->addItem(tr("Cone"), int(PrimitiveType::Cone));
    typeCombo_->addItem(tr("Plane / Slice"), int(PrimitiveType::Plane));
    typeCombo_->addItem(tr("Disk"), int(PrimitiveType::Disk));
    form->addRow(tr("Type:"), typeCombo_);
    connect(typeCombo_, &QComboBox::currentIndexChanged, this, [this] {
        showRelevantRows();
        applyEditor();
    });

    const auto vec3Row = [&](QDoubleSpinBox* (&spins)[3], double lo, double hi) {
        auto* w = new QWidget(editor);
        auto* h = new QHBoxLayout(w);
        h->setContentsMargins(0, 0, 0, 0);
        for (auto*& s : spins) {
            s = new QDoubleSpinBox(w);
            s->setRange(lo, hi);
            s->setDecimals(3);
            s->setSingleStep(0.25);
            h->addWidget(s);
            connect(s, &QDoubleSpinBox::valueChanged, this,
                    &CustomOverlayDialog::applyEditor);
        }
        return w;
    };
    auto* centerRow = vec3Row(centerSpin_, -1000, 1000);
    form->addRow(tr("Center / origin:"), centerRow);
    sizeRow_ = vec3Row(sizeSpin_, 0.01, 1000);
    form->addRow(tr("Radii / dimensions:"), sizeRow_);
    endRow_ = vec3Row(endSpin_, -1000, 1000);
    form->addRow(tr("End point:"), endRow_);
    normalRow_ = vec3Row(normalSpin_, -1000, 1000);
    form->addRow(tr("Normal:"), normalRow_);
    rotationRow_ = vec3Row(rotationSpin_, -360, 360);
    form->addRow(tr("Rotation (° XYZ):"), rotationRow_);

    radiusRow_ = new QWidget(editor);
    auto* radiusLayout = new QHBoxLayout(radiusRow_);
    radiusLayout->setContentsMargins(0, 0, 0, 0);
    radiusSpin_ = new QDoubleSpinBox(radiusRow_);
    radiusSpin_->setRange(0.01, 1000);
    radiusSpin_->setDecimals(3);
    radiusSpin_->setValue(1.5);
    radiusLayout->addWidget(radiusSpin_);
    connect(radiusSpin_, &QDoubleSpinBox::valueChanged, this,
            &CustomOverlayDialog::applyEditor);
    form->addRow(tr("Radius / extent:"), radiusRow_);

    resolutionSpin_ = new QSpinBox(editor);
    resolutionSpin_->setRange(2, 128);
    resolutionSpin_->setValue(32);
    connect(resolutionSpin_, &QSpinBox::valueChanged, this,
            &CustomOverlayDialog::applyEditor);
    form->addRow(tr("Resolution:"), resolutionSpin_);

    textureCombo_ = new QComboBox(editor);
    textureCombo_->addItem(tr("Solid color"), int(TextureStyle::Solid));
    textureCombo_->addItem(tr("Checkerboard"), int(TextureStyle::Checkerboard));
    textureCombo_->addItem(tr("Wireframe mesh"), int(TextureStyle::Wireframe));
    textureCombo_->addItem(tr("Translucent / glassy"), int(TextureStyle::Glassy));
    textureCombo_->addItem(tr("Color gradient"), int(TextureStyle::Gradient));
    connect(textureCombo_, &QComboBox::currentIndexChanged, this,
            &CustomOverlayDialog::applyEditor);
    form->addRow(tr("Texture:"), textureCombo_);

    finishCombo_ = new QComboBox(editor);
    finishCombo_->addItem(tr("Smooth"), int(SurfaceFinish::Smooth));
    finishCombo_->addItem(tr("Corrugated / wavy"), int(SurfaceFinish::Corrugated));
    connect(finishCombo_, &QComboBox::currentIndexChanged, this,
            &CustomOverlayDialog::applyEditor);
    form->addRow(tr("Surface finish:"), finishCombo_);

    auto* colorRow = new QHBoxLayout;
    colorButton_ = new QPushButton(tr("Color…"), editor);
    connect(colorButton_, &QPushButton::clicked, this, [this] {
        if (Primitive* p = current()) {
            const QColor c = QColorDialog::getColor(p->color, this, tr("Color"));
            if (c.isValid()) { p->color = c; rebuild(); }
        }
    });
    color2Button_ = new QPushButton(tr("2nd color…"), editor);
    connect(color2Button_, &QPushButton::clicked, this, [this] {
        if (Primitive* p = current()) {
            const QColor c =
                QColorDialog::getColor(p->color2, this, tr("Second color"));
            if (c.isValid()) { p->color2 = c; rebuild(); }
        }
    });
    colorRow->addWidget(colorButton_);
    colorRow->addWidget(color2Button_);
    form->addRow(tr("Colors:"), colorRow);

    opacitySlider_ = new QSlider(Qt::Horizontal, editor);
    opacitySlider_->setRange(0, 100);
    opacitySlider_->setValue(60);
    connect(opacitySlider_, &QSlider::valueChanged, this,
            &CustomOverlayDialog::applyEditor);
    form->addRow(tr("Opacity:"), opacitySlider_);

    visibleCheck_ = new QCheckBox(tr("Visible"), editor);
    visibleCheck_->setChecked(true);
    connect(visibleCheck_, &QCheckBox::toggled, this,
            &CustomOverlayDialog::applyEditor);
    form->addRow(QString(), visibleCheck_);

    root->addWidget(editor, 1);

    editor->setEnabled(false);
    showRelevantRows();
}

void CustomOverlayDialog::closeEvent(QCloseEvent* event)
{
    if (viewport_)
        viewport_->clearCustomOverlay();
    QDialog::closeEvent(event);
}

CustomOverlayDialog::Primitive* CustomOverlayDialog::current()
{
    const int row = list_->currentRow();
    if (row < 0 || row >= int(primitives_.size()))
        return nullptr;
    return &primitives_[std::size_t(row)];
}

void CustomOverlayDialog::addPrimitive()
{
    Primitive p;
    p.name = tr("Sphere %1").arg(primitives_.size() + 1);
    primitives_.push_back(p);
    list_->addItem(p.name);
    list_->setCurrentRow(int(primitives_.size()) - 1);
    rebuild();
}

void CustomOverlayDialog::removePrimitive()
{
    const int row = list_->currentRow();
    if (row < 0 || row >= int(primitives_.size()))
        return;
    primitives_.erase(primitives_.begin() + row);
    delete list_->takeItem(row);
    rebuild();
}

void CustomOverlayDialog::onSelectionChanged()
{
    Primitive* p = current();
    static_cast<QWidget*>(typeCombo_->parentWidget())->setEnabled(p != nullptr);
    if (p)
        loadEditor(*p);
}

void CustomOverlayDialog::loadEditor(const Primitive& p)
{
    loading_ = true;
    typeCombo_->setCurrentIndex(typeCombo_->findData(int(p.type)));
    const auto setVec = [](QDoubleSpinBox* (&s)[3], const core::Vec3& v) {
        s[0]->setValue(v.x); s[1]->setValue(v.y); s[2]->setValue(v.z);
    };
    setVec(centerSpin_, p.center);
    setVec(sizeSpin_, p.size);
    setVec(endSpin_, p.endPoint);
    setVec(normalSpin_, p.normal);
    setVec(rotationSpin_, p.rotationDeg);
    radiusSpin_->setValue(p.radius);
    resolutionSpin_->setValue(p.resolution);
    textureCombo_->setCurrentIndex(textureCombo_->findData(int(p.texture)));
    finishCombo_->setCurrentIndex(finishCombo_->findData(int(p.finish)));
    opacitySlider_->setValue(int(std::lround(p.opacity * 100.0)));
    visibleCheck_->setChecked(p.visible);
    loading_ = false;
    showRelevantRows();
}

void CustomOverlayDialog::applyEditor()
{
    if (loading_)
        return;
    Primitive* p = current();
    if (!p)
        return;
    p->type = static_cast<PrimitiveType>(typeCombo_->currentData().toInt());
    const auto getVec = [](QDoubleSpinBox* const (&s)[3]) {
        return core::Vec3{s[0]->value(), s[1]->value(), s[2]->value()};
    };
    p->center = getVec(centerSpin_);
    p->size = getVec(sizeSpin_);
    p->endPoint = getVec(endSpin_);
    p->normal = getVec(normalSpin_);
    p->rotationDeg = getVec(rotationSpin_);
    p->radius = radiusSpin_->value();
    p->resolution = resolutionSpin_->value();
    p->texture = static_cast<TextureStyle>(textureCombo_->currentData().toInt());
    p->finish = static_cast<SurfaceFinish>(finishCombo_->currentData().toInt());
    p->opacity = opacitySlider_->value() / 100.0;
    p->visible = visibleCheck_->isChecked();
    rebuild();
}

void CustomOverlayDialog::showRelevantRows()
{
    const auto type = static_cast<PrimitiveType>(typeCombo_->currentData().toInt());
    const bool round = type == PrimitiveType::Sphere
        || type == PrimitiveType::Ellipsoid;
    const bool box = type == PrimitiveType::Box;
    const bool tube = type == PrimitiveType::Cylinder || type == PrimitiveType::Cone;
    const bool planar = type == PrimitiveType::Plane || type == PrimitiveType::Disk;
    if (sizeRow_) sizeRow_->setVisible(round || box);
    if (endRow_) endRow_->setVisible(tube);
    if (normalRow_) normalRow_->setVisible(planar);
    if (rotationRow_) rotationRow_->setVisible(box);
    if (radiusRow_) radiusRow_->setVisible(tube || planar);
}

void CustomOverlayDialog::rebuild()
{
    if (!viewport_)
        return;
    std::vector<float> faces, edges;
    std::vector<render::StructureRenderer::OverlayRange> ranges;
    for (const Primitive& p : primitives_) {
        if (!p.visible)
            continue;
        std::vector<float> tri;
        generate(p, tri);
        if (tri.empty())
            continue;
        if (p.texture == TextureStyle::Wireframe) {
            // Convert each triangle (3 verts × 6 floats) into its 3 edges.
            for (std::size_t v = 0; v + 18 <= tri.size(); v += 18) {
                const auto edge = [&](std::size_t i0, std::size_t i1) {
                    for (int k = 0; k < 6; ++k) edges.push_back(tri[i0 + k]);
                    for (int k = 0; k < 6; ++k) edges.push_back(tri[i1 + k]);
                };
                edge(v, v + 6);
                edge(v + 6, v + 12);
                edge(v + 12, v);
            }
        } else {
            const int first = int(faces.size() / 6);
            faces.insert(faces.end(), tri.begin(), tri.end());
            ranges.push_back({first, int(tri.size() / 6),
                              float(p.opacity)});
        }
    }
    viewport_->setCustomOverlay(std::move(faces), std::move(edges),
                               std::move(ranges), !primitives_.empty());
}

} // namespace calango::gui
