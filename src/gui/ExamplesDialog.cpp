#include "gui/ExamplesDialog.hpp"

#include "python_bridge/MaterialsProject.hpp"

#include <QDialogButtonBox>
#include <QFormLayout>
#include <QGuiApplication>
#include <QSettings>
#include <QTabWidget>
#include <QVBoxLayout>

namespace calango::gui {

namespace {

const auto kApiKeySetting = QStringLiteral("materialsProject/apiKey");

struct Preset {
    const char* title;
    const char* file;
    const char* recommendation;
};

constexpr Preset kPresets[] = {
    {"Diamond (bulk C)", "diamond.vasp", "MACE-MP-0"},
    {"MoS₂ 2H (bulk)", "mos2_2h_bulk.vasp", "MACE-MP-0"},
    {"Graphene monolayer", "graphene.vasp", "MACE-MP-0"},
    {"MoS₂ 1H (monolayer)", "mos2_1h_monolayer.vasp", "MACE-MP-0"},
    {"Benzene", "benzene.xyz", "MACE-OFF (or EMT for quick tests)"},
    {"Naphthalene", "naphthalene.xyz", "MACE-OFF"},
    {"Coronene", "coronene.xyz", "MACE-OFF"},
};

} // namespace

ExamplesDialog::ExamplesDialog(QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle(tr("Database & Preset Browser"));
    resize(520, 420);

    auto* tabs = new QTabWidget(this);

    // --- Presets tab --------------------------------------------------------
    auto* presetsPage = new QWidget(this);
    auto* presetsLayout = new QVBoxLayout(presetsPage);
    presetList_ = new QListWidget(presetsPage);
    for (const Preset& preset : kPresets) {
        auto* item = new QListWidgetItem(
            QStringLiteral("%1   —   %2").arg(QString::fromUtf8(preset.title),
                                              QLatin1String(preset.recommendation)),
            presetList_);
        item->setData(Qt::UserRole,
                      QStringLiteral(":/assets/examples/")
                          + QLatin1String(preset.file));
        item->setData(Qt::UserRole + 1, QLatin1String(preset.recommendation));
    }
    presetsLayout->addWidget(presetList_);
    auto* loadPresetButton = new QPushButton(tr("Load Selected"), presetsPage);
    presetsLayout->addWidget(loadPresetButton);
    connect(loadPresetButton, &QPushButton::clicked,
            this, &ExamplesDialog::loadSelectedPreset);
    connect(presetList_, &QListWidget::itemDoubleClicked,
            this, &ExamplesDialog::loadSelectedPreset);
    tabs->addTab(presetsPage, tr("Presets"));

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

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    auto* layout = new QVBoxLayout(this);
    layout->addWidget(tabs);
    layout->addWidget(buttons);
}

void ExamplesDialog::loadSelectedPreset()
{
    QListWidgetItem* item = presetList_->currentItem();
    if (!item)
        return;
    Q_EMIT presetChosen(item->data(Qt::UserRole).toString(),
                        item->data(Qt::UserRole + 1).toString());
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
