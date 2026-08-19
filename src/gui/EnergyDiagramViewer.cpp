#include "gui/EnergyDiagramViewer.hpp"

#include "gui/GuiUtils.hpp"

#include <QFile>
#include <QFileDialog>
#include <QFont>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>
#include <QStringList>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QVBoxLayout>

#include <algorithm>

namespace calango::gui {

// ---------------------------------------------------------------------
// EnergyLevelDiagramWidget
// ---------------------------------------------------------------------

EnergyLevelDiagramWidget::EnergyLevelDiagramWidget(QWidget* parent)
    : QWidget(parent)
{
    setMinimumSize(220, 260);
    setToolTip(tr("Click a level to see its energy, occupation and "
                 "degeneracy. Occupied levels are drawn solid, virtual ones "
                 "hollow. The dashed line marks the HOMO-LUMO gap."));
}

void EnergyLevelDiagramWidget::setLevels(
    const std::vector<EnergyLevelDiagramEntry>& levels, int nspins)
{
    levels_ = levels;
    nspins_ = std::max(1, nspins);
    if (levels_.empty()) {
        eMin_ = -1.0;
        eMax_ = 1.0;
    } else {
        eMin_ = eMax_ = levels_.front().energyEv;
        for (const auto& lv : levels_) {
            eMin_ = std::min(eMin_, lv.energyEv);
            eMax_ = std::max(eMax_, lv.energyEv);
        }
        const double pad = std::max(0.3, (eMax_ - eMin_) * 0.08);
        eMin_ -= pad;
        eMax_ += pad;
    }
    selected_ = -1;
    update();
}

void EnergyLevelDiagramWidget::setSelected(int index)
{
    if (selected_ == index)
        return;
    selected_ = index;
    update();
}

void EnergyLevelDiagramWidget::setStyle(const EnergyDiagramStyle& style)
{
    style_ = style;
    update();
}

double EnergyLevelDiagramWidget::yFromEnergy(double e, double heightPx) const
{
    const double top = 24.0;
    const double bottom = heightPx - 30.0;
    const double span = eMax_ - eMin_;
    const double t = span > 0.0 ? (e - eMin_) / span : 0.5;
    // Energy increases UPWARD, screen y increases downward.
    return bottom - t * (bottom - top);
}

void EnergyLevelDiagramWidget::paintEvent(QPaintEvent*)
{
    QPainter painter(this);
    renderTo(painter, size());
}

void EnergyLevelDiagramWidget::renderTo(QPainter& painter, const QSize& size) const
{
    const double W = size.width();
    const double H = size.height();

    painter.fillRect(QRectF(0, 0, W, H), style_.canvasBackground);
    lastBars_.clear();

    if (levels_.empty()) {
        painter.setPen(style_.placeholderColor);
        painter.drawText(QRectF(0, 0, W, H), Qt::AlignCenter, tr("No levels"));
        return;
    }

    const double margin = 16.0;
    const double columnWidth = (W - 2.0 * margin) / static_cast<double>(nspins_);

    // HOMO/LUMO across every spin channel — the widest occupied energy and
    // the narrowest virtual one, exactly what EnergyDiagramScriptGenerator
    // itself reports as 'homo'/'lumo'.
    const EnergyLevelDiagramEntry* homo = nullptr;
    const EnergyLevelDiagramEntry* lumo = nullptr;
    for (const auto& lv : levels_) {
        if (lv.occupied() && (!homo || lv.energyEv > homo->energyEv))
            homo = &lv;
        if (!lv.occupied() && (!lumo || lv.energyEv < lumo->energyEv))
            lumo = &lv;
    }

    for (int spin = 0; spin < nspins_; ++spin) {
        const double columnLeft = margin + spin * columnWidth;
        const double barLeft = columnLeft + columnWidth * 0.15;
        const double barRight = columnLeft + columnWidth * 0.85;

        if (nspins_ > 1) {
            painter.setPen(style_.placeholderColor);
            painter.drawText(
                QRectF(columnLeft, 2.0, columnWidth, 16.0), Qt::AlignCenter,
                spin == 0 ? tr("Spin up") : tr("Spin down"));
        }

        for (std::size_t i = 0; i < levels_.size(); ++i) {
            const EnergyLevelDiagramEntry& lv = levels_[i];
            if (lv.spin != spin)
                continue;
            const double y = yFromEnergy(lv.energyEv, H);
            const bool isSelected = static_cast<int>(i) == selected_;

            QPen pen(lv.occupied() ? style_.occupiedColor : style_.unoccupiedColor);
            pen.setWidthF(isSelected ? style_.lineWidth + 1.0 : style_.lineWidth);
            if (!lv.occupied())
                pen.setStyle(Qt::DashLine);
            painter.setPen(pen);

            // Degenerate levels: several short parallel bars side by side
            // instead of one wide one, so the multiplicity is visible at a
            // glance rather than only in a tooltip.
            const int degeneracy = lv.degeneracy();
            const double slotWidth = (barRight - barLeft) / degeneracy;
            for (int slot = 0; slot < degeneracy; ++slot) {
                const double x0 = barLeft + slot * slotWidth + slotWidth * 0.1;
                const double x1 = barLeft + (slot + 1) * slotWidth - slotWidth * 0.1;
                painter.drawLine(QPointF(x0, y), QPointF(x1, y));
            }
            if (degeneracy > 1) {
                painter.setPen(style_.textColor);
                painter.drawText(QRectF(barRight + 2.0, y - 8.0, 40.0, 16.0),
                                 Qt::AlignLeft | Qt::AlignVCenter,
                                 QStringLiteral("x%1").arg(degeneracy));
            }

            const QRectF hitRect(barLeft, y - 6.0, barRight - barLeft, 12.0);
            lastBars_.push_back({hitRect, static_cast<int>(i)});

            if (&lv == homo || &lv == lumo) {
                painter.setPen(style_.gapLineColor);
                painter.drawText(
                    QRectF(barLeft - 34.0, y - 8.0, 32.0, 16.0),
                    Qt::AlignRight | Qt::AlignVCenter,
                    &lv == homo ? tr("HOMO") : tr("LUMO"));
            }
        }
    }

    if (homo && lumo) {
        const double yHomo = yFromEnergy(homo->energyEv, H);
        const double yLumo = yFromEnergy(lumo->energyEv, H);
        QPen gapPen(style_.gapLineColor);
        gapPen.setStyle(Qt::DotLine);
        painter.setPen(gapPen);
        const double x = W - margin - 4.0;
        painter.drawLine(QPointF(x, yHomo), QPointF(x, yLumo));
        painter.setPen(style_.textColor);
        const double gap = lumo->energyEv - homo->energyEv;
        painter.drawText(QRectF(x - 90.0, (yHomo + yLumo) / 2.0 - 8.0, 86.0, 16.0),
                         Qt::AlignRight | Qt::AlignVCenter,
                         QStringLiteral("%1 eV").arg(gap, 0, 'f', 3));
    }
}

void EnergyLevelDiagramWidget::mousePressEvent(QMouseEvent* event)
{
    if (event->button() != Qt::LeftButton)
        return;
    const QPointF pos = event->position();
    for (const BarRect& bar : lastBars_) {
        if (bar.rect.adjusted(0, -4, 0, 4).contains(pos)) {
            setSelected(bar.index);
            Q_EMIT levelClicked(bar.index);
            return;
        }
    }
}

// ---------------------------------------------------------------------
// EnergyDiagramViewer
// ---------------------------------------------------------------------

EnergyDiagramViewer::EnergyDiagramViewer(QWidget* parent) : QDialog(parent)
{
    setWindowTitle(tr("Energy Diagram Viewer"));
    auto* outer = new QVBoxLayout(this);

    auto* caveat = new QLabel(
        tr("Kohn-Sham eigenvalue-difference transitions — NOT TDDFT or BSE "
           "excitation energies."),
        this);
    caveat->setWordWrap(true);
    caveat->setStyleSheet(QStringLiteral("font-style: italic;"));
    outer->addWidget(caveat);

    auto* splitRow = new QHBoxLayout();

    auto* leftColumn = new QVBoxLayout();
    diagram_ = new EnergyLevelDiagramWidget(this);
    diagram_->setStyle(style_);
    leftColumn->addWidget(diagram_, 1);
    levelInfoLabel_ = new QLabel(tr("Click a level for details."), this);
    levelInfoLabel_->setWordWrap(true);
    leftColumn->addWidget(levelInfoLabel_);
    gapLabel_ = new QLabel(this);
    gapLabel_->setWordWrap(true);
    leftColumn->addWidget(gapLabel_);

    auto* diagramButtonRow = new QHBoxLayout();
    customizeButton_ = new QPushButton(tr("Customize Appearance…"), this);
    connect(customizeButton_, &QPushButton::clicked, this,
            &EnergyDiagramViewer::customizeAppearance);
    diagramButtonRow->addWidget(customizeButton_);
    exportImageButton_ = new QPushButton(tr("Export Image…"), this);
    connect(exportImageButton_, &QPushButton::clicked, this,
            &EnergyDiagramViewer::exportImage);
    diagramButtonRow->addWidget(exportImageButton_);
    exportLevelsButton_ = new QPushButton(tr("Export Levels…"), this);
    exportLevelsButton_->setEnabled(false);
    connect(exportLevelsButton_, &QPushButton::clicked, this,
            &EnergyDiagramViewer::exportLevelsCsv);
    diagramButtonRow->addWidget(exportLevelsButton_);
    leftColumn->addLayout(diagramButtonRow);

    splitRow->addLayout(leftColumn, 1);

    auto* rightColumn = new QVBoxLayout();
    transitionsTable_ = new QTableWidget(this);
    transitionsTable_->setColumnCount(6);
    transitionsTable_->setHorizontalHeaderLabels(
        {tr("Spin"), tr("i -> j"), tr("Energy (eV)"), tr("Wavelength (nm)"),
         tr("Oscillator strength"), tr("Allowed")});
    transitionsTable_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    transitionsTable_->setSelectionBehavior(QAbstractItemView::SelectRows);
    rightColumn->addWidget(transitionsTable_, 1);

    exportButton_ = new QPushButton(tr("Export Transitions…"), this);
    exportButton_->setEnabled(false);
    connect(exportButton_, &QPushButton::clicked, this,
            &EnergyDiagramViewer::exportTransitionsCsv);
    rightColumn->addWidget(exportButton_, 0, Qt::AlignRight);
    splitRow->addLayout(rightColumn, 1);

    outer->addLayout(splitRow, 1);

    connect(diagram_, &EnergyLevelDiagramWidget::levelClicked, this,
            &EnergyDiagramViewer::onLevelClicked);
}

bool EnergyDiagramViewer::loadResults(const QString& path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return false;
    const QJsonObject root = QJsonDocument::fromJson(file.readAll()).object();
    file.close();
    if (root.isEmpty())
        return false;

    const int nspins = root.value(QStringLiteral("nspins")).toInt(1);

    levels_.clear();
    for (const QJsonValue& v : root.value(QStringLiteral("groups")).toArray()) {
        const QJsonObject o = v.toObject();
        EnergyLevelDiagramEntry entry;
        entry.spin = o.value(QStringLiteral("spin")).toInt();
        entry.energyEv = o.value(QStringLiteral("energy_eV")).toDouble();
        entry.occupation = o.value(QStringLiteral("occupation")).toDouble();
        for (const QJsonValue& b : o.value(QStringLiteral("bands")).toArray())
            entry.bands.push_back(b.toInt());
        levels_.push_back(entry);
    }
    if (diagram_)
        diagram_->setLevels(levels_, nspins);
    if (exportLevelsButton_)
        exportLevelsButton_->setEnabled(!levels_.empty());

    transitions_.clear();
    for (const QJsonValue& v :
         root.value(QStringLiteral("transitions")).toArray()) {
        const QJsonObject o = v.toObject();
        Transition t;
        t.spin = o.value(QStringLiteral("spin")).toInt();
        t.fromBand = o.value(QStringLiteral("from_band")).toInt();
        t.toBand = o.value(QStringLiteral("to_band")).toInt();
        t.energyEv = o.value(QStringLiteral("energy_eV")).toDouble();
        const QJsonValue wl = o.value(QStringLiteral("wavelength_nm"));
        t.hasWavelength = wl.isDouble();
        t.wavelengthNm = wl.toDouble();
        t.oscillatorStrength =
            o.value(QStringLiteral("oscillator_strength")).toDouble();
        t.allowed = o.value(QStringLiteral("allowed")).toBool();
        transitions_.push_back(t);
    }
    rebuildTransitionsTable();
    if (exportButton_)
        exportButton_->setEnabled(!transitions_.empty());

    if (gapLabel_) {
        const QJsonValue gap = root.value(QStringLiteral("gap_eV"));
        gapLabel_->setText(gap.isDouble()
            ? tr("HOMO-LUMO gap: %1 eV").arg(gap.toDouble(), 0, 'f', 3)
            : tr("HOMO-LUMO gap: not available."));
    }

    hasData_ = !levels_.empty();
    return hasData_;
}

void EnergyDiagramViewer::rebuildTransitionsTable()
{
    if (!transitionsTable_)
        return;
    transitionsTable_->setRowCount(static_cast<int>(transitions_.size()));
    for (std::size_t row = 0; row < transitions_.size(); ++row) {
        const Transition& t = transitions_[row];
        const int r = static_cast<int>(row);
        transitionsTable_->setItem(
            r, 0, new QTableWidgetItem(QString::number(t.spin)));
        transitionsTable_->setItem(
            r, 1,
            new QTableWidgetItem(QStringLiteral("%1 -> %2")
                                     .arg(t.fromBand)
                                     .arg(t.toBand)));
        transitionsTable_->setItem(
            r, 2, new QTableWidgetItem(QString::number(t.energyEv, 'f', 4)));
        transitionsTable_->setItem(
            r, 3,
            new QTableWidgetItem(t.hasWavelength
                                     ? QString::number(t.wavelengthNm, 'f', 1)
                                     : QStringLiteral("—")));
        transitionsTable_->setItem(
            r, 4,
            new QTableWidgetItem(
                QString::number(t.oscillatorStrength, 'g', 4)));
        auto* allowedItem =
            new QTableWidgetItem(t.allowed ? tr("Allowed") : tr("Forbidden"));
        if (!t.allowed) {
            QFont italic = allowedItem->font();
            italic.setItalic(true);
            allowedItem->setFont(italic);
            allowedItem->setForeground(palette().color(QPalette::PlaceholderText));
        }
        transitionsTable_->setItem(r, 5, allowedItem);
    }
    transitionsTable_->resizeColumnsToContents();
}

void EnergyDiagramViewer::onLevelClicked(int index)
{
    if (index < 0 || index >= static_cast<int>(levels_.size()))
        return;
    const EnergyLevelDiagramEntry& lv = levels_[static_cast<std::size_t>(index)];
    if (levelInfoLabel_)
        levelInfoLabel_->setText(
            tr("Spin %1 · bands %2 · %3 eV · occupation %4 · degeneracy %5")
                .arg(lv.spin)
                .arg([&] {
                    QStringList names;
                    for (int b : lv.bands)
                        names << QString::number(b);
                    return names.join(QStringLiteral(", "));
                }())
                .arg(lv.energyEv, 0, 'f', 4)
                .arg(lv.occupation, 0, 'f', 3)
                .arg(lv.degeneracy()));
}

void EnergyDiagramViewer::exportTransitionsCsv()
{
    if (transitions_.empty()) {
        QMessageBox::information(this, tr("Export Transitions"),
                                 tr("No transitions were computed for this "
                                    "run."));
        return;
    }
    const QString path = QFileDialog::getSaveFileName(
        this, tr("Export Transitions"), QStringLiteral("transitions.csv"),
        tr("CSV (*.csv)"));
    if (path.isEmpty())
        return;

    writeTextFile(this, path, [&](QTextStream& out) {
        out << "spin,from_band,to_band,energy_eV,wavelength_nm,"
              "oscillator_strength,allowed\n";
        for (const Transition& t : transitions_) {
            out << t.spin << ',' << t.fromBand << ',' << t.toBand << ','
                << QString::number(t.energyEv, 'f', 6) << ','
                << (t.hasWavelength ? QString::number(t.wavelengthNm, 'f', 3)
                                    : QString())
                << ',' << QString::number(t.oscillatorStrength, 'g', 8) << ','
                << (t.allowed ? "true" : "false") << '\n';
        }
    });
}

void EnergyDiagramViewer::exportLevelsCsv()
{
    if (levels_.empty()) {
        QMessageBox::information(this, tr("Export Levels"),
                                 tr("No levels were computed for this run."));
        return;
    }
    const QString path = QFileDialog::getSaveFileName(
        this, tr("Export Levels"), QStringLiteral("energy_levels.csv"),
        tr("CSV (*.csv)"));
    if (path.isEmpty())
        return;

    writeTextFile(this, path, [&](QTextStream& out) {
        out << "spin,bands,energy_eV,occupation,occupied,degeneracy\n";
        for (const EnergyLevelDiagramEntry& lv : levels_) {
            QStringList bandNames;
            for (int b : lv.bands)
                bandNames << QString::number(b);
            out << lv.spin << ',' << '"' << bandNames.join(QStringLiteral(";"))
                << '"' << ',' << QString::number(lv.energyEv, 'f', 6) << ','
                << QString::number(lv.occupation, 'f', 6) << ','
                << (lv.occupied() ? "true" : "false") << ',' << lv.degeneracy()
                << '\n';
        }
    });
}

void EnergyDiagramViewer::exportImage()
{
    const QString path = QFileDialog::getSaveFileName(
        this, tr("Export Image"), QStringLiteral("energy_diagram.png"),
        tr("PNG image (*.png);;JPEG image (*.jpg *.jpeg)"));
    if (path.isEmpty())
        return;
    if (!diagram_)
        return;
    savePlotImage(this, path, diagram_->size(),
                 [this](QPainter& painter, const QSize& logical) {
                     diagram_->renderTo(painter, logical);
                 });
}

void EnergyDiagramViewer::customizeAppearance()
{
    auto* dialog = new EnergyDiagramStyleDialog(style_, this);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    connect(dialog, &EnergyDiagramStyleDialog::styleChanged, this,
            [this](const EnergyDiagramStyle& style) {
                style_ = style;
                if (diagram_)
                    diagram_->setStyle(style_);
            });
    dialog->show();
}

} // namespace calango::gui
