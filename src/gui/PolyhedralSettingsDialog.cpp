#include "gui/PolyhedralSettingsDialog.hpp"

#include "core/Element.hpp"
#include "core/Structure.hpp"
#include "gui/ViewportWidget.hpp"

#include <QCheckBox>
#include <QComboBox>
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

#include <set>

namespace calango::gui {

PolyhedralSettingsDialog::PolyhedralSettingsDialog(ViewportWidget* viewport,
                                                   QWidget* parent)
    : QDialog(parent), viewport_(viewport)
{
    setWindowTitle(tr("Edit Polyhedral Setup"));
    resize(460, 480);

    auto* layout = new QVBoxLayout(this);

    modeNoteLabel_ = new QLabel(this);
    modeNoteLabel_->setWordWrap(true);
    layout->addWidget(modeNoteLabel_);
    // Say so rather than letting the controls appear inert: every setting here
    // is real, it simply has nothing to draw on in the other modes.
    modeNoteLabel_->setText(
        viewport_->style().mode == render::RepresentationMode::Polyhedral
            ? tr("Polyhedra are drawn on every atom with four or more "
                 "coordinating neighbours.")
            : tr("⚠ The viewport is not in Polyhedral representation mode, so "
                 "these settings have nothing to draw on yet. Switch the "
                 "Representation panel's Mode to Polyhedral to see them."));

    // -- Appearance ---------------------------------------------------------
    auto* appearance = new QGroupBox(tr("Faces && Edges"), this);
    auto* form = new QFormLayout(appearance);

    opacitySpin_ = new QDoubleSpinBox(appearance);
    opacitySpin_->setRange(0.0, 1.0);
    opacitySpin_->setDecimals(2);
    opacitySpin_->setSingleStep(0.05);
    opacitySpin_->setValue(viewport_->style().polyhedronOpacity);
    opacitySpin_->setToolTip(
        tr("Face opacity. Keep it translucent: an opaque polyhedron hides the "
           "coordinated atoms it was drawn to explain."));
    form->addRow(tr("Face opacity:"), opacitySpin_);

    edgesCheck_ = new QCheckBox(tr("Show edge wireframe"), appearance);
    edgesCheck_->setChecked(viewport_->style().polyhedronEdges);
    edgesCheck_->setToolTip(
        tr("The edges are what make a translucent hull read as a solid rather "
           "than a smear of colour."));
    form->addRow(edgesCheck_);

    edgeWidthSpin_ = new QDoubleSpinBox(appearance);
    edgeWidthSpin_->setRange(0.5, 6.0);
    edgeWidthSpin_->setDecimals(1);
    edgeWidthSpin_->setSingleStep(0.5);
    edgeWidthSpin_->setValue(viewport_->style().polyhedronEdgeWidth);
    edgeWidthSpin_->setToolTip(
        tr("Edge line width in pixels. Core-profile OpenGL clamps line width "
           "on most drivers, so values much above 2 may not thicken further."));
    form->addRow(tr("Edge line width:"), edgeWidthSpin_);
    layout->addWidget(appearance);

    const auto apply = [this] {
        auto& style = viewport_->style();
        style.polyhedronOpacity = static_cast<float>(opacitySpin_->value());
        style.polyhedronEdges = edgesCheck_->isChecked();
        style.polyhedronEdgeWidth = static_cast<float>(edgeWidthSpin_->value());
        edgeWidthSpin_->setEnabled(edgesCheck_->isChecked());
        // Appearance only — no instance buffers to rebuild.
        viewport_->styleChanged(false);
    };
    connect(opacitySpin_, &QDoubleSpinBox::valueChanged, this, apply);
    connect(edgesCheck_, &QCheckBox::toggled, this, apply);
    connect(edgeWidthSpin_, &QDoubleSpinBox::valueChanged, this, apply);
    edgeWidthSpin_->setEnabled(edgesCheck_->isChecked());

    // -- Coordination cutoffs -----------------------------------------------
    auto* cutoffGroup = new QGroupBox(tr("Coordination Cutoff Overrides"), this);
    auto* cutoffLayout = new QVBoxLayout(cutoffGroup);
    auto* note = new QLabel(
        tr("By default a polyhedron is built from the atom's perceived bonds. "
           "An override replaces that for one central element with every "
           "neighbour inside an absolute radius — which is how you get a "
           "6-coordinate octahedron when covalent radii give 4 or 8."),
        cutoffGroup);
    note->setWordWrap(true);
    cutoffLayout->addWidget(note);

    auto* addRow = new QHBoxLayout;
    elementCombo_ = new QComboBox(cutoffGroup);
    cutoffSpin_ = new QDoubleSpinBox(cutoffGroup);
    cutoffSpin_->setRange(0.5, 12.0);
    cutoffSpin_->setDecimals(2);
    cutoffSpin_->setSingleStep(0.1);
    cutoffSpin_->setValue(2.6);
    cutoffSpin_->setSuffix(tr(" Å"));
    auto* addButton = new QPushButton(tr("Set"), cutoffGroup);
    addRow->addWidget(new QLabel(tr("Central element:"), cutoffGroup));
    addRow->addWidget(elementCombo_, 1);
    addRow->addWidget(cutoffSpin_);
    addRow->addWidget(addButton);
    cutoffLayout->addLayout(addRow);
    connect(addButton, &QPushButton::clicked, this,
            &PolyhedralSettingsDialog::addCutoffOverride);

    overrideTable_ = new QTableWidget(0, 2, cutoffGroup);
    overrideTable_->setHorizontalHeaderLabels({tr("Element"), tr("Cutoff")});
    overrideTable_->verticalHeader()->setVisible(false);
    overrideTable_->horizontalHeader()->setStretchLastSection(true);
    overrideTable_->setSelectionBehavior(QAbstractItemView::SelectRows);
    overrideTable_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    cutoffLayout->addWidget(overrideTable_, 1);

    auto* removeButton = new QPushButton(tr("Remove Selected"), cutoffGroup);
    connect(removeButton, &QPushButton::clicked, this,
            &PolyhedralSettingsDialog::removeCutoffOverride);
    cutoffLayout->addWidget(removeButton);
    layout->addWidget(cutoffGroup, 1);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);

    populateElementCombo();
    refreshOverrides();
}

void PolyhedralSettingsDialog::populateElementCombo()
{
    elementCombo_->clear();
    const auto structure = viewport_->structure();
    if (!structure)
        return;
    // Only the elements actually present: offering all 118 would bury the
    // handful that can be a polyhedron centre in this structure.
    std::set<int> present;
    for (const core::Atom& atom : structure->atoms())
        present.insert(atom.atomicNumber);
    for (const int z : present)
        elementCombo_->addItem(
            QString::fromLatin1(core::Elements::data(z).symbol), z);
}

void PolyhedralSettingsDialog::addCutoffOverride()
{
    if (elementCombo_->currentIndex() < 0)
        return;
    const int z = elementCombo_->currentData().toInt();
    viewport_->style().polyhedronCutoffOverrides[z] =
        static_cast<float>(cutoffSpin_->value());
    // The coordination shell changed, so the hull geometry must be rebuilt.
    viewport_->styleChanged(true);
    refreshOverrides();
}

void PolyhedralSettingsDialog::removeCutoffOverride()
{
    const int row = overrideTable_->currentRow();
    if (row < 0 || row >= overrideTable_->rowCount())
        return;
    const int z = overrideTable_->item(row, 0)->data(Qt::UserRole).toInt();
    viewport_->style().polyhedronCutoffOverrides.erase(z);
    viewport_->styleChanged(true);
    refreshOverrides();
}

void PolyhedralSettingsDialog::refreshOverrides()
{
    const auto& overrides = viewport_->style().polyhedronCutoffOverrides;
    overrideTable_->setRowCount(static_cast<int>(overrides.size()));
    int row = 0;
    for (const auto& [z, cutoff] : overrides) {
        auto* symbol = new QTableWidgetItem(
            QString::fromLatin1(core::Elements::data(z).symbol));
        symbol->setData(Qt::UserRole, z);
        overrideTable_->setItem(row, 0, symbol);
        overrideTable_->setItem(
            row, 1,
            new QTableWidgetItem(QStringLiteral("%1 Å").arg(cutoff, 0, 'f', 2)));
        ++row;
    }
}

} // namespace calango::gui
