#include "gui/SinglePointViewer.hpp"

#include "gui/GuiUtils.hpp"

#include <QApplication>
#include <QClipboard>
#include <QDialogButtonBox>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QSaveFile>
#include <QTableWidget>
#include <QTextStream>
#include <QVBoxLayout>

namespace calango::gui {

namespace {

/// Format a JSON value that may be `null` (Fermi level / SCF iterations are
/// absent for calculators that do not report them).
QString optionalNumber(const QJsonValue& v, const QString& suffix, int decimals)
{
    if (v.isNull() || v.isUndefined())
        return QStringLiteral("—");
    return QStringLiteral("%1%2").arg(v.toDouble(), 0, 'f', decimals).arg(suffix);
}

} // namespace

SinglePointViewer::SinglePointViewer(QWidget* parent) : QDialog(parent)
{
    setWindowTitle(tr("Single-Point Viewer"));
    resize(560, 560);

    auto* layout = new QVBoxLayout(this);

    // --- Physical summary --------------------------------------------------
    auto* summaryGroup = new QGroupBox(tr("Physical summary"), this);
    auto* form = new QFormLayout(summaryGroup);

    energyLabel_ = new QLabel(this);
    energyLabel_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    form->addRow(tr("Total Energy:"), energyLabel_);

    fermiLabel_ = new QLabel(this);
    fermiLabel_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    form->addRow(tr("Fermi Energy:"), fermiLabel_);

    forceLabel_ = new QLabel(this);
    forceLabel_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    form->addRow(tr("Max. Atomic Force:"), forceLabel_);

    // Total magnetic moment — only shown for spin-polarized runs (row hidden
    // otherwise; see loadResults).
    magmomLabel_ = new QLabel(this);
    magmomLabel_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    form->addRow(tr("Total Magnetic Moment:"), magmomLabel_);

    scfLabel_ = new QLabel(this);
    scfLabel_->setWordWrap(true);
    scfLabel_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    form->addRow(tr("Convergence:"), scfLabel_);

    layout->addWidget(summaryGroup);

    // --- Per-atom results --------------------------------------------------
    // Forces and magnetic moments in ONE table, keyed by atom index: they are
    // the two per-site results of an SCF, and reading "which atom carries the
    // moment" against "which atom is under strain" means having them side by
    // side. The moment column is filled only for a spin-polarized run.
    forcesGroup_ = new QGroupBox(tr("Per-atom results"), this);
    auto* forcesLayout = new QVBoxLayout(forcesGroup_);
    QGroupBox* forcesGroup = forcesGroup_;
    forcesTable_ = new QTableWidget(0, 5, forcesGroup);
    forcesTable_->setHorizontalHeaderLabels(
        {tr("atom #"), QStringLiteral("Fx"), QStringLiteral("Fy"),
         QStringLiteral("Fz"), tr("moment (μB)")});
    forcesTable_->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    forcesTable_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    forcesTable_->setSelectionBehavior(QAbstractItemView::SelectRows);
    forcesLayout->addWidget(forcesTable_);
    layout->addWidget(forcesGroup, 1);

    // --- Actions -----------------------------------------------------------
    auto* actionRow = new QHBoxLayout;
    volumetricButton_ = new QPushButton(tr("Get Volumetric Data"), this);
    volumetricButton_->setToolTip(
        tr("Export the charge density (.cube) from this run and register it in "
           "the Volumetric Data dock for 3D rendering."));
    connect(volumetricButton_, &QPushButton::clicked, this, [this] {
        if (!directory_.isEmpty())
            Q_EMIT getVolumetricDataRequested(directory_);
    });
    auto* copyButton = new QPushButton(tr("Copy metrics"), this);
    copyButton->setToolTip(tr("Copy the physical summary to the clipboard."));
    auto* exportJsonButton = new QPushButton(tr("Export JSON…"), this);
    auto* exportCsvButton = new QPushButton(tr("Export CSV…"), this);
    actionRow->addWidget(volumetricButton_);
    actionRow->addWidget(copyButton);
    actionRow->addWidget(exportJsonButton);
    actionRow->addWidget(exportCsvButton);
    actionRow->addStretch(1);
    layout->addLayout(actionRow);
    connect(copyButton, &QPushButton::clicked, this,
            &SinglePointViewer::copyToClipboard);
    connect(exportJsonButton, &QPushButton::clicked, this,
            &SinglePointViewer::exportJson);
    connect(exportCsvButton, &QPushButton::clicked, this,
            &SinglePointViewer::exportCsv);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);
}

bool SinglePointViewer::loadResults(const QString& jsonPath)
{
    QFile file(jsonPath);
    if (!file.open(QIODevice::ReadOnly)) {
        QMessageBox::warning(this, windowTitle(),
                             tr("Could not open %1").arg(jsonPath));
        return false;
    }
    QJsonParseError err{};
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject()) {
        QMessageBox::warning(this, windowTitle(),
                             tr("%1 is not a valid single-point summary.")
                                 .arg(QFileInfo(jsonPath).fileName()));
        return false;
    }
    data_ = doc.object();
    sourcePath_ = jsonPath;
    directory_ = QFileInfo(jsonPath).absolutePath();

    const double eV = data_.value(QStringLiteral("energy_eV")).toDouble();
    const double ha = data_.contains(QStringLiteral("energy_Hartree"))
        ? data_.value(QStringLiteral("energy_Hartree")).toDouble()
        : eV / 27.211386245988;
    energyLabel_->setText(
        tr("%1 eV (%2 Ha)").arg(eV, 0, 'f', 3).arg(ha, 0, 'f', 3));

    fermiLabel_->setText(
        optionalNumber(data_.value(QStringLiteral("fermi_eV")),
                       QStringLiteral(" eV"), 3));

    const double fmax = data_.value(QStringLiteral("fmax_eV_per_A")).toDouble();
    const int fmaxAtom = data_.value(QStringLiteral("fmax_atom")).toInt(-1);
    forceLabel_->setText(
        fmaxAtom >= 0
            ? tr("%1 eV/Å (Atom #%2)").arg(fmax, 0, 'f', 3).arg(fmaxAtom)
            : tr("%1 eV/Å").arg(fmax, 0, 'f', 3));

    // Total magnetic moment: a number for spin-polarized runs, "—" otherwise
    // (absent / null for unpolarized or non-collinear-vector results).
    magmomLabel_->setText(
        optionalNumber(data_.value(QStringLiteral("total_magnetic_moment")),
                       QStringLiteral(" μB"), 3));

    const QJsonObject scf = data_.value(QStringLiteral("scf")).toObject();
    QString scfText =
        optionalNumber(scf.value(QStringLiteral("iterations")), QString(), 0);
    if (scfText != QLatin1String("—"))
        scfText = tr("%1 SCF iterations").arg(scfText);
    else
        scfText = tr("iteration count not reported");
    scfLabel_->setText(
        tr("%1 · energy tolerance %2 eV · max %3 steps")
            .arg(scfText)
            .arg(scf.value(QStringLiteral("energy_tol_eV")).toDouble(), 0, 'g', 3)
            .arg(scf.value(QStringLiteral("max_steps")).toInt()));

    // Per-atom forces and converged moments, with the maximum-force site
    // highlighted.
    const QJsonArray forces =
        data_.value(QStringLiteral("forces_eV_per_A")).toArray();
    const QJsonArray moments =
        data_.value(QStringLiteral("magnetic_moments")).toArray();
    // Collinear runs report one number per atom; non-collinear reports a
    // vector, which is shown as its magnitude with the components in the tool
    // tip — the table has one column, and |m| is the quantity that answers
    // "is this site magnetic".
    const auto momentText = [](const QJsonValue& value) -> QPair<QString, QString> {
        if (value.isDouble())
            return {QString::number(value.toDouble(), 'f', 4), QString()};
        if (value.isArray()) {
            const QJsonArray v = value.toArray();
            double squared = 0.0;
            QStringList parts;
            for (const QJsonValue& c : v) {
                squared += c.toDouble() * c.toDouble();
                parts << QString::number(c.toDouble(), 'f', 4);
            }
            return {QString::number(std::sqrt(squared), 'f', 4),
                    QStringLiteral("(%1) μB").arg(parts.join(QStringLiteral(", ")))};
        }
        return {QStringLiteral("—"), QString()};
    };
    // The whole group is titled for what it actually contains, so an
    // unpolarized run does not advertise an empty moment column.
    const bool magnetic = !moments.isEmpty();
    forcesGroup_->setTitle(magnetic
                               ? tr("Per-atom results — forces (eV/Å) and "
                                    "converged magnetic moments (μB)")
                               : tr("Per-atom results — forces (eV/Å)"));
    forcesTable_->setColumnHidden(4, !magnetic);
    forcesTable_->setRowCount(forces.size());
    for (int row = 0; row < forces.size(); ++row) {
        const QJsonArray f = forces.at(row).toArray();
        const auto cell = [&](int col, const QString& text,
                              const QString& tip = QString()) {
            auto* item = new QTableWidgetItem(text);
            if (!tip.isEmpty())
                item->setToolTip(tip);
            if (row == fmaxAtom) {
                QFont bold = item->font();
                bold.setBold(true);
                item->setFont(bold);
                item->setForeground(QColor(0xd9, 0x53, 0x4f)); // highlight
            }
            forcesTable_->setItem(row, col, item);
        };
        cell(0, QString::number(row));
        for (int k = 0; k < 3 && k < f.size(); ++k)
            cell(k + 1, QString::number(f.at(k).toDouble(), 'f', 5));
        if (magnetic) {
            const auto [text, tip] = row < moments.size()
                ? momentText(moments.at(row))
                : QPair<QString, QString>{QStringLiteral("—"), QString()};
            cell(4, text, tip);
        }
    }
    if (fmaxAtom >= 0 && fmaxAtom < forcesTable_->rowCount())
        forcesTable_->scrollToItem(forcesTable_->item(fmaxAtom, 0));

    return true;
}

QString SinglePointViewer::plainTextSummary() const
{
    const double eV = data_.value(QStringLiteral("energy_eV")).toDouble();
    const double ha = data_.value(QStringLiteral("energy_Hartree"))
                          .toDouble(eV / 27.211386245988);
    const QJsonObject scf = data_.value(QStringLiteral("scf")).toObject();
    QString out;
    QTextStream s(&out);
    s << "Single-Point Calculation — summary\n"
      << "Total energy : " << QString::number(eV, 'f', 6) << " eV ("
      << QString::number(ha, 'f', 6) << " Ha)\n"
      << "Fermi energy : "
      << optionalNumber(data_.value(QStringLiteral("fermi_eV")),
                        QStringLiteral(" eV"), 4)
      << "\n"
      << "Max force    : "
      << QString::number(data_.value(QStringLiteral("fmax_eV_per_A")).toDouble(),
                         'f', 6)
      << " eV/A  (atom #"
      << data_.value(QStringLiteral("fmax_atom")).toInt(-1) << ")\n"
      << "Total moment : "
      << optionalNumber(data_.value(QStringLiteral("total_magnetic_moment")),
                        QStringLiteral(" uB"), 4)
      << "\n"
      << "SCF steps    : "
      << optionalNumber(scf.value(QStringLiteral("iterations")), QString(), 0)
      << "\n"
      << "Energy tol   : "
      << QString::number(scf.value(QStringLiteral("energy_tol_eV")).toDouble(),
                         'g', 3)
      << " eV\n"
      << "Atoms        : " << data_.value(QStringLiteral("natoms")).toInt()
      << "\n";
    return out;
}

void SinglePointViewer::copyToClipboard()
{
    QApplication::clipboard()->setText(plainTextSummary());
}

void SinglePointViewer::exportJson()
{
    const QString path = QFileDialog::getSaveFileName(
        this, tr("Export summary (JSON)"),
        QStringLiteral("single_point_summary.json"), tr("JSON (*.json)"));
    if (path.isEmpty())
        return;
    writeTextFile(this, path, [&](QTextStream& out) {
        out << QString::fromUtf8(QJsonDocument(data_).toJson(QJsonDocument::Indented));
    });
}

void SinglePointViewer::exportCsv()
{
    const QString path = QFileDialog::getSaveFileName(
        this, tr("Export summary (CSV)"),
        QStringLiteral("single_point_summary.csv"), tr("CSV (*.csv)"));
    if (path.isEmpty())
        return;
    writeTextFile(this, path, [&](QTextStream& out) {
        const QJsonObject scf = data_.value(QStringLiteral("scf")).toObject();
        out << "metric,value,unit\n";
        out << "total_energy," << data_.value(QStringLiteral("energy_eV")).toDouble()
            << ",eV\n";
        out << "total_energy,"
            << data_.value(QStringLiteral("energy_Hartree")).toDouble() << ",Hartree\n";
        const QJsonValue fermi = data_.value(QStringLiteral("fermi_eV"));
        if (!fermi.isNull() && !fermi.isUndefined())
            out << "fermi_energy," << fermi.toDouble() << ",eV\n";
        out << "max_force," << data_.value(QStringLiteral("fmax_eV_per_A")).toDouble()
            << ",eV/A\n";
        out << "max_force_atom," << data_.value(QStringLiteral("fmax_atom")).toInt(-1)
            << ",index\n";
        const QJsonValue magmom = data_.value(QStringLiteral("total_magnetic_moment"));
        if (!magmom.isNull() && !magmom.isUndefined())
            out << "total_magnetic_moment," << magmom.toDouble() << ",bohr_magneton\n";
        // Per-atom moments as their own rows: the total is zero for every
        // antiferromagnet, so a CSV carrying only the total loses the result.
        const QJsonArray moments =
            data_.value(QStringLiteral("magnetic_moments")).toArray();
        for (int i = 0; i < moments.size(); ++i) {
            const QJsonValue value = moments.at(i);
            if (value.isArray()) {
                const QJsonArray v = value.toArray();
                for (int k = 0; k < v.size(); ++k)
                    out << "magnetic_moment_" << i << "_"
                        << QLatin1Char(static_cast<char>('x' + k)) << ","
                        << v.at(k).toDouble() << ",bohr_magneton\n";
            } else {
                out << "magnetic_moment_" << i << "," << value.toDouble()
                    << ",bohr_magneton\n";
            }
        }
        out << "scf_iterations,"
            << scf.value(QStringLiteral("iterations")).toInt(-1) << ",count\n";
        out << "energy_tolerance,"
            << scf.value(QStringLiteral("energy_tol_eV")).toDouble() << ",eV\n";
        out << "natoms," << data_.value(QStringLiteral("natoms")).toInt() << ",count\n";
    });
}

} // namespace calango::gui
