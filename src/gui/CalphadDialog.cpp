#include "gui/CalphadDialog.hpp"

#include "gui/PhaseDiagramWindow.hpp"
#include "gui/TdbGeneratorDialog.hpp"

#include <QCheckBox>
#include <QDialogButtonBox>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QScrollArea>
#include <QTextStream>
#include <QVBoxLayout>

#include <algorithm>

namespace calango::gui {

namespace {

/// Elements per row in the periodic-ish grid. A unary database carries ~80,
/// and a single column would be a scroll bar rather than a chooser.
constexpr int kElementColumns = 8;

} // namespace

CalphadDialog::CalphadDialog(QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle(tr("CALPHAD — Thermodynamic Database"));
    resize(760, 620);

    auto* layout = new QVBoxLayout(this);

    auto* intro = new QLabel(
        tr("Load a thermodynamic database (<tt>.tdb</tt>) and choose the "
           "system: which <b>elements</b> it contains, and which <b>phases</b> "
           "are allowed to compete. Both lists come from the file — a database "
           "is what defines them."),
        this);
    intro->setWordWrap(true);
    intro->setTextFormat(Qt::RichText);
    layout->addWidget(intro);

    // -- The file ----------------------------------------------------------
    auto* fileRow = new QHBoxLayout;
    fileRow->addWidget(new QLabel(tr("Database:"), this));
    pathEdit_ = new QLineEdit(this);
    pathEdit_->setPlaceholderText(tr("path to a .tdb file"));
    pathEdit_->setReadOnly(true);
    fileRow->addWidget(pathEdit_, 1);
    auto* browse = new QPushButton(tr("Import…"), this);
    connect(browse, &QPushButton::clicked, this,
            &CalphadDialog::browseForDatabase);
    fileRow->addWidget(browse);
    layout->addLayout(fileRow);

    auto* statusRow = new QHBoxLayout;
    statusLabel_ = new QLabel(tr("No database loaded"), this);
    statusLabel_->setStyleSheet(QStringLiteral("color: gray;"));
    statusRow->addWidget(statusLabel_, 1);
    // Parsing is tolerant, so a partly-understood database still loads. The
    // button appears only when something was skipped, because a permanently
    // present "Warnings" control on a clean file trains people to ignore it.
    warningsButton_ = new QPushButton(tr("Warnings…"), this);
    warningsButton_->setVisible(false);
    connect(warningsButton_, &QPushButton::clicked, this, [this] {
        QStringList lines;
        for (const std::string& warning : database_.warnings)
            lines << QString::fromStdString(warning);
        QMessageBox::information(
            this, tr("Database Warnings"),
            tr("The database loaded, but %n statement(s) were not "
               "understood:\n\n%1",
               nullptr, static_cast<int>(lines.size()))
                .arg(lines.join(QStringLiteral("\n"))));
    });
    statusRow->addWidget(warningsButton_);
    layout->addLayout(statusRow);

    // -- Elements ----------------------------------------------------------
    auto* elementGroup = new QGroupBox(tr("Elements"), this);
    auto* elementOuter = new QVBoxLayout(elementGroup);
    auto* elementScroll = new QScrollArea(elementGroup);
    elementScroll->setWidgetResizable(true);
    elementScroll->setMinimumHeight(150);
    auto* elementHost = new QWidget(elementScroll);
    elementLayout_ = new QVBoxLayout(elementHost);
    elementLayout_->setContentsMargins(4, 4, 4, 4);
    elementScroll->setWidget(elementHost);
    elementOuter->addWidget(elementScroll);
    layout->addWidget(elementGroup, 1);

    // -- Phases ------------------------------------------------------------
    auto* phaseGroup = new QGroupBox(tr("Phases"), this);
    auto* phaseOuter = new QVBoxLayout(phaseGroup);
    auto* phaseNote = new QLabel(
        tr("Unticked phases are <i>suspended</i>: present in the database but "
           "not allowed to form. A phase greyed out cannot exist in the "
           "chosen system at all — hover it to see which sublattice it "
           "cannot fill."),
        phaseGroup);
    phaseNote->setWordWrap(true);
    phaseNote->setTextFormat(Qt::RichText);
    phaseOuter->addWidget(phaseNote);
    auto* phaseScroll = new QScrollArea(phaseGroup);
    phaseScroll->setWidgetResizable(true);
    phaseScroll->setMinimumHeight(150);
    auto* phaseHost = new QWidget(phaseScroll);
    phaseLayout_ = new QVBoxLayout(phaseHost);
    phaseLayout_->setContentsMargins(4, 4, 4, 4);
    phaseScroll->setWidget(phaseHost);
    phaseOuter->addWidget(phaseScroll);
    layout->addWidget(phaseGroup, 1);

    summaryLabel_ = new QLabel(this);
    summaryLabel_->setWordWrap(true);
    summaryLabel_->setTextFormat(Qt::RichText);
    layout->addWidget(summaryLabel_);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    // Both live here rather than on the Modules menu: a phase diagram needs a
    // system, and the system is the thing this dialog exists to define. A menu
    // item would have to ask for it all over again.
    diagramButton_ =
        buttons->addButton(tr("Phase Diagram…"), QDialogButtonBox::ActionRole);
    diagramButton_->setEnabled(false);
    connect(diagramButton_, &QPushButton::clicked, this,
            &CalphadDialog::openPhaseDiagram);
    generateButton_ =
        buttons->addButton(tr("From DFT…"), QDialogButtonBox::ActionRole);
    generateButton_->setToolTip(
        tr("Fit a Redlich-Kister model to formation energies computed here "
           "and write a .tdb. Needs no database loaded — it makes one."));
    connect(generateButton_, &QPushButton::clicked, this,
            &CalphadDialog::openGenerator);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    for (QPushButton* button : buttons->findChildren<QPushButton*>())
        button->setAutoDefault(false);
    layout->addWidget(buttons);

    refreshAvailability();
}

void CalphadDialog::openPhaseDiagram()
{
    const QStringList elements = selectedElements();
    const QStringList phases = selectedPhases();
    if (elements.size() < 2 || elements.size() > 3) {
        QMessageBox::information(
            this, tr("Phase Diagram"),
            tr("Select two elements for a T–x diagram, or three for an "
               "isothermal section. %n element(s) are selected.\n\n"
               "Higher-order systems are not refused because they are "
               "uninteresting but because there is no way to draw one: a "
               "quaternary section is a tetrahedron.",
               nullptr, static_cast<int>(elements.size())));
        return;
    }
    auto* window = new PhaseDiagramWindow(database_, elements, phases, this);
    window->setAttribute(Qt::WA_DeleteOnClose);
    window->show();
}

void CalphadDialog::openGenerator()
{
    auto* dialog = new TdbGeneratorDialog(this);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->show();
}

void CalphadDialog::browseForDatabase()
{
    const QString path = QFileDialog::getOpenFileName(
        this, tr("Import Thermodynamic Database"), QString(),
        tr("Thermodynamic databases (*.tdb *.TDB);;All files (*)"));
    if (!path.isEmpty())
        loadDatabase(path);
}

bool CalphadDialog::loadDatabase(const QString& path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        setStatus(tr("Could not open %1.").arg(QFileInfo(path).fileName()),
                  false);
        return false;
    }
    QTextStream stream(&file);
    const QString text = stream.readAll();
    pathEdit_->setText(path);
    return loadDatabaseText(text, QFileInfo(path).fileName());
}

bool CalphadDialog::loadDatabaseText(const QString& text, const QString& label)
{
    core::TdbDatabase parsed;
    std::string error;
    if (!parsed.parse(text.toStdString(), &error)) {
        // The previous database stays loaded: a mistyped path should not cost
        // the user the system they had already set up.
        setStatus(tr("%1 is not a usable database — %2")
                      .arg(label, QString::fromStdString(error)),
                  false);
        return false;
    }
    database_ = std::move(parsed);
    loaded_ = true;
    rebuildSelectors();

    const int elements = static_cast<int>(database_.selectableElements().size());
    const int phases = static_cast<int>(database_.phases.size());
    setStatus(tr("%1 — %n element(s), %2 phase(s), %3 parameter(s)", nullptr,
                 elements)
                  .arg(label)
                  .arg(phases)
                  .arg(static_cast<int>(database_.parameters.size())),
              true);
    warningsButton_->setVisible(!database_.warnings.empty());
    refreshAvailability();
    return true;
}

void CalphadDialog::rebuildSelectors()
{
    const auto clear = [](QVBoxLayout* layout, std::vector<QCheckBox*>& boxes) {
        boxes.clear();
        while (QLayoutItem* item = layout->takeAt(0)) {
            if (QWidget* widget = item->widget())
                widget->deleteLater();
            delete item;
        }
    };
    clear(elementLayout_, elementBoxes_);
    clear(phaseLayout_, phaseBoxes_);

    // -- Elements, in a grid --------------------------------------------
    auto* grid = new QWidget;
    auto* gridLayout = new QGridLayout(grid);
    gridLayout->setContentsMargins(0, 0, 0, 0);
    const std::vector<std::string> elements = database_.selectableElements();
    for (std::size_t i = 0; i < elements.size(); ++i) {
        auto* box = new QCheckBox(QString::fromStdString(elements[i]), grid);
        connect(box, &QCheckBox::toggled, this,
                &CalphadDialog::refreshAvailability);
        gridLayout->addWidget(box, static_cast<int>(i) / kElementColumns,
                              static_cast<int>(i) % kElementColumns);
        elementBoxes_.push_back(box);
    }
    elementLayout_->addWidget(grid);
    elementLayout_->addStretch(1);

    // -- Phases, one per row with their sublattice model ----------------
    for (const core::TdbPhase& phase : database_.phases) {
        auto* box = new QCheckBox(QString::fromStdString(phase.name));
        // Ticked by default: the usual intent is "let everything in this
        // database compete", and suspending is the deliberate act.
        box->setChecked(true);
        // The model, on the row, because "why is SIGMA unavailable" is
        // answered by its sublattices and by nothing else on screen.
        QStringList sublattices;
        for (std::size_t s = 0; s < phase.constituents.size(); ++s) {
            QStringList species;
            for (const std::string& name : phase.constituents[s])
                species << QString::fromStdString(name);
            const double sites =
                s < phase.siteRatios.size() ? phase.siteRatios[s] : 0.0;
            sublattices << QStringLiteral("(%1)%2")
                               .arg(species.join(QLatin1Char(',')))
                               .arg(sites, 0, 'g', 4);
        }
        box->setToolTip(sublattices.isEmpty()
                            ? tr("No constituent list in this database; the "
                                 "phase is offered because its model is not "
                                 "known here rather than because it fits.")
                            : sublattices.join(QString()));
        connect(box, &QCheckBox::toggled, this,
                &CalphadDialog::refreshAvailability);
        phaseLayout_->addWidget(box);
        phaseBoxes_.push_back(box);
    }
    phaseLayout_->addStretch(1);
}

void CalphadDialog::refreshAvailability()
{
    if (!loaded_) {
        summaryLabel_->setText(
            tr("<i>Import a database to choose a system.</i>"));
        return;
    }

    const QStringList chosen = selectedElements();
    std::vector<std::string> selection;
    selection.reserve(chosen.size());
    for (const QString& name : chosen)
        selection.push_back(name.toStdString());
    const std::vector<std::string> available =
        database_.phasesForElements(selection);

    int usable = 0;
    for (QCheckBox* box : phaseBoxes_) {
        const std::string name = box->text().toStdString();
        const bool ok = std::find(available.begin(), available.end(), name)
            != available.end();
        // Disabled rather than hidden: a phase vanishing from the list as an
        // element is unticked reads as a bug, and the user cannot then see
        // that the database HAS a sigma phase they are one element away from.
        box->setEnabled(ok && !chosen.isEmpty());
        if (!ok && !chosen.isEmpty())
            box->setToolTip(
                tr("Not available for %1: one of its sublattices has no "
                   "constituent among the chosen elements.")
                    .arg(chosen.join(QStringLiteral("-"))));
        if (ok && box->isChecked())
            ++usable;
    }

    // A diagram is drawable for a binary or a ternary and for nothing else, so
    // the button says so by being disabled rather than by opening a window
    // that then refuses.
    if (diagramButton_)
        diagramButton_->setEnabled(usable > 0 && chosen.size() >= 2
                                   && chosen.size() <= 3);

    if (chosen.isEmpty()) {
        summaryLabel_->setText(
            tr("<i>Select at least one element. Phase availability follows "
               "the element choice.</i>"));
        return;
    }
    summaryLabel_->setText(
        tr("<b>%1</b> — %n phase(s) active of %2 in the database.", nullptr,
           usable)
            .arg(chosen.join(QStringLiteral("-")))
            .arg(static_cast<int>(phaseBoxes_.size())));
}

QStringList CalphadDialog::selectedElements() const
{
    QStringList out;
    for (const QCheckBox* box : elementBoxes_)
        if (box->isChecked())
            out << box->text();
    return out;
}

QStringList CalphadDialog::selectedPhases() const
{
    QStringList out;
    // Enabled AND checked. A phase ticked before the element selection
    // narrowed is not part of the system any more, and returning it would
    // hand the solver a phase it cannot build.
    for (const QCheckBox* box : phaseBoxes_)
        if (box->isChecked() && box->isEnabled())
            out << box->text();
    return out;
}

void CalphadDialog::setStatus(const QString& text, bool ok)
{
    statusLabel_->setText(text);
    statusLabel_->setStyleSheet(ok ? QStringLiteral("color: gray;")
                                   : QStringLiteral("color: #c0392b;"));
}

} // namespace calango::gui
