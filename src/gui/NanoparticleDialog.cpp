#include "gui/NanoparticleDialog.hpp"

#include "core/Element.hpp"
#include "gui/PeriodicTableDialog.hpp"
#include "python_bridge/SurfaceScience.hpp"

#include <QApplication>
#include <QButtonGroup>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QPushButton>
#include <QRadioButton>
#include <QSpinBox>
#include <QStackedWidget>
#include <QTableWidget>
#include <QVBoxLayout>

namespace calango::gui {

NanoparticleDialog::NanoparticleDialog(QWidget* parent) : QDialog(parent)
{
    setWindowTitle(tr("Nanoparticle & Cluster Builder"));
    resize(480, 580);

    auto* root = new QVBoxLayout(this);
    stack_ = new QStackedWidget(this);
    root->addWidget(stack_, 1);

    // ======================= Stage 1: method ==============================
    auto* methodPage = new QWidget(this);
    auto* methodLayout = new QVBoxLayout(methodPage);
    auto* methodTitle = new QLabel(tr("<b>Stage 1 of 2 — Choose a method</b>"),
                                   methodPage);
    methodLayout->addWidget(methodTitle);

    wulffRadio_ = new QRadioButton(
        tr("Wulff Construction (Thermodynamic Equilibrium)"), methodPage);
    wulffRadio_->setChecked(true);
    auto* wulffHelp = new QLabel(
        tr("Equilibrium crystal shape minimizing total surface energy, built "
           "from per-facet surface-energy ratios γ(hkl) and a target size."),
        methodPage);
    wulffHelp->setWordWrap(true);
    wulffHelp->setIndent(24);
    wulffHelp->setEnabled(false);

    clusterRadio_ = new QRadioButton(tr("Symmetric Crystal Cluster"), methodPage);
    auto* clusterHelp = new QLabel(
        tr("Closed-shell magic clusters and carved crystallites: icosahedron, "
           "dodecahedron, cuboctahedron, octahedron, decahedron, or a "
           "spherical FCC/BCC/HCP cluster, sized by shell / layer count."),
        methodPage);
    clusterHelp->setWordWrap(true);
    clusterHelp->setIndent(24);
    clusterHelp->setEnabled(false);

    auto* methodGroup = new QButtonGroup(this);
    methodGroup->addButton(wulffRadio_);
    methodGroup->addButton(clusterRadio_);

    methodLayout->addSpacing(6);
    methodLayout->addWidget(wulffRadio_);
    methodLayout->addWidget(wulffHelp);
    methodLayout->addSpacing(10);
    methodLayout->addWidget(clusterRadio_);
    methodLayout->addWidget(clusterHelp);
    methodLayout->addStretch(1);
    stack_->addWidget(methodPage);

    // ======================= Stage 2: parameters ==========================
    auto* paramPage = new QWidget(this);
    auto* paramLayout = new QVBoxLayout(paramPage);
    stageTitle_ = new QLabel(paramPage);
    paramLayout->addWidget(stageTitle_);

    auto* sharedForm = new QFormLayout;
    paramLayout->addLayout(sharedForm);
    elementButton_ = new QPushButton(paramPage);
    const auto updateElement = [this] {
        elementButton_->setText(
            QStringLiteral("%1  (Z = %2)")
                .arg(QLatin1String(core::Elements::data(elementZ_).symbol))
                .arg(elementZ_));
    };
    updateElement();
    connect(elementButton_, &QPushButton::clicked, this, [this, updateElement] {
        if (const int z = PeriodicTableDialog::pickElement(this, elementZ_)) {
            elementZ_ = z;
            updateElement();
        }
    });
    sharedForm->addRow(tr("Element:"), elementButton_);

    latticeConstantSpin_ = new QDoubleSpinBox(paramPage);
    latticeConstantSpin_->setRange(0.0, 12.0);
    latticeConstantSpin_->setDecimals(4);
    latticeConstantSpin_->setSpecialValueText(tr("default"));
    latticeConstantSpin_->setValue(0.0);
    latticeConstantSpin_->setSuffix(QStringLiteral(" Å"));
    latticeConstantSpin_->setToolTip(
        tr("Cubic lattice constant; leave at default to use the tabulated "
           "reference value for the element."));
    sharedForm->addRow(tr("Lattice constant:"), latticeConstantSpin_);

    // --- Wulff section -----------------------------------------------------
    wulffSection_ = new QGroupBox(tr("Surface energies && target size"), paramPage);
    auto* wulffLayout = new QVBoxLayout(wulffSection_);
    wulffLayout->addWidget(new QLabel(
        tr("Facet surface energies γ(h k l) — only ratios matter:"), wulffSection_));
    facetTable_ = new QTableWidget(3, 4, wulffSection_);
    facetTable_->setHorizontalHeaderLabels(
        {QStringLiteral("h"), QStringLiteral("k"), QStringLiteral("l"),
         tr("γ (relative)")});
    facetTable_->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    facetTable_->verticalHeader()->setVisible(false);
    const int defaults[3][3] = {{1, 1, 1}, {1, 0, 0}, {1, 1, 0}};
    const double energies[3] = {1.0, 1.1, 1.2};
    for (int row = 0; row < 3; ++row) {
        for (int col = 0; col < 3; ++col)
            facetTable_->setItem(
                row, col, new QTableWidgetItem(QString::number(defaults[row][col])));
        facetTable_->setItem(
            row, 3, new QTableWidgetItem(QString::number(energies[row], 'f', 2)));
    }
    wulffLayout->addWidget(facetTable_);
    auto* facetButtons = new QHBoxLayout;
    auto* addFacet = new QPushButton(tr("Add Facet"), wulffSection_);
    auto* removeFacet = new QPushButton(tr("Remove Selected"), wulffSection_);
    facetButtons->addWidget(addFacet);
    facetButtons->addWidget(removeFacet);
    facetButtons->addStretch(1);
    wulffLayout->addLayout(facetButtons);
    connect(addFacet, &QPushButton::clicked, this, [this] {
        const int row = facetTable_->rowCount();
        facetTable_->insertRow(row);
        for (int col = 0; col < 3; ++col)
            facetTable_->setItem(row, col, new QTableWidgetItem(QStringLiteral("1")));
        facetTable_->setItem(row, 3, new QTableWidgetItem(QStringLiteral("1.00")));
    });
    connect(removeFacet, &QPushButton::clicked, this, [this] {
        if (facetTable_->currentRow() >= 0 && facetTable_->rowCount() > 1)
            facetTable_->removeRow(facetTable_->currentRow());
    });
    auto* wulffForm = new QFormLayout;
    wulffLayout->addLayout(wulffForm);
    wulffLatticeCombo_ = new QComboBox(wulffSection_);
    wulffLatticeCombo_->addItems({QStringLiteral("fcc"), QStringLiteral("bcc"),
                                  QStringLiteral("sc")});
    wulffForm->addRow(tr("Crystal structure:"), wulffLatticeCombo_);
    sizeSpin_ = new QSpinBox(wulffSection_);
    sizeSpin_->setRange(10, 100000);
    sizeSpin_->setValue(200);
    wulffForm->addRow(tr("Target size (atoms):"), sizeSpin_);
    roundingCombo_ = new QComboBox(wulffSection_);
    roundingCombo_->addItems({QStringLiteral("closest"), QStringLiteral("above"),
                              QStringLiteral("below")});
    wulffForm->addRow(tr("Size rounding:"), roundingCombo_);
    paramLayout->addWidget(wulffSection_);

    // --- Cluster section ---------------------------------------------------
    clusterSection_ = new QGroupBox(tr("Cluster geometry"), paramPage);
    auto* clusterForm = new QFormLayout(clusterSection_);
    clusterShapeCombo_ = new QComboBox(clusterSection_);
    clusterShapeCombo_->addItem(tr("Icosahedron"), QStringLiteral("icosahedron"));
    clusterShapeCombo_->addItem(tr("Dodecahedron"),
                                QStringLiteral("rhombic-dodecahedron"));
    clusterShapeCombo_->addItem(tr("Cuboctahedron"),
                                QStringLiteral("cuboctahedron"));
    clusterShapeCombo_->addItem(tr("Octahedron"), QStringLiteral("octahedron"));
    clusterShapeCombo_->addItem(tr("Decahedron"), QStringLiteral("decahedron"));
    clusterShapeCombo_->addItem(tr("Spherical cluster (FCC / BCC / HCP)"),
                                QString());
    clusterForm->addRow(tr("Shape:"), clusterShapeCombo_);

    shellSpin_ = new QSpinBox(clusterSection_);
    shellSpin_->setRange(1, 60);
    shellSpin_->setValue(3);
    shellSpin_->setToolTip(tr("Number of atomic shells / edge length / layers "
                              "that sets the cluster size."));
    clusterForm->addRow(tr("Shells / layers:"), shellSpin_);

    auto* decaWidget = new QWidget(clusterSection_);
    auto* decaRow = new QHBoxLayout(decaWidget);
    decaRow->setContentsMargins(0, 0, 0, 0);
    decaPSpin_ = new QSpinBox(decaWidget);
    decaPSpin_->setRange(1, 40);
    decaPSpin_->setValue(3);
    decaQSpin_ = new QSpinBox(decaWidget);
    decaQSpin_->setRange(1, 40);
    decaQSpin_->setValue(3);
    decaRSpin_ = new QSpinBox(decaWidget);
    decaRSpin_->setRange(0, 40);
    decaRSpin_->setValue(0);
    decaRow->addWidget(new QLabel(QStringLiteral("p"), decaWidget));
    decaRow->addWidget(decaPSpin_);
    decaRow->addWidget(new QLabel(QStringLiteral("q"), decaWidget));
    decaRow->addWidget(decaQSpin_);
    decaRow->addWidget(new QLabel(QStringLiteral("r"), decaWidget));
    decaRow->addWidget(decaRSpin_);
    clusterForm->addRow(tr("Decahedron p / q / r:"), decaWidget);

    sphericalLatticeCombo_ = new QComboBox(clusterSection_);
    sphericalLatticeCombo_->addItems({QStringLiteral("fcc"), QStringLiteral("bcc"),
                                      QStringLiteral("hcp")});
    clusterForm->addRow(tr("Crystal structure:"), sphericalLatticeCombo_);
    radiusSpin_ = new QDoubleSpinBox(clusterSection_);
    radiusSpin_->setRange(2.0, 100.0);
    radiusSpin_->setValue(10.0);
    radiusSpin_->setSuffix(QStringLiteral(" Å"));
    clusterForm->addRow(tr("Radius:"), radiusSpin_);
    paramLayout->addWidget(clusterSection_);

    connect(clusterShapeCombo_, &QComboBox::currentIndexChanged, this,
            &NanoparticleDialog::syncClusterControls);

    statusLabel_ = new QLabel(paramPage);
    statusLabel_->setWordWrap(true);
    paramLayout->addWidget(statusLabel_);
    paramLayout->addStretch(1);
    stack_->addWidget(paramPage);

    // ======================= Navigation bar ===============================
    auto* navRow = new QHBoxLayout;
    backButton_ = new QPushButton(tr("‹ Back"), this);
    nextButton_ = new QPushButton(tr("Next ›"), this);
    generateButton_ = new QPushButton(tr("Generate"), this);
    generateButton_->setDefault(true);
    auto* cancelButton = new QPushButton(tr("Cancel"), this);
    navRow->addWidget(backButton_);
    navRow->addStretch(1);
    navRow->addWidget(cancelButton);
    navRow->addWidget(nextButton_);
    navRow->addWidget(generateButton_);
    root->addLayout(navRow);

    connect(cancelButton, &QPushButton::clicked, this, &QDialog::reject);
    connect(nextButton_, &QPushButton::clicked, this,
            &NanoparticleDialog::showParameters);
    connect(backButton_, &QPushButton::clicked, this,
            &NanoparticleDialog::showMethod);
    connect(generateButton_, &QPushButton::clicked, this,
            &NanoparticleDialog::generate);

    showMethod(); // start on Stage 1
}

void NanoparticleDialog::syncClusterControls()
{
    const std::string shape = clusterShape();
    const bool spherical = shape.empty();
    const bool decahedron = shape == "decahedron";
    const bool usesShell = !spherical && !decahedron;

    auto* form = qobject_cast<QFormLayout*>(clusterSection_->layout());
    if (form) {
        form->setRowVisible(shellSpin_, usesShell);
        form->setRowVisible(decaPSpin_->parentWidget(), decahedron);
        form->setRowVisible(sphericalLatticeCombo_, spherical);
        form->setRowVisible(radiusSpin_, spherical);
    }
}

void NanoparticleDialog::showMethod()
{
    stack_->setCurrentIndex(0);
    backButton_->setVisible(false);
    nextButton_->setVisible(true);
    generateButton_->setVisible(false);
}

void NanoparticleDialog::showParameters()
{
    const bool wulff = wulffChosen();
    stageTitle_->setText(
        wulff ? tr("<b>Stage 2 of 2 — Wulff construction parameters</b>")
              : tr("<b>Stage 2 of 2 — Symmetric cluster parameters</b>"));
    wulffSection_->setVisible(wulff);
    clusterSection_->setVisible(!wulff);
    if (!wulff)
        syncClusterControls();
    statusLabel_->clear();

    stack_->setCurrentIndex(1);
    backButton_->setVisible(true);
    nextButton_->setVisible(false);
    generateButton_->setVisible(true);
}

bool NanoparticleDialog::wulffChosen() const
{
    return wulffRadio_->isChecked();
}

std::string NanoparticleDialog::clusterShape() const
{
    return clusterShapeCombo_->currentData().toString().toStdString();
}

QString NanoparticleDialog::resultName() const
{
    QString shapeName;
    if (wulffChosen())
        shapeName = tr("Wulff");
    else if (clusterShape().empty())
        shapeName = tr("spherical");
    else
        shapeName = clusterShapeCombo_->currentText();
    return tr("%1 nanoparticle (%2, %3 atoms)")
        .arg(QLatin1String(core::Elements::data(elementZ_).symbol), shapeName)
        .arg(result_ ? static_cast<int>(result_->size()) : 0);
}

void NanoparticleDialog::generate()
{
    const std::string symbol = core::Elements::data(elementZ_).symbol;
    QApplication::setOverrideCursor(Qt::WaitCursor);
    try {
        if (wulffChosen()) {
            std::vector<pybridge::SurfaceScience::WulffFacet> facets;
            for (int row = 0; row < facetTable_->rowCount(); ++row) {
                pybridge::SurfaceScience::WulffFacet facet;
                facet.h = facetTable_->item(row, 0)->text().toInt();
                facet.k = facetTable_->item(row, 1)->text().toInt();
                facet.l = facetTable_->item(row, 2)->text().toInt();
                facet.energy = facetTable_->item(row, 3)->text().toDouble();
                if (facet.energy > 0.0
                    && (facet.h != 0 || facet.k != 0 || facet.l != 0))
                    facets.push_back(facet);
            }
            if (facets.empty())
                throw std::runtime_error(
                    "enter at least one facet with a positive energy");
            result_ = pybridge::SurfaceScience::wulffNanoparticle(
                symbol, wulffLatticeCombo_->currentText().toStdString(), facets,
                sizeSpin_->value(), latticeConstantSpin_->value(),
                roundingCombo_->currentText().toStdString());
        } else if (const std::string shape = clusterShape(); !shape.empty()) {
            result_ = pybridge::SurfaceScience::polyhedralNanoparticle(
                symbol, shape, shellSpin_->value(), decaPSpin_->value(),
                decaQSpin_->value(), decaRSpin_->value(),
                latticeConstantSpin_->value());
        } else {
            result_ = pybridge::SurfaceScience::sphericalNanoparticle(
                symbol, sphericalLatticeCombo_->currentText().toStdString(),
                radiusSpin_->value(), latticeConstantSpin_->value());
        }
        QApplication::restoreOverrideCursor();
        accept();
    } catch (const std::exception& e) {
        QApplication::restoreOverrideCursor();
        statusLabel_->setText(QString::fromUtf8(e.what()));
    }
}

} // namespace calango::gui
