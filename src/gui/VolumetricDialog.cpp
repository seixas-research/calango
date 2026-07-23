#include "gui/VolumetricDialog.hpp"

#include "gui/VolumeViewWidget.hpp"
#include "render/ColorMap.hpp"

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QSaveFile>
#include <QSignalBlocker>
#include <QSlider>
#include <QTextStream>
#include <QVBoxLayout>
#include <QtConcurrent>

#include <algorithm>
#include <array>
#include <cmath>

namespace calango::gui {

namespace {

/// Colormaps offered in the volume viewer (indices into this table).
const render::ColorGradient kMaps[] = {
    render::ColorGradient::Viridis,
    render::ColorGradient::Plasma,
    render::ColorGradient::Coolwarm,
    render::ColorGradient::Rainbow,
};

QStringList colormapNames()
{
    return {QStringLiteral("Viridis"), QStringLiteral("Plasma"),
            QStringLiteral("Coolwarm"), QStringLiteral("Rainbow")};
}

} // namespace

VolumetricDialog::VolumetricDialog(QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle(tr("Volumetric Data"));
    resize(980, 620);

    auto* layout = new QHBoxLayout(this);
    view_ = new VolumeViewWidget(this);
    layout->addWidget(view_, 1);

    auto* side = new QVBoxLayout;
    layout->addLayout(side);

    // --- Fields ------------------------------------------------------------
    auto* fieldsGroup = new QGroupBox(tr("Fields"), this);
    auto* fieldsForm = new QFormLayout(fieldsGroup);
    auto* loadA = new QPushButton(tr("Load Field A…"), fieldsGroup);
    fieldALabel_ = new QLabel(tr("(none — shapes the isosurface)"), fieldsGroup);
    fieldALabel_->setWordWrap(true);
    auto* loadB = new QPushButton(tr("Load Field B…"), fieldsGroup);
    fieldBLabel_ = new QLabel(tr("(optional — colors the isosurface, EPM)"),
                              fieldsGroup);
    fieldBLabel_->setWordWrap(true);
    fieldsForm->addRow(loadA, fieldALabel_);
    fieldsForm->addRow(loadB, fieldBLabel_);
    side->addWidget(fieldsGroup);
    connect(loadA, &QPushButton::clicked, this, &VolumetricDialog::loadFieldA);
    connect(loadB, &QPushButton::clicked, this, &VolumetricDialog::loadFieldB);

    // --- Isosurface --------------------------------------------------------
    isoGroup_ = new QGroupBox(tr("Isosurface"), this);
    isoGroup_->setCheckable(true);
    auto* isoForm = new QFormLayout(isoGroup_);
    auto* isoRow = new QWidget(isoGroup_);
    auto* isoRowLayout = new QHBoxLayout(isoRow);
    isoRowLayout->setContentsMargins(0, 0, 0, 0);
    isoSlider_ = new QSlider(Qt::Horizontal, isoRow);
    isoSlider_->setRange(0, 1000);
    isoSlider_->setValue(100);
    isoSpin_ = new QDoubleSpinBox(isoRow);
    isoSpin_->setDecimals(6);
    isoSpin_->setRange(-1e9, 1e9);
    isoRowLayout->addWidget(isoSlider_, 1);
    isoRowLayout->addWidget(isoSpin_);
    isoForm->addRow(tr("Isovalue:"), isoRow);

    epmCheck_ = new QCheckBox(tr("Color by Field B (potential map)"), isoGroup_);
    isoForm->addRow(epmCheck_);
    isoColormapCombo_ = new QComboBox(isoGroup_);
    isoColormapCombo_->addItems(colormapNames());
    isoColormapCombo_->setCurrentIndex(2); // Coolwarm suits potentials
    isoForm->addRow(tr("Colormap:"), isoColormapCombo_);
    epmRangeLabel_ = new QLabel(isoGroup_);
    isoForm->addRow(tr("B range:"), epmRangeLabel_);
    side->addWidget(isoGroup_);

    connect(isoGroup_, &QGroupBox::toggled, this, &VolumetricDialog::rebuildIso);
    connect(isoSlider_, &QSlider::valueChanged, this, [this] {
        const QSignalBlocker blocker(isoSpin_);
        isoSpin_->setValue(isovalueFromSlider());
        rebuildIso();
    });
    connect(isoSpin_, &QDoubleSpinBox::valueChanged, this, [this](double value) {
        if (!fieldA_)
            return;
        const double lo = fieldA_->minValue(), hi = fieldA_->maxValue();
        const QSignalBlocker blocker(isoSlider_);
        isoSlider_->setValue(static_cast<int>(
            std::clamp((value - lo) / std::max(hi - lo, 1e-30), 0.0, 1.0)
            * 1000.0));
        rebuildIso();
    });
    connect(epmCheck_, &QCheckBox::toggled, this, &VolumetricDialog::rebuildIso);
    connect(isoColormapCombo_, &QComboBox::currentIndexChanged,
            this, &VolumetricDialog::rebuildIso);

    // --- Slice plane -------------------------------------------------------
    sliceGroup_ = new QGroupBox(tr("Slice plane"), this);
    sliceGroup_->setCheckable(true);
    sliceGroup_->setChecked(false);
    auto* sliceForm = new QFormLayout(sliceGroup_);
    planeCombo_ = new QComboBox(sliceGroup_);
    planeCombo_->addItems({QStringLiteral("XY"), QStringLiteral("XZ"),
                           QStringLiteral("YZ"), tr("Custom normal")});
    sliceForm->addRow(tr("Plane:"), planeCombo_);
    auto* normalRow = new QWidget(sliceGroup_);
    auto* normalLayout = new QHBoxLayout(normalRow);
    normalLayout->setContentsMargins(0, 0, 0, 0);
    for (int i = 0; i < 3; ++i) {
        normalSpin_[i] = new QDoubleSpinBox(normalRow);
        normalSpin_[i]->setRange(-10.0, 10.0);
        normalSpin_[i]->setDecimals(2);
        normalSpin_[i]->setValue(i == 2 ? 1.0 : 0.0);
        normalLayout->addWidget(normalSpin_[i]);
    }
    sliceForm->addRow(tr("Normal:"), normalRow);
    offsetSlider_ = new QSlider(Qt::Horizontal, sliceGroup_);
    offsetSlider_->setRange(0, 100);
    offsetSlider_->setValue(50);
    sliceForm->addRow(tr("Offset:"), offsetSlider_);
    sliceFieldCombo_ = new QComboBox(sliceGroup_);
    sliceFieldCombo_->addItems({tr("Field A"), tr("Field B")});
    sliceForm->addRow(tr("Field:"), sliceFieldCombo_);
    sliceColormapCombo_ = new QComboBox(sliceGroup_);
    sliceColormapCombo_->addItems(colormapNames());
    sliceForm->addRow(tr("Colormap:"), sliceColormapCombo_);
    side->addWidget(sliceGroup_);

    connect(sliceGroup_, &QGroupBox::toggled, this, &VolumetricDialog::rebuildSlice);
    connect(planeCombo_, &QComboBox::currentIndexChanged,
            this, &VolumetricDialog::rebuildSlice);
    for (auto* spin : normalSpin_)
        connect(spin, &QDoubleSpinBox::valueChanged,
                this, &VolumetricDialog::rebuildSlice);
    connect(offsetSlider_, &QSlider::valueChanged,
            this, &VolumetricDialog::rebuildSlice);
    connect(sliceFieldCombo_, &QComboBox::currentIndexChanged,
            this, &VolumetricDialog::rebuildSlice);
    connect(sliceColormapCombo_, &QComboBox::currentIndexChanged,
            this, &VolumetricDialog::rebuildSlice);

    // --- Export / close ----------------------------------------------------
    auto* exportRow = new QHBoxLayout;
    auto* exportIsoButton = new QPushButton(tr("Export Isosurface (OBJ)…"), this);
    auto* exportSliceButton = new QPushButton(tr("Export Slice (CSV)…"), this);
    exportRow->addWidget(exportIsoButton);
    exportRow->addWidget(exportSliceButton);
    side->addLayout(exportRow);
    connect(exportIsoButton, &QPushButton::clicked,
            this, &VolumetricDialog::exportIso);
    connect(exportSliceButton, &QPushButton::clicked,
            this, &VolumetricDialog::exportSlice);

    side->addStretch(1);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    side->addWidget(buttons);

    connect(&isoWatcher_, &QFutureWatcher<core::IsoMesh>::finished,
            this, &VolumetricDialog::onIsoExtractionFinished);
}

VolumetricDialog::~VolumetricDialog()
{
    // QFutureWatcher's destructor does not wait, and the running lambda holds
    // shared_ptrs into members that are about to die. Detach from the signal
    // (so no slot fires into a half-destroyed dialog) and block until the
    // worker is done.
    isoWatcher_.disconnect(this);
    if (isoWatcher_.isRunning())
        isoWatcher_.waitForFinished();
}

double VolumetricDialog::isovalueFromSlider() const
{
    if (!fieldA_)
        return 0.0;
    const double lo = fieldA_->minValue(), hi = fieldA_->maxValue();
    return lo + (hi - lo) * isoSlider_->value() / 1000.0;
}

const core::VolumetricData* VolumetricDialog::sliceField() const
{
    if (sliceFieldCombo_->currentIndex() == 1)
        return fieldB_.get();
    return fieldA_.get();
}

void VolumetricDialog::loadField(FieldPtr& field, QLabel* label,
                                 const QString& role)
{
    const QString path = QFileDialog::getOpenFileName(
        this, tr("Load Volumetric %1").arg(role), QString(),
        tr("Volumetric data (*.cube *.xsf CHGCAR* LOCPOT* PARCHG* ELFCAR*);;"
           "All files (*)"));
    if (path.isEmpty())
        return;
    QApplication::setOverrideCursor(Qt::WaitCursor);
    try {
        // Freshly allocated each load: the previous grid stays alive as long
        // as an in-flight extraction still references it.
        field = std::make_shared<const core::VolumetricData>(
            core::VolumetricData::load(path.toStdString()));
        QApplication::restoreOverrideCursor();
    } catch (const std::exception& e) {
        QApplication::restoreOverrideCursor();
        QMessageBox::warning(this, windowTitle(), QString::fromUtf8(e.what()));
        return;
    }
    label->setText(QStringLiteral("%1 — %2×%3×%4, [%5, %6]")
                       .arg(QString::fromStdString(field->label))
                       .arg(field->nx)
                       .arg(field->ny)
                       .arg(field->nz)
                       .arg(field->minValue(), 0, 'g', 4)
                       .arg(field->maxValue(), 0, 'g', 4));
}

void VolumetricDialog::loadFieldA()
{
    loadField(fieldA_, fieldALabel_, QStringLiteral("Field A"));
    if (!fieldA_)
        return;
    view_->setBox(*fieldA_);
    {
        // Default isovalue: 10% of the range above the minimum.
        const QSignalBlocker blocker(isoSlider_);
        isoSlider_->setValue(100);
        const QSignalBlocker blocker2(isoSpin_);
        isoSpin_->setValue(isovalueFromSlider());
    }
    rebuildIso();
    rebuildSlice();
}

void VolumetricDialog::loadFieldB()
{
    loadField(fieldB_, fieldBLabel_, QStringLiteral("Field B"));
    rebuildIso();
    rebuildSlice();
}

void VolumetricDialog::rebuildIso()
{
    if (!fieldA_ || !isoGroup_->isChecked()) {
        // Bump the generation so any in-flight extraction's result is
        // discarded rather than drawn over the cleared view.
        ++isoGeneration_;
        isoRequestPending_ = false;
        isoMesh_ = {};
        view_->clearIsoMesh();
        return;
    }
    isoRequestPending_ = true;
    startIsoExtraction();
}

void VolumetricDialog::startIsoExtraction()
{
    // One at a time: a slider drag would otherwise spawn an extraction per
    // pixel of travel. The newest request wins when the current one lands.
    if (!isoRequestPending_ || isoWatcher_.isRunning())
        return;
    isoRequestPending_ = false;
    isoRunningGeneration_ = ++isoGeneration_;

    // Capture shared_ptr copies (a refcount bump, not a grid copy) so the
    // worker's inputs stay alive and unchanged even if the user loads a new
    // file or toggles EPM while it runs.
    FieldPtr field = fieldA_;
    FieldPtr colorField = (epmCheck_->isChecked() && fieldB_) ? fieldB_ : nullptr;
    const double isovalue = isoSpin_->value();

    isoWatcher_.setFuture(QtConcurrent::run(
        [field = std::move(field), colorField = std::move(colorField), isovalue] {
            return core::extractIsosurface(*field, isovalue, colorField.get());
        }));
}

void VolumetricDialog::onIsoExtractionFinished()
{
    // Stale result: its inputs changed (or the surface was switched off)
    // while it ran. Drop it and serve whatever is queued now.
    if (isoRunningGeneration_ != isoGeneration_) {
        startIsoExtraction();
        return;
    }

    isoMesh_ = isoWatcher_.result();
    // The color range is recomputed here rather than in the worker: it is
    // O(vertices) on an already-materialized vector, and keeping it on this
    // side means the worker returns exactly one value.
    const bool epm = epmCheck_->isChecked() && fieldB_ != nullptr;
    double lo = 0.0, hi = 1.0;
    if (epm && !isoMesh_.colorValues.empty()) {
        lo = *std::min_element(isoMesh_.colorValues.begin(),
                               isoMesh_.colorValues.end());
        hi = *std::max_element(isoMesh_.colorValues.begin(),
                               isoMesh_.colorValues.end());
        epmRangeLabel_->setText(QStringLiteral("%1 … %2")
                                    .arg(lo, 0, 'g', 4)
                                    .arg(hi, 0, 'g', 4));
    } else {
        epmRangeLabel_->setText(tr("—"));
    }
    view_->setIsoMesh(isoMesh_, kMaps[isoColormapCombo_->currentIndex()], lo, hi,
                      epm);

    startIsoExtraction(); // serve a request that arrived while we were busy
}

void VolumetricDialog::rebuildSlice()
{
    const core::VolumetricData* field = sliceField();
    sliceSamples_.clear();
    if (!field || !sliceGroup_->isChecked()) {
        view_->clearSlice();
        return;
    }

    const double lo = field->minValue(), hi = field->maxValue();
    const double range = std::max(hi - lo, 1e-30);
    const auto gradient = kMaps[sliceColormapCombo_->currentIndex()];
    const double offset = offsetSlider_->value() / 100.0;
    const int plane = planeCombo_->currentIndex();
    std::vector<float> stream;

    const auto emit_ = [&](const core::Vec3& p, double value) {
        const QColor c = render::ColorMap::sample(
            gradient, static_cast<float>((value - lo) / range));
        stream.insert(stream.end(),
                      {static_cast<float>(p.x), static_cast<float>(p.y),
                       static_cast<float>(p.z), static_cast<float>(c.redF()),
                       static_cast<float>(c.greenF()),
                       static_cast<float>(c.blueF())});
        sliceSamples_.push_back({p.x, p.y, p.z, value});
    };

    if (plane < 3) {
        // Grid-aligned slices sample exactly on the field's own axes:
        // XY = (A,B) at fixed C, XZ = (A,C) at fixed B, YZ = (B,C) at A.
        const int uAxis = plane == 2 ? 1 : 0;
        const int vAxis = plane == 0 ? 1 : 2;
        const int wAxis = 3 - uAxis - vAxis;
        const int dims[3] = {field->nx, field->ny, field->nz};
        const int nu = dims[uAxis], nv = dims[vAxis];
        const double w = offset * dims[wAxis];
        const auto gridPoint = [&](double u, double v) {
            double g[3];
            g[uAxis] = u;
            g[vAxis] = v;
            g[wAxis] = w;
            return std::array<double, 3>{g[0], g[1], g[2]};
        };
        for (int iu = 0; iu < nu; ++iu) {
            for (int iv = 0; iv < nv; ++iv) {
                const std::array<double, 3> q[4] = {
                    gridPoint(iu, iv), gridPoint(iu + 1, iv),
                    gridPoint(iu + 1, iv + 1), gridPoint(iu, iv + 1)};
                static constexpr int kQuad[6] = {0, 1, 2, 0, 2, 3};
                for (const int k : kQuad)
                    emit_(field->position(q[k][0], q[k][1], q[k][2]),
                          field->samplePeriodic(q[k][0], q[k][1], q[k][2]));
            }
        }
    } else {
        // Custom normal: an oriented square patch through the box at the
        // chosen offset along the normal, sampled at 96x96.
        core::Vec3 n{normalSpin_[0]->value(), normalSpin_[1]->value(),
                     normalSpin_[2]->value()};
        if (n.norm() < 1e-9)
            n = {0.0, 0.0, 1.0};
        n = n * (1.0 / n.norm());
        const core::Vec3 helper = std::abs(n.z) < 0.9
            ? core::Vec3{0.0, 0.0, 1.0}
            : core::Vec3{1.0, 0.0, 0.0};
        core::Vec3 u = helper.cross(n);
        u = u * (1.0 / u.norm());
        const core::Vec3 v = n.cross(u);

        const core::Vec3 boxCenter = field->origin
            + (field->spanA + field->spanB + field->spanC) * 0.5;
        const double half =
            (field->spanA + field->spanB + field->spanC).norm() * 0.5;
        const core::Vec3 center = boxCenter + n * ((offset - 0.5) * 2.0 * half);

        // Cartesian -> fractional for periodic sampling.
        const auto frac = [&](const core::Vec3& p) {
            const core::Vec3 d = p - field->origin;
            const double m[9] = {field->spanA.x, field->spanB.x, field->spanC.x,
                                 field->spanA.y, field->spanB.y, field->spanC.y,
                                 field->spanA.z, field->spanB.z, field->spanC.z};
            const double det = m[0] * (m[4] * m[8] - m[5] * m[7])
                - m[1] * (m[3] * m[8] - m[5] * m[6])
                + m[2] * (m[3] * m[7] - m[4] * m[6]);
            const double inv = std::abs(det) > 1e-30 ? 1.0 / det : 0.0;
            return std::array<double, 3>{
                ((m[4] * m[8] - m[5] * m[7]) * d.x
                 + (m[2] * m[7] - m[1] * m[8]) * d.y
                 + (m[1] * m[5] - m[2] * m[4]) * d.z)
                    * inv,
                ((m[5] * m[6] - m[3] * m[8]) * d.x
                 + (m[0] * m[8] - m[2] * m[6]) * d.y
                 + (m[2] * m[3] - m[0] * m[5]) * d.z)
                    * inv,
                ((m[3] * m[7] - m[4] * m[6]) * d.x
                 + (m[1] * m[6] - m[0] * m[7]) * d.y
                 + (m[0] * m[4] - m[1] * m[3]) * d.z)
                    * inv};
        };
        const auto sampleAt = [&](const core::Vec3& p) {
            const auto f = frac(p);
            return field->samplePeriodic(f[0] * field->nx, f[1] * field->ny,
                                         f[2] * field->nz);
        };
        constexpr int kRes = 96;
        for (int i = 0; i < kRes; ++i) {
            for (int j = 0; j < kRes; ++j) {
                const auto corner = [&](int a, int b) {
                    const double s = (a / static_cast<double>(kRes) - 0.5) * 2.0
                        * half;
                    const double t = (b / static_cast<double>(kRes) - 0.5) * 2.0
                        * half;
                    return center + u * s + v * t;
                };
                const core::Vec3 q[4] = {corner(i, j), corner(i + 1, j),
                                         corner(i + 1, j + 1), corner(i, j + 1)};
                static constexpr int kQuad[6] = {0, 1, 2, 0, 2, 3};
                for (const int k : kQuad)
                    emit_(q[k], sampleAt(q[k]));
            }
        }
    }
    view_->setSlice(stream);
}

void VolumetricDialog::exportIso()
{
    if (isoMesh_.positions.empty()) {
        QMessageBox::information(this, windowTitle(),
                                 tr("Extract an isosurface first."));
        return;
    }
    const QString path = QFileDialog::getSaveFileName(
        this, tr("Export Isosurface"), QStringLiteral("isosurface.obj"),
        tr("Wavefront OBJ (*.obj)"));
    if (path.isEmpty())
        return;
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QMessageBox::warning(this, windowTitle(),
                             tr("Could not write %1").arg(path));
        return;
    }
    QTextStream out(&file);
    out << "# Calango isosurface (" << isoMesh_.positions.size() / 3
        << " triangles)\n";
    for (const auto& p : isoMesh_.positions)
        out << "v " << p.x << ' ' << p.y << ' ' << p.z << '\n';
    for (const auto& n : isoMesh_.normals)
        out << "vn " << n.x << ' ' << n.y << ' ' << n.z << '\n';
    for (std::size_t i = 0; i + 2 < isoMesh_.positions.size(); i += 3)
        out << "f " << i + 1 << "//" << i + 1 << ' ' << i + 2 << "//" << i + 2
            << ' ' << i + 3 << "//" << i + 3 << '\n';
    file.commit();
}

void VolumetricDialog::exportSlice()
{
    if (sliceSamples_.empty()) {
        QMessageBox::information(this, windowTitle(),
                                 tr("Enable a slice plane first."));
        return;
    }
    const QString path = QFileDialog::getSaveFileName(
        this, tr("Export Slice"), QStringLiteral("slice.csv"),
        tr("CSV (*.csv)"));
    if (path.isEmpty())
        return;
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QMessageBox::warning(this, windowTitle(),
                             tr("Could not write %1").arg(path));
        return;
    }
    QTextStream out(&file);
    out << "x_A,y_A,z_A,value\n";
    for (const auto& s : sliceSamples_)
        out << s[0] << ',' << s[1] << ',' << s[2] << ',' << s[3] << '\n';
    file.commit();
}

} // namespace calango::gui
