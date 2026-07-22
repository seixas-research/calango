#include "gui/NanoparticleDialog.hpp"

#include "core/Element.hpp"
#include "gui/PeriodicTableDialog.hpp"
#include "python_bridge/SurfaceScience.hpp"

#include <QApplication>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QPushButton>
#include <QSpinBox>
#include <QTableWidget>
#include <QVBoxLayout>

namespace calango::gui {

NanoparticleDialog::NanoparticleDialog(QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle(tr("Nanoparticle & Cluster Builder"));
    resize(460, 560);

    auto* layout = new QVBoxLayout(this);
    auto* form = new QFormLayout;
    layout->addLayout(form);

    elementButton_ = new QPushButton(this);
    const auto updateElement = [this] {
        elementButton_->setText(QStringLiteral("%1  (Z = %2)").arg(
            QLatin1String(core::Elements::data(elementZ_).symbol)).arg(elementZ_));
    };
    updateElement();
    connect(elementButton_, &QPushButton::clicked, this, [this, updateElement] {
        if (const int z = PeriodicTableDialog::pickElement(this, elementZ_)) {
            elementZ_ = z;
            updateElement();
        }
    });
    form->addRow(tr("Element:"), elementButton_);

    modeCombo_ = new QComboBox(this);
    // Order must match shapeForMode(): 0 Wulff, 1 spherical, then the faceted
    // ase.cluster shapes.
    modeCombo_->addItems({tr("Wulff construction (equilibrium shape)"),
                          tr("Spherical cluster (cutoff radius)"),
                          tr("Icosahedron (ase.cluster)"),
                          tr("Octahedron (ase.cluster)"),
                          tr("Cuboctahedron (truncated octahedron)"),
                          tr("Decahedron (ase.cluster)"),
                          tr("Rhombic dodecahedron ({110} facets)")});
    form->addRow(tr("Mode:"), modeCombo_);

    latticeCombo_ = new QComboBox(this);
    latticeCombo_->addItems({QStringLiteral("fcc"), QStringLiteral("bcc"),
                             QStringLiteral("sc"), QStringLiteral("hcp")});
    latticeCombo_->setToolTip(tr("Wulff construction supports fcc/bcc/sc; "
                                 "spherical clusters also hcp"));
    form->addRow(tr("Lattice:"), latticeCombo_);

    latticeConstantSpin_ = new QDoubleSpinBox(this);
    latticeConstantSpin_->setRange(0.0, 12.0);
    latticeConstantSpin_->setDecimals(4);
    latticeConstantSpin_->setSpecialValueText(tr("ASE default"));
    latticeConstantSpin_->setValue(0.0);
    latticeConstantSpin_->setSuffix(QStringLiteral(" Å"));
    form->addRow(tr("Lattice constant:"), latticeConstantSpin_);

    // --- Wulff facets ------------------------------------------------------
    facetTable_ = new QTableWidget(3, 4, this);
    facetTable_->setHorizontalHeaderLabels(
        {QStringLiteral("h"), QStringLiteral("k"), QStringLiteral("l"),
         tr("γ (relative)")});
    facetTable_->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    facetTable_->verticalHeader()->setVisible(false);
    const int defaults[3][3] = {{1, 1, 1}, {1, 0, 0}, {1, 1, 0}};
    const double energies[3] = {1.0, 1.1, 1.2};
    for (int row = 0; row < 3; ++row) {
        for (int col = 0; col < 3; ++col)
            facetTable_->setItem(row, col,
                                 new QTableWidgetItem(
                                     QString::number(defaults[row][col])));
        facetTable_->setItem(row, 3,
                             new QTableWidgetItem(
                                 QString::number(energies[row], 'f', 2)));
    }
    layout->addWidget(new QLabel(tr("Facet surface energies (Wulff):"), this));
    layout->addWidget(facetTable_, 1);
    auto* facetButtons = new QHBoxLayout;
    auto* addFacet = new QPushButton(tr("Add Facet"), this);
    auto* removeFacet = new QPushButton(tr("Remove Selected"), this);
    facetButtons->addWidget(addFacet);
    facetButtons->addWidget(removeFacet);
    facetButtons->addStretch(1);
    layout->addLayout(facetButtons);
    connect(addFacet, &QPushButton::clicked, this, [this] {
        const int row = facetTable_->rowCount();
        facetTable_->insertRow(row);
        for (int col = 0; col < 3; ++col)
            facetTable_->setItem(row, col,
                                 new QTableWidgetItem(QStringLiteral("1")));
        facetTable_->setItem(row, 3,
                             new QTableWidgetItem(QStringLiteral("1.00")));
    });
    connect(removeFacet, &QPushButton::clicked, this, [this] {
        if (facetTable_->currentRow() >= 0 && facetTable_->rowCount() > 1)
            facetTable_->removeRow(facetTable_->currentRow());
    });

    auto* sizeForm = new QFormLayout;
    layout->addLayout(sizeForm);
    sizeSpin_ = new QSpinBox(this);
    sizeSpin_->setRange(10, 100000);
    sizeSpin_->setValue(200);
    sizeForm->addRow(tr("Target atoms (Wulff):"), sizeSpin_);
    roundingCombo_ = new QComboBox(this);
    roundingCombo_->addItems({QStringLiteral("closest"), QStringLiteral("above"),
                              QStringLiteral("below")});
    sizeForm->addRow(tr("Size rounding:"), roundingCombo_);
    radiusSpin_ = new QDoubleSpinBox(this);
    radiusSpin_->setRange(2.0, 100.0);
    radiusSpin_->setValue(10.0);
    radiusSpin_->setSuffix(QStringLiteral(" Å"));
    sizeForm->addRow(tr("Radius (spherical):"), radiusSpin_);

    // --- Faceted ase.cluster shapes ---------------------------------------
    shellSpin_ = new QSpinBox(this);
    shellSpin_->setRange(1, 60);
    shellSpin_->setValue(3);
    shellSpin_->setToolTip(tr("Shells (icosahedron), edge length in atoms "
                              "(octahedron / cuboctahedron), or {110} layer "
                              "count (rhombic dodecahedron)."));
    sizeForm->addRow(tr("Shells / edge / layers:"), shellSpin_);

    auto* decaRow = new QHBoxLayout;
    decaPSpin_ = new QSpinBox(this);
    decaPSpin_->setRange(1, 40);
    decaPSpin_->setValue(3);
    decaQSpin_ = new QSpinBox(this);
    decaQSpin_->setRange(1, 40);
    decaQSpin_->setValue(3);
    decaRSpin_ = new QSpinBox(this);
    decaRSpin_->setRange(0, 40);
    decaRSpin_->setValue(0);
    decaRow->addWidget(new QLabel(QStringLiteral("p"), this));
    decaRow->addWidget(decaPSpin_);
    decaRow->addWidget(new QLabel(QStringLiteral("q"), this));
    decaRow->addWidget(decaQSpin_);
    decaRow->addWidget(new QLabel(QStringLiteral("r"), this));
    decaRow->addWidget(decaRSpin_);
    sizeForm->addRow(tr("Decahedron p / q / r:"), decaRow);

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
    connect(generateButton, &QPushButton::clicked,
            this, &NanoparticleDialog::generate);

    const auto syncMode = [this](int mode) {
        const bool wulff = mode == 0;
        const bool spherical = mode == 1;
        const std::string shape = shapeForMode(mode);
        const bool faceted = !shape.empty();
        const bool decahedron = shape == "decahedron";
        // Icosahedron/octa/cubocta/rhombic-dodeca take the single "shells /
        // edge / layers" count; the decahedron takes p/q/r instead.
        const bool usesShell = faceted && !decahedron;

        facetTable_->setEnabled(wulff);
        sizeSpin_->setEnabled(wulff);
        roundingCombo_->setEnabled(wulff);
        radiusSpin_->setEnabled(spherical);
        // The faceted ase.cluster shapes are all FCC references — lock the
        // lattice selector, which only matters for Wulff/spherical.
        latticeCombo_->setEnabled(wulff || spherical);
        shellSpin_->setEnabled(usesShell);
        decaPSpin_->setEnabled(decahedron);
        decaQSpin_->setEnabled(decahedron);
        decaRSpin_->setEnabled(decahedron);
    };
    connect(modeCombo_, &QComboBox::currentIndexChanged, this, syncMode);
    syncMode(0);
}

std::string NanoparticleDialog::shapeForMode(int mode) const
{
    switch (mode) {
    case 2: return "icosahedron";
    case 3: return "octahedron";
    case 4: return "cuboctahedron";
    case 5: return "decahedron";
    case 6: return "rhombic-dodecahedron";
    default: return {}; // 0 = Wulff, 1 = spherical
    }
}

QString NanoparticleDialog::resultName() const
{
    const int mode = modeCombo_->currentIndex();
    QString shapeName;
    switch (mode) {
    case 0: shapeName = tr("Wulff"); break;
    case 1: shapeName = tr("spherical"); break;
    case 2: shapeName = tr("icosahedron"); break;
    case 3: shapeName = tr("octahedron"); break;
    case 4: shapeName = tr("cuboctahedron"); break;
    case 5: shapeName = tr("decahedron"); break;
    case 6: shapeName = tr("rhombic dodecahedron"); break;
    default: shapeName = tr("cluster"); break;
    }
    return tr("%1 nanoparticle (%2, %3 atoms)")
        .arg(QLatin1String(core::Elements::data(elementZ_).symbol), shapeName)
        .arg(result_ ? static_cast<int>(result_->size()) : 0);
}

void NanoparticleDialog::generate()
{
    const std::string symbol = core::Elements::data(elementZ_).symbol;
    const std::string lattice = latticeCombo_->currentText().toStdString();
    const int mode = modeCombo_->currentIndex();
    const bool wulff = mode == 0;
    const std::string shape = shapeForMode(mode);
    if (wulff && lattice == "hcp") {
        statusLabel_->setText(tr("Wulff construction supports fcc/bcc/sc — "
                                 "use the spherical mode for hcp."));
        return;
    }

    QApplication::setOverrideCursor(Qt::WaitCursor);
    try {
        if (!shape.empty()) {
            result_ = pybridge::SurfaceScience::polyhedralNanoparticle(
                symbol, shape, shellSpin_->value(), decaPSpin_->value(),
                decaQSpin_->value(), decaRSpin_->value(),
                latticeConstantSpin_->value());
        } else if (wulff) {
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
                symbol, lattice, facets, sizeSpin_->value(),
                latticeConstantSpin_->value(),
                roundingCombo_->currentText().toStdString());
        } else {
            result_ = pybridge::SurfaceScience::sphericalNanoparticle(
                symbol, lattice, radiusSpin_->value(),
                latticeConstantSpin_->value());
        }
        QApplication::restoreOverrideCursor();
        accept();
    } catch (const std::exception& e) {
        QApplication::restoreOverrideCursor();
        statusLabel_->setText(QString::fromUtf8(e.what()));
    }
}

} // namespace calango::gui
