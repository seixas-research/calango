#include "gui/BandPdosWindow.hpp"
#include "gui/GuiUtils.hpp"

#include "core/BandGap.hpp"
#include "gui/BandPdosView.hpp"
#include "gui/PlotStyleDialog.hpp"

#include <QFrame>
#include <cmath>
#include <QCheckBox>
#include <QComboBox>
#include <QGroupBox>
#include <QSignalBlocker>
#include <QSlider>
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
        // A QFormLayout row header is a QLabel, so the subscript is typeset
        // properly here; the plot's axis title does the same via
        // drawWithSubscripts, and the check boxes below via richTextCheckBox.
        // Every E_F in this window is a real subscript — a literal underscore
        // in one of them next to a typeset one in the others reads as a bug.
        auto* fermiCaption = new QLabel(tr("Fermi level E<sub>F</sub>:"), this);
        fermiCaption->setTextFormat(Qt::RichText);
        form->addRow(fermiCaption, fermiLabel_);
    }

    QWidget* showFermiRow =
        richTextCheckBox(tr("Show Fermi level E<sub>F</sub>"), showFermiCheck_,
                         this);
    showFermiCheck_->setChecked(true);
    // Rich text in a tool tip too, so the subscript survives there: a tip that
    // is rich text needs <br> rather than \n for its line breaks.
    showFermiRow->setToolTip(
        tr("Visibility of the dashed E<sub>F</sub> reference line. Independent "
           "of the shift below: you can plot E − E<sub>F</sub> without drawing "
           "the line at zero, or show absolute energies with the line marking "
           "E<sub>F</sub>."));
    form->addRow(QString(), showFermiRow);
    connect(showFermiCheck_, &QCheckBox::toggled, this, [this](bool on) {
        auto style = view_->style();
        style.showFermi = on;
        view_->setStyle(style);
    });

    // The view always plots E − reference; the toggle simply chooses whether
    // the reference is E_F (bands sit around zero, the conventional
    // presentation) or 0 (raw eigenvalues on the calculator's own scale,
    // which is what you need when comparing against another code's output).
    QWidget* shiftFermiRow = richTextCheckBox(
        tr("Shift Fermi level to zero (E − E<sub>F</sub> = 0)"),
        shiftFermiCheck_, this);
    shiftFermiCheck_->setChecked(true);
    shiftFermiRow->setToolTip(
        tr("On: energies are plotted relative to E<sub>F</sub>, which is drawn "
           "as the dashed line at zero.<br>Off: absolute eigenvalues, with the "
           "dashed line at the Fermi level itself."));
    form->addRow(QString(), shiftFermiRow);

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

    // --- Orbital projections (fatbands) -----------------------------------
    fatbandGroup_ = new QGroupBox(tr("Orbital projections"), this);
    fatbandGroup_->setMaximumWidth(190);
    auto* fatbandLayout = new QVBoxLayout(fatbandGroup_);
    fatbandModeCombo_ = new QComboBox(fatbandGroup_);
    fatbandModeCombo_->addItem(
        tr("Off"), static_cast<int>(BandPdosView::FatbandMode::Off));
    fatbandModeCombo_->addItem(
        tr("Line width"), static_cast<int>(BandPdosView::FatbandMode::Width));
    fatbandModeCombo_->addItem(
        tr("Colour scale"), static_cast<int>(BandPdosView::FatbandMode::Color));
    fatbandModeCombo_->addItem(
        tr("Width + colour"), static_cast<int>(BandPdosView::FatbandMode::Both));
    // Width + colour is the default: the width is the classic fatband, and the
    // colormap — transparent at zero weight — is what keeps several channels
    // readable on the same axes.
    fatbandModeCombo_->setCurrentIndex(
        fatbandModeCombo_->findData(
            static_cast<int>(BandPdosView::FatbandMode::Both)));
    fatbandModeCombo_->setToolTip(
        tr("How the orbital weight of each band is drawn.\n\n"
           "Line width is the classic fatband: the band thickens where the "
           "selected orbital contributes. Colour scale keeps the width "
           "constant and maps the weight onto the channel's sequential "
           "colormap instead, which stays readable where many bands run close "
           "together. Both applies each."));
    fatbandLayout->addWidget(fatbandModeCombo_);
    fatbandList_ = new QListWidget(fatbandGroup_);
    fatbandList_->setToolTip(
        tr("Channels drawn over the dispersion. Several at once is the point: "
           "seeing metal d and ligand p on the same plot is how "
           "hybridization becomes visible.\n\n"
           "Each channel has its own sequential colormap (Greens, Blues, "
           "Reds, …) that is fully transparent at zero weight, so the "
           "channels superimpose instead of painting over one another."));
    fatbandLayout->addWidget(fatbandList_, 1);
    side->addWidget(fatbandGroup_, 1);
    fatbandGroup_->setVisible(false);
    connect(fatbandModeCombo_, &QComboBox::currentIndexChanged, this, [this] {
        view_->setFatbandMode(static_cast<BandPdosView::FatbandMode>(
            fatbandModeCombo_->currentData().toInt()));
    });
    connect(fatbandList_, &QListWidget::itemChanged, this,
            [this](QListWidgetItem* item) {
                view_->setFatbandChannelVisible(
                    item->text(), item->checkState() == Qt::Checked);
            });

    // --- Band symmetry ----------------------------------------------------
    symmetryGroup_ = new QGroupBox(tr("Band symmetry"), this);
    symmetryGroup_->setMaximumWidth(190);
    auto* symmetryLayout = new QVBoxLayout(symmetryGroup_);
    symmetryCheck_ = new QCheckBox(tr("Show irrep labels"), symmetryGroup_);
    symmetryCheck_->setChecked(true);
    symmetryCheck_->setToolTip(
        tr("Draw the irreducible representation each band (or degenerate "
           "multiplet) realizes beside the high-symmetry ticks.\n\n"
           "A two-dimensional irrep at the Fermi level is a Dirac point; two "
           "bands carrying different irreps cross rather than repel."));
    symmetryLayout->addWidget(symmetryCheck_);
    symmetryLineCheck_ =
        new QCheckBox(tr("…on symmetry lines too"), symmetryGroup_);
    symmetryLineCheck_->setToolTip(
        tr("Also label the midpoint of each path segment. Together with the "
           "endpoint labels these are the COMPATIBILITY RELATIONS: the irrep "
           "at a point decomposes into the irreps of the lines running out of "
           "it, which is how a degenerate level splits and which branch goes "
           "where.\n\nOff by default — the line labels are numerous."));
    symmetryLayout->addWidget(symmetryLineCheck_);
    symmetrySummary_ = new QLabel(symmetryGroup_);
    symmetrySummary_->setWordWrap(true);
    symmetrySummary_->setTextFormat(Qt::RichText);
    symmetryLayout->addWidget(symmetrySummary_);
    side->addWidget(symmetryGroup_);
    symmetryGroup_->setVisible(false);
    connect(symmetryCheck_, &QCheckBox::toggled, this, [this](bool on) {
        view_->setSymmetryLabelsVisible(on);
        symmetryLineCheck_->setEnabled(on);
    });
    connect(symmetryLineCheck_, &QCheckBox::toggled, view_,
            &BandPdosView::setSymmetryLineLabelsVisible);

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

    // -- Live PDOS smearing -------------------------------------------------
    //
    // The run stores the unbroadened histogram, so σ is a drawing parameter
    // here rather than something the SCF was committed to. That is the whole
    // point of the change: deciding whether a shoulder is a real feature or an
    // artifact of the broadening used to cost another full calculation.
    smearingBox_ = new QGroupBox(tr("DOS smearing"), this);
    auto* smearingLayout = new QVBoxLayout(smearingBox_);
    auto* smearingRow = new QHBoxLayout;
    smearingSpin_ = new QDoubleSpinBox(smearingBox_);
    smearingSpin_->setRange(0.001, 2.0);
    smearingSpin_->setDecimals(3);
    smearingSpin_->setSingleStep(0.01);
    smearingSpin_->setValue(0.1);
    smearingSpin_->setSuffix(tr(" eV"));
    smearingSpin_->setToolTip(
        tr("Gaussian σ each eigenvalue is broadened by. Applied when the curve "
           "is drawn, not when it was computed, so it costs a redraw rather "
           "than a re-run."));
    // A slider as well as the spin box: σ is explored by sweeping it — watching
    // which peaks survive — far more often than it is typed in.
    smearingSlider_ = new QSlider(Qt::Horizontal, smearingBox_);
    smearingSlider_->setRange(1, 2000); // milli-eV
    smearingSlider_->setValue(100);
    smearingRow->addWidget(smearingSlider_, 1);
    smearingRow->addWidget(smearingSpin_);
    smearingLayout->addLayout(smearingRow);
    smearingNote_ = new QLabel(smearingBox_);
    smearingNote_->setWordWrap(true);
    smearingLayout->addWidget(smearingNote_);
    side->addWidget(smearingBox_);

    // The two controls drive one value; each blocks the other while writing so
    // the round trip through σ does not fight the user's drag.
    connect(smearingSlider_, &QSlider::valueChanged, this, [this](int milli) {
        const QSignalBlocker blocker(smearingSpin_);
        smearingSpin_->setValue(milli / 1000.0);
        view_->setPdosSmearing(milli / 1000.0);
    });
    connect(smearingSpin_, &QDoubleSpinBox::valueChanged, this,
            [this](double sigma) {
                const QSignalBlocker blocker(smearingSlider_);
                smearingSlider_->setValue(
                    static_cast<int>(std::lround(sigma * 1000.0)));
                view_->setPdosSmearing(sigma);
            });

    auto* exportBandsButton = new QPushButton(tr("Export Bands…"), this);
    auto* exportPdosButton = new QPushButton(tr("Export PDOS…"), this);
    exportFatbandsButton_ = new QPushButton(tr("Export Fatbands…"), this);
    exportFatbandsButton_->setToolTip(
        tr("The orbital weights themselves, as one row per (k-point, spin, "
           "band) carrying that state's energy and one column per projection "
           "channel.\n\nThat shape rather than the wide one the band export "
           "uses: a weight is meaningless without the energy it belongs to, "
           "and a wide table would need channels × bands columns to keep the "
           "two together."));
    // Shown by loadFatbands() when the run actually wrote weights.
    exportFatbandsButton_->setVisible(false);
    side->addWidget(exportBandsButton);
    side->addWidget(exportPdosButton);
    side->addWidget(exportFatbandsButton_);
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
    connect(exportFatbandsButton_, &QPushButton::clicked,
            this, &BandPdosWindow::exportFatbands);

    loadDirectory(directory);
}

void BandPdosWindow::updateSmearingControl()
{
    if (!smearingBox_)
        return;
    const bool live = view_->pdosSmearingAvailable();
    smearingSlider_->setEnabled(live);
    smearingSpin_->setEnabled(live);
    if (live) {
        // Follow the value the view adopted from the run rather than leaving
        // the controls on their construction default.
        const QSignalBlocker spinBlocker(smearingSpin_);
        const QSignalBlocker sliderBlocker(smearingSlider_);
        smearingSpin_->setValue(view_->pdosSmearing());
        smearingSlider_->setValue(
            static_cast<int>(std::lround(view_->pdosSmearing() * 1000.0)));
        const double bin = view_->pdosData().binWidth;
        smearingNote_->setText(
            tr("<i>Applied to the stored eigenvalue histogram as the curve is "
               "drawn — no re-run. The %1 eV bin width is the resolution "
               "floor: a smaller σ than that cannot be shown.</i>")
                .arg(bin, 0, 'g', 3));
        return;
    }
    smearingBox_->setVisible(view_->pdosData().valid());
    // Two different reasons to have no slider, and they need different words.
    // A tetrahedron DOS is not "broadened with a σ we have lost" — it has no σ
    // at all, and offering to re-broaden it would be offering to smear an
    // exact integral. Telling the user their width is merely fixed would send
    // them off to re-run for a slider they do not want.
    if (view_->pdosData().integration == QLatin1String("tetrahedron")) {
        smearingNote_->setText(
            tr("<i>Integrated with the linear tetrahedron method (Blöchl): the "
               "bands were interpolated inside tetrahedra filling the "
               "Brillouin zone and the DOS integrated analytically, so there "
               "is no broadening width to vary. The band edges are as sharp as "
               "the k-mesh supports — re-run with <b>Sampling</b> if you want "
               "a σ slider.</i>"));
        return;
    }
    smearingNote_->setText(
        tr("<i>This run stored curves that were already broadened, so σ is "
           "fixed at whatever it was calculated with. Re-run to get a "
           "histogram that can be re-broadened here.</i>"));
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
        // "broadened" is written (as false) only by runs that store the RAW
        // histogram. Its ABSENCE identifies an older run whose curves already
        // carry a Gaussian, and those must be drawn as they are — hence the
        // default of true rather than false.
        pdosData.broadened =
            pdos.value(QStringLiteral("broadened")).toBool(true);
        pdosData.binWidth =
            pdos.value(QStringLiteral("bin_width")).toDouble(0.0);
        // 0 = not stated, which the view reads as "derive one from the bin
        // width". Older runs that DID state one keep it.
        pdosData.suggestedWidth =
            pdos.value(QStringLiteral("suggested_width")).toDouble(0.0);
        pdosData.integration =
            pdos.value(QStringLiteral("integration")).toString();
        // Derive the bin width when the run did not state it but the grid is
        // uniform, which is what makes broadening possible at all.
        if (pdosData.binWidth <= 0.0 && pdosData.energies.size() > 1) {
            pdosData.binWidth =
                pdosData.energies[1] - pdosData.energies[0];
        }
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
        updateSmearingControl();
    } else {
        projectionList_->addItem(tr("(no PDOS in this run)"));
        projectionList_->setEnabled(false);
        updateSmearingControl();
    }

    // Both are opt-in extras of the run: absent unless the wizard asked for
    // them, so their panels appear only when the files are there.
    fatbandGroup_->setVisible(loadFatbands(directory));
    symmetryGroup_->setVisible(loadSymmetry(directory));
}

bool BandPdosWindow::loadFatbands(const QString& directory)
{
    const QJsonObject root =
        readJsonObject(directory + QStringLiteral("/fatbands.json"));
    if (root.isEmpty())
        return false;

    BandPdosView::FatbandData data;
    data.maxWeight = root[QStringLiteral("max_weight")].toDouble(1.0);
    for (const auto& entry : root[QStringLiteral("projections")].toArray()) {
        const QJsonObject projection = entry.toObject();
        std::vector<std::vector<std::vector<double>>> weights;
        for (const auto& spin : projection[QStringLiteral("weights")].toArray()) {
            std::vector<std::vector<double>> kpoints;
            for (const auto& kpt : spin.toArray())
                kpoints.push_back(toDoubleVector(kpt.toArray()));
            weights.push_back(std::move(kpoints));
        }
        if (weights.empty())
            continue;
        data.projections.emplace_back(
            projection[QStringLiteral("label")].toString(), std::move(weights));
    }
    if (data.projections.empty())
        return false;
    // A zero or missing maximum would divide the whole overlay away.
    if (!(data.maxWeight > 0.0))
        data.maxWeight = 1.0;

    int index = 0;
    for (const auto& [label, weights] : data.projections) {
        (void)weights;
        auto* item = new QListWidgetItem(label, fatbandList_);
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
        item->setCheckState(Qt::Checked);
        item->setForeground(BandPdosView::fatbandColor(index));
        // Name the colormap on the row itself. A figure caption has to say
        // which orbital got which colour, and the swatch alone does not tell a
        // reader that "reddish" means the Reds map rather than the Oranges one.
        item->setToolTip(tr("%1 — %2 colormap")
                             .arg(label,
                                  BandPdosView::fatbandColormapName(index)));
        ++index;
    }
    view_->setFatbandData(std::move(data));
    view_->setFatbandMode(static_cast<BandPdosView::FatbandMode>(
        fatbandModeCombo_->currentData().toInt()));
    exportFatbandsButton_->setVisible(true);
    return true;
}

bool BandPdosWindow::loadSymmetry(const QString& directory)
{
    const QJsonObject root =
        readJsonObject(directory + QStringLiteral("/band_symmetry.json"));
    if (root.isEmpty())
        return false;

    BandPdosView::SymmetryData data;
    data.spaceGroup = root[QStringLiteral("space_group")].toString();
    int projective = 0;
    int points = 0;
    for (const auto& entry : root[QStringLiteral("points")].toArray()) {
        const QJsonObject point = entry.toObject();
        const bool onLine =
            point[QStringLiteral("kind")].toString() == QLatin1String("line");
        if (!onLine)
            ++points;
        if (point[QStringLiteral("projective")].toBool())
            ++projective;
        const double x = point[QStringLiteral("x")].toDouble();
        for (const auto& spinEntry : point[QStringLiteral("spins")].toArray()) {
            const QJsonObject spin = spinEntry.toObject();
            for (const auto& multipletEntry :
                 spin[QStringLiteral("multiplets")].toArray()) {
                const QJsonObject multiplet = multipletEntry.toObject();
                const QString label = multiplet[QStringLiteral("label")].toString();
                if (label.isEmpty() || label == QLatin1String("?"))
                    continue;
                BandPdosView::SymmetryLabel item;
                item.x = x;
                item.energy = multiplet[QStringLiteral("energy_eV")].toDouble();
                item.text = label;
                item.degeneracy =
                    multiplet[QStringLiteral("degeneracy")].toInt(1);
                item.onLine = onLine;
                data.labels.push_back(std::move(item));
            }
        }
    }
    if (data.labels.empty())
        return false;

    QString summary =
        tr("<b>%1</b> — %n high-symmetry point(s) classified.", nullptr, points)
            .arg(data.spaceGroup.isEmpty() ? tr("space group unknown")
                                           : data.spaceGroup);
    if (projective > 0) {
        // Not a footnote: at a zone-boundary point of a nonsymmorphic group
        // the little-group representations are projective, and no ordinary
        // Mulliken symbol applies to them. The run says so; so must the
        // viewer, rather than showing a nearest-fit label as if it were one.
        summary += tr("<br><span style='color:#d08a4a'>%n point(s) carry "
                      "PROJECTIVE representations (a zone boundary of a "
                      "nonsymmorphic group); their labels are approximate.",
                      nullptr, projective);
    }
    symmetrySummary_->setText(summary);
    view_->setSymmetryData(std::move(data));
    view_->setSymmetryLabelsVisible(symmetryCheck_->isChecked());
    view_->setSymmetryLineLabelsVisible(symmetryLineCheck_->isChecked());
    return true;
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
    writeTextFile(this, path, [&](QTextStream& out) {
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
    });
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
    // The BROADENED curves, not the stored histogram: what is exported has to
    // be what is on screen. The histogram is a comb of per-bin weights and
    // would plot as noise next to the figure it was exported from.
    const auto& curves = view_->pdosCurves();
    const double sigma = view_->pdosSmearing();
    writeTextFile(this, path, [&](QTextStream& out) {
        // The σ is part of the data, so it travels with it — a DOS curve
        // without its broadening is not reproducible.
        if (view_->pdosSmearingAvailable())
            out << "# gaussian_sigma_eV" << sep << sigma << "\n";
        out << "energy_eV";
        for (const auto& [label, curve] : curves) {
            (void)curve;
            out << sep << QString(label).replace(QLatin1Char(' '), QLatin1Char('_'));
        }
        out << "\n";
        for (std::size_t i = 0; i < data.energies.size(); ++i) {
            out << data.energies[i];
            for (const auto& [label, curve] : curves)
                out << sep << (i < curve.size() ? curve[i] : 0.0);
            out << "\n";
        }
    });
}

QString BandPdosWindow::fatbandTable(QChar separator) const
{
    const auto& fatbands = view_->fatbandData();
    const auto& bands = view_->bandData();
    if (!fatbands.valid() || !bands.valid())
        return {};

    QString table;
    QTextStream out(&table);
    out << "k_distance" << separator << "spin" << separator << "band"
        << separator << "energy_eV";
    for (const auto& [label, weights] : fatbands.projections) {
        (void)weights;
        out << separator
            << QString(label).replace(QLatin1Char(' '), QLatin1Char('_'));
    }
    out << "\n";

    for (std::size_t spin = 0; spin < bands.energies.size(); ++spin) {
        const auto& kEnergies = bands.energies[spin];
        for (std::size_t k = 0; k < kEnergies.size() && k < bands.x.size();
             ++k) {
            for (std::size_t band = 0; band < kEnergies[k].size(); ++band) {
                // Energies are ABSOLUTE, as bands.json carries them and as the
                // band export writes them — not shifted by the viewer's
                // current Fermi reference, which is a display setting.
                out << bands.x[k] << separator << (spin + 1) << separator
                    << (band + 1) << separator << kEnergies[k][band];
                for (const auto& [label, weights] : fatbands.projections) {
                    (void)label;
                    // A channel can legitimately be shorter than the band
                    // manifold: the weights come from a separate GPAW pass
                    // that may have covered fewer bands. Missing entries are
                    // zero weight, not a truncated file.
                    double weight = 0.0;
                    if (spin < weights.size() && k < weights[spin].size()
                        && band < weights[spin][k].size())
                        weight = weights[spin][k][band];
                    out << separator << weight;
                }
                out << "\n";
            }
        }
    }
    return table;
}

void BandPdosWindow::exportFatbands()
{
    if (!view_->fatbandData().valid() || !view_->bandData().valid()) {
        QMessageBox::information(
            this, windowTitle(),
            tr("This run has no orbital projections.\n\nTick \"Orbital "
               "projections (fatbands)\" in the Electronic Structure wizard "
               "before running to have the weights written."));
        return;
    }
    const QString path = QFileDialog::getSaveFileName(
        this, tr("Export Fatband Weights"), QStringLiteral("fatbands.csv"),
        tr("CSV (*.csv);;Gnuplot data (*.dat)"));
    if (path.isEmpty())
        return;
    const QChar separator =
        path.endsWith(QLatin1String(".dat")) ? QChar(' ') : QChar(',');
    writeTextFile(this, path, fatbandTable(separator));
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
               "entirely on one side of E<sub>F</sub>."));
        return;
    }
    if (info.metallic) {
        gapLabel_->setText(
            tr("<b>Metallic</b> — a band crosses E<sub>F</sub>, so there is no "
               "gap."));
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
                                    "relative to E<sub>F</sub>")
                               : tr("Lower edge of the plotted window, on the "
                                    "absolute energy scale"));
    // Its own text, not a copy of the minimum's: this row is the UPPER edge,
    // and the shared tip said "Lower edge" over the E max field.
    maxSpin_->setToolTip(shift ? tr("Upper edge of the plotted window, "
                                    "relative to E<sub>F</sub>")
                               : tr("Upper edge of the plotted window, on the "
                                    "absolute energy scale"));
}

} // namespace calango::gui
