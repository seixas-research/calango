#include "gui/GuiUtils.hpp"

#include <QAbstractItemView>

#include "core/CalculatorConfig.hpp"
#include "core/Element.hpp"
#include "core/Structure.hpp"

#include <cmath>

#include <QCheckBox>
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QFormLayout>
#include <QGroupBox>
#include <QHash>
#include <QFile>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QFontMetricsF>
#include <QLabel>
#include <QMouseEvent>
#include <QImage>
#include <QPainter>
#include <QRectF>
#include <QMessageBox>
#include <QPushButton>
#include <QSaveFile>
#include <QTextStream>
#include <QWidget>

#include <algorithm>
#include <set>
#include <vector>

namespace calango::gui {

void disableTypeToEdit(QAbstractItemView* view)
{
    if (!view)
        return;
    // Everything except AnyKeyPressed. See the header for why: AnyKeyPressed
    // is the one trigger that turns a dead key into an unbounded recursion
    // through the Cocoa input context.
    view->setEditTriggers(QAbstractItemView::DoubleClicked
                          | QAbstractItemView::SelectedClicked
                          | QAbstractItemView::EditKeyPressed);
}

QString volumetricDisplayName(const QString& fileName)
{
    // Keyed off core::densityFiles rather than off repeated string literals:
    // the generator and this table are the two ends of one contract, and when
    // they were written out independently they drifted — the dock looked up
    // "hartree_potential.cube" while the script wrote "potential_hartree.cube",
    // so the Hartree potential arrived labelled with its raw file name.
    static const QHash<QString, QString> kLabels = {
        {QLatin1String(core::densityFiles::kAllElectron),
         QCoreApplication::translate("calango::gui", "All-electron density")},
        {QLatin1String(core::densityFiles::kPseudo),
         QCoreApplication::translate("calango::gui", "Pseudodensity")},
        {QLatin1String(core::densityFiles::kSpin),
         QCoreApplication::translate("calango::gui", "Spin density")},
        {QLatin1String(core::densityFiles::kHartree),
         QCoreApplication::translate("calango::gui", "Hartree potential")},
        {QLatin1String(core::densityFiles::kElf),
         QCoreApplication::translate("calango::gui", "ELF η(r)")},
        {QLatin1String(core::densityFiles::kKineticEnergy),
         QCoreApplication::translate("calango::gui",
                                     "Kinetic energy density τ(r)")},
        {QLatin1String(core::densityFiles::kDensity),
         QCoreApplication::translate("calango::gui", "Charge density")},
        {QLatin1String(core::densityFiles::kChargeDensityDifference),
         QCoreApplication::translate("calango::gui",
                                     "Charge density difference Δρ")},
        // VASP writes its grids without an extension, so they are matched by
        // their exact file name.
        {QStringLiteral("CHGCAR"),
         QCoreApplication::translate("calango::gui", "Charge density (CHGCAR)")},
        {QStringLiteral("AECCAR0"),
         QCoreApplication::translate("calango::gui",
                                     "Core charge density (AECCAR0)")},
        {QStringLiteral("AECCAR2"),
         QCoreApplication::translate("calango::gui",
                                     "All-electron density (AECCAR2)")},
        {QStringLiteral("LOCPOT"),
         QCoreApplication::translate("calango::gui", "Local potential (LOCPOT)")},
        {QStringLiteral("ELFCAR"),
         QCoreApplication::translate("calango::gui", "ELF η(r) (ELFCAR)")},
    };
    // An HDF5-compressed density is named by APPENDING ".h5" to whatever the
    // original file was called (see
    // MainWindow::compressDensityFilesIfRequested) — "density_all_electron
    // .cube" -> "....cube.h5", "CHGCAR" -> "CHGCAR.h5" — rather than
    // replacing its extension, so the original name (and this table's key
    // for it) is recoverable just by chopping the suffix back off.
    if (fileName.endsWith(QStringLiteral(".h5"), Qt::CaseInsensitive)) {
        const auto it = kLabels.find(fileName.chopped(3));
        if (it != kLabels.end())
            return QCoreApplication::translate("calango::gui", "%1 (HDF5)")
                .arg(it.value());
    }
    return kLabels.value(fileName, fileName);
}

QStringList structureElements(const core::Structure* structure)
{
    if (!structure)
        return {};
    // std::set rather than sorting a list afterwards: it de-duplicates and
    // orders in one pass, and the counts here are tens of species at most.
    std::set<QString> symbols;
    for (const core::Atom& atom : structure->atoms())
        symbols.insert(QString::fromLatin1(atom.symbol()));
    QStringList result;
    result.reserve(static_cast<qsizetype>(symbols.size()));
    for (const QString& symbol : symbols)
        result << symbol;
    return result;
}

int guessVacuumAxis(const core::Structure* structure)
{
    if (!structure || !structure->cell().isDefined() || structure->empty())
        return -1;
    // The vacuum axis is the one whose atoms span far less than the cell.
    // The threshold is deliberately high: a false positive silently rescales
    // every sheet quantity, while a false negative only means the user picks
    // the axis themselves.
    int best = -1;
    double bestEmptiness = 0.35;
    for (int axis = 0; axis < 3; ++axis) {
        double lo = 1.0;
        double hi = 0.0;
        for (const core::Atom& atom : structure->atoms()) {
            const core::Vec3 f =
                structure->cell().cartesianToFractional(atom.position);
            const double v = axis == 0 ? f.x : (axis == 1 ? f.y : f.z);
            lo = std::min(lo, v);
            hi = std::max(hi, v);
        }
        const double emptiness = 1.0 - (hi - lo);
        if (emptiness > bestEmptiness) {
            bestEmptiness = emptiness;
            best = axis;
        }
    }
    return best;
}

bool writeTextFile(QWidget* parent, const QString& path,
                   const std::function<void(QTextStream&)>& body)
{
    // QSaveFile, not QFile: an export that fails halfway through must not
    // leave a truncated file where the previous good one was.
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QMessageBox::warning(
            parent, parent ? parent->windowTitle() : QString(),
            QCoreApplication::translate("calango::gui",
                                        "Could not write %1")
                .arg(path));
        return false;
    }
    {
        QTextStream out(&file);
        body(out);
    }
    if (!file.commit()) {
        QMessageBox::warning(
            parent, parent ? parent->windowTitle() : QString(),
            QCoreApplication::translate("calango::gui",
                                        "Could not write %1")
                .arg(path));
        return false;
    }
    return true;
}

bool writeTextFile(QWidget* parent, const QString& path, const QString& body)
{
    return writeTextFile(parent, path,
                         [&body](QTextStream& out) { out << body; });
}

double niceTickStep(double range, int maxTicks, double degenerate)
{
    if (range <= 0.0 || maxTicks < 1)
        return degenerate;
    const double rough = range / maxTicks;
    const double magnitude = std::pow(10.0, std::floor(std::log10(rough)));
    const double normalized = rough / magnitude; // in [1, 10)
    const double nice = normalized <= 1.0 ? 1.0
        : normalized <= 2.0              ? 2.0
        : normalized <= 5.0              ? 5.0
                                         : 10.0;
    return nice * magnitude;
}

void savePlotImage(QWidget* parent, const QString& path,
                   const QSize& logicalSize,
                   const std::function<void(QPainter&, const QSize&)>& renderTo)
{
    // Render at 3x the on-screen size for a crisp, print-quality raster.
    const int scale = 3;
    QImage image(logicalSize * scale, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::white);
    QPainter painter(&image);
    painter.scale(scale, scale);
    renderTo(painter, logicalSize);
    painter.end();

    if (!image.save(path))
        QMessageBox::warning(
            parent, QCoreApplication::translate("calango::gui", "Export Image"),
            QCoreApplication::translate("calango::gui",
                                        "Could not write the image to %1.")
                .arg(path));
}


void setFormRowVisible(QGroupBox* group, QWidget* field, bool visible)
{
    if (!group || !field)
        return;
    auto* form = qobject_cast<QFormLayout*>(group->layout());
    if (!form)
        return;
    int row = -1;
    QFormLayout::ItemRole role{};
    form->getWidgetPosition(field, &row, &role);
    if (row >= 0)
        form->setRowVisible(row, visible);
}

/// Draw `text` centered in `box`, rendering "_x" as a typographic subscript
/// (smaller font, dropped baseline). Qt ships no LaTeX engine and this
/// project has no QCustomPlot / MathJax dependency, so the two-run layout
/// below is what actually produces "E − E_F (eV)" with a proper subscript
/// instead of a literal underscore.
void drawWithSubscripts(QPainter& painter, const QRectF& box,
                        const QString& text)
{
    // Split into (run, isSubscript) pairs: "_" introduces a one-character
    // subscript, "_{...}" a braced multi-character one.
    struct Run {
        QString text;
        bool subscript;
    };
    std::vector<Run> runs;
    for (int i = 0; i < text.size(); ++i) {
        if (text.at(i) == QLatin1Char('_') && i + 1 < text.size()) {
            if (text.at(i + 1) == QLatin1Char('{')) {
                const int close = text.indexOf(QLatin1Char('}'), i + 2);
                if (close > 0) {
                    runs.push_back({text.mid(i + 2, close - i - 2), true});
                    i = close;
                    continue;
                }
            }
            runs.push_back({text.mid(i + 1, 1), true});
            ++i;
            continue;
        }
        if (runs.empty() || runs.back().subscript)
            runs.push_back({QString(), false});
        runs.back().text.append(text.at(i));
    }

    const QFont baseFont = painter.font();
    QFont subFont = baseFont;
    subFont.setPointSizeF(std::max(baseFont.pointSizeF() * 0.72, 6.0));
    const QFontMetricsF baseMetrics(baseFont);
    const QFontMetricsF subMetrics(subFont);

    double width = 0.0;
    for (const Run& run : runs) {
        width += (run.subscript ? subMetrics : baseMetrics)
                     .horizontalAdvance(run.text);
    }

    double x = box.center().x() - width / 2.0;
    const double baseline = box.center().y() + baseMetrics.ascent() / 2.0
        - baseMetrics.descent() / 2.0;
    const double drop = baseMetrics.descent() * 0.75;
    for (const Run& run : runs) {
        painter.setFont(run.subscript ? subFont : baseFont);
        painter.drawText(QPointF(x, run.subscript ? baseline + drop : baseline),
                         run.text);
        x += (run.subscript ? subMetrics : baseMetrics)
                 .horizontalAdvance(run.text);
    }
    painter.setFont(baseFont);
}

namespace {

/// Forwards a left click on a rich-text caption to the check box it labels, so
/// richTextCheckBox() behaves like the single widget it looks like. No
/// Q_OBJECT: it declares no signals or slots, only the virtual it overrides.
class CaptionClickForwarder : public QObject {
public:
    CaptionClickForwarder(QCheckBox* box, QObject* parent)
        : QObject(parent), box_(box)
    {
    }

protected:
    bool eventFilter(QObject* watched, QEvent* event) override
    {
        if (event->type() == QEvent::MouseButtonRelease && box_
            && box_->isEnabled()
            && static_cast<QMouseEvent*>(event)->button() == Qt::LeftButton) {
            box_->toggle();
            return true;
        }
        return QObject::eventFilter(watched, event);
    }

private:
    QCheckBox* box_;
};

} // namespace

QWidget* richTextCheckBox(const QString& html, QCheckBox*& box, QWidget* parent)
{
    auto* row = new QWidget(parent);
    auto* layout = new QHBoxLayout(row);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(4);
    box = new QCheckBox(row);
    auto* caption = new QLabel(html, row);
    caption->setTextFormat(Qt::RichText);
    // The buddy makes the caption's mnemonic (if the caller wrote one) reach
    // the box; the filter makes a plain click do the same.
    caption->setBuddy(box);
    caption->installEventFilter(new CaptionClickForwarder(box, caption));
    layout->addWidget(box);
    layout->addWidget(caption);
    layout->addStretch(1);
    return row;
}

QColor cpkColor(int atomicNumber)
{
    const calango::core::ElementData& data =
        calango::core::Elements::data(atomicNumber);
    return {data.rgb[0], data.rgb[1], data.rgb[2]};
}

QColor readableTextColor(const QColor& background)
{
    // WCAG relative luminance, then simply take whichever of black and white
    // CONTRASTS MORE. No threshold to tune, and provably the better of the two
    // choices for every possible background.
    //
    // A luma threshold was tried first and is not good enough here: Rec. 601
    // weights green at 0.587, so a saturated bright green like thulium's
    // #00D452 scores below any sensible cut-off and gets white text at a 1.99
    // contrast ratio — illegible. Measured over all 119 CPK colours, choosing
    // by contrast lifts the worst case to 4.12 and puts every element above
    // the 3.0 WCAG AA bar for large/bold text (which is what these 36x32 bold
    // swatch buttons are).
    const auto channel = [](double c) {
        return c <= 0.03928 ? c / 12.92 : std::pow((c + 0.055) / 1.055, 2.4);
    };
    const auto relativeLuminance = [&channel](const QColor& c) {
        return 0.2126 * channel(c.redF()) + 0.7152 * channel(c.greenF())
            + 0.0722 * channel(c.blueF());
    };
    const auto contrast = [](double a, double b) {
        return (std::max(a, b) + 0.05) / (std::min(a, b) + 0.05);
    };

    // Not pure black: #202020 matches the text colour used elsewhere in the
    // widget layer and is only a hair less contrasty.
    const QColor dark(0x20, 0x20, 0x20);
    const QColor light(0xFF, 0xFF, 0xFF);
    const double luminance = relativeLuminance(background);
    return contrast(relativeLuminance(dark), luminance)
            >= contrast(relativeLuminance(light), luminance)
        ? dark
        : light;
}

QJsonObject readJsonObject(const QString& path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return {};
    return QJsonDocument::fromJson(file.readAll()).object();
}

std::vector<double> toDoubleVector(const QJsonArray& array)
{
    std::vector<double> values;
    values.reserve(static_cast<std::size_t>(array.size()));
    for (const auto& value : array)
        values.push_back(value.toDouble());
    return values;
}


// ---------------------------------------------------------------------------
// CompactDoubleSpinBox
// ---------------------------------------------------------------------------

CompactDoubleSpinBox::CompactDoubleSpinBox(QWidget* parent)
    : QDoubleSpinBox(parent)
{
    // The base class rounds the STORED value to `decimals` places, so a low
    // setting would quantize the value itself and not merely its display. Keep
    // full precision internally and do all the shortening in textFromValue().
    setDecimals(12);
}

QString CompactDoubleSpinBox::textFromValue(double value) const
{
    if (value == 0.0)
        return QStringLiteral("0");
    const double magnitude = std::abs(value);
    // Outside this window a fixed-point rendering is either all leading zeros
    // or unreadably long, so switch to exponential.
    if (magnitude < 1e-3 || magnitude >= 1e5)
        return QString::number(value, 'e', 2); // 1.23e-02
    // Three significant figures: 'g' picks the shorter of fixed/exponential and
    // drops trailing zeros, which is exactly the compact form wanted here.
    return QString::number(value, 'g', 3);
}

double CompactDoubleSpinBox::valueFromText(const QString& text) const
{
    QString cleaned = text;
    cleaned.remove(prefix()).remove(suffix());
    bool ok = false;
    const double parsed = cleaned.trimmed().toDouble(&ok);
    return ok ? parsed : value();
}

QValidator::State CompactDoubleSpinBox::validate(QString& input, int& pos) const
{
    Q_UNUSED(pos);
    QString cleaned = input;
    cleaned.remove(prefix()).remove(suffix());
    cleaned = cleaned.trimmed();
    if (cleaned.isEmpty() || cleaned == QLatin1String("-")
        || cleaned == QLatin1String("+"))
        return QValidator::Intermediate;
    bool ok = false;
    const double parsed = cleaned.toDouble(&ok);
    if (!ok) {
        // A half-typed exponent ("1.2e", "1e-") is not a number yet but is on
        // its way to one; rejecting it would block the keystroke.
        return cleaned.endsWith(QLatin1Char('e'), Qt::CaseInsensitive)
                || cleaned.endsWith(QLatin1String("e-"), Qt::CaseInsensitive)
                || cleaned.endsWith(QLatin1String("e+"), Qt::CaseInsensitive)
            ? QValidator::Intermediate
            : QValidator::Invalid;
    }
    return parsed < minimum() || parsed > maximum() ? QValidator::Intermediate
                                                    : QValidator::Acceptable;
}

bool mlwfWavefunctionsAvailable(const QString& jobDir, QString* reason)
{
    const QDir dir(jobDir);
    const QJsonObject meta =
        readJsonObject(dir.filePath(QStringLiteral("wannier.json")));
    const QString recorded = meta.value(QStringLiteral("gpw")).toString();
    if (!recorded.isEmpty() && QFileInfo::exists(recorded))
        return true;
    if (!dir.entryList({QStringLiteral("*.gpw")}, QDir::Files).isEmpty())
        return true;
    if (reason) {
        *reason = recorded.isEmpty()
            ? QCoreApplication::translate(
                  "MlwfPreflight",
                  "This Wannier Functions run recorded no path to the GPAW wavefunctions "
                  "it localized, and left no .gpw in its own directory. "
                  "Re-run the Wannier Functions calculation — runs from this version "
                  "record the path.")
            : QCoreApplication::translate(
                  "MlwfPreflight",
                  "The GPAW wavefunctions this Wannier Functions run localized are no "
                  "longer at\n\n%1\n\nRe-run the Wannier Functions calculation, or restore "
                  "that file.")
                  .arg(recorded);
    }
    return false;
}

std::vector<int> parseAtomIndexList(const QString& text, int atomCount)
{
    // "0, 2, 5-8" — commas and/or whitespace separate entries, a dash makes a
    // closed range. Out-of-range entries are dropped rather than clamped: a
    // typo that silently addressed a different atom would be worse than one
    // that visibly does nothing. The dash search starts at 1 so a leading
    // minus sign is not mistaken for a range.
    std::vector<int> indices;
    const QString trimmed = text.trimmed();
    if (trimmed.isEmpty())
        return indices;
    std::set<int> unique;
    const QStringList parts =
        trimmed.split(QRegularExpression(QStringLiteral("[,\\s]+")),
                      Qt::SkipEmptyParts);
    for (const QString& part : parts) {
        const int dash = part.indexOf(QLatin1Char('-'), 1);
        if (dash > 0) {
            bool okLow = false;
            bool okHigh = false;
            const int low = part.left(dash).toInt(&okLow);
            const int high = part.mid(dash + 1).toInt(&okHigh);
            if (okLow && okHigh)
                for (int i = low; i <= high; ++i)
                    if (i >= 0 && (atomCount == 0 || i < atomCount))
                        unique.insert(i);
            continue;
        }
        bool ok = false;
        const int value = part.toInt(&ok);
        if (ok && value >= 0 && (atomCount == 0 || value < atomCount))
            unique.insert(value);
    }
    indices.assign(unique.begin(), unique.end());
    return indices;
}

/// Schönflies symbol of a crystallographic point group given its
/// Hermann-Mauguin (international) symbol as spglib reports it — e.g.
/// "3m" → "C<sub>3v</sub>", "m-3m" → "O<sub>h</sub>". Rich text (HTML
/// subscripts). Empty when the symbol is not one of the 32 crystallographic
/// point groups. Internal: reached through pointGroupDisplay().
static QString schoenfliesPointGroup(const QString& hermannMauguin)
{
    // The 32 crystallographic point groups, keyed by the short international
    // symbol exactly as spglib prints it (overbar as a leading '-'). Symbols
    // arrive with stray spaces from some spglib versions, hence the cleanup.
    static const QHash<QString, QString> kTable = {
        {QStringLiteral("1"), QStringLiteral("C<sub>1</sub>")},
        {QStringLiteral("-1"), QStringLiteral("C<sub>i</sub>")},
        {QStringLiteral("2"), QStringLiteral("C<sub>2</sub>")},
        {QStringLiteral("m"), QStringLiteral("C<sub>s</sub>")},
        {QStringLiteral("2/m"), QStringLiteral("C<sub>2h</sub>")},
        {QStringLiteral("222"), QStringLiteral("D<sub>2</sub>")},
        {QStringLiteral("mm2"), QStringLiteral("C<sub>2v</sub>")},
        {QStringLiteral("mmm"), QStringLiteral("D<sub>2h</sub>")},
        {QStringLiteral("4"), QStringLiteral("C<sub>4</sub>")},
        {QStringLiteral("-4"), QStringLiteral("S<sub>4</sub>")},
        {QStringLiteral("4/m"), QStringLiteral("C<sub>4h</sub>")},
        {QStringLiteral("422"), QStringLiteral("D<sub>4</sub>")},
        {QStringLiteral("4mm"), QStringLiteral("C<sub>4v</sub>")},
        {QStringLiteral("-42m"), QStringLiteral("D<sub>2d</sub>")},
        {QStringLiteral("-4m2"), QStringLiteral("D<sub>2d</sub>")},
        {QStringLiteral("4/mmm"), QStringLiteral("D<sub>4h</sub>")},
        {QStringLiteral("3"), QStringLiteral("C<sub>3</sub>")},
        {QStringLiteral("-3"), QStringLiteral("C<sub>3i</sub>")},
        {QStringLiteral("32"), QStringLiteral("D<sub>3</sub>")},
        {QStringLiteral("3m"), QStringLiteral("C<sub>3v</sub>")},
        {QStringLiteral("-3m"), QStringLiteral("D<sub>3d</sub>")},
        {QStringLiteral("6"), QStringLiteral("C<sub>6</sub>")},
        {QStringLiteral("-6"), QStringLiteral("C<sub>3h</sub>")},
        {QStringLiteral("6/m"), QStringLiteral("C<sub>6h</sub>")},
        {QStringLiteral("622"), QStringLiteral("D<sub>6</sub>")},
        {QStringLiteral("6mm"), QStringLiteral("C<sub>6v</sub>")},
        {QStringLiteral("-6m2"), QStringLiteral("D<sub>3h</sub>")},
        {QStringLiteral("-62m"), QStringLiteral("D<sub>3h</sub>")},
        {QStringLiteral("6/mmm"), QStringLiteral("D<sub>6h</sub>")},
        {QStringLiteral("23"), QStringLiteral("T")},
        {QStringLiteral("m-3"), QStringLiteral("T<sub>h</sub>")},
        {QStringLiteral("m3"), QStringLiteral("T<sub>h</sub>")},
        {QStringLiteral("432"), QStringLiteral("O")},
        {QStringLiteral("-43m"), QStringLiteral("T<sub>d</sub>")},
        {QStringLiteral("m-3m"), QStringLiteral("O<sub>h</sub>")},
        {QStringLiteral("m3m"), QStringLiteral("O<sub>h</sub>")},
    };
    QString key = hermannMauguin;
    key.remove(QLatin1Char(' '));
    return kTable.value(key);
}

QString pointGroupDisplay(const QString& hermannMauguin)
{
    const QString trimmed = hermannMauguin.trimmed();
    const QString schoenflies = schoenfliesPointGroup(trimmed);
    return schoenflies.isEmpty()
        ? trimmed
        : QStringLiteral("%1 (%2)").arg(trimmed, schoenflies);
}

// ---------------------------------------------------------------------------
// Structure file I/O filters
// ---------------------------------------------------------------------------

/// ASE's format name for Extended XYZ, as passed to write()/read().
static QString defaultStructureFormat()
{
    return QStringLiteral("extxyz");
}

/// ".extxyz" — the suffix a structure is saved with unless told otherwise.
static QString defaultStructureSuffix()
{
    return QStringLiteral(".extxyz");
}

QString defaultStructureFileName(const QString& stem)
{
    // Strip whatever extension the source file had — a document opened from
    // "quartz.cif" should be offered as "quartz.extxyz", not "quartz.cif" with
    // an extxyz filter selected, which is how a CIF-named file ends up holding
    // XYZ text.
    QString base = QFileInfo(stem.trimmed()).completeBaseName();
    // Path separators would send the save somewhere the user did not point at.
    base.remove(QLatin1Char('/'));
    base.remove(QLatin1Char('\\'));
    if (base.isEmpty())
        base = QStringLiteral("structure");
    return base + defaultStructureSuffix();
}

QString structureOpenFilters()
{
    return QCoreApplication::translate(
        "calango::gui",
        // Extended XYZ first, so it is what the dialog pre-selects. Both
        // spellings are in the pattern: ASE writes extended XYZ into plain
        // .xyz files too, and a user with such a file should not have to
        // switch filters to see it.
        "Extended XYZ (*.extxyz *.xyz);;"
        "All supported structures (*.xyz *.extxyz *.cif *.pdb POSCAR CONTCAR "
        "*.vasp *.traj *.in *.pwi *.pwo *.out *.cell *.data *.dump *.lammpstrj "
        "*.gjf *.com *.res);;"
        // Both CIF flavours share the extension; the reader tells them apart
        // by content, so one filter serves both.
        "CIF / PDBx-mmCIF (*.cif);;"
        "Protein Data Bank (*.pdb);;"
        "VASP (POSCAR CONTCAR *.vasp);;"
        "ASE trajectory (*.traj);;"
        "Quantum ESPRESSO (*.in *.pwi *.pwo *.out);;"
        "CASTEP (*.cell);;"
        "LAMMPS (*.data *.dump *.lammpstrj);;"
        "Gaussian (*.gjf *.com);;"
        "SHELX (*.res);;"
        "All files (*)");
}

QString trajectoryOpenFilters()
{
    return QCoreApplication::translate(
        "calango::gui",
        "Extended XYZ trajectory (*.extxyz *.xyz);;"
        "All supported trajectories (*.traj *.extxyz *.xyz *.pdb);;"
        "ASE trajectory (*.traj);;"
        "PDB multi-model (*.pdb);;"
        "All files (*)");
}

const QList<QPair<QString, QString>>& structureSaveFormats()
{
    static const QList<QPair<QString, QString>> kFormats = {
        {QCoreApplication::translate("calango::gui", "Extended XYZ (*.extxyz)"),
         QStringLiteral("extxyz")},
        {QCoreApplication::translate("calango::gui", "XYZ (*.xyz)"),
         QStringLiteral("xyz")},
        {QCoreApplication::translate("calango::gui", "CIF (*.cif)"),
         QStringLiteral("cif")},
        // PDBx shares the .cif extension with the crystallographic CIF above,
        // so the two are separate filters rather than one: which of them the
        // user wants cannot be read off the file name, only off the choice.
        {QCoreApplication::translate("calango::gui", "PDBx / mmCIF (*.cif)"),
         QStringLiteral("pdbx")},
        {QCoreApplication::translate("calango::gui", "VASP POSCAR (*.vasp)"),
         QStringLiteral("vasp")},
        {QCoreApplication::translate("calango::gui",
                                     "Quantum ESPRESSO input (*.pwi *.in)"),
         QStringLiteral("espresso-in")},
        {QCoreApplication::translate("calango::gui", "LAMMPS data (*.data)"),
         QStringLiteral("lammps-data")},
        {QCoreApplication::translate("calango::gui", "CASTEP cell (*.cell)"),
         QStringLiteral("castep-cell")},
        {QCoreApplication::translate("calango::gui",
                                     "Gaussian input (*.com *.gjf)"),
         QStringLiteral("gaussian-in")},
        {QCoreApplication::translate("calango::gui", "SHELX (*.res)"),
         QStringLiteral("res")},
    };
    return kFormats;
}

const QList<QPair<QString, QString>>& trajectorySaveFormats()
{
    static const QList<QPair<QString, QString>> kFormats = {
        {QCoreApplication::translate("calango::gui",
                                     "Extended XYZ trajectory (*.extxyz)"),
         QStringLiteral("extxyz")},
        {QCoreApplication::translate("calango::gui", "XYZ multi-frame (*.xyz)"),
         QStringLiteral("xyz")},
        {QCoreApplication::translate("calango::gui", "ASE trajectory (*.traj)"),
         QStringLiteral("traj")},
        {QCoreApplication::translate("calango::gui", "PDB multi-model (*.pdb)"),
         QStringLiteral("proteindatabank")},
    };
    return kFormats;
}

QString formatForFilter(const QList<QPair<QString, QString>>& formats,
                        const QString& filter)
{
    for (const auto& entry : formats)
        if (entry.first == filter)
            return entry.second;
    return formats.isEmpty() ? defaultStructureFormat() : formats.front().second;
}

QString withFilterSuffix(const QString& path, const QString& filter)
{
    if (path.isEmpty() || !QFileInfo(path).suffix().isEmpty())
        return path;
    // The first "*.ext" inside the parentheses is the filter's primary
    // extension. Filters whose pattern is a bare file name (VASP's "POSCAR")
    // have none, and a path typed against one of those is left alone — POSCAR
    // is a complete file name already.
    const int open = filter.indexOf(QLatin1Char('('));
    if (open < 0)
        return path;
    const int star = filter.indexOf(QLatin1String("*."), open);
    if (star < 0)
        return path;
    int end = star + 2;
    while (end < filter.size() && (filter.at(end).isLetterOrNumber()))
        ++end;
    const QString suffix = filter.mid(star + 1, end - star - 1);
    return suffix.size() > 1 ? path + suffix : path;
}

} // namespace calango::gui
