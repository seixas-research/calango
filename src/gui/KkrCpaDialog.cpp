#include "gui/KkrCpaDialog.hpp"

#include "core/LocaleSafeNumber.hpp"
#include "gui/GuiUtils.hpp"
#include "gui/SpectrumPlotWidget.hpp"

#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QTabWidget>
#include <QTableWidget>
#include <QVBoxLayout>

#include <cmath>

namespace calango::gui {

namespace {
enum Column { ColSymbol = 0, ColConcentration = 1, ColLevel = 2, ColSplit = 3 };
} // namespace

KkrCpaDialog::KkrCpaDialog(QWidget* parent) : QDialog(parent)
{
    setWindowTitle(tr("KKR-CPA — Random Alloys"));

    auto* outer = new QVBoxLayout(this);

    auto* intro = new QLabel(
        tr("Coherent Potential Approximation for a substitutionally disordered "
           "alloy."),
        this);
    intro->setWordWrap(true);
    intro->setToolTip(
        tr("The CPA replaces the random lattice by an effective medium whose "
           "self-energy makes a single embedded impurity scatter nothing on "
           "average — the condition sum_i c_i tau_i = tau_CPA.\n\n"
           "The medium's band arrives as a model density of states rather than "
           "from a multiple-scattering solve, so the on-site level stands in "
           "for a full t-matrix. Everything the CPA layer itself does is exact "
           "within that representation."));
    outer->addWidget(intro);

    // -- Components ---------------------------------------------------------
    auto* componentsGroup = new QGroupBox(tr("Alloy Components"), this);
    auto* componentsLayout = new QVBoxLayout(componentsGroup);

    table_ = new QTableWidget(0, 4, componentsGroup);
    table_->setHorizontalHeaderLabels({tr("species"), tr("concentration"),
                                       tr("on-site (eV)"),
                                       tr("exchange split (eV)")});
    table_->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    table_->setMaximumHeight(160);
    // Routine on a pt_BR keyboard: a dead key in an editable item view
    // recurses until the stack dies.
    disableTypeToEdit(table_);
    componentsLayout->addWidget(table_);

    auto* buttons = new QHBoxLayout;
    auto* addButton = new QPushButton(tr("Add"), componentsGroup);
    connect(addButton, &QPushButton::clicked, this,
            &KkrCpaDialog::addComponent);
    buttons->addWidget(addButton);
    auto* removeButton = new QPushButton(tr("Remove selected"), componentsGroup);
    connect(removeButton, &QPushButton::clicked, this,
            &KkrCpaDialog::removeComponent);
    buttons->addWidget(removeButton);
    buttons->addStretch(1);
    componentsLayout->addLayout(buttons);
    outer->addWidget(componentsGroup);

    // -- Medium -------------------------------------------------------------
    auto* mediumGroup = new QGroupBox(tr("Unperturbed Band"), this);
    auto* form = new QFormLayout(mediumGroup);

    bandShape_ = new QComboBox(mediumGroup);
    bandShape_->addItem(tr("Semi-elliptic (Bethe lattice)"), 0);
    bandShape_->addItem(tr("Rectangular"), 1);
    bandShape_->setToolTip(
        tr("Shape of the bare density of states the alloy scatters against. "
           "The semi-elliptic band is the one whose Hilbert transform is a "
           "closed form, which is why it is the default and the reference."));
    form->addRow(tr("Band shape:"), bandShape_);

    bandwidth_ = new QDoubleSpinBox(mediumGroup);
    bandwidth_->setRange(0.1, 50.0);
    bandwidth_->setValue(1.0);
    bandwidth_->setDecimals(3);
    bandwidth_->setSuffix(tr(" eV"));
    bandwidth_->setToolTip(
        tr("Half-bandwidth W. The ratio of the level separation to W is what "
           "decides whether the alloy is a common band or splits in two."));
    form->addRow(tr("Half-bandwidth:"), bandwidth_);

    broadening_ = new QDoubleSpinBox(mediumGroup);
    broadening_->setRange(0.001, 1.0);
    broadening_->setValue(0.02);
    broadening_->setDecimals(3);
    broadening_->setSuffix(tr(" eV"));
    broadening_->setToolTip(
        tr("Imaginary part added to every energy. Physical broadening as well "
           "as numerical regularisation: at zero the Green's function of a "
           "discrete quadrature is a comb of poles."));
    form->addRow(tr("Broadening η:"), broadening_);

    electrons_ = new QDoubleSpinBox(mediumGroup);
    electrons_->setRange(0.0, 2.0);
    electrons_->setValue(1.0);
    electrons_->setDecimals(3);
    electrons_->setToolTip(
        tr("Band filling per site, both spins included, which fixes the Fermi "
           "level. It is an input to the alloy problem, not an output of the "
           "CPA condition."));
    form->addRow(tr("Electrons per site:"), electrons_);
    outer->addWidget(mediumGroup);

    solveButton_ = new QPushButton(tr("Solve CPA"), this);
    connect(solveButton_, &QPushButton::clicked, this, &KkrCpaDialog::solve);
    outer->addWidget(solveButton_);

    summary_ = new QLabel(tr("No solution yet."), this);
    summary_->setWordWrap(true);
    summary_->setTextFormat(Qt::RichText);
    outer->addWidget(summary_);

    auto* tabs = new QTabWidget(this);
    dosPlot_ = new SpectrumPlotWidget(tabs);
    tabs->addTab(dosPlot_, tr("Density of States"));
    bsfPlot_ = new SpectrumPlotWidget(tabs);
    bsfPlot_->setToolTip(
        tr("A(k,E) at four band energies. Disorder gives the self-energy an "
           "imaginary part, so a sharp band becomes a Lorentzian whose width "
           "is the inverse lifetime — which is what a random alloy has instead "
           "of a band structure."));
    tabs->addTab(bsfPlot_, tr("Bloch Spectral Function"));
    outer->addWidget(tabs, 1);

    auto* close = new QPushButton(tr("Close"), this);
    connect(close, &QPushButton::clicked, this, &QDialog::accept);
    auto* closeRow = new QHBoxLayout;
    closeRow->addStretch(1);
    closeRow->addWidget(close);
    outer->addLayout(closeRow);

    // A binary alloy to start from, so the dialog is runnable on open rather
    // than presenting an empty table with a disabled button.
    addComponent();
    addComponent();
    table_->item(0, ColSymbol)->setText(QStringLiteral("A"));
    table_->item(0, ColLevel)->setText(QStringLiteral("-0.5"));
    table_->item(1, ColSymbol)->setText(QStringLiteral("B"));
    table_->item(1, ColLevel)->setText(QStringLiteral("0.5"));

    resize(880, 760);
    refreshEnabled();
}

void KkrCpaDialog::addComponent()
{
    const int row = table_->rowCount();
    table_->insertRow(row);
    table_->setItem(row, ColSymbol,
                    new QTableWidgetItem(QStringLiteral("X%1").arg(row + 1)));
    table_->setItem(row, ColConcentration, new QTableWidgetItem(
                                               QStringLiteral("0.5")));
    table_->setItem(row, ColLevel, new QTableWidgetItem(QStringLiteral("0.0")));
    table_->setItem(row, ColSplit, new QTableWidgetItem(QStringLiteral("0.0")));
    refreshEnabled();
}

void KkrCpaDialog::removeComponent()
{
    const int row = table_->currentRow();
    if (row >= 0)
        table_->removeRow(row);
    refreshEnabled();
}

void KkrCpaDialog::refreshEnabled()
{
    if (solveButton_)
        solveButton_->setEnabled(table_->rowCount() > 0);
}

bool KkrCpaDialog::readComponents(
    std::vector<core::CpaSolver::Component>& out, QString* error) const
{
    out.clear();
    double total = 0.0;
    for (int row = 0; row < table_->rowCount(); ++row) {
        core::CpaSolver::Component component;
        const auto* symbol = table_->item(row, ColSymbol);
        component.symbol =
            symbol ? symbol->text().toStdString() : std::string("X");

        // localeSafeParse throughout: this machine's LC_NUMERIC is pt_BR, and
        // QApplication adopts it, so "0.5" would otherwise parse as 0 inside
        // the GUI and not in a Qt-free test.
        const auto readCell = [&](int column, double fallback) {
            const auto* item = table_->item(row, column);
            if (!item)
                return fallback;
            return core::localeSafeToDouble(item->text().toStdString(),
                                            fallback);
        };
        component.concentration = readCell(ColConcentration, 0.0);
        component.onsiteEnergy = readCell(ColLevel, 0.0);
        component.exchangeSplitting = readCell(ColSplit, 0.0);

        if (component.concentration < 0.0) {
            if (error)
                *error = tr("Row %1 has a negative concentration.").arg(row + 1);
            return false;
        }
        total += component.concentration;
        out.push_back(component);
    }
    if (out.empty()) {
        if (error)
            *error = tr("Add at least one component.");
        return false;
    }
    if (total <= 0.0) {
        if (error)
            *error = tr("The concentrations sum to zero.");
        return false;
    }
    // Renormalised rather than refused: a user typing 1:1:1 means thirds, and
    // making them do the division is friction for nothing. The normalisation
    // is reported in the summary so it is never silent.
    for (auto& component : out)
        component.concentration /= total;
    return true;
}

void KkrCpaDialog::solve()
{
    std::vector<core::CpaSolver::Component> components;
    QString error;
    if (!readComponents(components, &error)) {
        QMessageBox::warning(this, tr("KKR-CPA"), error);
        return;
    }

    const double w = bandwidth_->value();
    const auto lattice = (bandShape_->currentData().toInt() == 0)
        ? core::CpaSolver::Lattice::semicircular(w, 1201)
        : core::CpaSolver::Lattice::rectangular(w, 1201);

    core::CpaSolver::Options options;
    options.broadening = broadening_->value();

    try {
        core::CpaSolver solver(components, lattice, options);
        const double span = 3.0 * w + 4.0;
        const auto grid = solver.computeDosGrid(-span, span, 801);
        const double fermi =
            core::CpaSolver::findFermiEnergy(grid, electrons_->value());
        const auto moments = solver.componentMoments(grid, fermi);

        // -- DOS ------------------------------------------------------------
        std::vector<QPair<QString, std::vector<double>>> dosSeries;
        dosSeries.push_back({tr("total"), grid.total});
        for (std::size_t i = 0; i < components.size(); ++i) {
            std::vector<double> partial(grid.energies.size(), 0.0);
            for (std::size_t j = 0; j < grid.energies.size(); ++j)
                partial[j] = grid.partial[j][i][0] + grid.partial[j][i][1];
            dosSeries.push_back(
                {QString::fromStdString(components[i].symbol), partial});
        }
        dosPlot_->setSeries(grid.energies, dosSeries, tr("Energy (eV)"),
                            tr("DOS (states/eV/site)"));
        dosPlot_->setReferenceLines({});
        dosPlot_->setXRange(-span, span);

        // -- Bloch spectral function ----------------------------------------
        // Four cuts across the band rather than a heat map: the point of the
        // BSF is the LINE SHAPE at fixed k, and a colour map hides exactly the
        // width that distinguishes an alloy from an ordered crystal.
        const double cuts[4] = {-0.75 * w, -0.25 * w, 0.25 * w, 0.75 * w};
        std::vector<QPair<QString, std::vector<double>>> bsfSeries;
        for (double bandEnergy : cuts) {
            std::vector<double> values(grid.energies.size(), 0.0);
            for (std::size_t j = 0; j < grid.energies.size(); ++j)
                values[j] = solver.blochSpectralFunction(bandEnergy,
                                                         grid.energies[j]);
            bsfSeries.push_back(
                {tr("ε(k) = %1 eV").arg(bandEnergy, 0, 'f', 2), values});
        }
        bsfPlot_->setSeries(grid.energies, bsfSeries, tr("Energy (eV)"),
                            tr("A(k,E) (1/eV)"));
        bsfPlot_->setXRange(-span, span);

        QString text = tr("<b>E<sub>F</sub> = %1 eV</b> at %2 electrons/site. ")
                           .arg(fermi, 0, 'f', 4)
                           .arg(electrons_->value(), 0, 'f', 3);
        text += tr("Concentrations normalised to ");
        for (std::size_t i = 0; i < components.size(); ++i)
            text += QStringLiteral("%1 %2%3")
                        .arg(QString::fromStdString(components[i].symbol))
                        .arg(components[i].concentration, 0, 'f', 3)
                        .arg(i + 1 < components.size() ? QStringLiteral(", ")
                                                       : QStringLiteral("."));
        bool anyMoment = false;
        for (double m : moments)
            anyMoment = anyMoment || std::abs(m) > 1e-6;
        if (anyMoment) {
            text += tr("<br>Moments (μ<sub>B</sub>/atom): ");
            for (std::size_t i = 0; i < components.size(); ++i)
                text += QStringLiteral("%1 %2%3")
                            .arg(QString::fromStdString(components[i].symbol))
                            .arg(moments[i], 0, 'f', 4)
                            .arg(i + 1 < components.size()
                                     ? QStringLiteral(", ")
                                     : QStringLiteral("."));
        }
        summary_->setText(text);
    } catch (const std::exception& e) {
        QMessageBox::warning(this, tr("KKR-CPA"),
                             tr("The CPA solver refused this input:\n%1")
                                 .arg(QString::fromUtf8(e.what())));
    }
}

} // namespace calango::gui
