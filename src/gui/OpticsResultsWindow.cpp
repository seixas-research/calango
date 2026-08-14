#include "gui/OpticsResultsWindow.hpp"

#include "gui/GuiUtils.hpp"

#include "gui/SpectrumPlotWidget.hpp"

#include "gui/OpticsPlotStyleDialog.hpp"

#include <cmath>
#include <complex>

#include <QColor>
#include <QSlider>
#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFile>
#include <QFileDialog>
#include <QFontMetrics>
#include <QHBoxLayout>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QMessageBox>
#include <QPainter>
#include <QPen>
#include <QPolygonF>
#include <QPushButton>
#include <QTextStream>
#include <QVBoxLayout>
#include <QWidget>

#include <algorithm>
#include <cmath>

namespace calango::gui {

// ---------------------------------------------------------------------------
// A minimal dependency-free multi-series line chart. The project ships no
// plotting library and LinePlotWidget only draws a single curve, so the optics
// viewer (ε₁ & ε₂, n & k) carries its own small painter here: autoscaled axes
// with ticks and a grid, N labelled curves with a legend, and a renderTo() the
// window reuses for high-resolution image export.
// ---------------------------------------------------------------------------


// ---------------------------------------------------------------------------
// OpticsResultsWindow
// ---------------------------------------------------------------------------
OpticsResultsWindow::OpticsResultsWindow(const QString& directory,
                                         QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle(tr("Optical Properties — %1").arg(directory));
    resize(880, 620);

    auto* layout = new QVBoxLayout(this);

    auto* controls = new QHBoxLayout;
    controls->addWidget(new QLabel(tr("Quantity:"), this));
    quantityCombo_ = new QComboBox(this);
    const auto addQuantity = [this](const QString& label, Quantity id) {
        quantityCombo_->addItem(label, static_cast<int>(id));
    };
    addQuantity(tr("Dielectric function (ε₁ & ε₂)"), Quantity::Dielectric);
    addQuantity(tr("Absorption α(ω)"), Quantity::Absorption);
    addQuantity(tr("Reflectivity R(ω)"), Quantity::Reflectivity);
    addQuantity(tr("Refractive index (n & k)"), Quantity::RefractiveIndex);
    addQuantity(tr("Energy loss L(ω)"), Quantity::Loss);
    controls->addWidget(quantityCombo_);
    controls->addSpacing(16);
    controls->addWidget(new QLabel(tr("Direction:"), this));
    directionCombo_ = new QComboBox(this);
    controls->addWidget(directionCombo_);
    controls->addSpacing(16);
    controls->addWidget(new QLabel(tr("X axis:"), this));
    unitCombo_ = new QComboBox(this);
    unitCombo_->addItem(tr("Energy (eV)"),
                        static_cast<int>(XAxisUnit::EnergyEv));
    unitCombo_->addItem(tr("Wavelength (nm)"),
                        static_cast<int>(XAxisUnit::WavelengthNm));
    unitCombo_->setToolTip(
        tr("λ = hc/E with hc = 1239.84197 eV·nm. Samples at E ≤ 0 have no "
           "finite wavelength and are omitted from the wavelength view."));
    controls->addWidget(unitCombo_);
    controls->addSpacing(16);
    visibleSpectrumCheck_ = new QCheckBox(tr("Show visible spectrum"), this);
    visibleSpectrumCheck_->setToolTip(
        tr("Shade the visible range behind the curves — 380–750 nm, which is "
           "3.26–1.65 eV (λ = hc/E, hc = 1239.84 eV·nm).\n\n"
           "The gradient is the colour of each wavelength, placed at its own "
           "position on whichever axis is shown. On the energy axis that is "
           "deliberately NOT a straight red-to-violet ramp: wavelength goes "
           "as 1/E, so an even ramp would put green near 2.45 eV where it "
           "belongs at 2.25 eV.\n\n"
           "Semi-transparent, and drawn behind the data — it is context for "
           "reading an absorption edge or a reflectivity feature against "
           "what the eye sees, not a series."));
    controls->addWidget(visibleSpectrumCheck_);
    connect(visibleSpectrumCheck_, &QCheckBox::toggled, this,
            &OpticsResultsWindow::updatePlot);
    controls->addStretch(1);
    layout->addLayout(controls);

    // Axis window + appearance, on their own row: the controls row above is
    // already three combos wide.
    auto* rangeRow = new QHBoxLayout;
    rangeRow->addWidget(new QLabel(tr("Range:"), this));
    xMinSpin_ = new QDoubleSpinBox(this);
    xMaxSpin_ = new QDoubleSpinBox(this);
    for (QDoubleSpinBox* spin : {xMinSpin_, xMaxSpin_}) {
        spin->setDecimals(2);
        spin->setRange(0.0, 100000.0);
        spin->setValue(0.0);
        // Both at zero is the "fit the data" default; the min box says so.
        spin->setSpecialValueText(tr("auto"));
        connect(spin, &QDoubleSpinBox::valueChanged, this,
                &OpticsResultsWindow::updatePlot);
        rangeRow->addWidget(spin);
    }
    xMinSpin_->setToolTip(
        tr("Lower display bound. Leave both at \"auto\" to fit the data. The "
           "vertical scale follows what is visible, so zooming in re-scales "
           "the y axis to the window rather than to an off-screen peak."));
    xMaxSpin_->setToolTip(xMinSpin_->toolTip());
    rangeRow->addSpacing(16);
    auto* appearanceButton = new QPushButton(tr("Customize Appearance…"), this);
    connect(appearanceButton, &QPushButton::clicked, this,
            &OpticsResultsWindow::customizeAppearance);
    rangeRow->addWidget(appearanceButton);
    rangeRow->addStretch(1);
    layout->addLayout(rangeRow);

    // -- Live broadening ----------------------------------------------------
    //
    // eta is a LIFETIME, so unlike a DOS smearing it cannot simply be left out
    // of the calculation — it sits inside the response function GPAW inverts.
    // What it can do is grow afterwards: Lorentzian widths add under
    // convolution, so the stored spectrum is exactly the one at its own eta and
    // every larger eta is one convolution away. See rebuildSpectra().
    auto* broadeningRow = new QHBoxLayout;
    broadeningRow->addWidget(new QLabel(tr("Broadening η:"), this));
    broadeningSlider_ = new QSlider(Qt::Horizontal, this);
    broadeningSpin_ = new QDoubleSpinBox(this);
    broadeningSpin_->setObjectName(QStringLiteral("opticsBroadening"));
    broadeningSpin_->setDecimals(3);
    broadeningSpin_->setSingleStep(0.01);
    broadeningSpin_->setSuffix(tr(" eV"));
    broadeningSpin_->setToolTip(
        tr("Lorentzian lifetime broadening applied as the spectrum is drawn.\n\n"
           "It can only be RAISED above the value the run computed at: adding "
           "width is a convolution, removing it is a deconvolution — an "
           "inverse problem, not a filter.\n\n"
           "Every curve in the viewer is re-derived from the broadened ε, so "
           "absorption, reflectivity, n/k, the loss function and the sheet "
           "observables all follow rather than going stale."));
    broadeningSlider_->setToolTip(broadeningSpin_->toolTip());
    broadeningRow->addWidget(broadeningSlider_, 1);
    broadeningRow->addWidget(broadeningSpin_);
    broadeningNote_ = new QLabel(this);
    broadeningNote_->setWordWrap(true);
    broadeningNote_->setTextFormat(Qt::RichText);
    layout->addLayout(broadeningRow);
    layout->addWidget(broadeningNote_);

    connect(broadeningSlider_, &QSlider::valueChanged, this, [this](int milli) {
        const QSignalBlocker blocker(broadeningSpin_);
        broadeningSpin_->setValue(milli / 1000.0);
        broadening_ = std::max(milli / 1000.0, etaStored_);
        rebuildSpectra();
        updatePlot();
    });
    connect(broadeningSpin_, &QDoubleSpinBox::valueChanged, this,
            [this](double eta) {
                const QSignalBlocker blocker(broadeningSlider_);
                broadeningSlider_->setValue(
                    static_cast<int>(std::lround(eta * 1000.0)));
                broadening_ = std::max(eta, etaStored_);
                rebuildSpectra();
                updatePlot();
            });

    plot_ = new SpectrumPlotWidget(this);
    layout->addWidget(plot_, 1);

    auto* buttons = new QHBoxLayout;
    auto* csvButton = new QPushButton(tr("Export CSV…"), this);
    auto* imageButton = new QPushButton(tr("Export Image…"), this);
    auto* closeButton = new QPushButton(tr("Close"), this);
    buttons->addWidget(csvButton);
    buttons->addWidget(imageButton);
    buttons->addStretch(1);
    buttons->addWidget(closeButton);
    layout->addLayout(buttons);

    connect(csvButton, &QPushButton::clicked, this,
            &OpticsResultsWindow::exportCsv);
    connect(imageButton, &QPushButton::clicked, this,
            &OpticsResultsWindow::exportImage);
    connect(closeButton, &QPushButton::clicked, this, &QDialog::accept);

    loadDirectory(directory);

    // The slider opens at the run's own η and cannot go below it. A run that
    // stated no η leaves the control disabled rather than offering a knob
    // whose zero point is unknown — broadening a spectrum by an unknown amount
    // is worse than not broadening it.
    {
        const QSignalBlocker spinBlocker(broadeningSpin_);
        const QSignalBlocker sliderBlocker(broadeningSlider_);
        const bool known = etaStored_ > 0.0;
        // Up to 2 eV, which is far past any lifetime a spectrum is read at.
        broadeningSpin_->setRange(etaStored_, std::max(2.0, etaStored_));
        broadeningSpin_->setValue(etaStored_);
        broadeningSlider_->setRange(
            static_cast<int>(std::lround(etaStored_ * 1000.0)),
            static_cast<int>(std::lround(std::max(2.0, etaStored_) * 1000.0)));
        broadeningSlider_->setValue(
            static_cast<int>(std::lround(etaStored_ * 1000.0)));
        broadeningSpin_->setEnabled(known);
        broadeningSlider_->setEnabled(known);
        broadeningNote_->setText(
            known
                ? tr("<i>Computed at η = %1 eV; that is the floor. Raising it "
                     "re-derives every curve here, including the sheet "
                     "observables — no re-run.</i>")
                      .arg(etaStored_, 0, 'g', 3)
                : tr("<i>This run recorded no η, so the broadening it already "
                     "carries is unknown and cannot be added to. Re-run to get "
                     "an adjustable spectrum.</i>"));
    }

    // Only the directions actually present in the file become selectable.
    for (int i = 0; i < directions_.size(); ++i)
        directionCombo_->addItem(directions_[i].first, i);

    // The sheet observables are offered only for a job that computed them.
    // ε₃D of a slab supercell depends on the vacuum padding, so for a 2D run
    // these — not ε — are the quantities that describe the material.
    const bool twoDimensional =
        std::any_of(directions_.cbegin(), directions_.cend(),
                    [](const auto& entry) { return entry.second.twoDimensional; });
    if (twoDimensional) {
        addQuantity(tr("Absorbance A(ω)"), Quantity::Absorbance);
        addQuantity(tr("Polarizability α₂D(ω)"), Quantity::Polarizability);
        addQuantity(tr("Conductivity σ₂D(ω)"), Quantity::Conductivity);
        setWindowTitle(tr("2D Optical Properties"));
        // Open on the quantity the user came for.
        quantityCombo_->setCurrentIndex(
            quantityCombo_->findData(static_cast<int>(Quantity::Absorbance)));
    }

    connect(quantityCombo_, &QComboBox::currentIndexChanged, this,
            &OpticsResultsWindow::updatePlot);
    connect(directionCombo_, &QComboBox::currentIndexChanged, this,
            &OpticsResultsWindow::updatePlot);
    // The unit is a property of the abscissa, so every quantity re-plots
    // through the same path — ε₁/ε₂, α, R, n/k, L and the 2D observables all
    // pick it up without each needing to know about it.
    connect(unitCombo_, &QComboBox::currentIndexChanged, this,
            &OpticsResultsWindow::retuneRangeForUnit);

    updatePlot();
}

void OpticsResultsWindow::loadDirectory(const QString& directory)
{
    QFile file(directory + QStringLiteral("/optics.json"));
    if (!file.open(QIODevice::ReadOnly))
        return;
    const QByteArray raw = file.readAll();
    file.close();

    QJsonParseError parseError{};
    const QJsonDocument doc = QJsonDocument::fromJson(raw, &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject())
        return;
    const QJsonObject root = doc.object();

    const auto toVector = [](const QJsonArray& array) {
        std::vector<double> values;
        values.reserve(array.size());
        for (const auto& item : array)
            values.push_back(item.toDouble());
        return values;
    };

    energy_ = toVector(root.value(QStringLiteral("energy_eV")).toArray());
    if (energy_.empty())
        return;

    // The broadening the run used, and the vacuum thickness the sheet
    // observables were divided by. Both are needed to re-derive anything.
    etaStored_ = root.value(QStringLiteral("eta_eV")).toDouble(0.0);
    broadening_ = etaStored_;
    vacuumLengthA_ = root.value(QStringLiteral("L_z_A")).toDouble(0.0);

    for (const char* key : {"xx", "yy", "zz"}) {
        const QJsonValue value = root.value(QLatin1String(key));
        if (!value.isObject())
            continue;
        const QJsonObject dir = value.toObject();
        DirectionData data;
        data.rawEps1 = toVector(dir.value(QStringLiteral("eps1")).toArray());
        data.rawEps2 = toVector(dir.value(QStringLiteral("eps2")).toArray());
        // Seeded from the file so a run whose broadening is never touched
        // shows exactly the numbers it computed, byte for byte, rather than
        // the numbers this viewer would re-derive at zero extra width.
        data.eps1 = data.rawEps1;
        data.eps2 = data.rawEps2;
        data.absorption =
            toVector(dir.value(QStringLiteral("absorption")).toArray());
        data.reflectivity =
            toVector(dir.value(QStringLiteral("reflectivity")).toArray());
        data.n = toVector(dir.value(QStringLiteral("n")).toArray());
        data.k = toVector(dir.value(QStringLiteral("k")).toArray());
        data.loss = toVector(dir.value(QStringLiteral("loss")).toArray());

        // Sheet observables, written only by the 2D variant of the wizard.
        const QJsonValue sheet =
            root.value(QLatin1String("twod_") + QLatin1String(key));
        if (sheet.isObject()) {
            const QJsonObject twod = sheet.toObject();
            data.alpha2dRe =
                toVector(twod.value(QStringLiteral("alpha_2D_re_A")).toArray());
            data.alpha2dIm =
                toVector(twod.value(QStringLiteral("alpha_2D_im_A")).toArray());
            data.absorbance =
                toVector(twod.value(QStringLiteral("absorbance")).toArray());
            data.sigma2dRe =
                toVector(twod.value(QStringLiteral("sigma_2D_re")).toArray());
            data.sigma2dIm =
                toVector(twod.value(QStringLiteral("sigma_2D_im")).toArray());
            data.twoDimensional = !data.absorbance.empty();
        }

        directions_.append({QString::fromLatin1(key), data});
    }

    hasData_ = !directions_.isEmpty();
}

OpticsResultsWindow::DerivedSpectra
OpticsResultsWindow::derivedSpectra(int direction) const
{
    DerivedSpectra out;
    if (direction < 0 || direction >= directions_.size())
        return out;
    const DirectionData& d = directions_[direction].second;
    out.energy = energy_;
    out.eps1 = d.eps1;
    out.eps2 = d.eps2;
    out.absorption = d.absorption;
    out.reflectivity = d.reflectivity;
    out.n = d.n;
    out.k = d.k;
    out.loss = d.loss;
    out.absorbance = d.absorbance;
    return out;
}

void OpticsResultsWindow::rebuildSpectra()
{
    // CODATA, matching OpticsScriptGenerator exactly. Two viewers deriving the
    // same observable from slightly different constants is a discrepancy
    // nobody would think to look for.
    constexpr double kHbarCeVcm = 197.3269804e-7;
    constexpr double kHbarCeVA = 1973.269804;
    constexpr double kAlphaFs = 1.0 / 137.035999084;

    const std::size_t n = energy_.size();
    if (n < 2)
        return;
    const double dw = (energy_.back() - energy_.front())
        / static_cast<double>(n - 1);
    const double extra = broadening_ - etaStored_;

    // The convolution kernel, once. Lorentzian tails fall as 1/x², so the
    // support has to be generous — the truncated weight is a genuine part of
    // the answer and must NOT be renormalized away (renormalizing measurably
    // worsens the result against the analytic form).
    std::vector<double> kernel;
    int half = 0;
    if (extra > 1e-9 && dw > 0.0) {
        half = std::max(1, static_cast<int>(std::ceil(60.0 * extra / dw)));
        kernel.resize(static_cast<std::size_t>(2 * half + 1));
        for (int i = -half; i <= half; ++i) {
            const double x = i * dw;
            kernel[static_cast<std::size_t>(i + half)] =
                (extra / M_PI) / (x * x + extra * extra) * dw;
        }
    }

    // ε₂ is ODD in ω and (ε₁ − 1) is EVEN, so the spectrum can be reflected
    // through ω = 0 before convolving. Without that the kernel runs off the
    // start of a grid that begins at zero and the low-energy edge comes out
    // badly wrong — a factor of 3000 worse at ω → 0 in the reference test.
    const auto convolve = [&](const std::vector<double>& f, bool odd) {
        std::vector<double> out(n, 0.0);
        const auto sample = [&](long index) {
            // Reflect: index < 0 maps to |index| with the parity's sign.
            if (index < 0) {
                const auto mirrored = static_cast<std::size_t>(-index);
                if (mirrored >= n)
                    return 0.0;
                return odd ? -f[mirrored] : f[mirrored];
            }
            const auto at = static_cast<std::size_t>(index);
            // Past the end there is no information; ε₂ has decayed and
            // (ε₁ − 1) with it, so zero is the honest continuation.
            return at < n ? f[at] : 0.0;
        };
        for (std::size_t i = 0; i < n; ++i) {
            double acc = 0.0;
            for (int j = -half; j <= half; ++j) {
                acc += sample(static_cast<long>(i) - j)
                    * kernel[static_cast<std::size_t>(j + half)];
            }
            out[i] = acc;
        }
        return out;
    };

    for (auto& entry : directions_) {
        DirectionData& d = entry.second;
        if (d.rawEps1.size() != n || d.rawEps2.size() != n)
            continue;

        if (kernel.empty()) {
            d.eps1 = d.rawEps1;
            d.eps2 = d.rawEps2;
        } else {
            std::vector<double> shifted(n);
            for (std::size_t i = 0; i < n; ++i)
                shifted[i] = d.rawEps1[i] - 1.0;   // the resolvent part
            d.eps1 = convolve(shifted, /*odd=*/false);
            for (double& value : d.eps1)
                value += 1.0;
            d.eps2 = convolve(d.rawEps2, /*odd=*/true);
        }

        // Everything else is algebraic in (ω, ε₁, ε₂) — the same expressions
        // the generator uses, so a broadened spectrum is self-consistent
        // rather than a broadened ε beside stale derived curves.
        d.absorption.assign(n, 0.0);
        d.reflectivity.assign(n, 0.0);
        d.n.assign(n, 0.0);
        d.k.assign(n, 0.0);
        d.loss.assign(n, 0.0);
        for (std::size_t i = 0; i < n; ++i) {
            const std::complex<double> eps(d.eps1[i], d.eps2[i]);
            const std::complex<double> refractive = std::sqrt(eps);
            const double nn = refractive.real();
            const double kk = refractive.imag();
            d.n[i] = nn;
            d.k[i] = kk;
            d.absorption[i] = 2.0 * (energy_[i] / kHbarCeVcm) * kk;
            d.reflectivity[i] = ((nn - 1.0) * (nn - 1.0) + kk * kk)
                / ((nn + 1.0) * (nn + 1.0) + kk * kk);
            // -Im(1/ε). A vanishing ε is a real pole, not a bug; guard the
            // division rather than emitting an infinity into the plot.
            const double magnitude = std::norm(eps);
            d.loss[i] = magnitude > 1e-30
                ? -(1.0 / eps).imag()
                : 0.0;
        }

        if (!d.twoDimensional || vacuumLengthA_ <= 0.0)
            continue;
        const double lz = vacuumLengthA_;
        const double toE2overH = 2.0 * M_PI / kAlphaFs;
        d.alpha2dRe.assign(n, 0.0);
        d.alpha2dIm.assign(n, 0.0);
        d.absorbance.assign(n, 0.0);
        d.sigma2dRe.assign(n, 0.0);
        d.sigma2dIm.assign(n, 0.0);
        for (std::size_t i = 0; i < n; ++i) {
            const double k = energy_[i] / kHbarCeVA;   // 1/Å
            d.alpha2dRe[i] = lz / (4.0 * M_PI) * (d.eps1[i] - 1.0);
            d.alpha2dIm[i] = lz / (4.0 * M_PI) * d.eps2[i];
            d.absorbance[i] = k * lz * d.eps2[i];
            d.sigma2dRe[i] = k * d.alpha2dIm[i] * toE2overH;
            d.sigma2dIm[i] = -k * d.alpha2dRe[i] * toE2overH;
        }
    }
}

const OpticsResultsWindow::DirectionData*
OpticsResultsWindow::currentDirection() const
{
    if (!directionCombo_ || directions_.isEmpty())
        return nullptr;
    int index = directionCombo_->currentData().toInt();
    if (index < 0 || index >= directions_.size())
        index = 0;
    return &directions_[index].second;
}

void OpticsResultsWindow::updatePlot()
{
    if (!plot_)
        return;
    const DirectionData* dir = currentDirection();
    if (!dir) {
        plot_->setSeries({}, {}, tr("Photon energy ħω (eV)"), QString());
        return;
    }

    std::vector<QPair<QString, std::vector<double>>> series;
    QString yLabel;
    const auto quantity = static_cast<Quantity>(
        quantityCombo_ ? quantityCombo_->currentData().toInt() : 0);
    switch (quantity) {
    case Quantity::Dielectric:
        series = {{tr("ε₁"), dir->eps1}, {tr("ε₂"), dir->eps2}};
        yLabel = tr("Dielectric function ε");
        break;
    case Quantity::Absorption:
        series = {{tr("α"), dir->absorption}};
        yLabel = tr("Absorption α (cm⁻¹)");
        break;
    case Quantity::Reflectivity:
        series = {{tr("R"), dir->reflectivity}};
        yLabel = tr("Reflectivity R");
        break;
    case Quantity::RefractiveIndex:
        series = {{tr("n"), dir->n}, {tr("k"), dir->k}};
        yLabel = tr("Refractive index");
        break;
    case Quantity::Loss:
        series = {{tr("L"), dir->loss}};
        yLabel = tr("Energy loss L");
        break;
    case Quantity::Absorbance:
        // Dimensionless: the fraction of normally incident light the sheet
        // absorbs. Graphene's πα ≈ 2.3% is the familiar landmark.
        series = {{tr("A"), dir->absorbance}};
        yLabel = tr("Absorbance A");
        break;
    case Quantity::Polarizability:
        series = {{tr("Re α₂D"), dir->alpha2dRe},
                  {tr("Im α₂D"), dir->alpha2dIm}};
        yLabel = tr("Sheet polarizability α₂D (Å)");
        break;
    case Quantity::Conductivity:
        series = {{tr("Re σ₂D"), dir->sigma2dRe},
                  {tr("Im σ₂D"), dir->sigma2dIm}};
        // e²/h is the convention the 2D literature quotes, where graphene's
        // universal conductivity is the familiar π/2 ≈ 1.57.
        yLabel = tr("Sheet conductivity σ₂D (e²/h)");
        break;
    }
    const std::vector<double> x = abscissa(series);
    plot_->setStyle(style_);
    plot_->setXRange(xMinSpin_ ? xMinSpin_->value() : 0.0,
                     xMaxSpin_ ? xMaxSpin_->value() : 0.0);
    // The band follows the axis unit, not just the checkbox: the same visible
    // range is 1.65–3.26 eV or 380–750 nm, and its colours run the opposite
    // way along the two.
    const auto unit = static_cast<XAxisUnit>(
        unitCombo_ ? unitCombo_->currentData().toInt()
                   : static_cast<int>(XAxisUnit::EnergyEv));
    plot_->setVisibleSpectrum(
        visibleSpectrumCheck_ && visibleSpectrumCheck_->isChecked(),
        unit == XAxisUnit::WavelengthNm
            ? SpectrumPlotWidget::SpectralAxis::WavelengthNm
            : SpectrumPlotWidget::SpectralAxis::EnergyEv);
    plot_->setSeries(x, series, xAxisLabel(), yLabel);
}

void OpticsResultsWindow::customizeAppearance()
{
    auto* dialog = new OpticsPlotStyleDialog(style_, this);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    // Live, not on-accept: the point of a styling dialog is to judge the
    // result against the real plot.
    connect(dialog, &OpticsPlotStyleDialog::styleChanged, this,
            [this](const OpticsPlotStyle& style) {
                style_ = style;
                plot_->setStyle(style_);
            });
    dialog->show();
}

void OpticsResultsWindow::retuneRangeForUnit()
{
    // Energy and wavelength differ by three orders of magnitude and invert
    // each other, so a window set in one unit is meaningless in the other.
    // Clearing it is honest; silently converting it would imply the user had
    // asked for the converted span.
    const QSignalBlocker blockMin(xMinSpin_);
    const QSignalBlocker blockMax(xMaxSpin_);
    const auto unit = static_cast<XAxisUnit>(unitCombo_->currentData().toInt());
    const bool wavelength = unit == XAxisUnit::WavelengthNm;
    for (QDoubleSpinBox* spin : {xMinSpin_, xMaxSpin_}) {
        spin->setSuffix(wavelength ? tr(" nm") : tr(" eV"));
        spin->setSingleStep(wavelength ? 50.0 : 0.5);
        spin->setValue(0.0);
    }
    updatePlot();
}

QString OpticsResultsWindow::xAxisLabel() const
{
    const auto unit = static_cast<XAxisUnit>(
        unitCombo_ ? unitCombo_->currentData().toInt() : 0);
    return unit == XAxisUnit::WavelengthNm ? tr("Wavelength λ (nm)")
                                           : tr("Photon energy ħω (eV)");
}

std::vector<double> OpticsResultsWindow::abscissa(
    std::vector<QPair<QString, std::vector<double>>>& series) const
{
    const auto unit = static_cast<XAxisUnit>(
        unitCombo_ ? unitCombo_->currentData().toInt() : 0);
    if (unit == XAxisUnit::EnergyEv)
        return energy_;

    // Wavelength view. E = 0 is a pole (λ → ∞) and negative energies are not
    // physical, so those samples are dropped from the abscissa AND from the
    // same index of every series. Filtering the two together is what keeps a
    // curve aligned with its x values; dropping from one alone would shift
    // every subsequent point by one sample.
    std::vector<std::size_t> keep;
    keep.reserve(energy_.size());
    for (std::size_t i = 0; i < energy_.size(); ++i)
        if (energy_[i] > 0.0 && std::isfinite(energy_[i]))
            keep.push_back(i);

    std::vector<double> wavelength;
    wavelength.reserve(keep.size());
    for (std::size_t i : keep)
        wavelength.push_back(kHcEvNm / energy_[i]);

    for (auto& entry : series) {
        std::vector<double> filtered;
        filtered.reserve(keep.size());
        for (std::size_t i : keep)
            filtered.push_back(i < entry.second.size() ? entry.second[i] : 0.0);
        entry.second = std::move(filtered);
    }
    return wavelength;
}

void OpticsResultsWindow::exportCsv()
{
    const DirectionData* dir = currentDirection();
    if (!dir) {
        QMessageBox::information(this, tr("Export CSV"),
                                 tr("No optical data was loaded from this job."));
        return;
    }
    const QString label = directionCombo_->currentText();
    const QString path = QFileDialog::getSaveFileName(
        this, tr("Export Optical Data"),
        QStringLiteral("optics_%1.csv").arg(label), tr("CSV (*.csv)"));
    if (path.isEmpty())
        return;

    writeTextFile(this, path, [&](QTextStream& out) {
        // A CSV starts with its header row — the direction is in the
        // suggested file name, not a '#' line a CSV reader trips over.
        out << "energy_eV,eps1,eps2,absorption_cm-1,reflectivity,n,k,loss";
        // The 2D columns are appended only for a sheet job, so a bulk export
        // keeps exactly the column set it always had.
        if (dir->twoDimensional)
            out << ",alpha_2D_re_A,alpha_2D_im_A,absorbance,sigma_2D_re,"
                   "sigma_2D_im";
        out << '\n';

        const auto at = [](const std::vector<double>& v, std::size_t i) {
            return i < v.size() ? v[i] : 0.0;
        };
        for (std::size_t i = 0; i < energy_.size(); ++i) {
            out << QString::number(energy_[i], 'f', 6) << ','
                << QString::number(at(dir->eps1, i), 'g', 8) << ','
                << QString::number(at(dir->eps2, i), 'g', 8) << ','
                << QString::number(at(dir->absorption, i), 'g', 8) << ','
                << QString::number(at(dir->reflectivity, i), 'g', 8) << ','
                << QString::number(at(dir->n, i), 'g', 8) << ','
                << QString::number(at(dir->k, i), 'g', 8) << ','
                << QString::number(at(dir->loss, i), 'g', 8);
            if (dir->twoDimensional) {
                out << ','
                    << QString::number(at(dir->alpha2dRe, i), 'g', 8) << ','
                    << QString::number(at(dir->alpha2dIm, i), 'g', 8) << ','
                    << QString::number(at(dir->absorbance, i), 'g', 8) << ','
                    << QString::number(at(dir->sigma2dRe, i), 'g', 8) << ','
                    << QString::number(at(dir->sigma2dIm, i), 'g', 8);
            }
            out << '\n';
        }
    });
}

void OpticsResultsWindow::exportImage()
{
    if (!plot_)
        return;
    const QString path = QFileDialog::getSaveFileName(
        this, tr("Export Image"), QStringLiteral("optics.png"),
        tr("PNG image (*.png);;JPEG image (*.jpg *.jpeg)"));
    if (path.isEmpty())
        return;

    savePlotImage(this, path, plot_->size(),
                  [this](QPainter& painter, const QSize& logical) {
                      plot_->renderTo(painter, logical);
                  });
}

} // namespace calango::gui
