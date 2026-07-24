#include "gui/BandPdosWindow.hpp"
#include "gui/GuiUtils.hpp"

#include "core/BandGap.hpp"
#include "gui/BandPdosView.hpp"
#include "gui/PlotStyleDialog.hpp"

#include <QFrame>
#include <cmath>
#include <QCheckBox>
#include <QSignalBlocker>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFile>
#include <QFileDialog>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QSaveFile>
#include <QTextStream>
#include <QVBoxLayout>

namespace calango::gui {

namespace {

} // namespace

BandPdosWindow::BandPdosWindow(const QString& directory, QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle(tr("Electronic Structure Viewer — %1").arg(directory));
    resize(980, 620);

    auto* layout = new QHBoxLayout(this);
    view_ = new BandPdosView(this);
    layout->addWidget(view_, 1);

    auto* side = new QVBoxLayout;
    layout->addLayout(side);
    auto* form = new QFormLayout;
    side->addLayout(form);

    fermiLabel_ = new QLabel(tr("—"), this);
    fermiLabel_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    fermiLabel_->setToolTip(
        tr("Fermi level as computed and written to bands.json. It is a result "
           "of the run, not a display setting, so it is shown rather than "
           "edited."));
    {
        // A QFormLayout row header is a QLabel, so the subscript can be typeset
        // properly here (the plot's axis title does the same via
        // drawWithSubscripts; a QCheckBox is plain-text only, so the shift
        // toggle below keeps the conventional "E_F" spelling).
        auto* fermiCaption = new QLabel(tr("Fermi level E<sub>F</sub>:"), this);
        fermiCaption->setTextFormat(Qt::RichText);
        form->addRow(fermiCaption, fermiLabel_);
    }

    showFermiCheck_ = new QCheckBox(tr("Show Fermi level"), this);
    showFermiCheck_->setChecked(true);
    showFermiCheck_->setToolTip(
        tr("Visibility of the dashed E_F reference line. Independent of the "
           "shift below: you can plot E − E_F without drawing the line at "
           "zero, or show absolute energies with the line marking E_F."));
    form->addRow(QString(), showFermiCheck_);
    connect(showFermiCheck_, &QCheckBox::toggled, this, [this](bool on) {
        auto style = view_->style();
        style.showFermi = on;
        view_->setStyle(style);
    });

    // The view always plots E − reference; the toggle simply chooses whether
    // the reference is E_F (bands sit around zero, the conventional
    // presentation) or 0 (raw eigenvalues on the calculator's own scale,
    // which is what you need when comparing against another code's output).
    shiftFermiCheck_ =
        new QCheckBox(tr("Shift Fermi level to zero (E − E_F = 0)"), this);
    shiftFermiCheck_->setChecked(true);
    shiftFermiCheck_->setToolTip(
        tr("On: energies are plotted relative to E_F, which is drawn as the "
           "dashed line at zero.\nOff: absolute eigenvalues, with the dashed "
           "line at the Fermi level itself."));
    form->addRow(QString(), shiftFermiCheck_);

    // Directly below the Fermi readout and its shift toggle: the gap is read
    // against E_F, so the three belong together.
    gapLabel_ = new QLabel(this);
    gapLabel_->setTextFormat(Qt::RichText);
    gapLabel_->setWordWrap(true);
    gapLabel_->setFrameShape(QFrame::StyledPanel);
    gapLabel_->setContentsMargins(8, 6, 8, 6);
    form->addRow(gapLabel_);

    minSpin_ = new QDoubleSpinBox(this);
    minSpin_->setRange(-100.0, 0.0);
    minSpin_->setValue(-10.0);
    minSpin_->setSuffix(QStringLiteral(" eV"));
    form->addRow(tr("E min:"), minSpin_);
    maxSpin_ = new QDoubleSpinBox(this);
    maxSpin_->setRange(0.0, 100.0);
    maxSpin_->setValue(10.0);
    maxSpin_->setSuffix(QStringLiteral(" eV"));
    form->addRow(tr("E max:"), maxSpin_);

    side->addWidget(new QLabel(tr("Projections:"), this));
    projectionList_ = new QListWidget(this);
    projectionList_->setMaximumWidth(190);
    side->addWidget(projectionList_, 1);

    auto* customizeButton = new QPushButton(tr("Customize Appearance…"), this);
    customizeButton->setToolTip(
        tr("Fonts, curve and reference-line styling, plot background, borders "
           "and tick strokes."));
    side->addWidget(customizeButton);
    connect(customizeButton, &QPushButton::clicked, this, [this] {
        auto* dialog = new PlotStyleDialog(view_->style(), /*phonon=*/false, this);
        connect(dialog, &PlotStyleDialog::styleChanged, view_,
                &BandPdosView::setStyle);
        // Keep the standalone checkbox in step when the dialog toggles the
        // same property, so the two never disagree.
        connect(dialog, &PlotStyleDialog::styleChanged, this,
                [this](const BandPdosView::Style& style) {
                    const QSignalBlocker blocker(showFermiCheck_);
                    showFermiCheck_->setChecked(style.showFermi);
                });
        dialog->setAttribute(Qt::WA_DeleteOnClose);
        dialog->show();
    });

    auto* exportImageButton = new QPushButton(tr("Export Image…"), this);
    exportImageButton->setToolTip(
        tr("PNG / JPEG at 3x for print, or PDF / SVG as resolution-independent "
           "vector art. Exports both panels exactly as displayed."));
    side->addWidget(exportImageButton);
    connect(exportImageButton, &QPushButton::clicked, this,
            [this] { view_->exportImage(this); });

    auto* exportBandsButton = new QPushButton(tr("Export Bands…"), this);
    auto* exportPdosButton = new QPushButton(tr("Export PDOS…"), this);
    side->addWidget(exportBandsButton);
    side->addWidget(exportPdosButton);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    side->addWidget(buttons);

    connect(shiftFermiCheck_, &QCheckBox::toggled, this,
            [this] { applyFermiShift(); });
    const auto applyWindow = [this] {
        view_->setEnergyWindow(minSpin_->value(), maxSpin_->value());
    };
    connect(minSpin_, &QDoubleSpinBox::valueChanged, this, applyWindow);
    connect(maxSpin_, &QDoubleSpinBox::valueChanged, this, applyWindow);
    connect(projectionList_, &QListWidget::itemChanged, this,
            [this](QListWidgetItem* item) {
                view_->setProjectionVisible(item->text(),
                                            item->checkState() == Qt::Checked);
            });
    connect(exportBandsButton, &QPushButton::clicked,
            this, &BandPdosWindow::exportBands);
    connect(exportPdosButton, &QPushButton::clicked,
            this, &BandPdosWindow::exportPdos);

    loadDirectory(directory);
}

void BandPdosWindow::loadDirectory(const QString& directory)
{
    const QJsonObject bands = readJsonObject(directory + QStringLiteral("/bands.json"));
    if (bands.isEmpty())
        return;

    BandPdosView::BandData data;
    data.x = toDoubleVector(bands[QStringLiteral("x")].toArray());
    data.specialX = toDoubleVector(bands[QStringLiteral("special_x")].toArray());
    for (const auto& label : bands[QStringLiteral("special_labels")].toArray())
        data.specialLabels << label.toString();
    data.efermi = bands[QStringLiteral("efermi")].toDouble();
    for (const auto& spin : bands[QStringLiteral("energies")].toArray()) {
        std::vector<std::vector<double>> kpts;
        for (const auto& kpt : spin.toArray())
            kpts.push_back(toDoubleVector(kpt.toArray()));
        data.energies.push_back(std::move(kpts));
    }
    fermiLevel_ = data.efermi;
    fermiLabel_->setText(QStringLiteral("%1 eV").arg(fermiLevel_, 0, 'f', 4));
    applyFermiShift();
    view_->setBandData(std::move(data));
    refreshBandGap();
    view_->setEnergyWindow(minSpin_->value(), maxSpin_->value());
    hasData_ = true;

    const QJsonObject pdos = readJsonObject(directory + QStringLiteral("/pdos.json"));
    if (!pdos.isEmpty()) {
        BandPdosView::PdosData pdosData;
        pdosData.energies = toDoubleVector(pdos[QStringLiteral("energies")].toArray());
        const QJsonObject projections =
            pdos[QStringLiteral("projections")].toObject();
        for (auto it = projections.begin(); it != projections.end(); ++it)
            pdosData.projections.emplace_back(it.key(),
                                              toDoubleVector(it.value().toArray()));
        int index = 0;
        for (const auto& [label, curve] : pdosData.projections) {
            (void)curve;
            auto* item = new QListWidgetItem(label, projectionList_);
            item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
            item->setCheckState(Qt::Checked);
            item->setForeground(BandPdosView::projectionColor(index++));
        }
        view_->setPdosData(std::move(pdosData));
    } else {
        projectionList_->addItem(tr("(no PDOS in this run)"));
        projectionList_->setEnabled(false);
    }
}

void BandPdosWindow::exportBands()
{
    const auto& data = view_->bandData();
    if (!data.valid())
        return;
    const QString path = QFileDialog::getSaveFileName(
        this, tr("Export Band Structure"), QStringLiteral("bands.csv"),
        tr("CSV (*.csv);;Gnuplot data (*.dat)"));
    if (path.isEmpty())
        return;
    const QChar sep = path.endsWith(QLatin1String(".dat")) ? QChar(' ')
                                                           : QChar(',');
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QMessageBox::warning(this, windowTitle(),
                             tr("Could not write %1").arg(path));
        return;
    }
    QTextStream out(&file);
    out << "k_distance";
    const std::size_t bandCount =
        data.energies.front().empty() ? 0 : data.energies.front().front().size();
    for (std::size_t s = 0; s < data.energies.size(); ++s)
        for (std::size_t b = 0; b < bandCount; ++b)
            out << sep
                << QStringLiteral("band_%1%2").arg(b + 1).arg(
                       data.energies.size() > 1
                           ? QStringLiteral("_spin%1").arg(s + 1)
                           : QString());
    out << "\n";
    for (std::size_t k = 0; k < data.x.size(); ++k) {
        out << data.x[k];
        for (const auto& spin : data.energies)
            for (std::size_t b = 0; b < bandCount; ++b)
                out << sep << spin[k][b];
        out << "\n";
    }
    file.commit();
}

void BandPdosWindow::exportPdos()
{
    const auto& data = view_->pdosData();
    if (!data.valid()) {
        QMessageBox::information(this, windowTitle(),
                                 tr("This run has no PDOS data."));
        return;
    }
    const QString path = QFileDialog::getSaveFileName(
        this, tr("Export PDOS"), QStringLiteral("pdos.csv"),
        tr("CSV (*.csv);;Gnuplot data (*.dat)"));
    if (path.isEmpty())
        return;
    const QChar sep = path.endsWith(QLatin1String(".dat")) ? QChar(' ')
                                                           : QChar(',');
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QMessageBox::warning(this, windowTitle(),
                             tr("Could not write %1").arg(path));
        return;
    }
    QTextStream out(&file);
    out << "energy_eV";
    for (const auto& [label, curve] : data.projections) {
        (void)curve;
        out << sep << QString(label).replace(QLatin1Char(' '), QLatin1Char('_'));
    }
    out << "\n";
    for (std::size_t i = 0; i < data.energies.size(); ++i) {
        out << data.energies[i];
        for (const auto& [label, curve] : data.projections)
            out << sep << (i < curve.size() ? curve[i] : 0.0);
        out << "\n";
    }
    file.commit();
}

void BandPdosWindow::refreshBandGap()
{
    const auto& bands = view_->bandData();
    if (!bands.valid()) {
        gapLabel_->setText(tr("No band data loaded."));
        return;
    }
    // Analyzed against the Fermi level currently in the spin box, not the one
    // baked into the file: correcting E_F is exactly why that field is
    // editable, and the gap must follow the correction.
    const auto info =
        core::analyzeBandGap(bands.energies, fermiLevel_);

    if (!info.valid) {
        gapLabel_->setText(
            tr("<b>Band gap:</b> not determinable — the plotted bands lie "
               "entirely on one side of E_F."));
        return;
    }
    if (info.metallic) {
        gapLabel_->setText(
            tr("<b>Metallic</b> — a band crosses E_F, so there is no gap."));
        return;
    }

    // Both gaps are always listed, not just the one that happens to be the
    // fundamental one:
    //   indirect / fundamental = CBM - VBM, wherever those sit on the path;
    //   direct                 = smallest SAME-k separation (optical onset).
    // For a direct-gap material the two coincide by definition — showing both
    // makes that agreement visible instead of leaving the reader to infer it.
    const QString nature = info.direct ? tr("direct") : tr("indirect");
    QString text =
        tr("<b>Band gap &nbsp; %1 eV</b> &nbsp; (%2)<br>"
           "E<sub>g, indirect</sub> = %3 eV<br>"
           "E<sub>g, direct</sub> &nbsp;&nbsp;= %4 eV")
            .arg(info.gap, 0, 'f', 3)
            .arg(nature)
            .arg(info.gap, 0, 'f', 3)
            .arg(info.directGap, 0, 'f', 3);

    // Name the k-points where the extrema sit when the path labels them.
    const auto labelFor = [&bands](std::size_t k) -> QString {
        if (k >= bands.x.size() || bands.specialLabels.isEmpty())
            return {};
        for (int i = 0; i < bands.specialLabels.size()
             && i < static_cast<int>(bands.specialX.size()); ++i) {
            if (std::abs(bands.specialX[static_cast<std::size_t>(i)] - bands.x[k])
                < 1e-9) {
                const QString raw = bands.specialLabels.at(i);
                return raw == QLatin1String("G") ? QString::fromUtf8("Γ") : raw;
            }
        }
        return {};
    };
    const QString vbmAt = labelFor(info.vbmKPoint);
    const QString cbmAt = labelFor(info.cbmKPoint);
    text += tr("<br>VBM %1 eV%2 &rarr; CBM %3 eV%4")
                .arg(info.vbm, 0, 'f', 3)
                .arg(vbmAt.isEmpty() ? QString() : QStringLiteral(" @ %1").arg(vbmAt))
                .arg(info.cbm, 0, 'f', 3)
                .arg(cbmAt.isEmpty() ? QString() : QStringLiteral(" @ %1").arg(cbmAt));
    text += tr("<br><span style='color:#909090'>Evaluated on the plotted "
               "k-path only.</span>");
    gapLabel_->setText(text);
}

void BandPdosWindow::applyFermiShift()
{
    const bool shift = shiftFermiCheck_->isChecked();
    view_->setReference(shift ? fermiLevel_ : 0.0);
    view_->setReferenceIsFermi(shift);
    if (gapLabel_ && view_->bandData().valid())
        refreshBandGap();
    // The E min / E max window is expressed in the *displayed* coordinate, so
    // its sensible defaults differ: ±10 eV around E_F when shifted, but a
    // window that still brackets E_F when showing absolute energies.
    minSpin_->setToolTip(shift ? tr("Lower edge of the plotted window, "
                                    "relative to E_F")
                               : tr("Lower edge of the plotted window, on the "
                                    "absolute energy scale"));
    maxSpin_->setToolTip(minSpin_->toolTip());
}

} // namespace calango::gui
