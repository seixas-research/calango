#include "gui/KPathSelector.hpp"

#include "core/Structure.hpp"
#include "python_bridge/AseBridge.hpp"

#include <QFile>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>

#include <exception>

namespace calango::gui {

namespace {

/// Convert a JSON high-symmetry label back to its ASE single-token form
/// ("Gamma"/"Γ" → "G"); other labels pass through unchanged.
QString aseLabel(const QString& label)
{
    if (label == QLatin1String("Gamma") || label == QString::fromUtf8("Γ"))
        return QStringLiteral("G");
    return label;
}

/// Reconstruct an ASE path string ("GXMG,UX") from a kpath.json object:
/// prefer the explicit "path" field, else rebuild it from the segment labels.
QString pathStringFromJson(const QJsonObject& root)
{
    const QString explicitPath = root.value(QStringLiteral("path")).toString();
    if (!explicitPath.isEmpty())
        return explicitPath;

    QStringList sections;
    for (const QJsonValue& segValue : root.value(QStringLiteral("segments")).toArray()) {
        const QJsonObject segment = segValue.toObject();
        QString labels;
        for (const QJsonValue& labelValue :
             segment.value(QStringLiteral("labels")).toArray())
            labels += aseLabel(labelValue.toString());
        if (labels.isEmpty()) {
            // Fall back to the per-point "label" fields.
            for (const QJsonValue& ptValue :
                 segment.value(QStringLiteral("points")).toArray())
                labels += aseLabel(ptValue.toObject()
                                       .value(QStringLiteral("label"))
                                       .toString());
        }
        if (!labels.isEmpty())
            sections << labels;
    }
    return sections.join(QLatin1Char(','));
}

} // namespace

KPathSelector::KPathSelector(std::shared_ptr<const core::Structure> structure,
                             QWidget* parent)
    : QWidget(parent)
    , structure_(std::move(structure))
{
    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);

    // ASE's Bravais-lattice detection for the default high-symmetry path.
    QString suggested;
    try {
        if (structure_)
            suggested = QString::fromStdString(
                pybridge::AseBridge::bandPathInfo(*structure_).suggestedPath);
    } catch (const std::exception&) {
        suggested.clear();
    }

    pathEdit_ = new QLineEdit(suggested, this);
    pathEdit_->setPlaceholderText(tr("empty = ASE suggestion (e.g. GXMG)"));
    pathEdit_->setToolTip(
        tr("Concatenated high-symmetry labels; ',' marks a discontinuous "
           "section (e.g. Γ→X→M→Γ = \"GXMG\"). Load a kpath.json exported from "
           "the Brillouin Zone Builder, or type a path directly."));
    auto* loadButton =
        new QPushButton(tr("Load k-Path File (kpath.json)…"), this);
    layout->addWidget(pathEdit_, 1);
    layout->addWidget(loadButton);
    connect(loadButton, &QPushButton::clicked, this,
            &KPathSelector::loadKpathFile);
}

QString KPathSelector::path() const
{
    return pathEdit_->text().trimmed();
}

void KPathSelector::loadKpathFile()
{
    const QString file = QFileDialog::getOpenFileName(
        this, tr("Load k-Path File"), QStringLiteral("kpath.json"),
        tr("k-path JSON (*.json);;All files (*)"));
    if (file.isEmpty())
        return;

    QFile handle(file);
    if (!handle.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QMessageBox::critical(this, tr("Load k-Path File"),
                              tr("Could not read %1").arg(file));
        return;
    }
    QJsonParseError error;
    const QJsonDocument doc =
        QJsonDocument::fromJson(handle.readAll(), &error);
    if (error.error != QJsonParseError::NoError || !doc.isObject()) {
        QMessageBox::critical(this, tr("Load k-Path File"),
                              tr("%1 is not a valid kpath.json file:\n%2")
                                  .arg(file, error.errorString()));
        return;
    }
    const QString loaded = pathStringFromJson(doc.object());
    if (loaded.isEmpty()) {
        QMessageBox::information(this, tr("Load k-Path File"),
                                 tr("No k-path was found in %1.").arg(file));
        return;
    }
    pathEdit_->setText(loaded);
}

} // namespace calango::gui
