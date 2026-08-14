#include "gui/WorkfunctionWindow.hpp"

#include "gui/GuiUtils.hpp"
#include "gui/SpectrumPlotWidget.hpp"

#include <QFileDialog>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QImage>
#include <QJsonArray>
#include <QJsonObject>
#include <QLabel>
#include <QMessageBox>
#include <QPainter>
#include <QPushButton>
#include <QTextStream>
#include <QVBoxLayout>

#include <cmath>

namespace calango::gui {

namespace {

/// Two faces are two RESULTS only when they differ beyond what the edge
/// sampling resolves; below this the split is noise and one Φ is the honest
/// headline. Matches the tolerance the generated script uses for its
/// two-faces CALANGO_INFO line, so the viewer and the log agree on which
/// case a run is.
constexpr double kTwoFaceToleranceEv = 0.02;

/// Beyond this |dV̄/dz| at the cell edges the vacuum level is being read off
/// a slope, and Φ would change with the cell size — the same 5 meV/Å the
/// generated script warns at.
constexpr double kFlatnessWarnEvPerA = 0.005;

} // namespace

WorkfunctionWindow::WorkfunctionWindow(QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle(tr("2D Workfunction Viewer"));
    resize(760, 620);

    auto* layout = new QVBoxLayout(this);

    auto* summaryGroup = new QGroupBox(tr("Work Function"), this);
    auto* summaryLayout = new QVBoxLayout(summaryGroup);

    headlineLabel_ = new QLabel(summaryGroup);
    headlineLabel_->setTextFormat(Qt::RichText);
    // The headline number carries the interpretation, so give it weight.
    QFont headline = headlineLabel_->font();
    headline.setBold(true);
    headline.setPointSizeF(headline.pointSizeF() + 2.0);
    headlineLabel_->setFont(headline);
    summaryLayout->addWidget(headlineLabel_);

    levelsLabel_ = new QLabel(summaryGroup);
    levelsLabel_->setTextFormat(Qt::RichText);
    levelsLabel_->setWordWrap(true);
    summaryLayout->addWidget(levelsLabel_);

    dipoleNote_ = new QLabel(summaryGroup);
    dipoleNote_->setWordWrap(true);
    summaryLayout->addWidget(dipoleNote_);

    warningLabel_ = new QLabel(summaryGroup);
    warningLabel_->setWordWrap(true);
    warningLabel_->setStyleSheet(QStringLiteral("color: #d9534f;"));
    warningLabel_->hide();
    summaryLayout->addWidget(warningLabel_);

    layout->addWidget(summaryGroup);

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
            &WorkfunctionWindow::exportCsv);
    connect(imageButton, &QPushButton::clicked, this,
            &WorkfunctionWindow::exportImage);
    connect(closeButton, &QPushButton::clicked, this, &QDialog::accept);
}

bool WorkfunctionWindow::loadResults(const QString& jsonPath)
{
    const QJsonObject data = readJsonObject(jsonPath);
    z_ = toDoubleVector(data.value(QStringLiteral("z_A")).toArray());
    potential_ =
        toDoubleVector(data.value(QStringLiteral("v_planar_eV")).toArray());
    // The curve is the one thing nothing here can be shown without; the
    // scalars below all default to 0.0 harmlessly if a hand-edited file
    // dropped one.
    if (z_.empty() || z_.size() != potential_.size())
        return false;

    const double efermi = data.value(QStringLiteral("efermi_eV")).toDouble();
    const double vacLow =
        data.value(QStringLiteral("vacuum_level_low_eV")).toDouble();
    const double vacHigh =
        data.value(QStringLiteral("vacuum_level_high_eV")).toDouble();
    const double phiLow =
        data.value(QStringLiteral("workfunction_low_eV")).toDouble();
    const double phiHigh =
        data.value(QStringLiteral("workfunction_high_eV")).toDouble();
    const double flatness =
        data.value(QStringLiteral("plateau_flatness_eV_per_A")).toDouble();

    const bool twoFaces = std::abs(phiLow - phiHigh) > kTwoFaceToleranceEv;
    if (twoFaces) {
        headlineLabel_->setText(
            tr("Φ = %1 eV (low-z face)&nbsp;&nbsp;·&nbsp;&nbsp;Φ = %2 eV "
               "(high-z face)")
                .arg(phiLow, 0, 'f', 3)
                .arg(phiHigh, 0, 'f', 3));
    } else {
        headlineLabel_->setText(
            tr("Work function Φ = %1 eV")
                .arg(0.5 * (phiLow + phiHigh), 0, 'f', 3));
    }

    levelsLabel_->setText(
        tr("E<sub>F</sub> = %1 eV&nbsp;&nbsp;·&nbsp;&nbsp;E<sub>vac</sub> = "
           "%2 / %3 eV (low / high edge)&nbsp;&nbsp;·&nbsp;&nbsp;plateau "
           "|dV̄/dz| = %4 meV/Å")
            .arg(efermi, 0, 'f', 3)
            .arg(vacLow, 0, 'f', 3)
            .arg(vacHigh, 0, 'f', 3)
            .arg(flatness * 1000.0, 0, 'f', 2));

    // Which case the numbers are: two genuinely different faces, or one
    // shared average. Said explicitly, because two equal Φ on an asymmetric
    // slab LOOK like a symmetric result while actually being an artifact of
    // a missing dipole correction in the baseline.
    if (twoFaces) {
        dipoleNote_->setText(
            tr("The two faces report different vacuum levels — the baseline "
               "carries a dipole correction, so each face's Φ is its own "
               "result."));
    } else {
        dipoleNote_->setText(
            tr("The two cell edges share one vacuum level. For a symmetric "
               "slab that is the physics; for an asymmetric one it means the "
               "baseline has no dipole correction, and this Φ is the average "
               "of two faces that genuinely differ."));
    }

    if (flatness > kFlatnessWarnEvPerA) {
        warningLabel_->setText(
            tr("⚠ V̄(z) is not flat at the cell edges (|dV̄/dz| = %1 meV/Å): "
               "the vacuum region is not converged — too thin for the "
               "potential to reach its asymptote, so this Φ would change "
               "with the cell size. Rebuild the slab with more vacuum and "
               "re-run the baseline.")
                .arg(flatness * 1000.0, 0, 'f', 1));
        warningLabel_->show();
    } else {
        warningLabel_->hide();
    }

    // -- V̄(z) with the levels the headline was read from ---------------------
    plot_->setSeries(z_, {{tr("V̄(z)"), potential_}}, tr("z (Å)"),
                     tr("Energy (eV)"));
    std::vector<QPair<QString, double>> references;
    references.push_back({tr("E_F"), efermi});
    if (twoFaces) {
        references.push_back({tr("E_vac (low)"), vacLow});
        references.push_back({tr("E_vac (high)"), vacHigh});
    } else {
        references.push_back({tr("E_vac"), 0.5 * (vacLow + vacHigh)});
    }
    plot_->setReferenceLines(references);
    return true;
}

void WorkfunctionWindow::exportCsv()
{
    const QString path = QFileDialog::getSaveFileName(
        this, tr("Export Planar-Averaged Potential"),
        QStringLiteral("workfunction.csv"), tr("CSV (*.csv)"));
    if (path.isEmpty())
        return;

    writeTextFile(this, path, [&](QTextStream& out) {
        // A CSV starts with its header row — the scalars (Φ, E_F, the vacuum
        // levels) live in the window and in workfunction.json, not as '#'
        // lines a CSV reader trips over.
        out << "z_A,v_planar_eV\n";
        for (std::size_t i = 0; i < z_.size(); ++i)
            out << QString::number(z_[i], 'f', 6) << ','
                << QString::number(potential_[i], 'g', 8) << '\n';
    });
}

void WorkfunctionWindow::exportImage()
{
    if (!plot_)
        return;
    const QString path = QFileDialog::getSaveFileName(
        this, tr("Export Image"), QStringLiteral("workfunction.png"),
        tr("PNG image (*.png);;JPEG image (*.jpg *.jpeg)"));
    if (path.isEmpty())
        return;

    savePlotImage(this, path, plot_->size(),
                  [this](QPainter& painter, const QSize& logical) {
                      plot_->renderTo(painter, logical);
                  });
}

} // namespace calango::gui
