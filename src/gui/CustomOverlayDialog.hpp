#pragma once

#include "core/Vec3.hpp"
#include "render/StructureRenderer.hpp"

#include <QColor>
#include <QDialog>
#include <QString>

#include <vector>

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QListWidget;
class QPushButton;
class QSlider;
class QSpinBox;

namespace calango::gui {

class ViewportWidget;

/// Viewport → "Custom overlay…": the Custom Overlay Manager. Adds, positions
/// and styles geometric primitives (spheres/ellipsoids, boxes/parallelepipeds,
/// cylinders/cones/tubes, planes/disks) drawn over the atomic structure in the
/// main 3D viewport. Each primitive has its own texture style (solid,
/// checkerboard, wireframe, translucent/glassy, gradient), a smooth or
/// corrugated surface finish, and an independent opacity. Modeless; the overlay
/// is cleared when the dialog closes.
class CustomOverlayDialog : public QDialog {
    Q_OBJECT

public:
    enum class PrimitiveType { Sphere, Ellipsoid, Box, Cylinder, Cone, Plane, Disk };
    enum class TextureStyle { Solid, Checkerboard, Wireframe, Glassy, Gradient };
    enum class SurfaceFinish { Smooth, Corrugated };

    /// One overlay primitive and all its styling.
    struct Primitive {
        QString name;
        PrimitiveType type = PrimitiveType::Sphere;
        core::Vec3 center{0, 0, 0};   ///< center / origin / cylinder start
        core::Vec3 size{2, 2, 2};     ///< radii (sphere/ellipsoid) or box dims
        core::Vec3 endPoint{0, 0, 4}; ///< cylinder/cone far end
        core::Vec3 normal{0, 0, 1};   ///< plane / disk normal
        core::Vec3 rotationDeg{0, 0, 0}; ///< box Euler rotation (deg, XYZ)
        double radius = 1.5;          ///< cylinder / cone / disk / plane extent
        QColor color{210, 180, 90};
        QColor color2{70, 90, 160};   ///< checkerboard 2nd / gradient end
        TextureStyle texture = TextureStyle::Glassy;
        SurfaceFinish finish = SurfaceFinish::Smooth;
        double opacity = 0.6;
        int resolution = 32;
        bool visible = true;
    };

    CustomOverlayDialog(ViewportWidget* viewport, QWidget* parent = nullptr);

protected:
    void closeEvent(QCloseEvent* event) override;

private Q_SLOTS:
    void addPrimitive();
    void removePrimitive();
    void onSelectionChanged();
    /// Read the editor widgets into the selected primitive and repaint.
    void applyEditor();

private:
    void loadEditor(const Primitive& p);
    void showRelevantRows();
    /// Rebuild every primitive's geometry and push it to the viewport.
    void rebuild();
    Primitive* current();

    ViewportWidget* viewport_;
    std::vector<Primitive> primitives_;
    bool loading_ = false; ///< guards editor→model writes during loadEditor()

    QListWidget* list_ = nullptr;
    QComboBox* typeCombo_ = nullptr;
    QDoubleSpinBox* centerSpin_[3] = {nullptr, nullptr, nullptr};
    QDoubleSpinBox* sizeSpin_[3] = {nullptr, nullptr, nullptr};
    QDoubleSpinBox* endSpin_[3] = {nullptr, nullptr, nullptr};
    QDoubleSpinBox* normalSpin_[3] = {nullptr, nullptr, nullptr};
    QDoubleSpinBox* rotationSpin_[3] = {nullptr, nullptr, nullptr};
    QDoubleSpinBox* radiusSpin_ = nullptr;
    QSpinBox* resolutionSpin_ = nullptr;
    QComboBox* textureCombo_ = nullptr;
    QComboBox* finishCombo_ = nullptr;
    QSlider* opacitySlider_ = nullptr;
    QCheckBox* visibleCheck_ = nullptr;
    QPushButton* colorButton_ = nullptr;
    QPushButton* color2Button_ = nullptr;

    // Rows/labels toggled per primitive type.
    QWidget* sizeRow_ = nullptr;
    QWidget* endRow_ = nullptr;
    QWidget* normalRow_ = nullptr;
    QWidget* rotationRow_ = nullptr;
    QWidget* radiusRow_ = nullptr;
};

} // namespace calango::gui
