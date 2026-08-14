#include "gui/WannierDialog.hpp"

#include "core/IsosurfaceContinuation.hpp"

#include "gui/VolumeViewWidget.hpp"
#include "render/ColorMap.hpp"

#include <QApplication>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSlider>
#include <QTableWidget>
#include <QVBoxLayout>
#include <QtConcurrent>

#include <algorithm>
#include <array>
#include <cmath>

namespace calango::gui {

namespace {

/// Colormaps offered in the Wannier viewer (indices into this table).
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

WannierDialog::WannierDialog(std::shared_ptr<core::Structure> structure,
                             QWidget* parent)
    : QDialog(parent), structure_(std::move(structure))
{
    setWindowTitle(tr("Wannier Functions"));
    resize(1040, 660);

    auto* layout = new QHBoxLayout(this);
    view_ = new VolumeViewWidget(this);
    layout->addWidget(view_, 1);

    auto* side = new QVBoxLayout;
    layout->addLayout(side);

    infoLabel_ = new QLabel(this);
    if (structure_)
        infoLabel_->setText(
            tr("Structure: %1 (%2 atoms)")
                .arg(QString::fromStdString(structure_->chemicalFormula()))
                .arg(structure_->size()));
    infoLabel_->setWordWrap(true);
    side->addWidget(infoLabel_);

    // --- Wannier functions (results) --------------------------------------
    auto* resultsGroup = new QGroupBox(tr("Wannier functions"), this);
    auto* resultsLayout = new QVBoxLayout(resultsGroup);

    table_ = new QTableWidget(0, 5, resultsGroup);
    table_->setHorizontalHeaderLabels(
        {tr("#"), tr("center x (Å)"), tr("center y (Å)"), tr("center z (Å)"),
         tr("spread Ω (Å²)")});
    table_->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    resultsLayout->addWidget(table_);

    totalSpreadLabel_ = new QLabel(tr("Total spread Ω: —"), resultsGroup);
    totalSpreadLabel_->setWordWrap(true);
    resultsLayout->addWidget(totalSpreadLabel_);

    auto* orbitalRow = new QHBoxLayout;
    orbitalRow->addWidget(new QLabel(tr("Show orbital:"), resultsGroup));
    orbitalCombo_ = new QComboBox(resultsGroup);
    orbitalCombo_->setToolTip(
        tr("Load the selected Wannier orbital's real-space isosurface "
           "(wannier_<n>.cube) from the results directory."));
    orbitalRow->addWidget(orbitalCombo_, 1);
    resultsLayout->addLayout(orbitalRow);
    side->addWidget(resultsGroup);
    connect(orbitalCombo_, &QComboBox::currentIndexChanged, this,
            &WannierDialog::orbitalSelected);

    // --- Load results ------------------------------------------------------
    auto* loadGroup = new QGroupBox(tr("Load results…"), this);
    auto* loadLayout = new QVBoxLayout(loadGroup);
    auto* loadButton = new QPushButton(tr("Load wannier.json…"), loadGroup);
    gridLabel_ = new QLabel(tr("(no orbital loaded)"), loadGroup);
    gridLabel_->setWordWrap(true);
    loadLayout->addWidget(loadButton);
    loadLayout->addWidget(gridLabel_);
    side->addWidget(loadGroup);
    connect(loadButton, &QPushButton::clicked, this,
            &WannierDialog::loadResultsDialog);

    // --- Isosurface --------------------------------------------------------
    isoGroup_ = new QGroupBox(tr("Isosurface"), this);
    isoGroup_->setCheckable(true);
    auto* isoForm = new QFormLayout(isoGroup_);
    auto* isoRow = new QWidget(isoGroup_);
    auto* isoRowLayout = new QHBoxLayout(isoRow);
    isoRowLayout->setContentsMargins(0, 0, 0, 0);
    // Wannier orbitals ψ(r) are signed; the slider 0..1000 maps linearly onto
    // [0, fieldMax_], the amplitude ceiling of the loaded cube (see loadCube).
    isoSlider_ = new QSlider(Qt::Horizontal, isoRow);
    isoSlider_->setRange(0, 1000);
    isoSlider_->setValue(200); // a low amplitude reveals the orbital's lobes
    isoSpin_ = new QDoubleSpinBox(isoRow);
    isoSpin_->setDecimals(4);
    isoSpin_->setRange(0.0, 1.0);
    isoSpin_->setSingleStep(0.01);
    isoSpin_->setValue(isovalueFromSlider());
    isoRowLayout->addWidget(isoSlider_, 1);
    isoRowLayout->addWidget(isoSpin_);
    isoForm->addRow(tr("Isovalue:"), isoRow);

    isoColormapCombo_ = new QComboBox(isoGroup_);
    isoColormapCombo_->addItems(colormapNames());
    isoForm->addRow(tr("Colormap:"), isoColormapCombo_);

    // How far past the home cell the orbital is followed. A Wannier function
    // is localized but not confined — its centre lands wherever the
    // wannierization put it — so without this the lobe is cut flat at the cell
    // face and its remainder appears across the box.
    continuationSpin_ = new QDoubleSpinBox(isoGroup_);
    continuationSpin_->setRange(0.0, core::kMaxContinuationMargin);
    continuationSpin_->setSingleStep(0.25);
    continuationSpin_->setDecimals(2);
    continuationSpin_->setSuffix(tr(" cells"));
    continuationSpin_->setValue(core::kDefaultContinuationMargin);
    continuationSpin_->setToolTip(
        tr("How far past the home cell the orbital's isosurface is followed, "
           "in cell units.\n\n"
           "The neighbouring periodic images hold the same function continued, "
           "so the surface is extracted over a window centred on the Wannier "
           "centre and the copies that window also covers are dropped. 0.5 "
           "shows a function whole wherever its centre fell; raise it for "
           "longer tails — the cost grows as the cube of the window."));
    isoForm->addRow(tr("Periodic continuation:"), continuationSpin_);
    connect(continuationSpin_, &QDoubleSpinBox::valueChanged, this,
            [this] { rebuildIso(); });

    side->addWidget(isoGroup_);

    connect(isoGroup_, &QGroupBox::toggled, this, &WannierDialog::rebuildIso);
    connect(isoSlider_, &QSlider::valueChanged, this, [this] {
        const QSignalBlocker blocker(isoSpin_);
        isoSpin_->setValue(isovalueFromSlider());
        rebuildIso();
    });
    connect(isoSpin_, &QDoubleSpinBox::valueChanged, this, [this](double value) {
        const QSignalBlocker blocker(isoSlider_);
        const double frac = fieldMax_ > 0.0 ? value / fieldMax_ : 0.0;
        isoSlider_->setValue(
            static_cast<int>(std::clamp(frac, 0.0, 1.0) * 1000.0));
        rebuildIso();
    });
    connect(isoColormapCombo_, &QComboBox::currentIndexChanged, this,
            &WannierDialog::rebuildIso);

    // --- Slice plane -------------------------------------------------------
    sliceGroup_ = new QGroupBox(tr("Slice plane"), this);
    auto* sliceForm = new QFormLayout(sliceGroup_);
    planeCombo_ = new QComboBox(sliceGroup_);
    planeCombo_->addItems({tr("Off"), QStringLiteral("XY"),
                           QStringLiteral("XZ"), QStringLiteral("YZ")});
    sliceForm->addRow(tr("Plane:"), planeCombo_);
    offsetSlider_ = new QSlider(Qt::Horizontal, sliceGroup_);
    offsetSlider_->setRange(0, 100);
    offsetSlider_->setValue(50);
    sliceForm->addRow(tr("Offset:"), offsetSlider_);
    sliceColormapCombo_ = new QComboBox(sliceGroup_);
    sliceColormapCombo_->addItems(colormapNames());
    sliceForm->addRow(tr("Colormap:"), sliceColormapCombo_);
    side->addWidget(sliceGroup_);

    connect(planeCombo_, &QComboBox::currentIndexChanged, this,
            &WannierDialog::rebuildSlice);
    connect(offsetSlider_, &QSlider::valueChanged, this,
            &WannierDialog::rebuildSlice);
    connect(sliceColormapCombo_, &QComboBox::currentIndexChanged, this,
            &WannierDialog::rebuildSlice);

    side->addStretch(1);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    side->addWidget(buttons);

    connect(&isoWatcher_, &QFutureWatcher<core::IsoMesh>::finished, this,
            &WannierDialog::onIsoExtractionFinished);
}

WannierDialog::~WannierDialog()
{
    // QFutureWatcher's destructor does not wait, and
    // the running lambda holds a shared_ptr into a member about to die. Detach
    // from the signal, then block until the worker is done.
    isoWatcher_.disconnect(this);
    if (isoWatcher_.isRunning())
        isoWatcher_.waitForFinished();
}

double WannierDialog::isovalueFromSlider() const
{
    return isoSlider_->value() / 1000.0 * fieldMax_;
}

void WannierDialog::loadResultsDialog()
{
    const QString path = QFileDialog::getOpenFileName(
        this, tr("Load Wannier results"), QString(),
        tr("Wannier results (wannier.json *.json);;All files (*)"));
    if (!path.isEmpty())
        loadResults(path);
}

void WannierDialog::loadResults(const QString& jsonPath)
{
    QFile file(jsonPath);
    if (!file.open(QIODevice::ReadOnly)) {
        QMessageBox::warning(this, windowTitle(),
                             tr("Could not open %1").arg(jsonPath));
        return;
    }
    const QJsonObject root = QJsonDocument::fromJson(file.readAll()).object();
    const QJsonArray centers = root.value(QStringLiteral("centers")).toArray();
    if (centers.isEmpty()) {
        QMessageBox::warning(this, windowTitle(),
                             tr("No 'centers' array found in %1").arg(jsonPath));
        return;
    }
    const QJsonArray spreads = root.value(QStringLiteral("spreads")).toArray();

    // The cubes live alongside the JSON; remember that directory so the orbital
    // selector can resolve wannier_<n>.cube.
    jobDir_ = QFileInfo(jsonPath).absolutePath();

    table_->setRowCount(centers.size());
    centres_.assign(static_cast<std::size_t>(centers.size()), core::Vec3{});
    for (int row = 0; row < centers.size(); ++row) {
        const QJsonArray c = centers.at(row).toArray();
        if (c.size() >= 3)
            centres_[static_cast<std::size_t>(row)] = {c.at(0).toDouble(),
                                                       c.at(1).toDouble(),
                                                       c.at(2).toDouble()};
        const auto cell = [&](int col, const QString& text) {
            table_->setItem(row, col, new QTableWidgetItem(text));
        };
        cell(0, QString::number(row));
        for (int k = 0; k < 3; ++k)
            cell(k + 1, QString::number(c.at(k).toDouble(), 'f', 4));
        const bool hasSpread = row < spreads.size();
        cell(4, hasSpread ? QString::number(spreads.at(row).toDouble(), 'f', 4)
                          : QStringLiteral("—"));
    }

    if (root.contains(QStringLiteral("total_spread"))) {
        const double omega = root.value(QStringLiteral("total_spread")).toDouble();
        totalSpreadLabel_->setText(
            tr("Total spread Ω: %1 Å²").arg(omega, 0, 'f', 4));
    } else {
        totalSpreadLabel_->setText(tr("Total spread Ω: —"));
    }

    // Repopulate the orbital selector; block signals while filling, then load
    // the first orbital explicitly (setCurrentIndex may not emit if it stays 0).
    {
        const QSignalBlocker blocker(orbitalCombo_);
        orbitalCombo_->clear();
        for (int i = 0; i < centers.size(); ++i)
            orbitalCombo_->addItem(tr("Wannier %1").arg(i), i);
    }
    if (orbitalCombo_->count() > 0) {
        orbitalCombo_->setCurrentIndex(0);
        orbitalSelected(0);
    }
}

void WannierDialog::orbitalSelected(int index)
{
    if (index < 0 || jobDir_.isEmpty())
        return;
    const QString path =
        QStringLiteral("%1/wannier_%2.cube").arg(jobDir_).arg(index);
    if (!QFileInfo::exists(path)) {
        gridLabel_->setText(tr("Missing %1").arg(QFileInfo(path).fileName()));
        return;
    }
    selectedOrbital_ = index;
    loadCube(path);
}

void WannierDialog::loadCube(const QString& path)
{
    QApplication::setOverrideCursor(Qt::WaitCursor);
    try {
        // Freshly allocated each load: the previous grid stays alive as long as
        // an in-flight extraction still references it.
        field_ = std::make_shared<const core::VolumetricData>(
            core::VolumetricData::load(path.toStdString()));
        // Resolved once per cube, not per extraction: the fallback walks the
        // whole grid, and the isovalue slider re-extracts on every step.
        fieldCentre_ =
            selectedOrbital_ >= 0
                && selectedOrbital_ < static_cast<int>(centres_.size())
            ? centres_[static_cast<std::size_t>(selectedOrbital_)]
            : core::periodicCentroid(*field_);
        QApplication::restoreOverrideCursor();
    } catch (const std::exception& e) {
        QApplication::restoreOverrideCursor();
        QMessageBox::warning(this, windowTitle(), QString::fromUtf8(e.what()));
        return;
    }

    // Wannier amplitudes are signed; scale the isovalue slider to the peak
    // magnitude of this cube so its full dynamic range is reachable.
    fieldMax_ = std::max({std::abs(field_->minValue()),
                          std::abs(field_->maxValue()), 1e-12});
    {
        const QSignalBlocker sBlocker(isoSpin_);
        isoSpin_->setRange(0.0, fieldMax_);
        isoSpin_->setSingleStep(fieldMax_ / 100.0);
        isoSpin_->setValue(isovalueFromSlider());
    }

    gridLabel_->setText(QStringLiteral("%1 — %2×%3×%4, [%5, %6]")
                            .arg(QString::fromStdString(field_->label))
                            .arg(field_->nx)
                            .arg(field_->ny)
                            .arg(field_->nz)
                            .arg(field_->minValue(), 0, 'g', 4)
                            .arg(field_->maxValue(), 0, 'g', 4));
    view_->setBox(*field_);
    view_->frameBox();
    rebuildIso();
    rebuildSlice();
}

void WannierDialog::rebuildIso()
{
    if (!field_ || !isoGroup_->isChecked()) {
        // Bump the generation so any in-flight extraction's result is discarded
        // rather than drawn over the cleared view.
        ++isoGeneration_;
        isoRequestPending_ = false;
        isoMesh_ = {};
        view_->clearIsoMesh();
        return;
    }
    isoRequestPending_ = true;
    startIsoExtraction();
}

void WannierDialog::startIsoExtraction()
{
    // One at a time: a slider drag would otherwise spawn an extraction per pixel
    // of travel. The newest request wins when the current one lands.
    if (!isoRequestPending_ || isoWatcher_.isRunning())
        return;
    isoRequestPending_ = false;
    isoRunningGeneration_ = ++isoGeneration_;

    // Capture a shared_ptr copy (a refcount bump, not a grid copy) so the
    // worker's input stays alive and unchanged even if the user loads a new
    // orbital while it runs.
    FieldPtr field = field_;
    const double isovalue = isoSpin_->value();
    const core::Vec3 centre = fieldCentre_;
    const double margin = continuationSpin_->value();

    isoWatcher_.setFuture(QtConcurrent::run(
        [field = std::move(field), isovalue, centre, margin] {
            // Continued into the neighbouring periodic images rather than cut
            // at the cell faces — see core/IsosurfaceContinuation.hpp. The
            // window is centred on the Wannier centre, so the function is
            // shown whole wherever in the cell it happened to sit.
            return core::extractContinuedIsosurface(*field, isovalue, centre,
                                                    margin);
        }));
}

void WannierDialog::onIsoExtractionFinished()
{
    // Stale result: its inputs changed (or the surface was switched off) while
    // it ran. Drop it and serve whatever is queued now.
    if (isoRunningGeneration_ != isoGeneration_) {
        startIsoExtraction();
        return;
    }

    isoMesh_ = isoWatcher_.result();
    // A single-field surface: no secondary color values, so the isosurface is
    // drawn in a flat shade (colored=false).
    view_->setIsoMesh(isoMesh_, kMaps[isoColormapCombo_->currentIndex()], 0.0,
                      1.0, false);

    startIsoExtraction(); // serve a request that arrived while we were busy
}

void WannierDialog::rebuildSlice()
{
    const int planeIndex = planeCombo_->currentIndex();
    if (!field_ || planeIndex == 0) { // 0 == "Off"
        view_->clearSlice();
        return;
    }

    const core::VolumetricData* field = field_.get();
    const double lo = field->minValue(), hi = field->maxValue();
    const double range = std::max(hi - lo, 1e-30);
    const auto gradient = kMaps[sliceColormapCombo_->currentIndex()];
    const double offset = offsetSlider_->value() / 100.0;
    // planeCombo_: 0=Off, 1=XY, 2=XZ, 3=YZ -> 0-based plane for the sampler.
    const int plane = planeIndex - 1;
    std::vector<float> stream;

    const auto emit_ = [&](const core::Vec3& p, double value) {
        const QColor c = render::ColorMap::sample(
            gradient, static_cast<float>((value - lo) / range));
        stream.insert(stream.end(),
                      {static_cast<float>(p.x), static_cast<float>(p.y),
                       static_cast<float>(p.z), static_cast<float>(c.redF()),
                       static_cast<float>(c.greenF()),
                       static_cast<float>(c.blueF())});
    };

    // Grid-aligned slices sample exactly on the field's own axes:
    // XY = (A,B) at fixed C, XZ = (A,C) at fixed B, YZ = (B,C) at fixed A.
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
    view_->setSlice(stream);
}

} // namespace calango::gui
