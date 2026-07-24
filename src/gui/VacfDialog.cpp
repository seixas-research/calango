#include "gui/VacfDialog.hpp"

#include "gui/LinePlotWidget.hpp"

#include <QDoubleSpinBox>
#include <QFile>
#include <QFileDialog>
#include <QFormLayout>
#include <QFrame>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QPainter>
#include <QPixmap>
#include <QPushButton>
#include <QSpinBox>
#include <QTextStream>
#include <QVBoxLayout>

#include <algorithm>
#include <utility>

namespace calango::gui {

VacfDialog::VacfDialog(std::vector<std::vector<core::Vec3>> velocities,
                       double defaultDtFs, QWidget* parent)
    : QDialog(parent), velocities_(std::move(velocities))
{
    setWindowTitle(tr("Velocity Autocorrelation Function (VACF)"));
    resize(760, 720);

    auto* layout = new QVBoxLayout(this);

    // --- Controls ----------------------------------------------------------
    auto* controls = new QHBoxLayout;
    auto* form = new QFormLayout;
    dtSpin_ = new QDoubleSpinBox(this);
    dtSpin_->setRange(0.001, 1000.0);
    dtSpin_->setDecimals(3);
    dtSpin_->setValue(defaultDtFs > 0.0 ? defaultDtFs : 1.0);
    dtSpin_->setSuffix(tr(" fs"));
    dtSpin_->setToolTip(tr("Time between stored trajectory frames."));
    form->addRow(tr("Frame timestep:"), dtSpin_);

    maxLagSpin_ = new QSpinBox(this);
    maxLagSpin_->setRange(0, static_cast<int>(velocities_.size()));
    maxLagSpin_->setValue(static_cast<int>(velocities_.size()) / 2);
    maxLagSpin_->setSpecialValueText(tr("auto (half)"));
    maxLagSpin_->setToolTip(
        tr("Maximum correlation lag (frames). 0 = half the trajectory."));
    form->addRow(tr("Max correlation lag:"), maxLagSpin_);
    controls->addLayout(form);
    controls->addStretch(1);

    // Trajectory sub-interval: Start / End frame (inclusive) + stride.
    const int lastFrame = static_cast<int>(velocities_.size()) - 1;
    auto* rangeForm = new QFormLayout;
    startSpin_ = new QSpinBox(this);
    startSpin_->setRange(0, lastFrame);
    startSpin_->setValue(0);
    startSpin_->setToolTip(tr("First trajectory frame to include (0-based)."));
    rangeForm->addRow(tr("Start frame:"), startSpin_);
    endSpin_ = new QSpinBox(this);
    endSpin_->setRange(0, lastFrame);
    endSpin_->setValue(lastFrame);
    endSpin_->setToolTip(tr("Last trajectory frame to include (inclusive)."));
    rangeForm->addRow(tr("End frame:"), endSpin_);
    stepSpin_ = new QSpinBox(this);
    stepSpin_->setRange(1, std::max(1, lastFrame));
    stepSpin_->setValue(1);
    stepSpin_->setToolTip(tr("Use every Nth frame in the range (stride)."));
    rangeForm->addRow(tr("Step / stride:"), stepSpin_);
    controls->addLayout(rangeForm);

    auto* computeButton = new QPushButton(tr("Recompute"), this);
    connect(computeButton, &QPushButton::clicked, this, &VacfDialog::recompute);
    controls->addWidget(computeButton);
    layout->addLayout(controls);

    connect(dtSpin_, &QDoubleSpinBox::valueChanged, this, &VacfDialog::recompute);
    connect(maxLagSpin_, &QSpinBox::valueChanged, this, &VacfDialog::recompute);
    for (QSpinBox* s : {startSpin_, endSpin_, stepSpin_})
        connect(s, &QSpinBox::valueChanged, this, &VacfDialog::recompute);

    // --- Plots -------------------------------------------------------------
    auto* cvGroup = new QGroupBox(tr("Normalized VACF  C_v(t)"), this);
    auto* cvLayout = new QVBoxLayout(cvGroup);
    cvPlot_ = new LinePlotWidget(cvGroup);
    cvPlot_->setAxisLabels(tr("time (fs)"), tr("C_v(t)"));
    cvLayout->addWidget(cvPlot_);
    layout->addWidget(cvGroup, 1);

    auto* vdosGroup = new QGroupBox(tr("Vibrational DOS  (FFT of C_v)"), this);
    auto* vdosLayout = new QVBoxLayout(vdosGroup);
    vdosPlot_ = new LinePlotWidget(vdosGroup);
    vdosPlot_->setAxisLabels(tr("frequency (THz)"), tr("VDOS (arb.)"));
    vdosLayout->addWidget(vdosPlot_);
    layout->addWidget(vdosGroup, 1);

    // --- Prominent transport-metric callouts -------------------------------
    // Two high-contrast cards highlighting the self-diffusion coefficient and
    // the momentum relaxation time in clean units (cm²/s and ps).
    auto* cards = new QHBoxLayout;
    const auto makeCard = [this](const QString& title, const QString& accent,
                                 QLabel*& valueOut) {
        auto* frame = new QFrame(this);
        frame->setFrameShape(QFrame::StyledPanel);
        frame->setStyleSheet(QStringLiteral(
            "QFrame { background-color: %1; border-radius: 8px; }").arg(accent));
        auto* v = new QVBoxLayout(frame);
        v->setContentsMargins(14, 10, 14, 10);
        auto* caption = new QLabel(title, frame);
        caption->setStyleSheet(QStringLiteral(
            "color: rgba(255,255,255,0.85); font-size: 11px; font-weight: 600;"));
        caption->setTextFormat(Qt::RichText);
        valueOut = new QLabel(QStringLiteral("—"), frame);
        valueOut->setStyleSheet(QStringLiteral(
            "color: white; font-size: 20px; font-weight: 700;"));
        v->addWidget(caption);
        v->addWidget(valueOut);
        return frame;
    };
    cards->addWidget(makeCard(tr("Self-diffusion coefficient <i>D</i>"),
                              QStringLiteral("#1565C0"), dValueLabel_), 1);
    cards->addWidget(makeCard(tr("Momentum relaxation time <i>&tau;</i><sub>relax</sub>"),
                              QStringLiteral("#00695C"), tauValueLabel_), 1);
    layout->addLayout(cards);

    noteLabel_ = new QLabel(this);
    noteLabel_->setTextFormat(Qt::RichText);
    noteLabel_->setStyleSheet(QStringLiteral("color: gray; font-size: 10px;"));
    layout->addWidget(noteLabel_);

    auto* buttons = new QHBoxLayout;
    buttons->addStretch(1);
    auto* csvButton = new QPushButton(tr("Export CSV…"), this);
    connect(csvButton, &QPushButton::clicked, this, &VacfDialog::exportCsv);
    buttons->addWidget(csvButton);
    auto* imgButton = new QPushButton(tr("Export Image…"), this);
    connect(imgButton, &QPushButton::clicked, this, &VacfDialog::exportImage);
    buttons->addWidget(imgButton);
    auto* closeButton = new QPushButton(tr("Close"), this);
    connect(closeButton, &QPushButton::clicked, this, &QDialog::accept);
    buttons->addWidget(closeButton);
    layout->addLayout(buttons);

    recompute();
}

void VacfDialog::recompute()
{
    // Slice the trajectory to the chosen [start, end] range with the stride.
    int start = startSpin_->value();
    int end = endSpin_->value();
    const int step = std::max(1, stepSpin_->value());
    if (end < start)
        std::swap(start, end);
    std::vector<std::vector<core::Vec3>> slice;
    for (int i = start; i <= end && i < static_cast<int>(velocities_.size());
         i += step)
        slice.push_back(velocities_[static_cast<std::size_t>(i)]);

    // The effective timestep grows with the stride.
    const double dtEff = dtSpin_->value() * step;
    result_ = core::computeVacf(slice, dtEff, maxLagSpin_->value());
    if (!result_.valid) {
        dValueLabel_->setText(QStringLiteral("—"));
        tauValueLabel_->setText(QStringLiteral("—"));
        noteLabel_->setText(tr("<span style='color:#c0392b'>%1</span>")
                                .arg(QString::fromStdString(result_.error)));
        cvPlot_->clear();
        vdosPlot_->clear();
        return;
    }
    cvPlot_->setData(result_.time, result_.cv);
    vdosPlot_->setData(result_.freq, result_.vdos);

    // Clean units: D in cm²/s (1 Å²/fs = 0.1 cm²/s); τ in ps (1 fs = 1e-3 ps).
    dValueLabel_->setText(tr("%1 cm²/s").arg(result_.diffusion * 0.1, 0, 'g', 4));
    tauValueLabel_->setText(
        tr("%1 ps").arg(result_.relaxationTime * 1e-3, 0, 'f', 4));
    noteLabel_->setText(
        tr("Green-Kubo D = ⅓∫⟨v(0)·v(t)⟩dt (= %1 Å²/fs) · frames %2–%3 "
           "step %4 · assumes velocities in Å/fs.")
            .arg(result_.diffusion, 0, 'g', 4)
            .arg(start)
            .arg(end)
            .arg(step));
}

void VacfDialog::exportCsv()
{
    if (!result_.valid)
        return;
    const QString path = QFileDialog::getSaveFileName(
        this, tr("Export VACF Data"), QStringLiteral("vacf.csv"),
        tr("CSV files (*.csv)"));
    if (path.isEmpty())
        return;
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QMessageBox::warning(this, windowTitle(),
                             tr("Could not write %1").arg(path));
        return;
    }
    QTextStream out(&file);
    out << "# VACF analysis\n"
        << "# D_Ang2_per_fs=" << result_.diffusion
        << " tau_relax_fs=" << result_.relaxationTime << "\n";
    out << "time_fs,Cv\n";
    for (std::size_t i = 0; i < result_.time.size(); ++i)
        out << result_.time[i] << ',' << result_.cv[i] << '\n';
    out << "\nfrequency_THz,VDOS\n";
    for (std::size_t i = 0; i < result_.freq.size(); ++i)
        out << result_.freq[i] << ',' << result_.vdos[i] << '\n';
}

void VacfDialog::exportImage()
{
    const QString path = QFileDialog::getSaveFileName(
        this, tr("Export VACF Plots"), QStringLiteral("vacf.png"),
        tr("PNG image (*.png)"));
    if (path.isEmpty())
        return;
    // Grab the two plots stacked, as shown.
    const QPixmap cv = cvPlot_->grab();
    const QPixmap vd = vdosPlot_->grab();
    QPixmap combined(std::max(cv.width(), vd.width()), cv.height() + vd.height());
    combined.fill(Qt::white);
    QPainter p(&combined);
    p.drawPixmap(0, 0, cv);
    p.drawPixmap(0, cv.height(), vd);
    p.end();
    if (!combined.save(path))
        QMessageBox::warning(this, windowTitle(),
                             tr("Could not save %1").arg(path));
}

} // namespace calango::gui
