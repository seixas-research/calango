#include "gui/ExamplesDialog.hpp"

#include "core/Element.hpp"
#include "gui/EnvFile.hpp"
#include "gui/PeriodicTableDialog.hpp"
#include "python_bridge/BulkBuilder.hpp"
#include "python_bridge/MaterialsProject.hpp"
#include "python_bridge/PubChem.hpp"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QGroupBox>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QMessageBox>
#include <QProgressDialog>
#include <QSettings>
#include <QSpinBox>
#include <QStackedWidget>
#include <QTabWidget>
#include <QTableWidget>
#include <QVBoxLayout>

#include <algorithm>
#include <limits>

namespace calango::gui {

namespace {

const auto kApiKeySetting = QStringLiteral("materialsProject/apiKey");

/// Result-table columns. Kept as an enum so the sort/filter code and the
/// population code cannot drift out of sync.
enum ResultColumn {
    ColFormula = 0,
    ColSpaceGroup,
    ColBandGap,
    ColEHull,
    ColSites,
    ColMaterialId,
    ResultColumnCount,
};

/// A numeric cell that sorts by value rather than by its displayed text
/// (so "10" doesn't sort before "9", and "—" sinks to the bottom).
class NumericItem : public QTableWidgetItem {
public:
    NumericItem(const QString& text, double value)
        : QTableWidgetItem(text)
        , value_(value)
    {
        setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
    }

    bool operator<(const QTableWidgetItem& other) const override
    {
        if (const auto* numeric = dynamic_cast<const NumericItem*>(&other))
            return value_ < numeric->value_;
        return QTableWidgetItem::operator<(other);
    }

private:
    double value_;
};

/// Element symbol under the cursor position in `edit`, used to preselect
/// the periodic table on the element the user is editing.
int symbolAtCursor(const QLineEdit* edit)
{
    const QString text = edit->text().trimmed();
    if (text.isEmpty())
        return 0;
    // Take the trailing element-looking token (e.g. "Mo" in "MoS2").
    int end = text.size();
    while (end > 0 && !text.at(end - 1).isLetter())
        --end;
    int start = end;
    while (start > 0 && text.at(start - 1).isLetter())
        --start;
    return core::Elements::atomicNumber(text.mid(start, end - start).toStdString());
}

} // namespace

ExamplesDialog::ExamplesDialog(QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle(tr("Database Browser"));
    resize(760, 620);

    auto* tabs = new QTabWidget(this);
    // Bulk first: it needs no network and no API key, so it is the entry
    // point that always works.
    tabs->addTab(createBulkTab(), tr("Bulk"));
    tabs->addTab(createMaterialsProjectTab(), tr("Materials Project"));
    tabs->addTab(createPubChemTab(), tr("PubChem"));

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    auto* layout = new QVBoxLayout(this);
    layout->addWidget(tabs);
    layout->addWidget(buttons);
}

QString ExamplesDialog::apiKey() const
{
    return apiKeyEdit_ ? apiKeyEdit_->text().trimmed() : QString();
}

// ---------------------------------------------------------------------------
// Bulk tab (ase.build.bulk / ase.spacegroup.crystal)
// ---------------------------------------------------------------------------

QPushButton* ExamplesDialog::makePeriodicTableButton(QWidget* parent,
                                                     QLineEdit* target,
                                                     bool append)
{
    auto* button = new QPushButton(tr("Periodic Table…"), parent);
    button->setToolTip(append
                           ? tr("Pick an element and append its symbol")
                           : tr("Pick an element"));
    connect(button, &QPushButton::clicked, this, [this, target, append] {
        const int z = PeriodicTableDialog::pickElement(this, symbolAtCursor(target));
        if (z <= 0)
            return;
        const QString symbol = QLatin1String(core::Elements::data(z).symbol);
        if (append)
            target->setText(target->text().trimmed() + symbol);
        else
            target->setText(symbol);
        target->setFocus();
    });
    return button;
}

QWidget* ExamplesDialog::createBulkTab()
{
    auto* page = new QWidget(this);
    auto* layout = new QVBoxLayout(page);

    bulkModeCombo_ = new QComboBox(page);
    bulkModeCombo_->addItem(tr("Prototype (ase.build.bulk)"));
    bulkModeCombo_->addItem(tr("Space group + Wyckoff basis"));
    auto* modeForm = new QFormLayout;
    modeForm->addRow(tr("Build from:"), bulkModeCombo_);
    layout->addLayout(modeForm);

    // --- Prototype page -----------------------------------------------------
    bulkPrototypePage_ = new QWidget(page);
    auto* protoForm = new QFormLayout(bulkPrototypePage_);
    protoForm->setContentsMargins(0, 0, 0, 0);

    bulkFormulaEdit_ = new QLineEdit(bulkPrototypePage_);
    bulkFormulaEdit_->setPlaceholderText(tr("e.g. Si, Au, NaCl, GaAs"));
    bulkFormulaEdit_->setText(QStringLiteral("Si"));
    auto* formulaRow = new QHBoxLayout;
    formulaRow->addWidget(bulkFormulaEdit_, 1);
    formulaRow->addWidget(
        makePeriodicTableButton(bulkPrototypePage_, bulkFormulaEdit_, /*append=*/true));
    protoForm->addRow(tr("Formula:"), formulaRow);

    bulkStructureCombo_ = new QComboBox(bulkPrototypePage_);
    for (const std::string& name : pybridge::BulkBuilder::prototypes())
        bulkStructureCombo_->addItem(QString::fromStdString(name));
    bulkStructureCombo_->setCurrentText(QStringLiteral("diamond"));
    protoForm->addRow(tr("Crystal structure:"), bulkStructureCombo_);

    bulkASpin_ = new QDoubleSpinBox(bulkPrototypePage_);
    bulkASpin_->setRange(0.0, 100.0);
    bulkASpin_->setDecimals(4);
    bulkASpin_->setSingleStep(0.05);
    bulkASpin_->setSuffix(QStringLiteral(" Å"));
    bulkASpin_->setSpecialValueText(tr("ASE default"));
    bulkASpin_->setValue(5.43);
    bulkASpin_->setToolTip(
        tr("Lattice constant a. Leave at 0 (\"ASE default\") to use ASE's "
           "tabulated experimental value for the element."));
    protoForm->addRow(tr("Lattice constant a:"), bulkASpin_);

    // Only the "orthorhombic" prototype takes an independent b; ASE builds a
    // cell with b = nan if it is left out there, so the row is shown (and
    // required) exactly for that case.
    bulkUseB_ = new QCheckBox(tr("Set b"), bulkPrototypePage_);
    bulkBLatticeSpin_ = new QDoubleSpinBox(bulkPrototypePage_);
    bulkBLatticeSpin_->setRange(0.1, 100.0);
    bulkBLatticeSpin_->setDecimals(4);
    bulkBLatticeSpin_->setSingleStep(0.05);
    bulkBLatticeSpin_->setSuffix(QStringLiteral(" Å"));
    bulkBLatticeSpin_->setValue(5.0);
    bulkBLatticeSpin_->setEnabled(false);
    auto* bRow = new QHBoxLayout;
    bRow->addWidget(bulkUseB_);
    bRow->addWidget(bulkBLatticeSpin_, 1);
    protoForm->addRow(tr("b:"), bRow);
    connect(bulkUseB_, &QCheckBox::toggled, bulkBLatticeSpin_,
            &QDoubleSpinBox::setEnabled);

    // c and c/a are alternatives — ASE rejects both at once.
    bulkUseCovera_ = new QCheckBox(tr("Set c/a ratio"), bulkPrototypePage_);
    bulkCoveraSpin_ = new QDoubleSpinBox(bulkPrototypePage_);
    bulkCoveraSpin_->setRange(0.1, 20.0);
    bulkCoveraSpin_->setDecimals(4);
    bulkCoveraSpin_->setSingleStep(0.01);
    bulkCoveraSpin_->setValue(1.6330); // ideal hcp, sqrt(8/3)
    bulkCoveraSpin_->setEnabled(false);
    auto* coveraRow = new QHBoxLayout;
    coveraRow->addWidget(bulkUseCovera_);
    coveraRow->addWidget(bulkCoveraSpin_, 1);
    protoForm->addRow(tr("c/a:"), coveraRow);

    bulkUseC_ = new QCheckBox(tr("Set c directly"), bulkPrototypePage_);
    bulkCSpin_ = new QDoubleSpinBox(bulkPrototypePage_);
    bulkCSpin_->setRange(0.1, 100.0);
    bulkCSpin_->setDecimals(4);
    bulkCSpin_->setSingleStep(0.05);
    bulkCSpin_->setSuffix(QStringLiteral(" Å"));
    bulkCSpin_->setValue(5.0);
    bulkCSpin_->setEnabled(false);
    auto* cRow = new QHBoxLayout;
    cRow->addWidget(bulkUseC_);
    cRow->addWidget(bulkCSpin_, 1);
    protoForm->addRow(tr("c:"), cRow);

    connect(bulkUseCovera_, &QCheckBox::toggled, this, [this](bool on) {
        bulkCoveraSpin_->setEnabled(on);
        if (on)
            bulkUseC_->setChecked(false); // ASE: c and c/a are mutually exclusive
    });
    connect(bulkUseC_, &QCheckBox::toggled, this, [this](bool on) {
        bulkCSpin_->setEnabled(on);
        if (on)
            bulkUseCovera_->setChecked(false);
    });

    bulkCubicCheck_ = new QCheckBox(tr("Conventional cubic cell"), bulkPrototypePage_);
    bulkOrthoCheck_ = new QCheckBox(tr("Orthorhombic cell"), bulkPrototypePage_);
    connect(bulkCubicCheck_, &QCheckBox::toggled, this, [this](bool on) {
        if (on)
            bulkOrthoCheck_->setChecked(false);
    });
    connect(bulkOrthoCheck_, &QCheckBox::toggled, this, [this](bool on) {
        if (on)
            bulkCubicCheck_->setChecked(false);
    });
    auto* cellRow = new QHBoxLayout;
    cellRow->addWidget(bulkCubicCheck_);
    cellRow->addWidget(bulkOrthoCheck_);
    cellRow->addStretch(1);
    protoForm->addRow(tr("Cell shape:"), cellRow);

    // --- Space-group / Wyckoff page ----------------------------------------
    bulkSpaceGroupPage_ = new QWidget(page);
    auto* sgLayout = new QVBoxLayout(bulkSpaceGroupPage_);
    sgLayout->setContentsMargins(0, 0, 0, 0);
    auto* sgForm = new QFormLayout;

    bulkSpaceGroupSpin_ = new QSpinBox(bulkSpaceGroupPage_);
    bulkSpaceGroupSpin_->setRange(1, 230);
    bulkSpaceGroupSpin_->setValue(225); // Fm-3m
    sgForm->addRow(tr("Space group (No.):"), bulkSpaceGroupSpin_);

    const auto lengthSpin = [this](double value) {
        auto* spin = new QDoubleSpinBox(bulkSpaceGroupPage_);
        spin->setRange(0.1, 100.0);
        spin->setDecimals(4);
        spin->setSingleStep(0.05);
        spin->setSuffix(QStringLiteral(" Å"));
        spin->setValue(value);
        return spin;
    };
    const auto angleSpin = [this] {
        auto* spin = new QDoubleSpinBox(bulkSpaceGroupPage_);
        spin->setRange(1.0, 179.0);
        spin->setDecimals(3);
        spin->setValue(90.0);
        spin->setSuffix(QStringLiteral("°"));
        return spin;
    };
    bulkSgASpin_ = lengthSpin(5.64); // NaCl, matching the default basis below
    bulkBSpin_ = lengthSpin(5.64);
    bulkSgCSpin_ = lengthSpin(5.64);
    bulkAlphaSpin_ = angleSpin();
    bulkBetaSpin_ = angleSpin();
    bulkGammaSpin_ = angleSpin();
    auto* lengthsRow = new QHBoxLayout;
    lengthsRow->addWidget(bulkSgASpin_);
    lengthsRow->addWidget(bulkBSpin_);
    lengthsRow->addWidget(bulkSgCSpin_);
    sgForm->addRow(tr("a, b, c:"), lengthsRow);
    auto* anglesRow = new QHBoxLayout;
    anglesRow->addWidget(bulkAlphaSpin_);
    anglesRow->addWidget(bulkBetaSpin_);
    anglesRow->addWidget(bulkGammaSpin_);
    sgForm->addRow(tr("α, β, γ:"), anglesRow);

    bulkPrimitiveCheck_ =
        new QCheckBox(tr("Reduce to the primitive cell"), bulkSpaceGroupPage_);
    sgForm->addRow(QString(), bulkPrimitiveCheck_);
    sgLayout->addLayout(sgForm);

    auto* sitesGroup = new QGroupBox(tr("Wyckoff sites (representative positions)"),
                                     bulkSpaceGroupPage_);
    auto* sitesLayout = new QVBoxLayout(sitesGroup);
    bulkSitesTable_ = new QTableWidget(0, 5, sitesGroup);
    bulkSitesTable_->setHorizontalHeaderLabels(
        {tr("Element"), tr("u"), tr("v"), tr("w"), tr("Occupancy")});
    bulkSitesTable_->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    bulkSitesTable_->verticalHeader()->setVisible(false);
    bulkSitesTable_->setToolTip(
        tr("One row per symmetry-inequivalent site. The space group generates "
           "the remaining equivalent positions."));
    sitesLayout->addWidget(bulkSitesTable_);

    const auto addSite = [this](const QString& symbol, double u, double v, double w) {
        const int row = bulkSitesTable_->rowCount();
        bulkSitesTable_->insertRow(row);
        bulkSitesTable_->setItem(row, 0, new QTableWidgetItem(symbol));
        const double values[3] = {u, v, w};
        for (int i = 0; i < 3; ++i) {
            bulkSitesTable_->setItem(
                row, i + 1, new QTableWidgetItem(QString::number(values[i], 'f', 4)));
        }
        bulkSitesTable_->setItem(row, 4, new QTableWidgetItem(QStringLiteral("1.0")));
    };
    addSite(QStringLiteral("Na"), 0.0, 0.0, 0.0);
    addSite(QStringLiteral("Cl"), 0.5, 0.5, 0.5);

    auto* siteButtons = new QHBoxLayout;
    auto* addSiteButton = new QPushButton(tr("Add Site"), sitesGroup);
    auto* removeSiteButton = new QPushButton(tr("Remove Site"), sitesGroup);
    auto* sitePeriodicButton = new QPushButton(tr("Periodic Table…"), sitesGroup);
    sitePeriodicButton->setToolTip(
        tr("Set the selected row's element from the periodic table"));
    siteButtons->addWidget(addSiteButton);
    siteButtons->addWidget(removeSiteButton);
    siteButtons->addWidget(sitePeriodicButton);
    siteButtons->addStretch(1);
    sitesLayout->addLayout(siteButtons);
    sgLayout->addWidget(sitesGroup, 1);

    connect(addSiteButton, &QPushButton::clicked, this,
            [addSite] { addSite(QStringLiteral("H"), 0.0, 0.0, 0.0); });
    connect(removeSiteButton, &QPushButton::clicked, this, [this] {
        const int row = bulkSitesTable_->currentRow();
        if (row >= 0)
            bulkSitesTable_->removeRow(row);
    });
    connect(sitePeriodicButton, &QPushButton::clicked, this, [this] {
        int row = bulkSitesTable_->currentRow();
        if (row < 0 && bulkSitesTable_->rowCount() > 0)
            row = 0;
        if (row < 0)
            return;
        QTableWidgetItem* cell = bulkSitesTable_->item(row, 0);
        const int current = cell
            ? core::Elements::atomicNumber(cell->text().trimmed().toStdString())
            : 0;
        const int z = PeriodicTableDialog::pickElement(this, current);
        if (z <= 0)
            return;
        const QString symbol = QLatin1String(core::Elements::data(z).symbol);
        if (cell)
            cell->setText(symbol);
        else
            bulkSitesTable_->setItem(row, 0, new QTableWidgetItem(symbol));
    });

    auto* stack = new QStackedWidget(page);
    stack->addWidget(bulkPrototypePage_);
    stack->addWidget(bulkSpaceGroupPage_);
    layout->addWidget(stack, 1);
    connect(bulkModeCombo_, &QComboBox::currentIndexChanged, stack,
            &QStackedWidget::setCurrentIndex);
    connect(bulkModeCombo_, &QComboBox::currentIndexChanged, this,
            [this] { updateBulkParameterVisibility(); });
    connect(bulkStructureCombo_, &QComboBox::currentTextChanged, this,
            [this] { updateBulkParameterVisibility(); });

    bulkBuildButton_ = new QPushButton(tr("Build Crystal"), page);
    layout->addWidget(bulkBuildButton_);
    bulkStatus_ = new QLabel(page);
    bulkStatus_->setWordWrap(true);
    layout->addWidget(bulkStatus_);
    connect(bulkBuildButton_, &QPushButton::clicked,
            this, &ExamplesDialog::buildBulkCrystal);
    connect(bulkFormulaEdit_, &QLineEdit::returnPressed,
            this, &ExamplesDialog::buildBulkCrystal);

    updateBulkParameterVisibility();
    return page;
}

void ExamplesDialog::updateBulkParameterVisibility()
{
    const bool prototype = bulkModeCombo_->currentIndex() == 0;
    if (!prototype)
        return;
    // Only the anisotropic prototypes take c / c-over-a; leaving the controls
    // enabled elsewhere invites an ASE error the user can't act on.
    const bool anisotropic = pybridge::BulkBuilder::usesCOverA(
        bulkStructureCombo_->currentText().toStdString());
    for (QWidget* w : {static_cast<QWidget*>(bulkUseCovera_),
                       static_cast<QWidget*>(bulkUseC_)}) {
        w->setEnabled(anisotropic);
    }
    if (!anisotropic) {
        bulkUseCovera_->setChecked(false);
        bulkUseC_->setChecked(false);
    }
    bulkCoveraSpin_->setEnabled(anisotropic && bulkUseCovera_->isChecked());
    bulkCSpin_->setEnabled(anisotropic && bulkUseC_->isChecked());

    const bool needsB = pybridge::BulkBuilder::usesB(
        bulkStructureCombo_->currentText().toStdString());
    bulkUseB_->setEnabled(needsB);
    // b is not optional for the prototype that has it — preselect it so the
    // user doesn't have to discover the nan cell the hard way.
    if (needsB && !bulkUseB_->isChecked())
        bulkUseB_->setChecked(true);
    else if (!needsB)
        bulkUseB_->setChecked(false);
    bulkBLatticeSpin_->setEnabled(needsB && bulkUseB_->isChecked());
}

void ExamplesDialog::buildBulkCrystal()
{
    bulkBuildButton_->setEnabled(false);
    bulkStatus_->setStyleSheet(QString());
    bulkStatus_->setText(tr("Building…"));
    QGuiApplication::setOverrideCursor(Qt::WaitCursor);
    QGuiApplication::processEvents();

    try {
        std::shared_ptr<core::Structure> structure;
        QString name;

        if (bulkModeCombo_->currentIndex() == 0) {
            pybridge::BulkBuilder::PrototypeSpec spec;
            spec.name = bulkFormulaEdit_->text().trimmed().toStdString();
            spec.crystalStructure = bulkStructureCombo_->currentText().toStdString();
            spec.a = bulkASpin_->value();
            spec.hasB = bulkUseB_->isChecked();
            spec.b = bulkBLatticeSpin_->value();
            spec.hasC = bulkUseC_->isChecked();
            spec.c = bulkCSpin_->value();
            spec.hasCovera = bulkUseCovera_->isChecked();
            spec.covera = bulkCoveraSpin_->value();
            spec.cubic = bulkCubicCheck_->isChecked();
            spec.orthorhombic = bulkOrthoCheck_->isChecked();
            structure = std::make_shared<core::Structure>(
                pybridge::BulkBuilder::buildPrototype(spec));
            name = QStringLiteral("%1 (%2)")
                       .arg(bulkFormulaEdit_->text().trimmed(),
                            bulkStructureCombo_->currentText());
        } else {
            pybridge::BulkBuilder::SpaceGroupSpec spec;
            spec.spaceGroup = bulkSpaceGroupSpin_->value();
            spec.a = bulkSgASpin_->value();
            spec.b = bulkBSpin_->value();
            spec.c = bulkSgCSpin_->value();
            spec.alpha = bulkAlphaSpin_->value();
            spec.beta = bulkBetaSpin_->value();
            spec.gamma = bulkGammaSpin_->value();
            spec.primitive = bulkPrimitiveCheck_->isChecked();
            for (int row = 0; row < bulkSitesTable_->rowCount(); ++row) {
                const auto cellText = [this, row](int column) {
                    const QTableWidgetItem* item = bulkSitesTable_->item(row, column);
                    return item ? item->text().trimmed() : QString();
                };
                pybridge::BulkBuilder::WyckoffSite site;
                site.symbol = cellText(0).toStdString();
                if (site.symbol.empty())
                    continue; // blank row: treat as "not filled in yet"
                site.u = cellText(1).toDouble();
                site.v = cellText(2).toDouble();
                site.w = cellText(3).toDouble();
                const QString occupancy = cellText(4);
                site.occupancy = occupancy.isEmpty() ? 1.0 : occupancy.toDouble();
                spec.sites.push_back(std::move(site));
            }
            structure = std::make_shared<core::Structure>(
                pybridge::BulkBuilder::buildFromSpaceGroup(spec));
            name = QStringLiteral("%1 (SG %2)")
                       .arg(QString::fromStdString(structure->chemicalFormula()))
                       .arg(spec.spaceGroup);
        }

        bulkStatus_->setText(tr("Built %1 (%2 atoms)")
                                 .arg(QString::fromStdString(
                                     structure->chemicalFormula()))
                                 .arg(structure->size()));
        Q_EMIT structureFetched(std::move(structure), name);
    } catch (const std::exception& e) {
        bulkStatus_->setStyleSheet(QStringLiteral("color: #d9534f;"));
        bulkStatus_->setText(QString::fromUtf8(e.what()));
    }
    QGuiApplication::restoreOverrideCursor();
    bulkBuildButton_->setEnabled(true);
}

// ---------------------------------------------------------------------------
// Materials Project tab
// ---------------------------------------------------------------------------

QWidget* ExamplesDialog::createMaterialsProjectTab()
{
    auto* mpPage = new QWidget(this);
    auto* mpLayout = new QVBoxLayout(mpPage);

    auto* form = new QFormLayout;
    apiKeyEdit_ = new QLineEdit(mpPage);
    apiKeyEdit_->setEchoMode(QLineEdit::Password);
    apiKeyEdit_->setPlaceholderText(tr("API key from materialsproject.org/api"));
    // Stored key first; otherwise the environment (auto-loaded from the
    // configured .env file at launch — see Preferences).
    QString storedKey = QSettings().value(kApiKeySetting).toString();
    if (storedKey.isEmpty())
        storedKey = qEnvironmentVariable("MP_API_KEY");
    apiKeyEdit_->setText(storedKey);
    connect(apiKeyEdit_, &QLineEdit::textChanged, this, [this] {
        QSettings().setValue(kApiKeySetting, apiKeyEdit_->text());
    });
    form->addRow(tr("API key:"), apiKeyEdit_);

    // .env location: the key can live in a custom directory's .env file;
    // Browse points Calango there, Reload re-imports MP_API_KEY. The key
    // itself is only ever shown password-masked above.
    auto* envRow = new QHBoxLayout;
    auto* envLabel = new QLabel(envFilePath(), mpPage);
    envLabel->setWordWrap(true);
    auto* envBrowseButton = new QPushButton(tr("Browse…"), mpPage);
    auto* envReloadButton = new QPushButton(tr("Reload"), mpPage);
    envRow->addWidget(envLabel, 1);
    envRow->addWidget(envBrowseButton);
    envRow->addWidget(envReloadButton);
    form->addRow(tr(".env file:"), envRow);

    const auto applyEnvKey = [this] {
        loadEnvironmentFile(/*overrideExisting=*/true);
        const QString envKey = qEnvironmentVariable("MP_API_KEY");
        if (!envKey.isEmpty())
            apiKeyEdit_->setText(envKey); // stays password-masked on screen
    };
    connect(envBrowseButton, &QPushButton::clicked, this,
            [this, envLabel, applyEnvKey] {
                const QString dir = QFileDialog::getExistingDirectory(
                    this, tr("Select Directory Containing .env"));
                if (dir.isEmpty())
                    return;
                setEnvFilePath(dir + QStringLiteral("/.env"));
                envLabel->setText(envFilePath());
                applyEnvKey();
            });
    connect(envReloadButton, &QPushButton::clicked, this,
            [applyEnvKey] { applyEnvKey(); });
    mpLayout->addLayout(form);

    // --- Direct mp-id fetch (unchanged single-structure path) ---------------
    auto* directGroup = new QGroupBox(tr("Fetch a single entry by ID"), mpPage);
    auto* directLayout = new QHBoxLayout(directGroup);
    materialIdEdit_ = new QLineEdit(directGroup);
    materialIdEdit_->setPlaceholderText(tr("e.g. mp-149 (silicon)"));
    fetchButton_ = new QPushButton(tr("Fetch Structure"), directGroup);
    directLayout->addWidget(materialIdEdit_, 1);
    directLayout->addWidget(fetchButton_);
    mpLayout->addWidget(directGroup);
    connect(fetchButton_, &QPushButton::clicked,
            this, &ExamplesDialog::fetchFromMaterialsProject);
    connect(materialIdEdit_, &QLineEdit::returnPressed,
            this, &ExamplesDialog::fetchFromMaterialsProject);

    // --- Multi-material search ---------------------------------------------
    auto* searchGroup = new QGroupBox(tr("Search"), mpPage);
    auto* searchLayout = new QVBoxLayout(searchGroup);

    auto* searchRow = new QHBoxLayout;
    searchEdit_ = new QLineEdit(searchGroup);
    searchEdit_->setPlaceholderText(tr("e.g. Li-Fe-O, or Li Fe O, or LiFePO4"));
    searchModeCombo_ = new QComboBox(searchGroup);
    searchModeCombo_->addItem(tr("Chemical system (exact)"));
    searchModeCombo_->addItem(tr("Contains elements"));
    searchModeCombo_->addItem(tr("Formula"));
    searchModeCombo_->setToolTip(
        tr("Exact: phases made of only these elements.\n"
           "Contains: phases including these elements (plus any others).\n"
           "Formula: a specific stoichiometry such as LiFePO4."));
    searchLimitSpin_ = new QSpinBox(searchGroup);
    searchLimitSpin_->setRange(1, 1000);
    searchLimitSpin_->setValue(100);
    searchLimitSpin_->setPrefix(tr("max "));
    searchLimitSpin_->setToolTip(
        tr("Results are sorted by energy above hull, so a capped search "
           "returns the most stable phases."));
    searchButton_ = new QPushButton(tr("Search"), searchGroup);
    searchRow->addWidget(searchEdit_, 1);
    searchRow->addWidget(searchModeCombo_);
    searchRow->addWidget(searchLimitSpin_);
    searchRow->addWidget(searchButton_);
    searchLayout->addLayout(searchRow);

    filterEdit_ = new QLineEdit(searchGroup);
    filterEdit_->setPlaceholderText(tr("Filter results (formula, space group, mp-id)…"));
    filterEdit_->setClearButtonEnabled(true);
    searchLayout->addWidget(filterEdit_);

    resultsTable_ = new QTableWidget(0, ResultColumnCount, searchGroup);
    resultsTable_->setHorizontalHeaderLabels({tr("Formula"), tr("Space group"),
                                              tr("Band gap (eV)"),
                                              tr("E above hull (eV/atom)"),
                                              tr("Sites"), tr("MP-ID")});
    resultsTable_->setSelectionBehavior(QAbstractItemView::SelectRows);
    resultsTable_->setSelectionMode(QAbstractItemView::ExtendedSelection);
    resultsTable_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    resultsTable_->setSortingEnabled(true);
    resultsTable_->verticalHeader()->setVisible(false);
    resultsTable_->horizontalHeader()->setStretchLastSection(true);
    searchLayout->addWidget(resultsTable_, 1);

    auto* actionRow = new QHBoxLayout;
    openSeparatelyButton_ =
        new QPushButton(tr("Open Selected in Separate Workspace Tabs"), searchGroup);
    groupTrajectoryButton_ =
        new QPushButton(tr("Group Selected into Single Trajectory File"), searchGroup);
    groupTrajectoryButton_->setToolTip(
        tr("Combines the selected structures into one multi-frame trajectory "
           "document — useful for scanning a composition series side by side."));
    openSeparatelyButton_->setEnabled(false);
    groupTrajectoryButton_->setEnabled(false);
    actionRow->addWidget(openSeparatelyButton_);
    actionRow->addWidget(groupTrajectoryButton_);
    actionRow->addStretch(1);
    searchLayout->addLayout(actionRow);
    mpLayout->addWidget(searchGroup, 1);

    fetchStatus_ = new QLabel(mpPage);
    fetchStatus_->setWordWrap(true);
    mpLayout->addWidget(fetchStatus_);

    connect(searchButton_, &QPushButton::clicked,
            this, &ExamplesDialog::searchMaterialsProject);
    connect(searchEdit_, &QLineEdit::returnPressed,
            this, &ExamplesDialog::searchMaterialsProject);
    connect(openSeparatelyButton_, &QPushButton::clicked,
            this, &ExamplesDialog::openSelectedSeparately);
    connect(groupTrajectoryButton_, &QPushButton::clicked,
            this, &ExamplesDialog::groupSelectedIntoTrajectory);
    connect(resultsTable_, &QTableWidget::itemSelectionChanged, this, [this] {
        const int count = selectedMaterialIds().size();
        openSeparatelyButton_->setEnabled(count > 0);
        // A one-frame "trajectory" is just a structure; require two.
        groupTrajectoryButton_->setEnabled(count > 1);
    });
    connect(filterEdit_, &QLineEdit::textChanged, this, [this](const QString& text) {
        const QString needle = text.trimmed();
        for (int row = 0; row < resultsTable_->rowCount(); ++row) {
            bool match = needle.isEmpty();
            for (int col = 0; col < ResultColumnCount && !match; ++col) {
                const QTableWidgetItem* item = resultsTable_->item(row, col);
                match = item && item->text().contains(needle, Qt::CaseInsensitive);
            }
            resultsTable_->setRowHidden(row, !match);
        }
    });

    return mpPage;
}

void ExamplesDialog::setMaterialsProjectBusy(bool busy)
{
    searchButton_->setEnabled(!busy);
    fetchButton_->setEnabled(!busy);
    openSeparatelyButton_->setEnabled(!busy && !selectedMaterialIds().isEmpty());
    groupTrajectoryButton_->setEnabled(!busy && selectedMaterialIds().size() > 1);
}

void ExamplesDialog::searchMaterialsProject()
{
    const QString query = searchEdit_->text().trimmed();
    const int mode = searchModeCombo_->currentIndex();

    setMaterialsProjectBusy(true);
    fetchStatus_->setStyleSheet(QString());
    fetchStatus_->setText(tr("Searching Materials Project for “%1”…").arg(query));
    QGuiApplication::setOverrideCursor(Qt::WaitCursor);
    QGuiApplication::processEvents();

    std::vector<pybridge::MaterialsProject::SearchHit> hits;
    QString error;
    try {
        hits = pybridge::MaterialsProject::search(
            query.toStdString(), apiKey().toStdString(),
            /*asFormula=*/mode == 2,
            /*exactSystem=*/mode == 0,
            searchLimitSpin_->value());
    } catch (const std::exception& e) {
        error = QString::fromUtf8(e.what());
    }
    QGuiApplication::restoreOverrideCursor();

    if (!error.isEmpty()) {
        fetchStatus_->setStyleSheet(QStringLiteral("color: #d9534f;"));
        fetchStatus_->setText(error);
        setMaterialsProjectBusy(false);
        return;
    }

    // Sorting must be off while filling, or rows shuffle mid-population and
    // the item/row mapping breaks.
    resultsTable_->setSortingEnabled(false);
    resultsTable_->clearContents();
    resultsTable_->setRowCount(static_cast<int>(hits.size()));
    for (int row = 0; row < static_cast<int>(hits.size()); ++row) {
        const auto& hit = hits[static_cast<std::size_t>(row)];
        auto* formulaItem =
            new QTableWidgetItem(QString::fromStdString(hit.formula));
        if (hit.isStable) {
            QFont font = formulaItem->font();
            font.setBold(true); // on-hull phases stand out in a long list
            formulaItem->setFont(font);
            formulaItem->setToolTip(tr("On the convex hull (stable)"));
        }
        resultsTable_->setItem(row, ColFormula, formulaItem);

        const QString spaceGroup = hit.spaceGroupNumber > 0
            ? QStringLiteral("%1 (%2)")
                  .arg(QString::fromStdString(hit.spaceGroup))
                  .arg(hit.spaceGroupNumber)
            : QString::fromStdString(hit.spaceGroup);
        resultsTable_->setItem(row, ColSpaceGroup, new QTableWidgetItem(spaceGroup));

        resultsTable_->setItem(
            row, ColBandGap,
            new NumericItem(hit.hasBandGap ? QString::number(hit.bandGap, 'f', 3)
                                           : QStringLiteral("—"),
                            hit.hasBandGap ? hit.bandGap
                                           : std::numeric_limits<double>::max()));
        resultsTable_->setItem(
            row, ColEHull,
            new NumericItem(hit.hasEnergyAboveHull
                                ? QString::number(hit.energyAboveHull, 'f', 4)
                                : QStringLiteral("—"),
                            hit.hasEnergyAboveHull
                                ? hit.energyAboveHull
                                : std::numeric_limits<double>::max()));
        resultsTable_->setItem(row, ColSites,
                               new NumericItem(QString::number(hit.nSites),
                                               hit.nSites));
        resultsTable_->setItem(
            row, ColMaterialId,
            new QTableWidgetItem(QString::fromStdString(hit.materialId)));
    }
    resultsTable_->setSortingEnabled(true);
    resultsTable_->resizeColumnsToContents();
    filterEdit_->clear();

    if (hits.empty()) {
        fetchStatus_->setText(tr("No materials matched “%1”.").arg(query));
    } else {
        fetchStatus_->setText(
            tr("%1 material(s) found%2. Select one or more rows, then choose "
               "an action below.")
                .arg(hits.size())
                .arg(static_cast<int>(hits.size()) >= searchLimitSpin_->value()
                         ? tr(" (capped at %1 — raise the limit for more)")
                               .arg(searchLimitSpin_->value())
                         : QString()));
    }
    setMaterialsProjectBusy(false);
}

QStringList ExamplesDialog::selectedMaterialIds() const
{
    QStringList ids;
    if (!resultsTable_)
        return ids;
    // selectedItems() reports cells; walk rows so each entry appears once,
    // and skip rows the text filter has hidden.
    const QModelIndexList rows =
        resultsTable_->selectionModel()->selectedRows(ColMaterialId);
    for (const QModelIndex& index : rows) {
        if (resultsTable_->isRowHidden(index.row()))
            continue;
        const QTableWidgetItem* item = resultsTable_->item(index.row(), ColMaterialId);
        if (item && !item->text().isEmpty())
            ids.append(item->text());
    }
    return ids;
}

std::vector<ExamplesDialog::FetchedEntry>
ExamplesDialog::fetchSelected(const QStringList& ids, QStringList& errors)
{
    std::vector<FetchedEntry> entries;
    entries.reserve(static_cast<std::size_t>(ids.size()));

    QProgressDialog progress(tr("Fetching structures from Materials Project…"),
                             tr("Cancel"), 0, ids.size(), this);
    progress.setWindowModality(Qt::WindowModal);
    progress.setMinimumDuration(0);

    const std::string key = apiKey().toStdString();
    for (int i = 0; i < ids.size(); ++i) {
        progress.setValue(i);
        progress.setLabelText(tr("Fetching %1 (%2 of %3)…")
                                  .arg(ids.at(i))
                                  .arg(i + 1)
                                  .arg(ids.size()));
        QGuiApplication::processEvents();
        if (progress.wasCanceled())
            break;
        try {
            entries.push_back(
                {ids.at(i), std::make_shared<core::Structure>(
                                pybridge::MaterialsProject::fetchStructure(
                                    ids.at(i).toStdString(), key))});
        } catch (const std::exception& e) {
            // One bad id must not abort the rest of a 20-entry selection.
            errors.append(QStringLiteral("%1: %2").arg(
                ids.at(i), QString::fromUtf8(e.what()).section('\n', 0, 0)));
        }
    }
    progress.setValue(ids.size());
    return entries;
}

void ExamplesDialog::openSelectedSeparately()
{
    const QStringList ids = selectedMaterialIds();
    if (ids.isEmpty())
        return;

    setMaterialsProjectBusy(true);
    QStringList errors;
    auto entries = fetchSelected(ids, errors);

    for (auto& entry : entries) {
        // Tab label: "mp-149 Si" — the id alone is unreadable in a row of
        // tabs, the formula alone is ambiguous across polymorphs.
        const QString label =
            QStringLiteral("%1 %2").arg(
                entry.materialId,
                QString::fromStdString(entry.structure->chemicalFormula()));
        Q_EMIT structureFetched(std::move(entry.structure), label);
    }

    fetchStatus_->setStyleSheet(errors.isEmpty() ? QString()
                                                 : QStringLiteral("color: #d9534f;"));
    fetchStatus_->setText(
        errors.isEmpty()
            ? tr("Opened %1 structure(s) in separate tabs.").arg(entries.size())
            : tr("Opened %1 of %2; failed: %3")
                  .arg(entries.size())
                  .arg(ids.size())
                  .arg(errors.join(QStringLiteral("; "))));
    setMaterialsProjectBusy(false);
}

void ExamplesDialog::groupSelectedIntoTrajectory()
{
    const QStringList ids = selectedMaterialIds();
    if (ids.size() < 2)
        return;

    setMaterialsProjectBusy(true);
    QStringList errors;
    auto entries = fetchSelected(ids, errors);

    if (entries.size() < 2) {
        fetchStatus_->setStyleSheet(QStringLiteral("color: #d9534f;"));
        fetchStatus_->setText(
            tr("Need at least two structures to build a trajectory (fetched %1). %2")
                .arg(entries.size())
                .arg(errors.join(QStringLiteral("; "))));
        setMaterialsProjectBusy(false);
        return;
    }

    // Frame order follows the table's current sort order (which is the user's
    // chosen ordering — e.g. by stability or band gap), so the trajectory
    // scrubs through the series the way the list reads.
    std::vector<std::shared_ptr<core::Structure>> frames;
    frames.reserve(entries.size());
    for (auto& entry : entries)
        frames.push_back(std::move(entry.structure));

    // Frames of a trajectory normally share an atom count; these deliberately
    // do not (different phases). That is fine for the viewer and for extxyz,
    // but say so rather than let it look like a bug later.
    const bool ragged =
        std::any_of(frames.begin(), frames.end(),
                    [&frames](const std::shared_ptr<core::Structure>& frame) {
                        return frame->size() != frames.front()->size();
                    });

    const QString name = tr("MP group (%1 entries)").arg(frames.size());
    const std::size_t frameCount = frames.size();
    Q_EMIT trajectoryFetched(std::move(frames), name);

    QString message = tr("Grouped %1 structures into one trajectory document.")
                          .arg(frameCount);
    if (ragged) {
        message += tr(" Frames have differing atom counts — save as extxyz "
                      "(not .traj) to keep them all.");
    }
    if (!errors.isEmpty())
        message += tr(" Failed: %1").arg(errors.join(QStringLiteral("; ")));
    fetchStatus_->setStyleSheet(errors.isEmpty() ? QString()
                                                 : QStringLiteral("color: #d9534f;"));
    fetchStatus_->setText(message);
    setMaterialsProjectBusy(false);
}

void ExamplesDialog::fetchFromMaterialsProject()
{
    const QString materialId = materialIdEdit_->text().trimmed();
    setMaterialsProjectBusy(true);
    fetchStatus_->setStyleSheet(QString());
    fetchStatus_->setText(tr("Fetching %1…").arg(materialId));
    QGuiApplication::setOverrideCursor(Qt::WaitCursor);
    QGuiApplication::processEvents();

    try {
        auto structure = std::make_shared<core::Structure>(
            pybridge::MaterialsProject::fetchStructure(
                materialId.toStdString(), apiKey().toStdString()));
        fetchStatus_->setText(tr("Fetched %1: %2 (%3 atoms)")
                                  .arg(materialId,
                                       QString::fromStdString(
                                           structure->chemicalFormula()))
                                  .arg(structure->size()));
        Q_EMIT structureFetched(std::move(structure), materialId);
    } catch (const std::exception& e) {
        fetchStatus_->setStyleSheet(QStringLiteral("color: #d9534f;"));
        fetchStatus_->setText(QString::fromUtf8(e.what()));
    }
    QGuiApplication::restoreOverrideCursor();
    setMaterialsProjectBusy(false);
}

// ---------------------------------------------------------------------------
// PubChem tab
// ---------------------------------------------------------------------------

QWidget* ExamplesDialog::createPubChemTab()
{
    auto* pubchemPage = new QWidget(this);
    auto* pubchemLayout = new QVBoxLayout(pubchemPage);
    auto* pubchemForm = new QFormLayout;
    pubchemFieldCombo_ = new QComboBox(pubchemPage);
    pubchemFieldCombo_->addItem(tr("Name"), QStringLiteral("name"));
    pubchemFieldCombo_->addItem(tr("SMILES"), QStringLiteral("smiles"));
    pubchemFieldCombo_->addItem(tr("CID"), QStringLiteral("cid"));
    pubchemQueryEdit_ = new QLineEdit(pubchemPage);
    pubchemQueryEdit_->setPlaceholderText(
        tr("e.g. benzene, or c1ccccc1, or 241"));
    pubchemForm->addRow(tr("Search by:"), pubchemFieldCombo_);
    pubchemForm->addRow(tr("Query:"), pubchemQueryEdit_);
    pubchemLayout->addLayout(pubchemForm);
    pubchemButton_ = new QPushButton(tr("Fetch 3D Conformer"), pubchemPage);
    pubchemLayout->addWidget(pubchemButton_);
    pubchemStatus_ = new QLabel(
        tr("Retrieves the 3D molecular geometry from the online PubChem "
           "database (no API key required)."),
        pubchemPage);
    pubchemStatus_->setWordWrap(true);
    pubchemLayout->addWidget(pubchemStatus_);
    pubchemLayout->addStretch(1);
    connect(pubchemButton_, &QPushButton::clicked,
            this, &ExamplesDialog::fetchFromPubChem);
    connect(pubchemQueryEdit_, &QLineEdit::returnPressed,
            this, &ExamplesDialog::fetchFromPubChem);
    return pubchemPage;
}

void ExamplesDialog::fetchFromPubChem()
{
    const QString query = pubchemQueryEdit_->text().trimmed();
    const QString field = pubchemFieldCombo_->currentData().toString();
    pubchemButton_->setEnabled(false);
    pubchemStatus_->setStyleSheet(QString());
    pubchemStatus_->setText(tr("Searching PubChem for “%1”…").arg(query));
    QGuiApplication::setOverrideCursor(Qt::WaitCursor);
    QGuiApplication::processEvents();

    try {
        auto structure = std::make_shared<core::Structure>(
            pybridge::PubChem::fetchStructure(query.toStdString(),
                                              field.toStdString()));
        const QString name = QString::fromStdString(structure->chemicalFormula());
        pubchemStatus_->setText(tr("Fetched %1 (%2 atoms)")
                                    .arg(name)
                                    .arg(structure->size()));
        Q_EMIT structureFetched(std::move(structure),
                                query.isEmpty() ? name : query);
    } catch (const std::exception& e) {
        pubchemStatus_->setStyleSheet(QStringLiteral("color: #d9534f;"));
        pubchemStatus_->setText(QString::fromUtf8(e.what()));
    }
    QGuiApplication::restoreOverrideCursor();
    pubchemButton_->setEnabled(true);
}

} // namespace calango::gui
