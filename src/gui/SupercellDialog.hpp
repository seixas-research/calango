#pragma once

#include "core/Structure.hpp"
#include "core/Vec3.hpp"

#include <QDialog>
#include <QWidget>

#include <array>
#include <memory>

class QLabel;
class QSpinBox;

namespace calango::gui {

/// Draws the original Bravais lattice vectors (blue) and the transformed
/// supercell vectors (orange) side by side, in a fixed isometric projection.
/// Purely a preview — it holds a copy of the base cell and the current 3×3
/// integer transformation matrix and repaints whenever either changes.
class LatticePreviewWidget : public QWidget {
    Q_OBJECT

public:
    explicit LatticePreviewWidget(QWidget* parent = nullptr);

    /// Base lattice vectors (rows, ASE convention). Empty/undefined cells
    /// leave the canvas blank with an explanatory note.
    void setCell(const std::array<core::Vec3, 3>& cell, bool defined);

    /// Current integer transformation matrix P (row-major); the supercell
    /// vectors drawn are P · cell.
    void setMatrix(const int p[3][3]);

    QSize sizeHint() const override { return {320, 320}; }
    QSize minimumSizeHint() const override { return {220, 220}; }

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    /// Isometric projection of a Cartesian vector to 2D (world units, +y down).
    static QPointF project(const core::Vec3& v);

    std::array<core::Vec3, 3> cell_{};
    int p_[3][3] = {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}};
    bool defined_ = false;
};

/// Build → "Supercell (Transformation Matrix)": arbitrary non-diagonal
/// supercells. The 3×3 integer spin-box grid feeds a live lattice preview;
/// accepting exposes the matrix via matrix() for AseBridge::makeSupercellMatrix.
class SupercellDialog : public QDialog {
    Q_OBJECT

public:
    explicit SupercellDialog(std::shared_ptr<const core::Structure> structure,
                             QWidget* parent = nullptr);

    /// Copies the chosen transformation matrix into out (row-major).
    void matrix(int out[3][3]) const;

private Q_SLOTS:
    void onMatrixChanged();
    void resetToIdentity();

private:
    std::shared_ptr<const core::Structure> structure_;
    QSpinBox* spins_[3][3];
    LatticePreviewWidget* preview_;
    QLabel* detLabel_;
};

} // namespace calango::gui
