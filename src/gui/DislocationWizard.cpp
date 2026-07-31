#include "gui/DislocationWizard.hpp"

#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QVBoxLayout>

#include <stdexcept>

namespace calango::gui {

namespace {

using Builder = core::DislocationBuilder;

/// Elastic constants (GPa) for a few common cubic metals, offered as presets
/// so the anisotropic page starts from real numbers rather than from three
/// blanks a user has to leave and go look up.
struct CubicPreset {
    const char* name;
    double c11, c12, c44;
};

constexpr CubicPreset kCubicPresets[] = {
    {"Cu (copper)", 168.4, 121.4, 75.4},
    {"Al (aluminium)", 106.8, 60.4, 28.3},
    {"Fe (alpha iron)", 231.4, 134.7, 116.4},
    {"Ni (nickel)", 246.5, 147.3, 124.7},
    {"W (tungsten)", 522.4, 204.4, 160.8},
    {"Si (silicon)", 165.6, 63.9, 79.5},
};

QString typeExplanation(Builder::Type type)
{
    switch (type) {
    case Builder::Type::Edge:
        return QObject::tr(
            "A single edge dislocation: the Burgers vector is perpendicular to "
            "the line, and the crystal carries an extra half-plane ending on "
            "it. The atom count is unchanged — this is the elastic field, not "
            "a plane of atoms inserted by hand.\n\n"
            "A single dislocation has a net Burgers vector, so the cell can "
            "only stay periodic ALONG THE LINE. The other two directions "
            "become free surfaces.");
    case Builder::Type::Screw:
        return QObject::tr(
            "A single screw dislocation: the Burgers vector runs along the "
            "line, and the lattice planes around it form one continuous helical "
            "ramp. Only the component along the line moves.\n\n"
            "As with the edge, periodicity survives only along the line.");
    case Builder::Type::Glide:
        return QObject::tr(
            "Two edge dislocations of opposite sign in the SAME glide plane — "
            "the configuration a dislocation leaves behind after gliding a "
            "distance d. Between the two cores the crystal above the glide "
            "plane has slipped by one Burgers vector relative to the crystal "
            "below.\n\n"
            "Glide is conservative: no atom is created or destroyed. The two "
            "Burgers vectors cancel, so the cell stays periodic in all three "
            "directions.");
    case Builder::Type::Climb:
        return QObject::tr(
            "Two edge dislocations of opposite sign stacked normal to the "
            "glide plane, with a platelet of MISSING material between them — a "
            "collapsed vacancy disc one Burgers vector thick.\n\n"
            "Climb is non-conservative: it is mass transport, so this is the "
            "one construction that changes the atom count. The cell contracts "
            "to account for what was removed and stays fully periodic.\n\n"
            "Only the vacancy sense is built. The interstitial sense would "
            "mean inserting a partial plane of atoms, which needs the host "
            "lattice and not merely its Burgers vector.");
    case Builder::Type::Anisotropic:
        break;
    }
    return QObject::tr(
        "A single dislocation solved in a genuinely anisotropic medium, via "
        "Stroh's sextic formalism, from the full elastic tensor. This is the "
        "only option that can describe a MIXED dislocation — one whose Burgers "
        "vector has both an edge and a screw component — and the only one that "
        "is correct for a strongly anisotropic crystal.\n\n"
        "Elastic isotropy is a degenerate case of the formalism (a triple root "
        "at p = i). Isotropic constants are handled by lifting the degeneracy "
        "with a perturbation far below any physically meaningful difference; "
        "for a genuinely isotropic medium prefer the Edge or Screw type, whose "
        "closed forms are exact.");
}

bool isDipole(Builder::Type type)
{
    return type == Builder::Type::Glide || type == Builder::Type::Climb;
}

} // namespace

// ---------------------------------------------------------------------------
// Stage 1
// ---------------------------------------------------------------------------

DislocationTypePage::DislocationTypePage(DislocationWizard* wizard)
    : wizard_(wizard)
{
    setTitle(tr("Dislocation Type and Geometry"));
    setSubTitle(tr("Which line defect to insert, along which direction, and "
                   "with what Burgers vector."));

    auto* layout = new QVBoxLayout(this);
    auto* form = new QFormLayout;
    layout->addLayout(form);

    typeCombo_ = new QComboBox(this);
    typeCombo_->setObjectName(QStringLiteral("dislocationTypeCombo"));
    typeCombo_->addItem(tr("Edge dislocation"),
                        static_cast<int>(Builder::Type::Edge));
    typeCombo_->addItem(tr("Screw dislocation"),
                        static_cast<int>(Builder::Type::Screw));
    typeCombo_->addItem(tr("Glide dislocation (conservative dipole)"),
                        static_cast<int>(Builder::Type::Glide));
    typeCombo_->addItem(tr("Climb dislocation (vacancy platelet)"),
                        static_cast<int>(Builder::Type::Climb));
    typeCombo_->addItem(tr("Dislocation in an anisotropic medium"),
                        static_cast<int>(Builder::Type::Anisotropic));
    form->addRow(tr("Type:"), typeCombo_);

    lineCombo_ = new QComboBox(this);
    lineCombo_->addItem(tr("x"), static_cast<int>(Builder::Axis::X));
    lineCombo_->addItem(tr("y"), static_cast<int>(Builder::Axis::Y));
    lineCombo_->addItem(tr("z"), static_cast<int>(Builder::Axis::Z));
    lineCombo_->setCurrentIndex(2);
    lineCombo_->setToolTip(
        tr("The direction the dislocation line runs along. The other two axes "
           "follow in cyclic order: for a line along z they are (x, y), and an "
           "edge dislocation then has its Burgers vector along x and its glide "
           "plane normal along y."));
    form->addRow(tr("Line direction:"), lineCombo_);

    burgersSpin_ = new QDoubleSpinBox(this);
    burgersSpin_->setRange(0.01, 100.0);
    burgersSpin_->setDecimals(4);
    burgersSpin_->setSingleStep(0.1);
    burgersSpin_->setValue(2.5);
    burgersSpin_->setSuffix(tr(" Å"));
    burgersSpin_->setToolTip(
        tr("|b|. Use a lattice repeat of the host crystal — a perfect "
           "dislocation's Burgers vector IS a lattice vector, and any other "
           "length leaves a stacking fault trailing behind the line that this "
           "builder does not model."));
    form->addRow(tr("Burgers vector |b|:"), burgersSpin_);

    signCombo_ = new QComboBox(this);
    signCombo_->addItem(tr("positive (+b)"), 1);
    signCombo_->addItem(tr("negative (−b)"), -1);
    form->addRow(tr("Sign:"), signCombo_);

    auto* centerRow = new QHBoxLayout;
    for (int axis = 0; axis < 2; ++axis) {
        centerSpins_[axis] = new QDoubleSpinBox(this);
        centerSpins_[axis]->setRange(0.0, 1.0);
        centerSpins_[axis]->setDecimals(4);
        centerSpins_[axis]->setSingleStep(0.01);
        // Deliberately not exactly 0.5: linear elasticity is singular ON the
        // line, and a line placed exactly on a lattice site evaluates the
        // field at its own singularity for that atom.
        centerSpins_[axis]->setValue(0.5083);
        centerRow->addWidget(centerSpins_[axis], 1);
    }
    centerSpins_[0]->setToolTip(
        tr("Where the line sits in the plane normal to it, as a fraction of "
           "the structure's extent. The default is a little off centre on "
           "purpose: the elastic field is singular exactly on the line, so a "
           "line placed on a lattice site would evaluate that atom's "
           "displacement at infinity."));
    auto* centerWidget = new QWidget(this);
    centerWidget->setLayout(centerRow);
    form->addRow(tr("Line position (e₁, e₂):"), centerWidget);

    summaryLabel_ = new QLabel(this);
    summaryLabel_->setWordWrap(true);
    summaryLabel_->setTextFormat(Qt::RichText);
    layout->addWidget(summaryLabel_, 1);

    connect(typeCombo_, &QComboBox::currentIndexChanged, this,
            &DislocationTypePage::refresh);
    connect(lineCombo_, &QComboBox::currentIndexChanged, this,
            &DislocationTypePage::refresh);
    connect(signCombo_, &QComboBox::currentIndexChanged, this,
            &DislocationTypePage::refresh);
    connect(burgersSpin_, &QDoubleSpinBox::valueChanged, this,
            [this](double) { refresh(); });
    for (QDoubleSpinBox* spin : centerSpins_)
        connect(spin, &QDoubleSpinBox::valueChanged, this,
                [this](double) { refresh(); });
    refresh();
}

void DislocationTypePage::initializePage() { refresh(); }

bool DislocationTypePage::isComplete() const { return valid_; }

void DislocationTypePage::refresh()
{
    auto& params = wizard_->params;
    params.type = static_cast<Builder::Type>(typeCombo_->currentData().toInt());
    params.lineAxis =
        static_cast<Builder::Axis>(lineCombo_->currentData().toInt());
    params.burgers = burgersSpin_->value();
    params.burgersSign = signCombo_->currentData().toInt();
    params.center = {centerSpins_[0]->value(), centerSpins_[1]->value()};

    const core::Structure* source = wizard_->source.get();
    valid_ = source != nullptr && !source->empty()
        && source->cell().isDefined() && params.burgers > 0.0;

    QString text = typeExplanation(params.type);
    if (!source || source->empty())
        text = tr("<b>No structure.</b> Open or build a crystal first.");
    else if (!source->cell().isDefined())
        text = tr("<b>This structure has no periodic cell.</b> A dislocation "
                  "line is periodic along a lattice direction, so the host "
                  "needs one.");
    else
        text += tr("<br><br>Host: %1 atoms, %2.")
                    .arg(static_cast<int>(source->size()))
                    .arg(QString::fromStdString(source->chemicalFormula()));
    summaryLabel_->setText(text);
    Q_EMIT completeChanged();
}

// ---------------------------------------------------------------------------
// Stage 2
// ---------------------------------------------------------------------------

DislocationElasticityPage::DislocationElasticityPage(DislocationWizard* wizard)
    : wizard_(wizard)
{
    setTitle(tr("Elasticity"));
    setSubTitle(tr("The medium the displacement field is computed in, and the "
                   "parameters the chosen type needs."));

    auto* layout = new QVBoxLayout(this);
    form_ = new QFormLayout;
    layout->addLayout(form_);

    poissonSpin_ = new QDoubleSpinBox(this);
    poissonSpin_->setRange(0.0, 0.499);
    poissonSpin_->setDecimals(3);
    poissonSpin_->setSingleStep(0.01);
    poissonSpin_->setValue(0.33);
    poissonSpin_->setToolTip(
        tr("ν for the isotropic solution. It sets how much of the edge "
           "dislocation's displacement goes into the direction normal to the "
           "glide plane; a screw dislocation does not depend on it at all. "
           "0.5 is incompressible and the field is singular there."));
    form_->addRow(tr("Poisson ratio ν:"), poissonSpin_);

    separationSpin_ = new QDoubleSpinBox(this);
    separationSpin_->setRange(0.0, 100000.0);
    separationSpin_->setDecimals(3);
    separationSpin_->setSingleStep(1.0);
    separationSpin_->setValue(0.0);
    separationSpin_->setSpecialValueText(tr("auto (⅓ of the cell)"));
    separationSpin_->setSuffix(tr(" Å"));
    separationSpin_->setToolTip(
        tr("Distance between the two cores of the dipole — along the Burgers "
           "direction for glide (the distance the dislocation travelled) and "
           "normal to the glide plane for climb (the height of the vacancy "
           "platelet)."));
    form_->addRow(tr("Core separation:"), separationSpin_);

    symmetryCombo_ = new QComboBox(this);
    symmetryCombo_->addItem(tr("Cubic (C₁₁, C₁₂, C₄₄)"),
                            static_cast<int>(Builder::ElasticSymmetry::Cubic));
    symmetryCombo_->addItem(
        tr("Hexagonal (C₁₁, C₁₂, C₁₃, C₃₃, C₄₄)"),
        static_cast<int>(Builder::ElasticSymmetry::Hexagonal));
    symmetryCombo_->addItem(
        tr("Isotropic (C₁₁, C₁₂)"),
        static_cast<int>(Builder::ElasticSymmetry::Isotropic));
    form_->addRow(tr("Elastic symmetry:"), symmetryCombo_);

    auto* presetCombo = new QComboBox(this);
    presetCombo->addItem(tr("— pick a material —"), -1);
    for (int i = 0; i < static_cast<int>(std::size(kCubicPresets)); ++i)
        presetCombo->addItem(QLatin1String(kCubicPresets[i].name), i);
    presetCombo->setToolTip(
        tr("Room-temperature single-crystal constants, in GPa. Only the "
           "RATIOS matter to a displacement field — the absolute scale cancels "
           "— but keeping physical units means numbers can be read straight "
           "off a table."));
    form_->addRow(tr("Cubic presets:"), presetCombo);

    const auto makeConstant = [this](const QString& label, double value) {
        auto* spin = new QDoubleSpinBox(this);
        spin->setRange(0.1, 10000.0);
        spin->setDecimals(2);
        spin->setSingleStep(5.0);
        spin->setValue(value);
        spin->setSuffix(tr(" GPa"));
        form_->addRow(label, spin);
        return spin;
    };
    c11Spin_ = makeConstant(tr("C₁₁:"), 168.4);
    c12Spin_ = makeConstant(tr("C₁₂:"), 121.4);
    c44Spin_ = makeConstant(tr("C₄₄:"), 75.4);
    c13Spin_ = makeConstant(tr("C₁₃:"), 68.0);
    c33Spin_ = makeConstant(tr("C₃₃:"), 190.0);

    connect(presetCombo, &QComboBox::currentIndexChanged, this,
            [this, presetCombo](int) {
                const int index = presetCombo->currentData().toInt();
                if (index < 0)
                    return;
                const CubicPreset& preset =
                    kCubicPresets[static_cast<std::size_t>(index)];
                symmetryCombo_->setCurrentIndex(0);
                c11Spin_->setValue(preset.c11);
                c12Spin_->setValue(preset.c12);
                c44Spin_->setValue(preset.c44);
            });

    auto* directionRow = new QHBoxLayout;
    const char* const labels[3] = {"b₁", "b₂", "b₃"};
    for (int i = 0; i < 3; ++i) {
        burgersDirectionSpins_[i] = new QDoubleSpinBox(this);
        burgersDirectionSpins_[i]->setRange(-10.0, 10.0);
        burgersDirectionSpins_[i]->setDecimals(3);
        burgersDirectionSpins_[i]->setSingleStep(0.1);
        directionRow->addWidget(new QLabel(QLatin1String(labels[i]), this));
        directionRow->addWidget(burgersDirectionSpins_[i], 1);
    }
    burgersDirectionSpins_[0]->setValue(1.0);
    burgersDirectionSpins_[0]->setToolTip(
        tr("The Burgers vector's direction in the dislocation frame "
           "(e₁, e₂, e₃), normalized internally. (1, 0, 0) is a pure edge, "
           "(0, 0, 1) a pure screw, and anything between is a mixed "
           "dislocation — the case no isotropic closed form covers."));
    auto* directionWidget = new QWidget(this);
    directionWidget->setLayout(directionRow);
    form_->addRow(tr("Burgers direction:"), directionWidget);

    wrapCheck_ = new QCheckBox(tr("Wrap displaced atoms back into the cell"),
                               this);
    wrapCheck_->setToolTip(
        tr("On for a fully periodic dipole. Off for a single dislocation, "
           "whose lateral directions are free surfaces — wrapping there would "
           "fold the surface layers through the box."));
    form_->addRow(wrapCheck_);

    estimateLabel_ = new QLabel(this);
    estimateLabel_->setWordWrap(true);
    estimateLabel_->setTextFormat(Qt::RichText);
    layout->addWidget(estimateLabel_, 1);

    for (QDoubleSpinBox* spin : {poissonSpin_, separationSpin_, c11Spin_,
                                 c12Spin_, c44Spin_, c13Spin_, c33Spin_})
        connect(spin, &QDoubleSpinBox::valueChanged, this,
                [this](double) { refresh(); });
    for (QDoubleSpinBox* spin : burgersDirectionSpins_)
        connect(spin, &QDoubleSpinBox::valueChanged, this,
                [this](double) { refresh(); });
    connect(symmetryCombo_, &QComboBox::currentIndexChanged, this,
            &DislocationElasticityPage::refresh);
    connect(wrapCheck_, &QCheckBox::toggled, this,
            [this](bool) { refresh(); });
}

void DislocationElasticityPage::initializePage()
{
    // Wrapping is right for a dipole and wrong for a single dislocation, so it
    // follows the type unless the user has said otherwise on this visit.
    wrapCheck_->setChecked(isDipole(wizard_->params.type));
    applyTypeVisibility();
    refresh();
}

bool DislocationElasticityPage::isComplete() const { return true; }

void DislocationElasticityPage::applyTypeVisibility()
{
    const Builder::Type type = wizard_->params.type;
    const bool anisotropic = type == Builder::Type::Anisotropic;
    const bool hexagonal = anisotropic
        && static_cast<Builder::ElasticSymmetry>(
               symmetryCombo_->currentData().toInt())
            == Builder::ElasticSymmetry::Hexagonal;
    const bool isotropicTensor = anisotropic
        && static_cast<Builder::ElasticSymmetry>(
               symmetryCombo_->currentData().toInt())
            == Builder::ElasticSymmetry::Isotropic;

    form_->setRowVisible(poissonSpin_, !anisotropic);
    form_->setRowVisible(separationSpin_, isDipole(type));
    form_->setRowVisible(symmetryCombo_, anisotropic);
    form_->setRowVisible(c11Spin_, anisotropic);
    form_->setRowVisible(c12Spin_, anisotropic);
    form_->setRowVisible(c44Spin_, anisotropic && !isotropicTensor);
    form_->setRowVisible(c13Spin_, hexagonal);
    form_->setRowVisible(c33Spin_, hexagonal);
    form_->setRowVisible(burgersDirectionSpins_[0]->parentWidget(),
                         anisotropic);
}

void DislocationElasticityPage::refresh()
{
    auto& params = wizard_->params;
    params.poisson = poissonSpin_->value();
    params.dipoleSeparation = separationSpin_->value();
    params.symmetry = static_cast<Builder::ElasticSymmetry>(
        symmetryCombo_->currentData().toInt());
    params.c11 = c11Spin_->value();
    params.c12 = c12Spin_->value();
    params.c44 = c44Spin_->value();
    params.c13 = c13Spin_->value();
    params.c33 = c33Spin_->value();
    params.burgersDirection = {burgersDirectionSpins_[0]->value(),
                               burgersDirectionSpins_[1]->value(),
                               burgersDirectionSpins_[2]->value()};
    params.wrapIntoCell = wrapCheck_->isChecked();
    applyTypeVisibility();

    // Actually build it. The estimate is not a model of what the builder will
    // do — it IS what the builder does, run on the same parameters, so the two
    // cannot disagree.
    QString error;
    if (!wizard_->build(&error)) {
        estimateLabel_->setText(
            tr("<span style='color:#c0392b'>%1</span>").arg(error));
        return;
    }
    const auto& result = *wizard_->result();
    QStringList lines;
    lines << tr("<b>%1</b>").arg(QString::fromStdString(result.description));
    lines << tr("%1 atoms; largest displacement %2 Å.")
                 .arg(static_cast<int>(result.structure.size()))
                 .arg(result.maxDisplacement, 0, 'f', 3);
    if (result.atomsRemoved > 0)
        lines << tr("%1 atoms removed into the vacancy platelet.")
                     .arg(result.atomsRemoved);
    if (result.minSeparation > 0.0)
        lines << tr("Closest pair afterwards: %1 Å.")
                     .arg(result.minSeparation, 0, 'f', 3);
    for (const std::string& warning : result.warnings)
        lines << tr("<span style='color:#b9770e'>⚠ %1</span>")
                     .arg(QString::fromStdString(warning));
    lines << tr("<i>Linear elasticity is singular on the line: the innermost "
                "atoms are placed by a formula that does not apply to them. "
                "Relax the core before quoting anything about it.</i>");
    estimateLabel_->setText(lines.join(QStringLiteral("<br>")));
}

bool DislocationElasticityPage::validatePage()
{
    QString error;
    if (wizard_->build(&error))
        return true;
    estimateLabel_->setText(
        tr("<span style='color:#c0392b'>%1</span>").arg(error));
    return false;
}

// ---------------------------------------------------------------------------
// Wizard
// ---------------------------------------------------------------------------

DislocationWizard::DislocationWizard(
    std::shared_ptr<const core::Structure> sourceStructure, QWidget* parent)
    : QWizard(parent)
    , source(std::move(sourceStructure))
{
    setWindowTitle(tr("Dislocation Builder"));
    setWizardStyle(QWizard::ModernStyle);
    setOption(QWizard::NoBackButtonOnStartPage, true);
    addPage(new DislocationTypePage(this));
    addPage(new DislocationElasticityPage(this));
    resize(640, 620);
}

bool DislocationWizard::build(QString* error)
{
    result_.reset();
    if (!source) {
        if (error)
            *error = tr("No structure to displace.");
        return false;
    }
    try {
        result_ = core::DislocationBuilder::generate(*source, params);
    } catch (const std::exception& failure) {
        if (error)
            *error = QString::fromUtf8(failure.what());
        return false;
    }
    return true;
}

} // namespace calango::gui
