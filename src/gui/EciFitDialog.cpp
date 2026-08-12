#include "gui/EciFitDialog.hpp"

#include "gui/GuiUtils.hpp"

#include <QComboBox>
#include <QFile>
#include <QFileDialog>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QTableWidget>
#include <QVBoxLayout>

#include <cmath>

namespace calango::gui {

EciFitDialog::EciFitDialog(QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle(tr("Effective Cluster Interactions"));
    auto* outer = new QVBoxLayout(this);

    auto* intro = new QLabel(
        tr("Fit Effective Cluster Interactions (ECI) to the energies of a "
           "completed Cluster Expansion run.\n\n"
           "The run's cluster_expansion.json supplies both halves: each "
           "configuration's cluster correlations are the design matrix, and "
           "its energy per atom is what they are regressed against."),
        this);
    intro->setWordWrap(true);
    outer->addWidget(intro);

    auto* sourceRow = new QHBoxLayout();
    sourceLabel_ = new QLabel(tr("No run loaded."), this);
    sourceLabel_->setWordWrap(true);
    sourceRow->addWidget(sourceLabel_, 1);
    auto* browseButton = new QPushButton(tr("Open Run…"), this);
    connect(browseButton, &QPushButton::clicked, this, &EciFitDialog::browse);
    sourceRow->addWidget(browseButton);
    outer->addLayout(sourceRow);

    auto* options = new QGroupBox(tr("Fit"), this);
    auto* form = new QFormLayout(options);
    methodCombo_ = new QComboBox(options);
    methodCombo_->addItem(tr("LASSO (selects clusters)"),
                          static_cast<int>(core::EciMethod::Lasso));
    methodCombo_->addItem(tr("Ridge (keeps all clusters)"),
                          static_cast<int>(core::EciMethod::Ridge));
    methodCombo_->addItem(tr("ARD / Bayesian"),
                          static_cast<int>(core::EciMethod::Ard));
    methodCombo_->setToolTip(
        tr("LASSO is the usual choice for a cluster expansion: it drives "
           "unimportant orbits to exactly zero, and choosing WHICH clusters "
           "to keep is the central difficulty of the method.\n\n"
           "Ridge keeps every orbit and only shrinks it, so it cannot tell "
           "you that a cluster does not matter."));
    form->addRow(tr("Method:"), methodCombo_);

    foldsSpin_ = new QSpinBox(options);
    foldsSpin_->setRange(0, 100);
    foldsSpin_->setValue(0);
    foldsSpin_->setSpecialValueText(tr("leave-one-out"));
    foldsSpin_->setToolTip(
        tr("Cross-validation folds. Leave-one-out is the cluster-expansion "
           "convention and is what \"the CV score\" means in the CE "
           "literature."));
    form->addRow(tr("CV folds:"), foldsSpin_);
    outer->addWidget(options);

    fitButton_ = new QPushButton(tr("Fit ECIs"), this);
    fitButton_->setEnabled(false);
    connect(fitButton_, &QPushButton::clicked, this, &EciFitDialog::fit);
    outer->addWidget(fitButton_);

    diagnosticLabel_ = new QLabel(this);
    diagnosticLabel_->setWordWrap(true);
    diagnosticLabel_->setTextFormat(Qt::RichText);
    outer->addWidget(diagnosticLabel_);

    table_ = new QTableWidget(0, 5, this);
    table_->setHorizontalHeaderLabels({tr("Cluster"), tr("Order"),
                                       tr("r (Å)"), tr("ECI (eV)"),
                                       tr("m·ECI (eV)")});
    table_->horizontalHeader()->setStretchLastSection(true);
    table_->verticalHeader()->setVisible(false);
    table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    // A dead key on an item view recurses through type-to-edit until the
    // stack dies; enforced by the dialog-construction test.
    disableTypeToEdit(table_);
    outer->addWidget(table_, 1);

    report_ = new QPlainTextEdit(this);
    report_->setReadOnly(true);
    report_->setMaximumHeight(140);
    outer->addWidget(report_);

    auto* buttons = new QHBoxLayout();
    cvmButton_ = new QPushButton(tr("Send to CVM…"), this);
    cvmButton_->setEnabled(false);
    cvmButton_->setToolTip(
        tr("Open the CVM / Alloy Thermodynamics module with the fitted "
           "nearest-neighbour pair ECI already applied, so the configurational "
           "entropy is computed from THIS fit rather than from a number typed "
           "in by hand."));
    connect(cvmButton_, &QPushButton::clicked, this, &EciFitDialog::sendToCvm);
    buttons->addWidget(cvmButton_);
    buttons->addStretch(1);
    auto* close = new QPushButton(tr("Close"), this);
    connect(close, &QPushButton::clicked, this, &QDialog::accept);
    buttons->addWidget(close);
    outer->addLayout(buttons);

    resize(720, 620);
}

void EciFitDialog::browse()
{
    const QString directory = QFileDialog::getExistingDirectory(
        this, tr("Open a Cluster Expansion run"));
    if (directory.isEmpty())
        return;
    QString error;
    if (!loadDirectory(directory, &error)) {
        QMessageBox::warning(this, tr("Effective Cluster Interactions"), error);
        return;
    }
    fit();
}

bool EciFitDialog::loadDirectory(const QString& directory, QString* error)
{
    correlations_.clear();
    energies_.clear();
    columns_.clear();
    result_ = core::EciFitResult{};
    fitButton_->setEnabled(false);
    cvmButton_->setEnabled(false);

    const QString path = directory + QStringLiteral("/cluster_expansion.json");
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        if (error)
            *error = tr("No cluster_expansion.json in %1.").arg(directory);
        return false;
    }
    const auto document = QJsonDocument::fromJson(file.readAll());
    const QJsonObject root = document.object();
    const QJsonArray configs = root.value(QStringLiteral("configurations"))
                                   .toArray();
    if (configs.isEmpty()) {
        if (error)
            *error = tr("%1 lists no configurations.").arg(path);
        return false;
    }

    for (const QJsonValue& value : configs) {
        const QJsonObject record = value.toObject();
        const QJsonArray row =
            record.value(QStringLiteral("correlation")).toArray();
        const QJsonValue energy =
            record.value(QStringLiteral("energy_per_atom"));
        if (row.isEmpty() || energy.isNull() || !energy.isDouble())
            continue; // a failed configuration, or one with no matrix row
        std::vector<double> correlation;
        correlation.reserve(row.size());
        for (const QJsonValue& entry : row)
            correlation.push_back(entry.toDouble());
        const double e = energy.toDouble();
        if (!std::isfinite(e))
            continue;
        correlations_.push_back(std::move(correlation));
        energies_.push_back(e);
    }

    if (correlations_.empty()) {
        // The specific, actionable refusal. An older run has energies but no
        // correlations, and fitting against anything else would produce ECIs
        // that reproduce the training energies and mean nothing.
        if (error)
            *error = tr(
                "%1 carries energies but no cluster correlations, so there is "
                "no design matrix to regress against.\n\nThis run predates "
                "correlation output. Rebuild the ensemble with the Cluster "
                "Expansion builder and re-run it; the new "
                "cluster_expansion.json will carry a \"correlation\" row per "
                "configuration.").arg(path);
        return false;
    }

    const QJsonArray labels = root.value(QStringLiteral("orbit_labels"))
                                  .toArray();
    for (int i = 0; i < labels.size(); ++i) {
        core::EciColumn column;
        column.label = labels.at(i).toString().toStdString();
        // Order and radius are parsed out of the label the builder wrote;
        // they are presentation only, so a label this does not recognise
        // costs a blank cell rather than a wrong fit.
        const QString text = labels.at(i).toString();
        if (text.startsWith(QStringLiteral("pair")))
            column.order = 2;
        else if (text.startsWith(QStringLiteral("triplet")))
            column.order = 3;
        else if (text.startsWith(QStringLiteral("quad")))
            column.order = 4;
        const auto parts = text.split(QRegularExpression("[^0-9.]+"),
                                      Qt::SkipEmptyParts);
        if (!parts.isEmpty())
            column.radius = parts.first().toDouble();
        columns_.push_back(column);
    }

    directory_ = directory;
    sourceLabel_->setText(tr("%1 — %2 configurations x %3 clusters")
                              .arg(directory)
                              .arg(correlations_.size())
                              .arg(correlations_.front().size()));
    fitButton_->setEnabled(true);
    return true;
}

void EciFitDialog::fit()
{
    if (correlations_.empty())
        return;
    core::EciFitOptions options;
    options.method =
        static_cast<core::EciMethod>(methodCombo_->currentData().toInt());
    options.cvFolds = foldsSpin_->value();
    result_ = core::fitEffectiveClusterInteractions(correlations_, energies_,
                                                    options, columns_);
    if (!result_.ok) {
        diagnosticLabel_->setText(
            tr("<b>The fit failed.</b><br>%1")
                .arg(QString::fromStdString(result_.note)));
        cvmButton_->setEnabled(false);
        return;
    }

    // The CV score first and the training error beside it. A cluster
    // expansion that reproduces its training set and predicts nothing is the
    // classic failure, and showing only one number hides it.
    const bool overfit = result_.cvScore > 3.0 * result_.rmse;
    diagnosticLabel_->setText(
        tr("<b>CV score %1 eV/atom</b> — the diagnostic. "
           "Training RMSE %2 eV/atom, %3 active clusters, λ = %4.%5")
            .arg(result_.cvScore, 0, 'g', 4)
            .arg(result_.rmse, 0, 'g', 4)
            .arg(result_.activeTerms)
            .arg(result_.lambda, 0, 'g', 3)
            .arg(overfit ? tr("<br><span style='color:#c0392b'>The CV score "
                              "is more than three times the training error: "
                              "this expansion fits its own data and does not "
                              "predict. Add configurations or reduce the "
                              "cluster cutoffs.</span>")
                         : QString()));
    refreshTable();
    report_->setPlainText(
        QString::fromStdString(core::formatEciReport(result_)));
    cvmButton_->setEnabled(std::abs(nearestNeighbourPairEci()) > 0.0);
}

void EciFitDialog::refreshTable()
{
    table_->setRowCount(0);
    for (const core::EciTerm& term : result_.terms) {
        if (term.eci == 0.0)
            continue; // LASSO dropped it; showing zeros buries the signal
        const int row = table_->rowCount();
        table_->insertRow(row);
        table_->setItem(row, 0,
                        new QTableWidgetItem(
                            QString::fromStdString(term.label)));
        table_->setItem(row, 1,
                        new QTableWidgetItem(QString::number(term.order)));
        table_->setItem(row, 2,
                        new QTableWidgetItem(
                            QString::number(term.radius, 'f', 3)));
        table_->setItem(row, 3,
                        new QTableWidgetItem(
                            QString::number(term.eci, 'g', 5)));
        table_->setItem(row, 4,
                        new QTableWidgetItem(
                            QString::number(term.weightedEci, 'g', 5)));
    }
}

double EciFitDialog::nearestNeighbourPairEci() const
{
    // The smallest-radius pair orbit with a non-zero ECI. The CVM solver in
    // this program is a NEAREST-NEIGHBOUR model, so that is the only term it
    // can consume; longer-range pairs and multi-body terms are shown in the
    // table but do not travel, and the CVM window says so.
    const core::EciTerm* best = nullptr;
    for (const core::EciTerm& term : result_.terms) {
        if (term.order != 2 || term.eci == 0.0)
            continue;
        if (!best || term.radius < best->radius)
            best = &term;
    }
    return best ? best->eci : 0.0;
}

void EciFitDialog::sendToCvm()
{
    const double eci = nearestNeighbourPairEci();
    if (eci == 0.0) {
        QMessageBox::information(
            this, tr("Send to CVM"),
            tr("This fit kept no nearest-neighbour pair interaction, so there "
               "is nothing for the nearest-neighbour CVM solver to use.\n\n"
               "That is a result about the alloy, not an error: its ordering "
               "is carried by longer-range or multi-body clusters, which this "
               "CVM does not model."));
        return;
    }
    Q_EMIT sendToCvmRequested(eci);
    accept();
}

} // namespace calango::gui
