#include "gui/SqsDialog.hpp"

#include "core/Element.hpp"

#include <QApplication>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QSpinBox>
#include <QVBoxLayout>

#include <set>

namespace calango::gui {

SqsDialog::SqsDialog(std::shared_ptr<const core::Structure> structure,
                     QWidget* parent)
    : QDialog(parent)
    , structure_(std::move(structure))
{
    setWindowTitle(tr("Special Quasirandom Structure (SQS)"));

    auto* layout = new QVBoxLayout(this);
    auto* form = new QFormLayout;
    layout->addLayout(form);

    auto* repeatRow = new QWidget(this);
    auto* repeatLayout = new QHBoxLayout(repeatRow);
    repeatLayout->setContentsMargins(0, 0, 0, 0);
    for (QSpinBox** spin : {&nxSpin_, &nySpin_, &nzSpin_}) {
        *spin = new QSpinBox(repeatRow);
        (*spin)->setRange(1, 20);
        (*spin)->setValue(2);
        repeatLayout->addWidget(*spin);
    }
    form->addRow(tr("Supercell (n₁ × n₂ × n₃):"), repeatRow);

    elementCombo_ = new QComboBox(this);
    std::set<int> present;
    for (const auto& atom : structure_->atoms())
        present.insert(atom.atomicNumber);
    for (const int z : present)
        elementCombo_->addItem(
            QString::fromUtf8(core::Elements::data(z).symbol));
    form->addRow(tr("Replace element:"), elementCombo_);
    elementCombo_->setToolTip(tr("Sites of this element form the alloy "
                                 "sublattice being decorated"));

    compositionEdit_ = new QLineEdit(this);
    compositionEdit_->setPlaceholderText(QStringLiteral("Cu:0.75, Au:0.25"));
    compositionEdit_->setToolTip(
        tr("Target composition of the sublattice as symbol:fraction pairs;\n"
           "fractions are normalized and rounded to whole atoms"));
    form->addRow(tr("Composition:"), compositionEdit_);

    shell1Spin_ = new QDoubleSpinBox(this);
    shell1Spin_->setRange(1.0, 20.0);
    shell1Spin_->setValue(3.2);
    shell1Spin_->setSuffix(QStringLiteral(" Å"));
    form->addRow(tr("First shell cutoff:"), shell1Spin_);

    shell2Spin_ = new QDoubleSpinBox(this);
    shell2Spin_->setRange(0.0, 25.0);
    shell2Spin_->setValue(4.8);
    shell2Spin_->setSuffix(QStringLiteral(" Å"));
    shell2Spin_->setSpecialValueText(tr("off"));
    form->addRow(tr("Second shell cutoff:"), shell2Spin_);

    stepsSpin_ = new QSpinBox(this);
    stepsSpin_->setRange(100, 1000000);
    stepsSpin_->setValue(20000);
    stepsSpin_->setToolTip(
        tr("Monte Carlo swap attempts in the simulated annealing. More steps "
           "explore more decorations; the objective reported after generating "
           "tells you whether it was enough."));
    form->addRow(tr("MC steps:"), stepsSpin_);

    seedSpin_ = new QSpinBox(this);
    seedSpin_->setRange(0, 1000000);
    seedSpin_->setValue(42);
    form->addRow(tr("Random seed:"), seedSpin_);

    statusLabel_ = new QLabel(this);
    statusLabel_->setWordWrap(true);
    layout->addWidget(statusLabel_);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Cancel, this);
    auto* generateButton =
        buttons->addButton(tr("Generate"), QDialogButtonBox::AcceptRole);
    generateButton->setDefault(true);
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    // AcceptRole would close immediately; generate() accepts on success.
    disconnect(buttons, &QDialogButtonBox::accepted, nullptr, nullptr);
    connect(generateButton, &QPushButton::clicked, this, &SqsDialog::generate);
}

void SqsDialog::generate()
{
    core::SqsGenerator::Params params;
    params.nx = nxSpin_->value();
    params.ny = nySpin_->value();
    params.nz = nzSpin_->value();
    params.replaceElement = elementCombo_->currentText().toStdString();
    params.shell1 = shell1Spin_->value();
    params.shell2 = shell2Spin_->value();
    params.steps = stepsSpin_->value();
    params.seed = static_cast<unsigned>(seedSpin_->value());

    // "Cu:0.75, Au:0.25" -> {{"Cu",0.75},{"Au",0.25}}
    const QStringList entries = compositionEdit_->text().split(
        QLatin1Char(','), Qt::SkipEmptyParts);
    for (const QString& entry : entries) {
        const QStringList kv = entry.split(QLatin1Char(':'), Qt::SkipEmptyParts);
        bool ok = false;
        const double fraction = kv.size() == 2 ? kv[1].trimmed().toDouble(&ok) : 0.0;
        const std::string symbol =
            kv.isEmpty() ? std::string() : kv[0].trimmed().toStdString();
        if (!ok || fraction <= 0.0 || core::Elements::atomicNumber(symbol) == 0) {
            QMessageBox::warning(
                this, windowTitle(),
                tr("Could not parse \"%1\" — use symbol:fraction pairs like "
                   "\"Cu:0.75, Au:0.25\".").arg(entry.trimmed()));
            return;
        }
        params.composition.emplace_back(symbol, fraction);
    }
    if (params.composition.size() < 2) {
        QMessageBox::warning(this, windowTitle(),
                             tr("Enter at least two species, e.g. "
                                "\"Cu:0.75, Au:0.25\"."));
        return;
    }

    statusLabel_->setText(tr("Generating…"));
    QApplication::setOverrideCursor(Qt::WaitCursor);
    try {
        result_ = core::SqsGenerator::generate(*structure_, params);
        QApplication::restoreOverrideCursor();
        accept();
    } catch (const std::exception& e) {
        QApplication::restoreOverrideCursor();
        statusLabel_->setText(QString::fromUtf8(e.what()));
    }
}

QString SqsDialog::resultSummary() const
{
    if (!result_)
        return QString();
    const auto& r = *result_;
    // The absolute objective means little on its own — how far it fell from
    // the random starting decoration is what says the annealing worked.
    return tr("SQS: ΔΠ = %1 (from %2) over %3 shell(s), %4 sites, "
              "%5/%6 swaps accepted")
        .arg(r.objective, 0, 'g', 4)
        .arg(r.initialObjective, 0, 'g', 4)
        .arg(r.shells)
        .arg(r.sublatticeSites)
        .arg(r.accepted)
        .arg(r.steps);
}

} // namespace calango::gui
