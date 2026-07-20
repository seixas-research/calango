#include "gui/NanoBuilderDialog.hpp"

#include "python_bridge/AseBridge.hpp"

#include <QDialogButtonBox>
#include <QFormLayout>
#include <QMessageBox>
#include <QPushButton>
#include <QVBoxLayout>

namespace calango::gui {

NanoBuilderDialog::NanoBuilderDialog(QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle(tr("Nanomaterial Builder"));

    typeCombo_ = new QComboBox(this);
    typeCombo_->addItems({tr("Graphene sheet"), tr("Graphene nanoribbon"),
                          tr("Carbon nanotube"), tr("TMD monolayer (MX₂)")});
    pages_ = new QStackedWidget(this);
    connect(typeCombo_, &QComboBox::currentIndexChanged,
            pages_, &QStackedWidget::setCurrentIndex);

    const auto makeVacuumSpin = [this](double value) {
        auto* spin = new QDoubleSpinBox(this);
        spin->setRange(0.0, 60.0);
        spin->setValue(value);
        spin->setSuffix(tr(" Å"));
        return spin;
    };

    // --- Graphene sheet -----------------------------------------------------
    {
        auto* page = new QWidget(this);
        auto* form = new QFormLayout(page);
        sheetASpin_ = new QDoubleSpinBox(page);
        sheetASpin_->setRange(2.0, 3.0);
        sheetASpin_->setDecimals(3);
        sheetASpin_->setValue(2.46);
        sheetASpin_->setSuffix(tr(" Å"));
        sheetNxSpin_ = new QSpinBox(page);
        sheetNxSpin_->setRange(1, 40);
        sheetNxSpin_->setValue(4);
        sheetNySpin_ = new QSpinBox(page);
        sheetNySpin_->setRange(1, 40);
        sheetNySpin_->setValue(4);
        sheetVacuumSpin_ = makeVacuumSpin(10.0);
        form->addRow(tr("Lattice constant a:"), sheetASpin_);
        form->addRow(tr("Repeat nx:"), sheetNxSpin_);
        form->addRow(tr("Repeat ny:"), sheetNySpin_);
        form->addRow(tr("Vacuum:"), sheetVacuumSpin_);
        pages_->addWidget(page);
    }

    // --- Nanoribbon -----------------------------------------------------------
    {
        auto* page = new QWidget(this);
        auto* form = new QFormLayout(page);
        ribbonWidthSpin_ = new QSpinBox(page);
        ribbonWidthSpin_->setRange(1, 60);
        ribbonWidthSpin_->setValue(6);
        ribbonLengthSpin_ = new QSpinBox(page);
        ribbonLengthSpin_->setRange(1, 60);
        ribbonLengthSpin_->setValue(6);
        ribbonEdgeCombo_ = new QComboBox(page);
        ribbonEdgeCombo_->addItems({tr("Zigzag"), tr("Armchair")});
        ribbonSaturateCheck_ = new QCheckBox(tr("Hydrogen-terminate edges"), page);
        ribbonSaturateCheck_->setChecked(true);
        ribbonVacuumSpin_ = makeVacuumSpin(10.0);
        form->addRow(tr("Width (unit cells):"), ribbonWidthSpin_);
        form->addRow(tr("Length (unit cells):"), ribbonLengthSpin_);
        form->addRow(tr("Edge type:"), ribbonEdgeCombo_);
        form->addRow(ribbonSaturateCheck_);
        form->addRow(tr("Vacuum:"), ribbonVacuumSpin_);
        pages_->addWidget(page);
    }

    // --- Nanotube ---------------------------------------------------------------
    {
        auto* page = new QWidget(this);
        auto* form = new QFormLayout(page);
        tubeNSpin_ = new QSpinBox(page);
        tubeNSpin_->setRange(0, 40);
        tubeNSpin_->setValue(6);
        tubeMSpin_ = new QSpinBox(page);
        tubeMSpin_->setRange(0, 40);
        tubeMSpin_->setValue(6);
        tubeLengthSpin_ = new QSpinBox(page);
        tubeLengthSpin_->setRange(1, 40);
        tubeLengthSpin_->setValue(4);
        tubeBondSpin_ = new QDoubleSpinBox(page);
        tubeBondSpin_->setRange(1.2, 1.7);
        tubeBondSpin_->setDecimals(3);
        tubeBondSpin_->setValue(1.42);
        tubeBondSpin_->setSuffix(tr(" Å"));
        tubeVacuumSpin_ = makeVacuumSpin(8.0);
        form->addRow(tr("Chiral index n:"), tubeNSpin_);
        form->addRow(tr("Chiral index m:"), tubeMSpin_);
        form->addRow(tr("Length (unit cells):"), tubeLengthSpin_);
        form->addRow(tr("C-C bond:"), tubeBondSpin_);
        form->addRow(tr("Vacuum:"), tubeVacuumSpin_);
        pages_->addWidget(page);
    }

    // --- TMD (mx2) ---------------------------------------------------------------
    {
        auto* page = new QWidget(this);
        auto* form = new QFormLayout(page);
        tmdFormulaCombo_ = new QComboBox(page);
        tmdFormulaCombo_->setEditable(true); // any MX2 formula
        tmdFormulaCombo_->addItems({QStringLiteral("MoS2"), QStringLiteral("MoSe2"),
                                    QStringLiteral("WS2"), QStringLiteral("WSe2"),
                                    QStringLiteral("MoTe2"), QStringLiteral("NbSe2")});
        tmdPhaseCombo_ = new QComboBox(page);
        tmdPhaseCombo_->addItems({QStringLiteral("2H"), QStringLiteral("1T")});
        tmdASpin_ = new QDoubleSpinBox(page);
        tmdASpin_->setRange(2.5, 4.5);
        tmdASpin_->setDecimals(3);
        tmdASpin_->setValue(3.16);
        tmdASpin_->setSuffix(tr(" Å"));
        tmdThicknessSpin_ = new QDoubleSpinBox(page);
        tmdThicknessSpin_->setRange(2.0, 5.0);
        tmdThicknessSpin_->setDecimals(3);
        tmdThicknessSpin_->setValue(3.19);
        tmdThicknessSpin_->setSuffix(tr(" Å"));
        tmdNxSpin_ = new QSpinBox(page);
        tmdNxSpin_->setRange(1, 40);
        tmdNxSpin_->setValue(4);
        tmdNySpin_ = new QSpinBox(page);
        tmdNySpin_->setRange(1, 40);
        tmdNySpin_->setValue(4);
        tmdVacuumSpin_ = makeVacuumSpin(10.0);
        form->addRow(tr("Formula (MX₂):"), tmdFormulaCombo_);
        form->addRow(tr("Phase:"), tmdPhaseCombo_);
        form->addRow(tr("Lattice constant a:"), tmdASpin_);
        form->addRow(tr("X-X thickness:"), tmdThicknessSpin_);
        form->addRow(tr("Repeat nx:"), tmdNxSpin_);
        form->addRow(tr("Repeat ny:"), tmdNySpin_);
        form->addRow(tr("Vacuum:"), tmdVacuumSpin_);
        pages_->addWidget(page);
    }

    auto* buttons = new QDialogButtonBox(this);
    auto* buildButton = buttons->addButton(tr("Build"), QDialogButtonBox::AcceptRole);
    buttons->addButton(QDialogButtonBox::Cancel);
    buildButton->setDefault(true);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    // Build validates first; accept() only on success (see build()).
    disconnect(buttons, &QDialogButtonBox::accepted, nullptr, nullptr);
    connect(buildButton, &QPushButton::clicked, this, &NanoBuilderDialog::build);

    auto* layout = new QVBoxLayout(this);
    layout->addWidget(typeCombo_);
    layout->addWidget(pages_);
    layout->addWidget(buttons);
}

void NanoBuilderDialog::build()
{
    try {
        switch (typeCombo_->currentIndex()) {
        case 0:
            result_ = std::make_shared<core::Structure>(pybridge::AseBridge::buildGraphene(
                sheetASpin_->value(), sheetNxSpin_->value(), sheetNySpin_->value(),
                sheetVacuumSpin_->value()));
            resultName_ = tr("Graphene %1×%2")
                              .arg(sheetNxSpin_->value())
                              .arg(sheetNySpin_->value());
            break;
        case 1: {
            const bool zigzag = ribbonEdgeCombo_->currentIndex() == 0;
            result_ = std::make_shared<core::Structure>(pybridge::AseBridge::buildNanoribbon(
                ribbonWidthSpin_->value(), ribbonLengthSpin_->value(), zigzag,
                ribbonSaturateCheck_->isChecked(), ribbonVacuumSpin_->value()));
            resultName_ = tr("%1 GNR %2×%3")
                              .arg(zigzag ? tr("Zigzag") : tr("Armchair"))
                              .arg(ribbonWidthSpin_->value())
                              .arg(ribbonLengthSpin_->value());
            break;
        }
        case 2:
            if (tubeNSpin_->value() == 0 && tubeMSpin_->value() == 0) {
                QMessageBox::warning(this, windowTitle(),
                                     tr("Chiral indices (0, 0) are not a nanotube."));
                return;
            }
            result_ = std::make_shared<core::Structure>(pybridge::AseBridge::buildNanotube(
                tubeNSpin_->value(), tubeMSpin_->value(), tubeLengthSpin_->value(),
                tubeBondSpin_->value(), tubeVacuumSpin_->value()));
            resultName_ = tr("CNT (%1,%2)")
                              .arg(tubeNSpin_->value())
                              .arg(tubeMSpin_->value());
            break;
        default:
            result_ = std::make_shared<core::Structure>(pybridge::AseBridge::buildMx2(
                tmdFormulaCombo_->currentText().trimmed().toStdString(),
                tmdPhaseCombo_->currentText().toStdString(), tmdASpin_->value(),
                tmdThicknessSpin_->value(), tmdNxSpin_->value(), tmdNySpin_->value(),
                tmdVacuumSpin_->value()));
            resultName_ = tr("%1 %2 monolayer")
                              .arg(tmdFormulaCombo_->currentText().trimmed(),
                                   tmdPhaseCombo_->currentText());
            break;
        }
        accept();
    } catch (const std::exception& e) {
        QMessageBox::critical(this, windowTitle(), QString::fromUtf8(e.what()));
    }
}

} // namespace calango::gui
