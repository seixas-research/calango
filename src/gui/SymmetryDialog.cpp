#include "gui/SymmetryDialog.hpp"

#include "core/Element.hpp"
#include "python_bridge/AseBridge.hpp"

#include <QApplication>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QPushButton>
#include <QTableWidget>
#include <QVBoxLayout>

namespace calango::gui {

SymmetryDialog::SymmetryDialog(std::shared_ptr<const core::Structure> structure,
                               QWidget* parent)
    : QDialog(parent), structure_(std::move(structure))
{
    setWindowTitle(tr("Symmetry"));
    resize(520, 560);

    auto* layout = new QVBoxLayout(this);

    auto* topForm = new QFormLayout;
    layout->addLayout(topForm);
    tolSpin_ = new QDoubleSpinBox(this);
    tolSpin_->setDecimals(4);
    tolSpin_->setRange(0.0001, 1.0);
    tolSpin_->setSingleStep(0.0005);
    tolSpin_->setValue(0.001);
    tolSpin_->setSuffix(QStringLiteral(" Å"));
    tolSpin_->setToolTip(tr("Symmetry-finding tolerance (symprec): larger "
                            "values merge nearly-symmetric atoms."));
    auto* tolRow = new QHBoxLayout;
    tolRow->addWidget(tolSpin_, 1);
    auto* detectButton = new QPushButton(tr("Detect"), this);
    tolRow->addWidget(detectButton);
    topForm->addRow(tr("Tolerance:"), tolRow);
    connect(detectButton, &QPushButton::clicked, this, &SymmetryDialog::detect);

    auto* infoBox = new QGroupBox(tr("Symmetry"), this);
    auto* infoForm = new QFormLayout(infoBox);
    spaceGroupLabel_ = new QLabel(QStringLiteral("—"), infoBox);
    pointGroupLabel_ = new QLabel(QStringLiteral("—"), infoBox);
    crystalLabel_ = new QLabel(QStringLiteral("—"), infoBox);
    hallLabel_ = new QLabel(QStringLiteral("—"), infoBox);
    sitesLabel_ = new QLabel(QStringLiteral("—"), infoBox);
    infoForm->addRow(tr("Space group:"), spaceGroupLabel_);
    infoForm->addRow(tr("Point group:"), pointGroupLabel_);
    infoForm->addRow(tr("Crystal system:"), crystalLabel_);
    infoForm->addRow(tr("Hall number:"), hallLabel_);
    infoForm->addRow(tr("Inequivalent sites:"), sitesLabel_);
    layout->addWidget(infoBox);

    layout->addWidget(new QLabel(tr("Equivalent atomic positions (fractional):"),
                                 this));
    table_ = new QTableWidget(0, 6, this);
    table_->setHorizontalHeaderLabels(
        {tr("#"), tr("Element"), QStringLiteral("x"), QStringLiteral("y"),
         QStringLiteral("z"), tr("Wyckoff")});
    table_->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    table_->verticalHeader()->setVisible(false);
    table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    layout->addWidget(table_, 1);

    statusLabel_ = new QLabel(this);
    statusLabel_->setWordWrap(true);
    layout->addWidget(statusLabel_);

    // Inspection only: the "Standardize Cell" / "Reduce to Primitive Cell"
    // transforms moved to the Edit Structure dialog, which owns cell editing.
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    detect();
}

void SymmetryDialog::detect()
{
    QApplication::setOverrideCursor(Qt::WaitCursor);
    const auto info =
        pybridge::AseBridge::symmetryInfo(*structure_, tolSpin_->value());
    QApplication::restoreOverrideCursor();

    const bool ok = info.error.empty();
    if (!ok) {
        const QString reason = QString::fromStdString(info.error);
        for (QLabel* l : {spaceGroupLabel_, pointGroupLabel_, crystalLabel_,
                          hallLabel_, sitesLabel_})
            l->setText(QStringLiteral("—"));
        table_->setRowCount(0);
        statusLabel_->setText(tr("No symmetry: %1").arg(reason));
        return;
    }

    spaceGroupLabel_->setText(QStringLiteral("%1 (No. %2)")
                                  .arg(QString::fromStdString(info.spaceGroupSymbol))
                                  .arg(info.spaceGroupNumber));
    pointGroupLabel_->setText(QString::fromStdString(info.pointGroup));
    crystalLabel_->setText(QString::fromStdString(info.crystalSystem));
    hallLabel_->setText(info.hallNumber > 0 ? QString::number(info.hallNumber)
                                            : QStringLiteral("—"));
    sitesLabel_->setText(QString::number(info.uniqueSites));
    statusLabel_->clear();

    const auto& atoms = structure_->atoms();
    const auto& cell = structure_->cell();
    table_->setRowCount(static_cast<int>(atoms.size()));
    for (int i = 0; i < static_cast<int>(atoms.size()); ++i) {
        const core::Vec3 frac = cell.cartesianToFractional(atoms[i].position);
        const auto set = [&](int col, const QString& text) {
            table_->setItem(i, col, new QTableWidgetItem(text));
        };
        set(0, QString::number(i));
        set(1, QLatin1String(core::Elements::data(atoms[i].atomicNumber).symbol));
        set(2, QString::number(frac.x, 'f', 4));
        set(3, QString::number(frac.y, 'f', 4));
        set(4, QString::number(frac.z, 'f', 4));
        const QString wyckoff =
            i < static_cast<int>(info.wyckoffLetters.size())
            ? QString::fromStdString(info.wyckoffLetters[static_cast<std::size_t>(i)])
            : QString();
        // Append the equivalence class so users can see which atoms are related.
        const int cls = i < static_cast<int>(info.equivalentAtoms.size())
            ? info.equivalentAtoms[static_cast<std::size_t>(i)]
            : -1;
        set(5, cls >= 0 ? tr("%1  (class %2)").arg(wyckoff).arg(cls) : wyckoff);
    }
}

} // namespace calango::gui
