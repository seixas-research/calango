#include "gui/HubbardUWizard.hpp"

#include "core/Element.hpp"

#include <QCheckBox>
#include <QComboBox>
#include <QGroupBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QRegularExpression>
#include <QSpinBox>
#include <QTableWidget>
#include <QVBoxLayout>

#include <algorithm>
#include <set>

namespace calango::core {
std::string generateHubbardScript(const HubbardRunConfig&, const std::string&);
}

namespace calango::gui {

namespace {

/// Which shell a U is conventionally put on for a given element.
///
/// A default rather than a rule: the transition metals get d, the lanthanides
/// and actinides f, and everything else p — which is right often enough to be
/// worth pre-selecting and wrong seldom enough that the combo stays editable.
core::HubbardShell defaultShellFor(int z)
{
    const bool transitionMetal = (z >= 21 && z <= 30) || (z >= 39 && z <= 48)
        || (z >= 72 && z <= 80) || (z >= 104 && z <= 112);
    const bool fBlock = (z >= 57 && z <= 71) || (z >= 89 && z <= 103);
    if (fBlock)
        return core::HubbardShell::F;
    if (transitionMetal)
        return core::HubbardShell::D;
    return core::HubbardShell::P;
}

/// Whether an element is one a Hubbard U is normally wanted for — an open
/// d or f shell. Used only to decide which rows start ticked; every atom of
/// the structure is listed either way, because "normally" is not "always"
/// (a U on the O-2p of a transition-metal oxide is a real calculation).
bool likelyNeedsU(int z)
{
    return defaultShellFor(z) != core::HubbardShell::P;
}

} // namespace

HubbardUWizard::HubbardUWizard(std::shared_ptr<const core::Structure> structure,
                               QWidget* parent)
    : GpawElectronicWizard(parent), structure_(std::move(structure))
{
    buildUi();
    // VASP leads: the linear-response recipe this module automates is the one
    // written up on the VASP wiki, and LDAUTYPE = 3 is the most direct
    // expression of the method's α anywhere in either engine.
    selectCalculator(core::CalculatorKind::Vasp);
    populateSites();
    electronic_.updateEnabled();
    updateSummary();
}

QString HubbardUWizard::wizardTitle() const
{
    return tr("Hubbard Parameter Calculation");
}

QStringList HubbardUWizard::calculatorElements() const
{
    QStringList symbols;
    if (!structure_)
        return symbols;
    for (const core::Atom& atom : structure_->atoms()) {
        const QString symbol = QString::fromLatin1(atom.symbol());
        if (!symbols.contains(symbol))
            symbols.append(symbol);
    }
    return symbols;
}

QWidget* HubbardUWizard::buildSettingsPage()
{
    auto* page = new QWidget(this);
    auto* layout = new QVBoxLayout(page);

    auto* intro = new QLabel(
        tr("<b>Linear response</b> — Cococcioni and de Gironcoli, "
           "<i>Phys. Rev. B</i> <b>71</b>, 035105 (2005)."),
        page);
    intro->setWordWrap(true);
    intro->setToolTip(
        tr("A localized potential α is added to the Hubbard manifold of one "
           "atom and the occupation of that manifold is measured twice: after "
           "a single diagonalization at the unperturbed potential (the "
           "non-interacting response χ₀) and after full self-consistency (the "
           "screened response χ). The effective interaction is the difference "
           "of the inverse responses, U_eff = χ₀⁻¹ − χ⁻¹.\n\n"
           "The result is a first-principles U for THIS site in THIS "
           "structure, not a literature value transplanted from another "
           "compound."));
    intro->setTextFormat(Qt::RichText);
    layout->addWidget(intro);

    // -- Sites --------------------------------------------------------------
    auto* siteGroup = new QGroupBox(tr("Perturbed sites"), page);
    auto* siteLayout = new QVBoxLayout(siteGroup);
    auto* siteNote = new QLabel(
        tr("U belongs to a site, not an element: inequivalent atoms of the "
           "same species have different ones. Tick each site you want a U "
           "for."),
        siteGroup);
    siteNote->setWordWrap(true);
    siteNote->setToolTip(
        tr("Every ticked site is perturbed in turn and measured in all of the "
           "runs, which is what makes the response a matrix rather than a set "
           "of independent numbers."));
    siteLayout->addWidget(siteNote);

    siteTable_ = new QTableWidget(0, 3, siteGroup);
    siteTable_->setHorizontalHeaderLabels(
        {tr("Site"), tr("Element"), tr("Position (Å)")});
    siteTable_->verticalHeader()->setVisible(false);
    siteTable_->setSelectionMode(QAbstractItemView::NoSelection);
    siteTable_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    siteTable_->horizontalHeader()->setSectionResizeMode(
        2, QHeaderView::Stretch);
    siteTable_->setMaximumHeight(190);
    siteLayout->addWidget(siteTable_);

    shellCombo_ = new QComboBox(siteGroup);
    shellCombo_->addItem(tr("d — transition metals"),
                         static_cast<int>(core::HubbardShell::D));
    shellCombo_->addItem(tr("f — lanthanides and actinides"),
                         static_cast<int>(core::HubbardShell::F));
    shellCombo_->addItem(tr("p — main-group anions"),
                         static_cast<int>(core::HubbardShell::P));
    shellCombo_->setToolTip(
        tr("The manifold the projectors span, and the shell the α is applied "
           "to.\n\n"
           "One shell for the whole run: a single U matrix mixes the sites it "
           "spans, and both engines take one manifold per species anyway. "
           "Sites needing different shells are separate calculations."));
    auto* shellRow = new QFormLayout;
    shellRow->addRow(tr("Hubbard manifold:"), shellCombo_);
    siteLayout->addLayout(shellRow);
    layout->addWidget(siteGroup);

    // -- Supercell and α ----------------------------------------------------
    auto* responseGroup = new QGroupBox(tr("Perturbation"), page);
    auto* responseForm = new QFormLayout(responseGroup);

    auto* cellRow = new QWidget(responseGroup);
    auto* cellLayout = new QHBoxLayout(cellRow);
    cellLayout->setContentsMargins(0, 0, 0, 0);
    for (int i = 0; i < 3; ++i) {
        supercellSpins_[i] = new QSpinBox(cellRow);
        supercellSpins_[i]->setRange(1, 8);
        supercellSpins_[i]->setValue(2);
        cellLayout->addWidget(supercellSpins_[i]);
        if (i < 2)
            cellLayout->addWidget(new QLabel(QStringLiteral("×"), cellRow));
        connect(supercellSpins_[i], &QSpinBox::valueChanged, this, [this] {
            updateSummary();
            refreshPreview();
        });
    }
    cellLayout->addStretch(1);
    responseForm->addRow(tr("Supercell:"), cellRow);
    cellRow->setToolTip(
        tr("The convergence parameter of the whole method, not a performance "
           "knob.\n\n"
           "In a periodic cell the perturbation is applied to every image of "
           "the atom at once, so what is measured is the response to a "
           "LATTICE of perturbations rather than to a single one — which makes "
           "U come out too small. Repeat on a larger supercell; U is converged "
           "when it stops moving. 1×1×1 measures the fully interacting lattice "
           "and is almost never the answer."));

    alphaEdit_ = new QLineEdit(
        QStringLiteral("-0.15, -0.10, -0.05, 0.05, 0.10, 0.15"), responseGroup);
    alphaEdit_->setToolTip(
        tr("Perturbation strengths in eV, separated by commas or spaces.\n\n"
           "Two requirements pull in opposite directions: large enough that "
           "the occupation moves out of the numerical noise, small enough that "
           "the response is still linear. Symmetric about zero so the fit is "
           "not biased by the curvature that remains, and α = 0 is not listed "
           "— the unperturbed run supplies that point.\n\n"
           "The script reports the residual of each straight-line fit; a "
           "residual that is not small means these are too large."));
    connect(alphaEdit_, &QLineEdit::textChanged, this, [this] {
        updateSummary();
        refreshPreview();
    });
    responseForm->addRow(tr("Perturbations α:"), alphaEdit_);
    layout->addWidget(responseGroup);

    summaryLabel_ = new QLabel(page);
    summaryLabel_->setWordWrap(true);
    summaryLabel_->setTextFormat(Qt::RichText);
    layout->addWidget(summaryLabel_);

    connect(shellCombo_, &QComboBox::currentIndexChanged, this,
            [this] { refreshPreview(); });

    layout->addStretch(1);
    return page;
}

void HubbardUWizard::populateSites()
{
    if (!siteTable_ || !structure_)
        return;
    const auto& atoms = structure_->atoms();
    siteTable_->setRowCount(static_cast<int>(atoms.size()));

    // Pre-tick the FIRST atom of each open-d/f element rather than all of
    // them. The symmetry-equivalent copies of a site have the same U by
    // construction, so perturbing every one of them multiplies the cost
    // without adding an independent number — and the user who genuinely has
    // two inequivalent sites of one element can tick the second.
    std::set<int> seeded;
    for (int row = 0; row < static_cast<int>(atoms.size()); ++row) {
        const core::Atom& atom = atoms[static_cast<std::size_t>(row)];

        auto* check = new QTableWidgetItem(QString::number(row));
        check->setFlags(Qt::ItemIsEnabled | Qt::ItemIsUserCheckable);
        const bool seed = likelyNeedsU(atom.atomicNumber)
            && seeded.insert(atom.atomicNumber).second;
        check->setCheckState(seed ? Qt::Checked : Qt::Unchecked);
        siteTable_->setItem(row, 0, check);

        auto* element = new QTableWidgetItem(QString::fromLatin1(atom.symbol()));
        element->setFlags(Qt::ItemIsEnabled);
        siteTable_->setItem(row, 1, element);

        auto* position = new QTableWidgetItem(
            QStringLiteral("%1, %2, %3")
                .arg(atom.position.x, 0, 'f', 3)
                .arg(atom.position.y, 0, 'f', 3)
                .arg(atom.position.z, 0, 'f', 3));
        position->setFlags(Qt::ItemIsEnabled);
        siteTable_->setItem(row, 2, position);
    }
    siteTable_->resizeColumnsToContents();
    siteTable_->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);

    // Default the manifold to what the seeded sites actually have.
    if (shellCombo_ && !seeded.empty()) {
        const int index = shellCombo_->findData(
            static_cast<int>(defaultShellFor(*seeded.begin())));
        if (index >= 0)
            shellCombo_->setCurrentIndex(index);
    }

    connect(siteTable_, &QTableWidget::itemChanged, this, [this] {
        updateSummary();
        refreshPreview();
    });
}

std::vector<double> HubbardUWizard::alphas() const
{
    std::vector<double> values;
    if (!alphaEdit_)
        return values;
    const QStringList tokens = alphaEdit_->text().split(
        QRegularExpression(QStringLiteral("[\\s,;]+")), Qt::SkipEmptyParts);
    for (const QString& token : tokens) {
        bool ok = false;
        const double value = token.toDouble(&ok);
        // Zero is dropped rather than rejected: it is a legitimate thing to
        // type, it is already covered by the unperturbed run, and running it
        // again would add a duplicate point to the fit at no information gain.
        if (ok && std::abs(value) > 1e-9)
            values.push_back(value);
    }
    std::sort(values.begin(), values.end());
    values.erase(std::unique(values.begin(), values.end()), values.end());
    return values;
}

core::HubbardRunConfig HubbardUWizard::config() const
{
    core::HubbardRunConfig c;
    c.calculator = baseCalculatorConfig();
    c.calculator.task = core::TaskKind::SinglePoint;
    electronic_.applyTo(c.calculator);

    const auto shell = shellCombo_
        ? static_cast<core::HubbardShell>(shellCombo_->currentData().toInt())
        : core::HubbardShell::D;
    if (siteTable_ && structure_) {
        for (int row = 0; row < siteTable_->rowCount(); ++row) {
            const QTableWidgetItem* item = siteTable_->item(row, 0);
            if (!item || item->checkState() != Qt::Checked)
                continue;
            core::HubbardSite site;
            site.atomIndex = row;
            site.element = structure_->atoms()[static_cast<std::size_t>(row)]
                               .symbol();
            site.shell = shell;
            c.sites.push_back(site);
        }
    }
    for (int i = 0; i < 3; ++i)
        c.supercell[i] = supercellSpins_[i] ? supercellSpins_[i]->value() : 2;
    c.alphas = alphas();
    return c;
}

void HubbardUWizard::updateSummary()
{
    if (!summaryLabel_)
        return;
    const core::HubbardRunConfig c = config();
    const int sites = static_cast<int>(c.sites.size());
    const int steps = static_cast<int>(c.alphas.size());
    const int cells = c.supercell[0] * c.supercell[1] * c.supercell[2];
    const int atoms = structure_
        ? static_cast<int>(structure_->atoms().size()) * cells
        : 0;
    // 1 unperturbed + 2 per (site, α): the self-consistent χ run and the
    // single-diagonalization χ₀ run.
    const int runs = sites > 0 && steps > 0 ? 1 + 2 * sites * steps : 0;

    QStringList problems;
    if (sites == 0)
        problems << tr("No site is ticked — there is nothing to perturb.");
    if (steps == 0)
        problems << tr("No perturbation strengths: α needs at least two "
                       "non-zero values for a slope to be defined.");
    else if (steps == 1)
        problems << tr("One α gives a slope through two points with no way to "
                       "tell whether the response is linear. Use at least "
                       "four, symmetric about zero.");
    if (cells == 1)
        problems << tr("A 1×1×1 cell perturbs every periodic image at once, "
                       "so the measured response is that of the whole lattice "
                       "and U comes out too small.");

    QString text = tr("<b>%1</b> SCF runs on a <b>%2-atom</b> supercell: one "
                      "unperturbed reference, then a χ and a χ₀ run for each "
                      "of %3 site(s) × %4 perturbation(s).")
                       .arg(runs)
                       .arg(atoms)
                       .arg(sites)
                       .arg(steps);
    if (!problems.isEmpty()) {
        text += QStringLiteral("<br/><br/><span style='color:#d9534f;'>%1</span>")
                    .arg(problems.join(QStringLiteral("<br/>")));
    }
    summaryLabel_->setText(text);
}

void HubbardUWizard::goNext()
{
    // Refuse to leave the setup stage on a configuration that cannot produce a
    // number, rather than generating a script that dies partway through a
    // queue of expensive SCFs. Both failures are silent otherwise: with no
    // site the loop body never runs, and with one α the fit is a line through
    // two points that always fits perfectly.
    const core::HubbardRunConfig c = config();
    if (c.sites.empty()) {
        QMessageBox::warning(
            this, tr("Hubbard Parameter Calculation"),
            tr("Tick at least one site to perturb.\n\nU is a property of a "
               "site's localized manifold; with nothing selected there is no "
               "occupation to differentiate."));
        return;
    }
    if (c.alphas.size() < 2) {
        QMessageBox::warning(
            this, tr("Hubbard Parameter Calculation"),
            tr("At least two non-zero perturbation strengths are needed.\n\n"
               "χ and χ₀ are slopes dn/dα. One α gives a line through two "
               "points, which fits perfectly whether or not the response is "
               "linear — so it reports no error even when it is wrong."));
        return;
    }
    SimulationWizardBase::goNext();
}

QString HubbardUWizard::generateScript() const
{
    return QString::fromStdString(
        core::generateHubbardScript(config(), "structure.extxyz"));
}

} // namespace calango::gui
