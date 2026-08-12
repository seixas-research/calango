#include "gui/ClusterExpansionDialog.hpp"

#include "core/Element.hpp"

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QRegularExpression>
#include <QSpinBox>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>
#include <numeric>
#include <set>

namespace calango::gui {

ClusterExpansionDialog::ClusterExpansionDialog(
    std::shared_ptr<const core::Structure> structure, QWidget* parent)
    : QDialog(parent), structure_(std::move(structure))
{
    setWindowTitle(tr("Cluster Expansion — Configuration Generator"));
    resize(480, 560);

    auto* layout = new QVBoxLayout(this);
    auto* intro = new QLabel(
        tr("Generate a symmetry-inequivalent ensemble of alloy configurations "
           "by decorating the active sublattice of a supercell and reducing "
           "each decoration to its cluster-correlation fingerprint (pairs / "
           "triplets / quadruplets within the cutoffs). No ICET dependency."),
        this);
    intro->setWordWrap(true);
    layout->addWidget(intro);

    auto* form = new QFormLayout;
    layout->addLayout(form);

    // Active parent element = the sublattice being substituted. Populate from
    // the species actually present (same idiom as the SQS dialog).
    activeCombo_ = new QComboBox(this);
    std::set<int> present;
    for (const auto& atom : structure_->atoms())
        present.insert(atom.atomicNumber);
    for (int z : present)
        activeCombo_->addItem(
            QStringLiteral("%1 (Z=%2)")
                .arg(QLatin1String(core::Elements::data(z).symbol))
                .arg(z),
            z);
    form->addRow(tr("Active (parent) element:"), activeCombo_);

    speciesEdit_ = new QLineEdit(QStringLiteral("Cu, Au"), this);
    speciesEdit_->setToolTip(
        tr("Two or more element symbols placed on the active sites, "
           "comma-separated (e.g. \"Cu, Au\" or \"Fe, Co, Ni\")."));
    form->addRow(tr("Substitution species:"), speciesEdit_);

    auto* superRow = new QHBoxLayout;
    for (auto*& spin : supercellSpins_) {
        spin = new QSpinBox(this);
        spin->setRange(1, 8);
        spin->setValue(2);
        superRow->addWidget(spin);
    }
    form->addRow(tr("Supercell (nx·ny·nz):"), superRow);

    pairCutoffSpin_ = new QDoubleSpinBox(this);
    pairCutoffSpin_->setRange(0.0, 20.0);
    pairCutoffSpin_->setDecimals(2);
    pairCutoffSpin_->setValue(4.0);
    pairCutoffSpin_->setSuffix(tr(" Å"));
    pairCutoffSpin_->setSpecialValueText(tr("off"));
    form->addRow(tr("Pair cutoff:"), pairCutoffSpin_);

    tripletCutoffSpin_ = new QDoubleSpinBox(this);
    tripletCutoffSpin_->setRange(0.0, 20.0);
    tripletCutoffSpin_->setDecimals(2);
    // Triplets ON by default. A pair-only basis cannot distinguish A3B from
    // AB3 — the model is symmetric under A <-> B at complementary
    // compositions — so an ensemble built without them silently discards the
    // term that chooses the ordered structure. 3.0 A keeps it to the nearest
    // triangles on a typical close-packed lattice.
    tripletCutoffSpin_->setValue(3.0);
    tripletCutoffSpin_->setSuffix(tr(" Å"));
    tripletCutoffSpin_->setSpecialValueText(tr("off"));
    form->addRow(tr("Triplet cutoff:"), tripletCutoffSpin_);

    quadCutoffSpin_ = new QDoubleSpinBox(this);
    quadCutoffSpin_->setRange(0.0, 20.0);
    quadCutoffSpin_->setDecimals(2);
    // Quadruplets stay OFF by default: each orbit adds K^4 histogram columns
    // to the design matrix, and with the usual handful of configurations the
    // fit runs out of samples before it runs out of clusters. Turn them on
    // deliberately, with the configuration count to support them.
    quadCutoffSpin_->setValue(0.0);
    quadCutoffSpin_->setSuffix(tr(" Å"));
    quadCutoffSpin_->setSpecialValueText(tr("off"));
    form->addRow(tr("Quadruplet cutoff:"), quadCutoffSpin_);

    maxConfigsSpin_ = new QSpinBox(this);
    maxConfigsSpin_->setRange(1, 100000);
    maxConfigsSpin_->setValue(200);
    form->addRow(tr("Max configurations:"), maxConfigsSpin_);

    seedSpin_ = new QSpinBox(this);
    seedSpin_->setRange(0, 1000000);
    seedSpin_->setValue(42);
    form->addRow(tr("Random seed (sampling):"), seedSpin_);

    fixedCompCheck_ = new QCheckBox(tr("Fix composition"), this);
    form->addRow(fixedCompCheck_);
    compositionEdit_ = new QLineEdit(QStringLiteral("Cu:0.5, Au:0.5"), this);
    compositionEdit_->setToolTip(
        tr("Target site fractions per species; rounded to whole sites. "
           "Only used when 'Fix composition' is checked."));
    compositionEdit_->setEnabled(false);
    form->addRow(tr("Composition:"), compositionEdit_);
    connect(fixedCompCheck_, &QCheckBox::toggled, compositionEdit_,
            &QWidget::setEnabled);

    statusLabel_ = new QLabel(this);
    statusLabel_->setWordWrap(true);
    layout->addWidget(statusLabel_);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Cancel, this);
    auto* generateButton =
        buttons->addButton(tr("Generate"), QDialogButtonBox::AcceptRole);
    generateButton->setDefault(true);
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    disconnect(buttons, &QDialogButtonBox::accepted, nullptr, nullptr);
    connect(generateButton, &QPushButton::clicked, this,
            &ClusterExpansionDialog::generate);
}

void ClusterExpansionDialog::generate()
{
    core::ClusterExpansionOptions options;
    options.activeZ = activeCombo_->currentData().toInt();

    // Parse substitution species.
    const QStringList speciesTokens = speciesEdit_->text().split(
        QRegularExpression(QStringLiteral("[,\\s]+")), Qt::SkipEmptyParts);
    for (const QString& token : speciesTokens) {
        const int z = core::Elements::atomicNumber(token.toStdString());
        if (z == 0) {
            statusLabel_->setText(tr("Unknown element symbol: %1").arg(token));
            return;
        }
        options.speciesZ.push_back(z);
    }
    if (options.speciesZ.size() < 2) {
        statusLabel_->setText(tr("Enter at least two substitution species."));
        return;
    }

    for (int i = 0; i < 3; ++i)
        options.supercell[i] = supercellSpins_[i]->value();
    options.pairCutoff = pairCutoffSpin_->value();
    options.tripletCutoff = tripletCutoffSpin_->value();
    options.quadCutoff = quadCutoffSpin_->value();
    options.maxConfigs = maxConfigsSpin_->value();
    options.seed = static_cast<unsigned>(seedSpin_->value());

    if (options.pairCutoff <= 0.0 && options.tripletCutoff <= 0.0
        && options.quadCutoff <= 0.0) {
        statusLabel_->setText(
            tr("Enable at least one cluster order (a positive cutoff)."));
        return;
    }

    // Count active sites in the supercell to resolve a fixed composition.
    const int perCell = static_cast<int>(std::count_if(
        structure_->atoms().begin(), structure_->atoms().end(),
        [&](const core::Atom& a) { return a.atomicNumber == options.activeZ; }));
    const int activeSites = perCell * options.supercell[0]
        * options.supercell[1] * options.supercell[2];
    if (activeSites == 0) {
        statusLabel_->setText(tr("The chosen element has no sites in the cell."));
        return;
    }

    if (fixedCompCheck_->isChecked()) {
        options.fixedComposition = true;
        std::vector<double> fractions(options.speciesZ.size(), 0.0);
        const QStringList terms = compositionEdit_->text().split(
            QChar(','), Qt::SkipEmptyParts);
        for (const QString& term : terms) {
            const QStringList kv = term.split(QChar(':'));
            if (kv.size() != 2)
                continue;
            const int z = core::Elements::atomicNumber(kv[0].trimmed().toStdString());
            for (std::size_t s = 0; s < options.speciesZ.size(); ++s)
                if (options.speciesZ[s] == z)
                    fractions[s] = kv[1].trimmed().toDouble();
        }
        // Largest-remainder rounding of fractions to integer site counts.
        options.composition.assign(options.speciesZ.size(), 0);
        double sum = 0.0;
        for (double f : fractions)
            sum += f;
        if (sum <= 0.0) {
            statusLabel_->setText(tr("Provide positive composition fractions."));
            return;
        }
        std::vector<double> exact(options.speciesZ.size());
        int assigned = 0;
        for (std::size_t s = 0; s < fractions.size(); ++s) {
            exact[s] = fractions[s] / sum * activeSites;
            options.composition[s] = static_cast<int>(std::floor(exact[s]));
            assigned += options.composition[s];
        }
        // Distribute the remainder to the largest fractional parts.
        std::vector<std::size_t> order(fractions.size());
        std::iota(order.begin(), order.end(), 0);
        std::sort(order.begin(), order.end(), [&](std::size_t a, std::size_t b) {
            return (exact[a] - std::floor(exact[a]))
                > (exact[b] - std::floor(exact[b]));
        });
        for (int i = 0; assigned < activeSites; ++i, ++assigned)
            ++options.composition[order[static_cast<std::size_t>(i) % order.size()]];
    }

    QApplication::setOverrideCursor(Qt::WaitCursor);
    core::ClusterExpansionResult res =
        core::generateClusterExpansion(*structure_, options);
    QApplication::restoreOverrideCursor();

    if (res.configs.empty()) {
        statusLabel_->setText(res.note.empty()
                                  ? tr("No configurations generated.")
                                  : QString::fromStdString(res.note));
        return;
    }
    result_ = std::move(res);
    accept();
}

} // namespace calango::gui
