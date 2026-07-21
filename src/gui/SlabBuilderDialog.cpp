#include "gui/SlabBuilderDialog.hpp"

#include "gui/ViewportWidget.hpp"
#include "python_bridge/AseBridge.hpp"

#include <QFormLayout>
#include <QHBoxLayout>
#include <QPushButton>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>
#include <limits>

namespace calango::gui {

SlabBuilderDialog::SlabBuilderDialog(std::shared_ptr<const core::Structure> bulk,
                                     QWidget* parent)
    : QDialog(parent)
    , bulk_(std::move(bulk))
{
    setWindowTitle(tr("Cleave Surface (Slab) — Live Preview"));
    resize(980, 560);

    auto* form = new QFormLayout;

    auto* millerRow = new QHBoxLayout;
    const char* names[3] = {"h", "k", "l"};
    for (int i = 0; i < 3; ++i) {
        millerSpins_[i] = new QSpinBox(this);
        millerSpins_[i]->setRange(-9, 9);
        millerSpins_[i]->setValue(i == 2 ? 1 : 0); // default (0 0 1)
        millerSpins_[i]->setPrefix(QStringLiteral("%1 = ").arg(QLatin1String(names[i])));
        millerRow->addWidget(millerSpins_[i], 1);
        connect(millerSpins_[i], &QSpinBox::valueChanged,
                this, &SlabBuilderDialog::scheduleRebuild);
    }
    form->addRow(tr("Miller indices:"), millerRow);

    layersSpin_ = new QSpinBox(this);
    layersSpin_->setRange(1, 40);
    layersSpin_->setValue(4);
    form->addRow(tr("Layers:"), layersSpin_);
    connect(layersSpin_, &QSpinBox::valueChanged,
            this, &SlabBuilderDialog::scheduleRebuild);

    vacuumSpin_ = new QDoubleSpinBox(this);
    vacuumSpin_->setRange(0.0, 60.0);
    vacuumSpin_->setValue(10.0);
    vacuumSpin_->setSuffix(tr(" Å"));
    form->addRow(tr("Vacuum (each side):"), vacuumSpin_);
    connect(vacuumSpin_, &QDoubleSpinBox::valueChanged,
            this, &SlabBuilderDialog::scheduleRebuild);

    infoLabel_ = new QLabel(this);
    infoLabel_->setWordWrap(true);
    infoLabel_->setTextFormat(Qt::RichText);
    form->addRow(infoLabel_);

    statusLabel_ = new QLabel(this);
    statusLabel_->setWordWrap(true);
    form->addRow(statusLabel_);

    preview_ = new ViewportWidget(this);

    buttons_ = new QDialogButtonBox(this);
    auto* insertButton =
        buttons_->addButton(tr("Insert Slab"), QDialogButtonBox::AcceptRole);
    buttons_->addButton(QDialogButtonBox::Cancel);
    insertButton->setDefault(true);
    connect(buttons_, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons_, &QDialogButtonBox::rejected, this, &QDialog::reject);

    auto* side = new QVBoxLayout;
    side->addLayout(form);
    side->addStretch(1);

    auto* content = new QHBoxLayout;
    auto* sideWidget = new QWidget(this);
    sideWidget->setLayout(side);
    sideWidget->setFixedWidth(330);
    content->addWidget(sideWidget);
    content->addWidget(preview_, 1);

    auto* layout = new QVBoxLayout(this);
    layout->addLayout(content, 1);
    layout->addWidget(buttons_);

    debounce_.setSingleShot(true);
    debounce_.setInterval(250);
    connect(&debounce_, &QTimer::timeout, this, &SlabBuilderDialog::rebuildPreview);

    rebuildPreview(); // show the default (0 0 1) slab immediately
}

QString SlabBuilderDialog::resultLabel() const
{
    return tr("(%1%2%3) slab")
        .arg(millerSpins_[0]->value())
        .arg(millerSpins_[1]->value())
        .arg(millerSpins_[2]->value());
}

void SlabBuilderDialog::scheduleRebuild()
{
    debounce_.start();
}

void SlabBuilderDialog::rebuildPreview()
{
    const int h = millerSpins_[0]->value();
    const int k = millerSpins_[1]->value();
    const int l = millerSpins_[2]->value();

    if (h == 0 && k == 0 && l == 0) {
        result_.reset();
        preview_->setStructure(nullptr);
        infoLabel_->clear();
        statusLabel_->setStyleSheet(QStringLiteral("color: #d9534f;"));
        statusLabel_->setText(tr("Miller indices (0 0 0) are not a valid plane."));
        for (auto* button : buttons_->buttons())
            if (buttons_->buttonRole(button) == QDialogButtonBox::AcceptRole)
                button->setEnabled(false);
        return;
    }

    try {
        result_ = std::make_shared<core::Structure>(pybridge::AseBridge::makeSlab(
            *bulk_, h, k, l, layersSpin_->value(), vacuumSpin_->value()));

        // Surface cell vectors u = a1, v = a2 of the cleaved cell.
        const auto& v = result_->cell().vectors();
        const core::Vec3 u = v[0];
        const core::Vec3 w = v[1];
        const double lenU = u.norm();
        const double lenV = w.norm();
        const double angle = (lenU > 1e-9 && lenV > 1e-9)
            ? std::acos(std::clamp(u.dot(w) / (lenU * lenV), -1.0, 1.0)) * 180.0 / M_PI
            : 0.0;

        // Slab thickness: extent of the atoms along z (the surface normal
        // after ase.build.surface reorients the cell).
        double zMin = std::numeric_limits<double>::max();
        double zMax = std::numeric_limits<double>::lowest();
        for (const core::Atom& atom : result_->atoms()) {
            zMin = std::min(zMin, atom.position.z);
            zMax = std::max(zMax, atom.position.z);
        }
        const double thickness = result_->empty() ? 0.0 : zMax - zMin;

        infoLabel_->setText(
            tr("<b>Surface (%1 %2 %3)</b><br>"
               "u = (%4, %5, %6) Å, |u| = %7 Å<br>"
               "v = (%8, %9, %10) Å, |v| = %11 Å<br>"
               "∠(u, v) = %12°<br>"
               "Slab thickness: %13 Å · Atoms: %14")
                .arg(h)
                .arg(k)
                .arg(l)
                .arg(u.x, 0, 'f', 3)
                .arg(u.y, 0, 'f', 3)
                .arg(u.z, 0, 'f', 3)
                .arg(lenU, 0, 'f', 3)
                .arg(w.x, 0, 'f', 3)
                .arg(w.y, 0, 'f', 3)
                .arg(w.z, 0, 'f', 3)
                .arg(lenV, 0, 'f', 3)
                .arg(angle, 0, 'f', 2)
                .arg(thickness, 0, 'f', 3)
                .arg(result_->size()));
        statusLabel_->setStyleSheet(QString());
        statusLabel_->setText(tr("Preview updated."));
        preview_->setStructure(result_);
    } catch (const std::exception& e) {
        result_.reset();
        preview_->setStructure(nullptr);
        infoLabel_->clear();
        statusLabel_->setStyleSheet(QStringLiteral("color: #d9534f;"));
        statusLabel_->setText(QString::fromUtf8(e.what()));
    }

    for (auto* button : buttons_->buttons())
        if (buttons_->buttonRole(button) == QDialogButtonBox::AcceptRole)
            button->setEnabled(result_ != nullptr);
}

} // namespace calango::gui
