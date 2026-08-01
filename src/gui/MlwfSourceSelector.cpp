#include "gui/MlwfSourceSelector.hpp"

#include "gui/GuiUtils.hpp"

#include <QComboBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonObject>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

namespace calango::gui {

MlwfSourceSelector::MlwfSourceSelector(
    const QList<QPair<QString, QString>>& candidates, QWidget* parent)
    : QWidget(parent)
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);

    auto* row = new QHBoxLayout;
    combo_ = new QComboBox(this);
    combo_->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    combo_->setToolTip(
        tr("A completed Wannier Functions run. This module "
           "diagonalizes the localized Hamiltonian H(R) that run produced, so "
           "the Wannier count, the trial projections and the wavefunctions all "
           "come from it — none of the settings below can stand in for a "
           "different source."));
    for (const auto& [label, directory] : candidates)
        combo_->addItem(label, directory);
    // Newest last in the process list, and the newest run is nearly always the
    // one being followed up — so it is the one preselected.
    if (combo_->count() > 0)
        combo_->setCurrentIndex(combo_->count() - 1);
    row->addWidget(combo_, 1);

    auto* browseButton = new QPushButton(tr("Browse…"), this);
    browseButton->setToolTip(
        tr("Pick a Wannier Functions job directory that is not in the list — a run from an "
           "earlier session, or one copied back from a cluster. The directory "
           "is the one holding wannier.json."));
    row->addWidget(browseButton);
    layout->addLayout(row);

    status_ = new QLabel(this);
    status_->setWordWrap(true);
    status_->setTextFormat(Qt::RichText);
    layout->addWidget(status_);

    connect(browseButton, &QPushButton::clicked, this,
            &MlwfSourceSelector::browse);
    connect(combo_, &QComboBox::currentIndexChanged, this, [this] {
        revalidate();
        Q_EMIT changed();
    });

    revalidate();
}

QString MlwfSourceSelector::directory() const
{
    return combo_->currentIndex() < 0 ? QString()
                                      : combo_->currentData().toString();
}

void MlwfSourceSelector::browse()
{
    const QString chosen = QFileDialog::getExistingDirectory(
        this, tr("Select Wannier Functions Job Directory"), directory());
    if (chosen.isEmpty())
        return;
    // Added rather than swapped in, so a browse that turns out to be the wrong
    // folder does not lose the tracked runs the user could pick instead.
    // Re-selected rather than duplicated when it is already listed.
    const QString absolute = QDir(chosen).absolutePath();
    for (int i = 0; i < combo_->count(); ++i) {
        if (QDir(combo_->itemData(i).toString()).absolutePath() == absolute) {
            combo_->setCurrentIndex(i);
            return;
        }
    }
    combo_->addItem(QDir(absolute).dirName(), absolute);
    combo_->setCurrentIndex(combo_->count() - 1);
}

void MlwfSourceSelector::revalidate()
{
    valid_ = false;
    invalidReason_.clear();

    const QString dir = directory();
    if (dir.isEmpty()) {
        invalidReason_ = tr(
            "No MLWF process is selected. This module post-processes a "
            "completed Wannier Functions run — run "
            "Electronics → Wannier Functions… "
            "first, or use Browse… to point at a finished job directory.");
        status_->setText(tr("<b>No Wannier Functions process selected.</b>"));
        return;
    }

    // wannier.json is the completion marker: the MLWF script writes it last,
    // so its presence is what distinguishes a finished run from a directory
    // that merely exists.
    const QString summaryPath =
        QDir(dir).filePath(QStringLiteral("wannier.json"));
    if (!QFileInfo::exists(summaryPath)) {
        invalidReason_ =
            tr("%1 holds no wannier.json, so it is not a completed Wannier Functions run. "
               "That file is written last, when the localization has "
               "converged.")
                .arg(dir);
        status_->setText(
            tr("<b style=\"color:#e06c5a\">Not a completed Wannier Functions run</b> — no "
               "wannier.json in this directory."));
        return;
    }

    // Completed is not the same as usable: the wavefunctions have to still be
    // there. This is the shared pre-flight — an MLWF run that borrowed a
    // single-point baseline's .gpw wrote none of its own, and deleting that
    // baseline leaves a complete-looking run nothing can restart from.
    QString reason;
    if (!mlwfWavefunctionsAvailable(dir, &reason)) {
        invalidReason_ = reason;
        status_->setText(QStringLiteral("<b style=\"color:#e06c5a\">%1</b>")
                             .arg(reason.toHtmlEscaped()));
        return;
    }

    // Report what will actually be inherited. These are the numbers that decide
    // the result, and showing them is how a user notices they picked the
    // 4-Wannier run when they meant the 8-Wannier one.
    const QJsonObject meta = readJsonObject(summaryPath);
    const int nwannier = meta.value(QStringLiteral("nwannier")).toInt(
        meta.value(QStringLiteral("centers")).toArray().size());
    const QString projection =
        meta.value(QStringLiteral("projection")).toString(
            QStringLiteral("orbitals"));

    QString text = tr("<b>Ready.</b> %n Wannier function(s), ", nullptr,
                      nwannier)
        + tr("trial projections: <b>%1</b>.").arg(projection);
    if (const QJsonValue spread = meta.value(QStringLiteral("total_spread"));
        spread.isDouble()) {
        text += tr(" Total spread %1 Å².")
                    .arg(spread.toDouble(), 0, 'f', 3);
    }
    status_->setText(text);
    valid_ = true;
}

} // namespace calango::gui
