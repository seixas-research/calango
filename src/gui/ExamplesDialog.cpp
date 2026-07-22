#include "gui/ExamplesDialog.hpp"

#include "gui/EnvFile.hpp"
#include "python_bridge/MaterialsProject.hpp"
#include "python_bridge/PubChem.hpp"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QSettings>
#include <QTabWidget>
#include <QVBoxLayout>

namespace calango::gui {

namespace {

const auto kApiKeySetting = QStringLiteral("materialsProject/apiKey");

} // namespace

ExamplesDialog::ExamplesDialog(QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle(tr("Database Browser"));
    resize(520, 420);

    auto* tabs = new QTabWidget(this);

    // --- Materials Project tab ----------------------------------------------
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
    materialIdEdit_ = new QLineEdit(mpPage);
    materialIdEdit_->setPlaceholderText(tr("e.g. mp-149 (silicon)"));
    form->addRow(tr("API key:"), apiKeyEdit_);
    form->addRow(tr("Material ID:"), materialIdEdit_);

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

    fetchButton_ = new QPushButton(tr("Fetch Structure"), mpPage);
    mpLayout->addWidget(fetchButton_);
    fetchStatus_ = new QLabel(mpPage);
    fetchStatus_->setWordWrap(true);
    mpLayout->addWidget(fetchStatus_);
    mpLayout->addStretch(1);
    connect(fetchButton_, &QPushButton::clicked,
            this, &ExamplesDialog::fetchFromMaterialsProject);
    connect(materialIdEdit_, &QLineEdit::returnPressed,
            this, &ExamplesDialog::fetchFromMaterialsProject);
    tabs->addTab(mpPage, tr("Materials Project"));

    // --- PubChem tab --------------------------------------------------------
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
    tabs->addTab(pubchemPage, tr("PubChem"));

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    auto* layout = new QVBoxLayout(this);
    layout->addWidget(tabs);
    layout->addWidget(buttons);
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

void ExamplesDialog::fetchFromMaterialsProject()
{
    const QString materialId = materialIdEdit_->text().trimmed();
    fetchButton_->setEnabled(false);
    fetchStatus_->setStyleSheet(QString());
    fetchStatus_->setText(tr("Fetching %1…").arg(materialId));
    QGuiApplication::setOverrideCursor(Qt::WaitCursor);
    QGuiApplication::processEvents();

    try {
        auto structure = std::make_shared<core::Structure>(
            pybridge::MaterialsProject::fetchStructure(
                materialId.toStdString(), apiKeyEdit_->text().trimmed().toStdString()));
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
    fetchButton_->setEnabled(true);
}

} // namespace calango::gui
