#include "gui/DatasetManagerDialog.hpp"

#include "core/DatasetSplit.hpp"
#include "gui/GuiUtils.hpp"

#include <pybind11/embed.h>
#include <pybind11/stl.h>

#include <QApplication>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QSpinBox>
#include <QVBoxLayout>

#include <numeric>
#include <string>
#include <vector>

namespace py = pybind11;

namespace calango::gui {

DatasetManagerDialog::DatasetManagerDialog(QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle(tr("MLIP Dataset Manager"));
    resize(640, 560);

    auto* layout = new QVBoxLayout(this);

    // --- Sources -----------------------------------------------------------
    auto* sourceGroup = new QGroupBox(tr("Source trajectories / structures"), this);
    auto* sourceLayout = new QVBoxLayout(sourceGroup);
    fileList_ = new QListWidget(sourceGroup);
    sourceLayout->addWidget(fileList_, 1);
    auto* sourceButtons = new QHBoxLayout;
    auto* addButton = new QPushButton(tr("Add Files…"), sourceGroup);
    auto* clearButton = new QPushButton(tr("Clear"), sourceGroup);
    sourceButtons->addWidget(addButton);
    sourceButtons->addWidget(clearButton);
    sourceButtons->addStretch(1);
    sourceLayout->addLayout(sourceButtons);
    summaryLabel_ = new QLabel(tr("No frames loaded."), sourceGroup);
    summaryLabel_->setWordWrap(true);
    sourceLayout->addWidget(summaryLabel_);
    layout->addWidget(sourceGroup, 1);
    connect(addButton, &QPushButton::clicked,
            this, &DatasetManagerDialog::addFiles);
    connect(clearButton, &QPushButton::clicked,
            this, &DatasetManagerDialog::clearFiles);

    // --- Split -------------------------------------------------------------
    auto* splitGroup = new QGroupBox(tr("Train / validation / test split"), this);
    auto* splitForm = new QFormLayout(splitGroup);
    trainSpin_ = new QSpinBox(splitGroup);
    trainSpin_->setRange(1, 100);
    trainSpin_->setValue(80);
    trainSpin_->setSuffix(QStringLiteral(" %"));
    splitForm->addRow(tr("Training:"), trainSpin_);
    validationSpin_ = new QSpinBox(splitGroup);
    validationSpin_->setRange(0, 99);
    validationSpin_->setValue(10);
    validationSpin_->setSuffix(QStringLiteral(" %"));
    splitForm->addRow(tr("Validation:"), validationSpin_);
    testLabel_ = new QLabel(splitGroup);
    splitForm->addRow(tr("Test:"), testLabel_);
    seedSpin_ = new QSpinBox(splitGroup);
    seedSpin_->setRange(0, 1000000);
    seedSpin_->setValue(42);
    splitForm->addRow(tr("Random seed:"), seedSpin_);
    layout->addWidget(splitGroup);

    const auto syncTestLabel = [this] {
        if (trainSpin_->value() + validationSpin_->value() > 100)
            validationSpin_->setValue(100 - trainSpin_->value());
        testLabel_->setText(QStringLiteral("%1 %").arg(
            100 - trainSpin_->value() - validationSpin_->value()));
    };
    connect(trainSpin_, &QSpinBox::valueChanged, this, syncTestLabel);
    connect(validationSpin_, &QSpinBox::valueChanged, this, syncTestLabel);
    syncTestLabel();

    // --- Query by Committee ------------------------------------------------
    auto* qbcGroup = new QGroupBox(tr("Query-by-Committee ensembles"), this);
    auto* qbcForm = new QFormLayout(qbcGroup);
    committeeSpin_ = new QSpinBox(qbcGroup);
    committeeSpin_->setRange(1, 64);
    committeeSpin_->setValue(1);
    committeeSpin_->setToolTip(tr("1 exports a single dataset; N > 1 adds "
                                  "committee_01…N subdirectories with "
                                  "distinct training sets"));
    qbcForm->addRow(tr("Committee members:"), committeeSpin_);
    committeeModeCombo_ = new QComboBox(qbcGroup);
    committeeModeCombo_->addItems(
        {tr("Independent splits (seed + k)"),
         tr("Bootstrap resampling of the training set")});
    qbcForm->addRow(tr("Diversity:"), committeeModeCombo_);
    layout->addWidget(qbcGroup);

    // --- Export ------------------------------------------------------------
    auto* exportRow = new QHBoxLayout;
    formatCombo_ = new QComboBox(this);
    formatCombo_->addItems({tr("Extended XYZ (MACE-ready)"),
                            tr("ASE database (.db)")});
    exportButton_ = new QPushButton(tr("Export…"), this);
    exportRow->addWidget(new QLabel(tr("Format:"), this));
    exportRow->addWidget(formatCombo_, 1);
    exportRow->addWidget(exportButton_);
    layout->addLayout(exportRow);
    connect(exportButton_, &QPushButton::clicked,
            this, &DatasetManagerDialog::exportDatasets);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);
}

int DatasetManagerDialog::totalFrames() const
{
    return std::accumulate(frameCounts_.begin(), frameCounts_.end(), 0);
}

void DatasetManagerDialog::addFiles()
{
    // The shared application-wide filter list, so Extended XYZ is the
    // pre-selected default here as everywhere else, with the one format only a
    // dataset takes appended: an ASE .db is a whole collection of structures
    // rather than a single geometry, so it belongs to this dialog alone.
    const QStringList paths = QFileDialog::getOpenFileNames(
        this, tr("Add Structures / Trajectories"), QString(),
        structureOpenFilters() + tr(";;ASE database (*.db)"));
    if (paths.isEmpty())
        return;

    QApplication::setOverrideCursor(Qt::WaitCursor);
    for (const QString& path : paths) {
        try {
            py::dict scope;
            scope["path"] = path.toStdString();
            py::exec(R"PY(
import ase.io
frames = ase.io.read(path, index=":")
if not isinstance(frames, list):
    frames = [frames]
count = len(frames)
formulas = sorted({a.get_chemical_formula() for a in frames})
)PY",
                     scope, scope);
            const int count = scope["count"].cast<int>();
            files_ << path;
            frameCounts_.push_back(count);
            const auto formulas = scope["formulas"].cast<std::vector<std::string>>();
            QStringList display;
            for (const auto& f : formulas)
                display << QString::fromStdString(f);
            fileList_->addItem(QStringLiteral("%1 — %2 frame(s), %3")
                                   .arg(QFileInfo(path).fileName())
                                   .arg(count)
                                   .arg(display.join(QStringLiteral(", "))));
        } catch (const py::error_already_set& e) {
            QApplication::restoreOverrideCursor();
            QMessageBox::warning(this, windowTitle(),
                                 tr("Could not read %1:\n%2")
                                     .arg(path, QString::fromUtf8(e.what())));
            QApplication::setOverrideCursor(Qt::WaitCursor);
        }
    }
    QApplication::restoreOverrideCursor();
    refreshSummary();
}

void DatasetManagerDialog::clearFiles()
{
    files_.clear();
    frameCounts_.clear();
    fileList_->clear();
    refreshSummary();
}

void DatasetManagerDialog::refreshSummary()
{
    summaryLabel_->setText(
        tr("%n frame(s) from %1 file(s) loaded.", nullptr, totalFrames())
            .arg(files_.size()));
}

void DatasetManagerDialog::exportDatasets()
{
    const int count = totalFrames();
    if (count < 3) {
        QMessageBox::information(this, windowTitle(),
                                 tr("Load at least 3 frames first."));
        return;
    }
    const QString directory = QFileDialog::getExistingDirectory(
        this, tr("Export Dataset Into Directory"));
    if (directory.isEmpty())
        return;

    const double trainFrac = trainSpin_->value() / 100.0;
    const double valFrac = validationSpin_->value() / 100.0;
    const auto seed = static_cast<unsigned>(seedSpin_->value());
    const int committees = committeeSpin_->value();
    const bool bootstrap = committeeModeCombo_->currentIndex() == 1;

    // Master split (fixed test set) + per-committee training sets.
    const auto master = core::DatasetSplit::make(count, trainFrac, valFrac, seed);
    // name -> frame indices, exported as separate files/db tables.
    std::vector<std::pair<std::string, std::vector<int>>> subsets{
        {"train", master.train},
        {"valid", master.validation},
        {"test", master.test},
    };
    for (int k = 1; k < committees + 1 && committees > 1; ++k) {
        const std::string prefix =
            "committee_" + std::string(k < 10 ? "0" : "") + std::to_string(k);
        if (bootstrap) {
            subsets.emplace_back(prefix + "/train",
                                 core::bootstrapSample(master.train, seed + k));
            subsets.emplace_back(prefix + "/valid", master.validation);
        } else {
            // Independent split of the non-test pool with seed + k.
            std::vector<int> pool = master.train;
            pool.insert(pool.end(), master.validation.begin(),
                        master.validation.end());
            const double poolTrain = pool.empty()
                ? 0.0
                : static_cast<double>(master.train.size()) / pool.size();
            const auto sub = core::DatasetSplit::make(
                static_cast<int>(pool.size()), poolTrain, 1.0 - poolTrain,
                seed + k);
            std::vector<int> train, valid;
            for (const int i : sub.train)
                train.push_back(pool[static_cast<std::size_t>(i)]);
            for (const int i : sub.validation)
                valid.push_back(pool[static_cast<std::size_t>(i)]);
            subsets.emplace_back(prefix + "/train", std::move(train));
            subsets.emplace_back(prefix + "/valid", std::move(valid));
        }
    }

    QApplication::setOverrideCursor(Qt::WaitCursor);
    try {
        py::dict scope;
        std::vector<std::string> paths;
        for (const QString& file : files_)
            paths.push_back(file.toStdString());
        scope["paths"] = paths;
        scope["outdir"] = directory.toStdString();
        scope["as_db"] = formatCombo_->currentIndex() == 1;
        py::dict subsetDict;
        for (const auto& [name, indices] : subsets)
            subsetDict[py::str(name)] = indices;
        scope["subsets"] = subsetDict;
        py::exec(R"PY(
import os
import ase.io

frames = []
for p in paths:
    part = ase.io.read(p, index=":")
    frames.extend(part if isinstance(part, list) else [part])

written = []
for name, indices in subsets.items():
    if not indices:
        continue
    selection = [frames[i] for i in indices]
    if as_db:
        import ase.db
        target = os.path.join(outdir, name.replace("/", "_") + ".db")
        if os.path.exists(target):
            os.remove(target)
        with ase.db.connect(target) as db:
            for atoms in selection:
                db.write(atoms)
    else:
        target = os.path.join(outdir, name + ".extxyz")
        os.makedirs(os.path.dirname(target), exist_ok=True)
        ase.io.write(target, selection, format="extxyz")
    written.append(f"{name} ({len(selection)})")
)PY",
                 scope, scope);
        QApplication::restoreOverrideCursor();
        const auto written = scope["written"].cast<std::vector<std::string>>();
        QStringList report;
        for (const auto& w : written)
            report << QString::fromStdString(w);
        QMessageBox::information(
            this, windowTitle(),
            tr("Exported to %1:\n%2").arg(directory,
                                          report.join(QStringLiteral("\n"))));
    } catch (const py::error_already_set& e) {
        QApplication::restoreOverrideCursor();
        QMessageBox::warning(this, windowTitle(),
                             tr("Export failed:\n%1")
                                 .arg(QString::fromUtf8(e.what())));
    }
}

} // namespace calango::gui
