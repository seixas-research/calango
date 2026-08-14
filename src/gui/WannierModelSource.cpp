#include "gui/WannierModelSource.hpp"

#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QVBoxLayout>

namespace calango::gui {

WannierModelSource::WannierModelSource(QWidget* parent) : QWidget(parent)
{
    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);

    auto* row = new QHBoxLayout;
    summary_ = new QLabel(tr("No Wannier Hamiltonian loaded."), this);
    summary_->setWordWrap(true);
    row->addWidget(summary_, 1);

    auto* browse = new QPushButton(tr("Open _hr.dat…"), this);
    browse->setToolTip(
        tr("Read a Wannier Hamiltonian you already have, with Calango's own "
           "parser. The file is a plain text table of H(R); nothing external "
           "is executed to read it."));
    connect(browse, &QPushButton::clicked, this, &WannierModelSource::browse);
    row->addWidget(browse);
    outer->addLayout(row);

    auto* demoRow = new QHBoxLayout;
    demoRow->addWidget(new QLabel(tr("Lattice constant:"), this));
    latticeConstant_ = new QDoubleSpinBox(this);
    latticeConstant_->setRange(0.5, 30.0);
    latticeConstant_->setValue(3.0);
    latticeConstant_->setDecimals(3);
    latticeConstant_->setSuffix(tr(" Å"));
    latticeConstant_->setToolTip(
        tr("Cell parameter used for the built-in demo models, and the length "
           "that turns dH/dk into a velocity."));
    demoRow->addWidget(latticeConstant_);

    auto* cubic = new QPushButton(tr("Demo: cubic metal"), this);
    cubic->setToolTip(
        tr("One s-like band on a simple cubic lattice, bandwidth 12t. A good "
           "metal, and the model the transport tests use."));
    connect(cubic, &QPushButton::clicked, this,
            &WannierModelSource::loadCubicDemo);
    demoRow->addWidget(cubic);

    auto* chern = new QPushButton(tr("Demo: Chern insulator"), this);
    chern->setToolTip(
        tr("The two-band Qi-Wu-Zhang model, which has Chern number ±1 for "
           "|m| < 2 and 0 outside. The model the Berry-phase tests use, so "
           "what this panel reports can be checked against them."));
    connect(chern, &QPushButton::clicked, this,
            &WannierModelSource::loadChernDemo);
    demoRow->addWidget(chern);
    demoRow->addStretch(1);
    outer->addLayout(demoRow);
}

void WannierModelSource::adopt(core::WannierHamiltonian model,
                               const QString& description)
{
    model_ = std::move(model);
    loaded_ = true;
    summary_->setText(description);
    Q_EMIT modelChanged();
}

void WannierModelSource::browse()
{
    const QString path = QFileDialog::getOpenFileName(
        this, tr("Open a Wannier Hamiltonian"), QString(),
        tr("Wannier Hamiltonian (*_hr.dat);;All files (*)"));
    if (path.isEmpty())
        return;

    const double a = latticeConstant_->value();
    std::string error;
    auto model = core::WannierHamiltonian::fromHrDat(
        path.toStdString(), {{{a, 0.0, 0.0}, {0.0, a, 0.0}, {0.0, 0.0, a}}},
        &error);
    if (model.orbitals() == 0) {
        QMessageBox::warning(this, tr("Wannier Hamiltonian"),
                             QString::fromStdString(error));
        return;
    }
    adopt(std::move(model),
          tr("%1 — %2 orbitals, %3 lattice vectors. The cell is the cubic box "
             "above; edit it for a non-cubic system.")
              .arg(QFileInfo(path).fileName())
              .arg(model.orbitals())
              .arg(model.hoppings().size()));
}

void WannierModelSource::loadCubicDemo()
{
    const double a = latticeConstant_->value();
    const double t = 0.5;
    std::vector<core::WannierHamiltonian::HoppingBlock> hoppings;
    for (int axis = 0; axis < 3; ++axis)
        for (int sign : {-1, 1}) {
            core::WannierHamiltonian::HoppingBlock block;
            block.lattice = {0, 0, 0};
            block.lattice[static_cast<std::size_t>(axis)] = sign;
            block.matrix = {-t};
            hoppings.push_back(block);
        }
    adopt(core::WannierHamiltonian(
              1, {{{a, 0.0, 0.0}, {0.0, a, 0.0}, {0.0, 0.0, a}}},
              std::move(hoppings)),
          tr("Demo: simple-cubic single band, t = 0.5 eV, bandwidth 6 eV."));
}

void WannierModelSource::loadChernDemo()
{
    const double a = latticeConstant_->value();
    const double m = 1.0;
    std::vector<core::WannierHamiltonian::HoppingBlock> hoppings;
    {
        core::WannierHamiltonian::HoppingBlock onsite;
        onsite.lattice = {0, 0, 0};
        onsite.matrix = {m, 0.0, 0.0, -m};
        hoppings.push_back(onsite);
    }
    {
        core::WannierHamiltonian::HoppingBlock px;
        px.lattice = {1, 0, 0};
        px.matrix = {0.5, 0.0, 0.0, -0.5};
        px.imaginary = {0.0, -0.5, -0.5, 0.0};
        hoppings.push_back(px);
        core::WannierHamiltonian::HoppingBlock mx;
        mx.lattice = {-1, 0, 0};
        mx.matrix = {0.5, 0.0, 0.0, -0.5};
        mx.imaginary = {0.0, 0.5, 0.5, 0.0};
        hoppings.push_back(mx);
    }
    {
        core::WannierHamiltonian::HoppingBlock py;
        py.lattice = {0, 1, 0};
        py.matrix = {0.5, -0.5, 0.5, -0.5};
        hoppings.push_back(py);
        core::WannierHamiltonian::HoppingBlock my;
        my.lattice = {0, -1, 0};
        my.matrix = {0.5, 0.5, -0.5, -0.5};
        hoppings.push_back(my);
    }
    // A thick third axis: the model is two dimensional, and giving it a real
    // out-of-plane period keeps the cell volume — and therefore every SI
    // conversion — well defined.
    adopt(core::WannierHamiltonian(
              2, {{{a, 0.0, 0.0}, {0.0, a, 0.0}, {0.0, 0.0, 10.0}}},
              std::move(hoppings)),
          tr("Demo: Qi-Wu-Zhang two-band model, m = 1 (Chern number ±1)."));
}

} // namespace calango::gui
