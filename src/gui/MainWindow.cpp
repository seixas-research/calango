#include "gui/MainWindow.hpp"

#include "core/AseScriptGenerator.hpp"
#include "core/BrillouinZone.hpp"
#include "core/HydrogenCompletion.hpp"
#include "core/PdbxFile.hpp"
#include "core/Structure.hpp"
#include "core/StructureTransforms.hpp"
#include "gui/BrillouinZoneDialog.hpp"
#include "gui/CoordinationDialog.hpp"
#include "gui/DistributionDialog.hpp"
#include "gui/StructureFactorDialog.hpp"
#include "gui/XrdDialog.hpp"
#include "gui/DatabaseImportDialog.hpp"
#include "gui/ExamplesDialog.hpp"
#include "gui/NanoBuilderDialog.hpp"
#include "gui/PointOfViewDialog.hpp"
#include "gui/RayTraceDialog.hpp"
#include "gui/RdfDialog.hpp"
#include "gui/BondEditorDialog.hpp"
#include "gui/CellAxesTabs.hpp"
#include "gui/EnvFile.hpp"
#include "gui/VisualEffectsPanel.hpp"
#include "gui/PeriodicTableDialog.hpp"
#include "gui/PreferencesDialog.hpp"
#include "gui/BrandingPanel.hpp"
#include "gui/RemoteAccessPanel.hpp"
#include "gui/RepresentationPanel.hpp"
#include "gui/SlabWizard.hpp"
#include "gui/AddAdsorbateDialog.hpp"
#include "gui/AdsorptionDialog.hpp"
#include "gui/BornChargesViewer.hpp"
#include "gui/BornChargesWizard.hpp"
#include "gui/BandPdosWindow.hpp"
#include "gui/ClusterExpansionDialog.hpp"
#include "gui/ClusterExpansionWizard.hpp"
#include "gui/ConvexHullWindow.hpp"
#include "gui/EffectiveBandsWizard.hpp"
#include "gui/EffectiveBandsWindow.hpp"
#include "gui/GeometryOptimizationWizard.hpp"
#include "gui/ElectronicBandsWizard.hpp"
#include "gui/MolecularDynamicsWizard.hpp"
#include "gui/RandomNoiseViewer.hpp"
#include "gui/RandomNoiseWizard.hpp"
#include "gui/CddWizard.hpp"
#include "gui/CondaEnvs.hpp"
#include "gui/ConvergenceResultsWindow.hpp"
#include "gui/CutoffConvergenceWizard.hpp"
#include "gui/KpointsConvergenceWizard.hpp"
#include "gui/OrchestrationWindow.hpp"
#include "gui/EnginePresets.hpp"
#include "dft/CalangoDFTEngine.hpp"
#include "gui/SinglePointWizard.hpp"
#include "gui/MonteCarloWizard.hpp"
#include "gui/PhononWizard.hpp"
#include "gui/SimulationWizardBase.hpp"
#include "gui/NanoparticleDialog.hpp"
#include "gui/NebDialog.hpp"
#include "gui/PhononPlotWindow.hpp"
#include "gui/SupercellDialog.hpp"
#include "gui/PartialChargeDialog.hpp"
#include "gui/NonlinearOpticsResultsWindow.hpp"
#include "gui/NonlinearOpticsWizard.hpp"
#include "gui/OpticsWizard.hpp"
#include "gui/OverlayPanel.hpp"
#include "gui/GrapheneOxideWizard.hpp"
#include "gui/GuiUtils.hpp"
#include "gui/GwResultsWindow.hpp"
#include "gui/OpticsResultsWindow.hpp"
#include "gui/WannierDialog.hpp"
#include "gui/MlwfViewer.hpp"
#include "gui/SinglePointViewer.hpp"
#include "gui/DefectDiagramWindow.hpp"
#include "gui/FermiSurfaceDialog.hpp"
#include "gui/FermiSurfaceWindow.hpp"
#include "gui/TopologyDialog.hpp"
#include "gui/TopologyWindow.hpp"
#include "gui/WannierInterpolationDialog.hpp"
#include "gui/Defect2dWizard.hpp"
#include "gui/DefectWizard.hpp"
#include "gui/TwoDBandsWindow.hpp"
#include "gui/TwoDBandsWizard.hpp"
#include "gui/VolumetricPanel.hpp"
#include "gui/WannierWizard.hpp"
#include "gui/WorkfunctionWindow.hpp"
#include "gui/WorkfunctionWizard.hpp"
#include "gui/XasResultsWindow.hpp"
#include "gui/HubbardUWizard.hpp"
#include "gui/XasWizard.hpp"
#include "gui/MagneticSpaceGroupDialog.hpp"
#include "gui/SymmetryDialog.hpp"
#include "gui/VacfDialog.hpp"
#include "gui/DatasetManagerDialog.hpp"
#include "gui/ProcessManagerPanel.hpp"
#include "gui/ScriptViewerDialog.hpp"
#include "gui/SettingsManager.hpp"
#include "gui/MaceTrainerDialog.hpp"
#include "gui/SystemStatusBar.hpp"
#include "gui/ThemeManager.hpp"
#include "ui/IconManager.hpp"
#include "gui/WelcomeDialog.hpp"
#include "gui/RamanIrViewer.hpp"
#include "gui/RamanIrWizard.hpp"
#include "gui/GeometryOptimizationViewer.hpp"
#include "gui/GwWizard.hpp"
#include "gui/MacromoleculeWizard.hpp"
#include "gui/MolecularDynamicsViewer.hpp"
#include "gui/RunCommands.hpp"
#include "gui/WaterIceWizard.hpp"
#include "gui/DislocationWizard.hpp"
#include "gui/LiquidInterfaceWizard.hpp"
#include "gui/SolidInterfaceWizard.hpp"
#include "gui/SqsDialog.hpp"
#include "gui/WarrenCowleyDialog.hpp"
#include "gui/LocalEntropyDialog.hpp"
#include "gui/JobLogWidget.hpp"
#include "gui/MetricPlotWidget.hpp"
#include "gui/ProjectSerializer.hpp"
#include "gui/StructureEditorDialog.hpp"
#include "gui/StructureInfoWidget.hpp"
#include "gui/FilmProductionDialog.hpp"
#include "gui/FilmTimelineWidget.hpp"
#include "gui/TimelineWidget.hpp"
#include "gui/ViewportWidget.hpp"
#include "jobs/JobRunner.hpp"
#include "python_bridge/AlembicExporter.hpp"
#include "python_bridge/AnimationExporter.hpp"
#include "python_bridge/AseBridge.hpp"
#include "python_bridge/PythonEngine.hpp"

#include <QActionGroup>
#include <QApplication>
#include <QGuiApplication>
#include <QScreen>
#include <QScrollArea>
#include <QStyleHints>
#include <QCheckBox>
#include <QCloseEvent>
#include <QColorDialog>
#include <QComboBox>
#include <QDateTime>
#include <QCursor>
#include <QDesktopServices>
#include <QUrl>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QDockWidget>
#include <QDoubleSpinBox>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QImageWriter>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTimer>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QPainter>
#include <QProgressDialog>
#include <QSaveFile>
#include <QFontDatabase>
#include <QPlainTextEdit>
#include <QSettings>
#include <QSlider>
#include <QSpinBox>
#include <QStandardPaths>
#include <QStatusBar>
#include <QTabBar>
#include <QTemporaryDir>
#include <QToolBar>
#include <QWidgetAction>
#include <QToolButton>
#include <QTabWidget>
#include <QTextStream>
#include <QVBoxLayout>

#include <algorithm>
#include <array>
#include <stdexcept>
#include <string>

namespace calango::gui {

namespace {
constexpr std::size_t kMaxUndoDepth = 50;
/// The rate the trajectory timeline plays at, and therefore the rate
/// "Trajectory priority" means by a trajectory's NATURAL duration —
/// the length the user already sees when they scrub it.
constexpr double kTrajectoryPlaybackFps = 15.0;
/// Version tag for saveState/restoreState. Bumped when the default dock
/// grid changes so stale saved layouts don't override the new default
/// (v2 = the 8-zone grid workspace, v3 = the 12-zone grid with the
/// branding and Remote Access panels, v4 = the "Job" dock renamed to
/// "Results" with a process selector, v5 = the "Lighting" dock renamed to
/// the tabbed "Visual Effects" panel, v6 = zones 9/12 width-locked to the
/// side columns and the branding card hidden by default).
// Bumped whenever the DOCK SET or its default arrangement changes, so
// restoreState() rejects a layout saved against the previous one rather than
// reinstating a half-matching arrangement (Qt silently drops docks it cannot
// find and leaves the freed space empty).
//   9: "Unit Cell & Axes" removed (its tabs moved into Representation) and
//      Visual Effects relocated into the bottom row's right-hand slot.
//  10: "Cell, Axes & Vectors" added at the right end of the bottom row (the
//      overlay tabs moved back out of Representation, joined by Vectors).
//  11: default grid realigned to the reference capture — "Cell, Axes &
//      Vectors" moved from the bottom row into the right column under
//      Representation, and the branding card shown by default again.
//  12: Visual Effects joined the right column, which now runs the full window
//      height (both bottom corners belong to the side areas); the bottom row
//      is Results | Remote Access, the latter pinned to its minimum width.
//  13: "Additional overlays" added to the left column between Volumetric Data
//      and Processes. A new dock is exactly the case the version guards: a
//      layout saved under 12 has no slot for it, and restoring one would hide
//      the dock permanently with no way back short of Reset Layout.
//  14: the branding card grew a version caption under the logo, raising its
//      minimum height; a layout saved under 13 pins the old 30 px strip and
//      would clip the caption forever.
//  15: "Workflow" joined the bottom row, which is now Workflow | Remote Access
//      | Results — a new dock, so a layout saved under 14 has no slot for it
//      and would strand the canvas with no way back. Remote Access and
//      Additional Overlays also became hidden-by-default in the same change.
//  16: the Workflow dock was renamed Orchestration, objectName included
//      ("workflowDock" → "orchestrationDock"). restoreState() matches docks
//      by objectName, so a layout saved under 15 holds state for a dock that
//      no longer exists — the renamed dock would come up stranded.
constexpr int kLayoutVersion = 16;

/// Painted icons for the frame-panel camera toolbar (icon-only buttons).
/// Plane icons use the axes-triad colors: x red, y green, z blue.
QIcon cameraToolbarIcon(const QString& kind)
{
    QPixmap pixmap(32, 32);
    pixmap.fill(Qt::transparent);
    QPainter p(&pixmap);
    p.setRenderHint(QPainter::Antialiasing);
    const QColor neutral(155, 160, 170);
    const QColor xColor(235, 100, 90);
    const QColor yColor(110, 210, 130);
    const QColor zColor(90, 148, 250);

    const auto arrow = [&p](const QPointF& from, const QPointF& to,
                            const QColor& color) {
        p.setPen(QPen(color, 3.0, Qt::SolidLine, Qt::RoundCap));
        p.drawLine(from, to);
        const QLineF line(from, to);
        const QPointF dir = (to - from) / line.length();
        const QPointF perp(-dir.y(), dir.x());
        p.drawLine(to, to - dir * 7.0 + perp * 4.0);
        p.drawLine(to, to - dir * 7.0 - perp * 4.0);
    };

    if (kind == QLatin1String("reset")) {
        p.setPen(QPen(neutral, 2.6));
        p.drawEllipse(QPointF(16, 16), 8.5, 8.5);
        for (const auto& [from, to] :
             {std::pair{QPointF(16, 2), QPointF(16, 8)},
              std::pair{QPointF(16, 24), QPointF(16, 30)},
              std::pair{QPointF(2, 16), QPointF(8, 16)},
              std::pair{QPointF(24, 16), QPointF(30, 16)}})
            p.drawLine(from, to);
        p.setBrush(neutral);
        p.setPen(Qt::NoPen);
        p.drawEllipse(QPointF(16, 16), 2.4, 2.4);
    } else if (kind == QLatin1String("ortho")) {
        p.setPen(QPen(neutral, 2.4));
        p.drawRect(QRectF(5, 11, 15, 15));   // front face
        p.drawRect(QRectF(11, 5, 15, 15));   // parallel back face
        p.setPen(QPen(neutral, 1.6));
        for (const auto& [from, to] :
             {std::pair{QPointF(5, 11), QPointF(11, 5)},
              std::pair{QPointF(20, 11), QPointF(26, 5)},
              std::pair{QPointF(5, 26), QPointF(11, 20)},
              std::pair{QPointF(20, 26), QPointF(26, 20)}})
            p.drawLine(from, to);
    } else if (kind == QLatin1String("xy")) {
        arrow(QPointF(7, 25), QPointF(28, 25), xColor);
        arrow(QPointF(7, 25), QPointF(7, 4), yColor);
    } else if (kind == QLatin1String("xz")) {
        arrow(QPointF(7, 25), QPointF(28, 25), xColor);
        arrow(QPointF(7, 25), QPointF(7, 4), zColor);
    } else if (kind == QLatin1String("yz")) {
        arrow(QPointF(7, 25), QPointF(28, 25), yColor);
        arrow(QPointF(7, 25), QPointF(7, 4), zColor);
    } else if (kind == QLatin1String("rotate")) {
        // Orbit arc with an arrowhead.
        p.setPen(QPen(neutral, 2.6, Qt::SolidLine, Qt::RoundCap));
        p.drawArc(QRectF(6, 6, 20, 20), 45 * 16, 250 * 16);
        arrow(QPointF(22.2, 24.5), QPointF(24.5, 22.0), neutral);
    } else if (kind == QLatin1String("pan")) {
        // Four-way move cross.
        arrow(QPointF(16, 16), QPointF(16, 3), neutral);
        arrow(QPointF(16, 16), QPointF(16, 29), neutral);
        arrow(QPointF(16, 16), QPointF(3, 16), neutral);
        arrow(QPointF(16, 16), QPointF(29, 16), neutral);
    } else if (kind == QLatin1String("select")) {
        // Dashed selection box around a couple of "atoms".
        QPen dashed(neutral, 2.0);
        dashed.setStyle(Qt::DashLine);
        p.setPen(dashed);
        p.drawRect(QRectF(5, 5, 22, 22));
        p.setPen(Qt::NoPen);
        p.setBrush(neutral);
        p.drawEllipse(QPointF(12, 14), 3.2, 3.2);
        p.drawEllipse(QPointF(21, 20), 3.2, 3.2);
    } else if (kind == QLatin1String("insert")) {
        // New atom (circle) with a plus.
        p.setPen(QPen(neutral, 2.4));
        p.setBrush(Qt::NoBrush);
        p.drawEllipse(QPointF(13, 19), 8.0, 8.0);
        p.setPen(QPen(neutral, 3.0, Qt::SolidLine, Qt::RoundCap));
        p.drawLine(QPointF(24, 4), QPointF(24, 14));
        p.drawLine(QPointF(19, 9), QPointF(29, 9));
    } else if (kind == QLatin1String("distance")) {
        // Two atoms joined by a dashed ruler line.
        QPen dashed(neutral, 2.0);
        dashed.setStyle(Qt::DashLine);
        p.setPen(dashed);
        p.drawLine(QPointF(8, 24), QPointF(24, 8));
        p.setPen(Qt::NoPen);
        p.setBrush(neutral);
        p.drawEllipse(QPointF(7, 25), 4.0, 4.0);
        p.drawEllipse(QPointF(25, 7), 4.0, 4.0);
    } else if (kind == QLatin1String("angle")) {
        // Two rays from a vertex with the angle arc.
        p.setPen(QPen(neutral, 2.4, Qt::SolidLine, Qt::RoundCap));
        p.drawLine(QPointF(6, 26), QPointF(28, 26));
        p.drawLine(QPointF(6, 26), QPointF(22, 6));
        p.setPen(QPen(neutral, 2.0));
        p.drawArc(QRectF(6 - 9, 26 - 9, 18, 18), 0, 52 * 16);
    }
    return QIcon(pixmap);
}

/// Human-readable identifier for the C/C++ compiler that built this binary,
/// derived from the standard predefined macros. Clang is checked before GCC
/// because Clang also defines __GNUC__ for compatibility.
QString compilerVersionString()
{
#if defined(__clang__)
    return QStringLiteral("Clang %1.%2.%3")
        .arg(__clang_major__).arg(__clang_minor__).arg(__clang_patchlevel__);
#elif defined(__GNUC__)
    return QStringLiteral("GCC %1.%2.%3")
        .arg(__GNUC__).arg(__GNUC_MINOR__).arg(__GNUC_PATCHLEVEL__);
#elif defined(_MSC_VER)
    return QStringLiteral("MSVC %1").arg(_MSC_VER);
#else
    return QStringLiteral("unknown");
#endif
}
} // namespace

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent)
    , jobRunner_(new jobs::JobRunner(this))
    , metricsTimer_(new QTimer(this))
{
    setWindowTitle(QStringLiteral("Calango"));
    resize(1360, 860);

    // Live Results-graph updates read the running job's metrics.json (~1 s).
    metricsTimer_->setInterval(1000);
    connect(metricsTimer_, &QTimer::timeout, this, &MainWindow::pollLiveMetrics);

    // Publish MP_API_KEY (Materials Project) from the configured .env file
    // — ~/.env by default, overridable in Edit → Preferences.
    loadEnvironmentFile();

    // Central column: document tab bar on top, then the shared 3D
    // viewport, then the playback timeline (job console dock sits below).
    tabBar_ = new QTabBar(this);
    tabBar_->setDocumentMode(true);
    tabBar_->setTabsClosable(true);
    tabBar_->setExpanding(false);
    tabBar_->setMovable(true); // drag-reorder; documents_ follows via onTabMoved
    viewport_ = new ViewportWidget(this);
    timeline_ = new TimelineWidget(this);
    timeline_->hide(); // appears when the current document has frames
    filmTimeline_ = new FilmTimelineWidget(this);
    filmTimeline_->hide(); // appears only in Film mode

    // Compact icon-only camera toolbar living inside the frame panel
    // (replaces the old top application toolbar). Projection toggling lives
    // solely here on the 'O' toolbar button (no View-menu duplicate).
    orthoAction_ = new QAction(tr("Orthographic"), this);
    ui::IconManager::bind(orthoAction_, QStringLiteral("box-3-line"));
    orthoAction_->setCheckable(true);
    // Reflect the camera's ACTUAL projection rather than assuming unchecked.
    // The viewport now starts orthographic (render::kDefaultProjection), and a
    // toggle that opened unchecked over an orthographic camera would both
    // misreport the state and need two clicks to reach perspective.
    orthoAction_->setChecked(viewport_->camera().projectionMode()
                             == render::CameraProjection::Orthographic);
    orthoAction_->setShortcut(QKeySequence(Qt::Key_O));
    orthoAction_->setToolTip(tr("Toggle perspective / orthographic projection  [O]"));
    connect(orthoAction_, &QAction::toggled,
            viewport_, &ViewportWidget::setOrthographic);

    auto* frameToolbar = new QToolBar(this);
    frameToolbar->setObjectName(QStringLiteral("frameToolbar"));
    // Icons enlarged 20% (18 → 22) for clearer viewport-header glyphs.
    frameToolbar->setIconSize(QSize(22, 22));
    frameToolbar->setMovable(false);
    frameToolbar->setToolButtonStyle(Qt::ToolButtonIconOnly);

    // --- Mouse interaction modes (exclusive, single-letter hotkeys) --------
    // Plain-letter shortcuts are safe: Qt's ShortcutOverride lets text
    // widgets keep the keys while typing (same as the View menu's "F").
    auto* modeGroup = new QActionGroup(this);
    modeGroup->setExclusive(true);
    // `iconName` is a RemixIcon file stem, tinted to the theme by IconManager.
    const auto addModeAction =
        [this, frameToolbar, modeGroup](const QString& iconName, const QString& text,
                                        ViewportWidget::InteractionMode mode,
                                        const QKeySequence& key) {
            QAction* action = frameToolbar->addAction(
                tr("%1  [%2]").arg(text, key.toString(QKeySequence::NativeText)));
            ui::IconManager::bind(action, iconName);
            action->setCheckable(true);
            action->setShortcut(key);
            modeGroup->addAction(action);
            connect(action, &QAction::triggered, this,
                    [this, mode] { viewport_->setInteractionMode(mode); });
            return action;
        };
    QAction* rotateMode = addModeAction(
        QStringLiteral("anticlockwise-2-line"),
        tr("Rotation mode — drag orbits the camera around the structure"),
        ViewportWidget::InteractionMode::Rotate, QKeySequence(Qt::Key_R));
    addModeAction(QStringLiteral("drag-move-2-line"),
                  tr("Translation mode — drag pans the scene"),
                  ViewportWidget::InteractionMode::Pan, QKeySequence(Qt::Key_T));
    addModeAction(QStringLiteral("cursor-line"),
                  tr("Selection mode — drag a box to select multiple atoms "
                     "(and their bonds); Delete/Backspace removes them"),
                  ViewportWidget::InteractionMode::Select,
                  QKeySequence(Qt::Key_S));
    addModeAction(QStringLiteral("edit-fill"),
                  tr("Insertion mode — click empty space to add an atom of "
                     "the active element;\ndrag from one atom to another to "
                     "bond them"),
                  ViewportWidget::InteractionMode::Insert,
                  QKeySequence(Qt::Key_I));

    // Chemical Element Selector, placed directly after the Insert toggle:
    // opens the periodic table and shows the active element symbol over that
    // element's own CPK colour, so the element Insertion mode will place is
    // both named and previewed.
    //
    // The button used to be a fixed red regardless of the element, which meant
    // it matched carbon, oxygen and iron equally badly and told the user
    // nothing beyond the symbol already written on it.
    elementButton_ = new QToolButton(frameToolbar);
    elementButton_->setToolButtonStyle(Qt::ToolButtonTextOnly);
    elementButton_->setToolTip(tr("Element inserted by Insertion mode — "
                                  "click to choose from the periodic table"));
    const auto updateElementButton = [this] {
        const QColor background = cpkColor(activeElementZ_);
        const QColor text = readableTextColor(background);
        // The border is a darkened swatch and the hover a lightened one, so
        // both track the element instead of clashing with it.
        elementButton_->setStyleSheet(
            QStringLiteral(
                "QToolButton { background-color: %1; color: %2;"
                " font-weight: bold; border: 1px solid %3;"
                " border-radius: 3px; padding: 1px 6px; }"
                "QToolButton:hover { background-color: %4; }")
                .arg(background.name(), text.name(),
                     background.darker(140).name(),
                     background.lighter(115).name()));
        elementButton_->setText(
            QLatin1String(core::Elements::data(activeElementZ_).symbol));
    };
    updateElementButton();
    connect(elementButton_, &QToolButton::clicked, this,
            [this, updateElementButton] {
                if (const int z =
                        PeriodicTableDialog::pickElement(this, activeElementZ_)) {
                    activeElementZ_ = z;
                    updateElementButton();
                }
            });
    frameToolbar->addWidget(elementButton_);

    // "Complete with hydrogens", directly after the element selector and
    // inside the editing group. It came from the Representation dock, which
    // was the wrong home: everything else in that panel changes how the
    // existing atoms are DRAWN, while this one adds atoms and pushes an undo
    // entry. It belongs with Insertion mode and the element it inserts.
    QAction* completeHydrogensAction =
        frameToolbar->addAction(tr("Complete with hydrogens"));
    ui::IconManager::bind(completeHydrogensAction, QStringLiteral("magic-line"));
    completeHydrogensAction->setToolTip(
        tr("Complete with hydrogens — add the hydrogens each atom's standard "
           "valence implies:\na carbon with three bonds gets one, an sp3 "
           "oxygen with one bond gets one, and so on.\n\n"
           "Positions come from relaxing the new bonds against the existing "
           "ones, so\nthe geometry follows the coordination (tetrahedral, "
           "trigonal, bent).\nMetals and transition metals are left alone. "
           "Undoable."));
    connect(completeHydrogensAction, &QAction::triggered, this,
            &MainWindow::completeWithHydrogens);

    // Visual break: navigation/editing + element selector | measurement modes.
    frameToolbar->addSeparator();
    addModeAction(QStringLiteral("ruler-2-line"),
                  tr("Distance measurement — click two atoms to read their "
                     "separation in Å\n(click empty space to reset)"),
                  ViewportWidget::InteractionMode::MeasureDistance,
                  QKeySequence(Qt::Key_D));
    addModeAction(QStringLiteral("triangle-fill"),
                  tr("Angle measurement — click three atoms (vertex second) "
                     "to read the angle in degrees\n(click empty space to "
                     "reset)"),
                  ViewportWidget::InteractionMode::MeasureAngle,
                  QKeySequence(Qt::Key_A));
    rotateMode->setChecked(true);
    frameToolbar->addSeparator();

    QAction* resetAction = frameToolbar->addAction(
        tr("Reset camera  [F]"));
    ui::IconManager::bind(resetAction, QStringLiteral("focus-3-line"));
    resetAction->setToolTip(
        tr("Reset camera — restore the default point-of-view saved in "
           "~/.calango/settings.json, or, when none has been set, center and "
           "frame the structure.  [F]"));
    // The 'F' shortcut lives here now that the View → Alignment submenu is gone.
    resetAction->setShortcut(QKeySequence(Qt::Key_F));
    connect(resetAction, &QAction::triggered, this, &MainWindow::resetCamera);
    frameToolbar->addAction(orthoAction_);
    // --- Perspective (field of view) --------------------------------------
    //
    // Directly after the projection toggle, because the two are one decision
    // taken in two steps: that toggle chooses WHETHER the scene has
    // perspective, and this chooses HOW MUCH. Moving the slider even switches
    // the toggle off for you, since a dolly zoom has nothing to show under a
    // parallel projection — and a control that changes another one has no
    // business sitting at the far end of the toolbar from it.
    //
    // A popup rather than a slider sitting in the toolbar: the control is
    // vertical — wide-angle at one end, long-lens at the other, which is how a
    // lens is thought about — and a vertical slider inline would set the
    // height of the entire toolbar for something reached occasionally.
    auto* perspectiveButton = new QToolButton(frameToolbar);
    ui::IconManager::bind(perspectiveButton, QStringLiteral("camera-lens-line"));
    perspectiveButton->setFocusPolicy(Qt::NoFocus);
    perspectiveButton->setToolTip(
        tr("Perspective — a dolly zoom over the camera's field of view.\n\n"
           "The eye moves with the angle, so the structure keeps its size on "
           "screen and only the depth relationship changes: narrow is a long "
           "lens from far off, flattening the near and far faces of a cell "
           "onto each other, while wide is a short lens from close in, "
           "throwing them apart and driving the eye down a channel or a "
           "pore.\n\n"
           "It needs the perspective projection to have anything to show, so "
           "moving it leaves orthographic. Reset restores both the default "
           "angle and the projection you started in."));
    {
        // A QMenu holding a QWidgetAction, rather than a bare Qt::Popup widget
        // positioned by hand. Qt then owns the mouse grab, the placement and
        // the dismissal — including the release that follows the opening
        // click, which on a hand-rolled popup lands on whatever now sits under
        // the cursor and had the slider jump to it.
        auto* menu = new QMenu(perspectiveButton);
        auto* page = new QWidget(menu);
        auto* pageLayout = new QVBoxLayout(page);
        pageLayout->setContentsMargins(10, 8, 10, 8);

        auto* readout = new QLabel(page);
        readout->setAlignment(Qt::AlignHCenter);
        auto* slider = new QSlider(Qt::Vertical, page);
        slider->setRange(static_cast<int>(render::kMinFieldOfViewDeg),
                         static_cast<int>(render::kMaxFieldOfViewDeg));
        slider->setMinimumHeight(150);
        auto* resetButton = new QPushButton(tr("Reset"), page);
        resetButton->setToolTip(tr("Back to %1°, the default.")
                                    .arg(static_cast<int>(
                                        render::kDefaultFieldOfViewDeg)));

        pageLayout->addWidget(readout);
        pageLayout->addWidget(slider, 1, Qt::AlignHCenter);
        pageLayout->addWidget(resetButton);

        auto* action = new QWidgetAction(menu);
        action->setDefaultWidget(page);
        menu->addAction(action);
        perspectiveButton->setMenu(menu);
        perspectiveButton->setPopupMode(QToolButton::InstantPopup);

        const auto syncReadout = [readout](int degrees) {
            readout->setText(tr("%1°").arg(degrees));
        };
        // What was orthographic before the popup opened, so Reset can put it
        // back rather than leaving the user in a projection they never chose.
        auto wasOrthographic = std::make_shared<bool>(false);

        connect(slider, &QSlider::valueChanged, this,
                [this, syncReadout](int degrees) {
                    syncReadout(degrees);
                    // A dolly zoom needs somewhere to dolly TO. Under the
                    // orthographic projection this application defaults to,
                    // distance·tan(fov/2) IS the frustum height, so the
                    // compensated move cancels the angle exactly and the
                    // slider would appear dead. Drive the projection from the
                    // action, not the viewport, so the View-menu check mark
                    // and the [O] shortcut stay honest about the state.
                    if (orthoAction_ && orthoAction_->isChecked())
                        orthoAction_->setChecked(false);
                    viewport_->dollyFieldOfView(static_cast<float>(degrees));
                });
        connect(resetButton, &QPushButton::clicked, slider,
                [slider, wasOrthographic, this] {
                    slider->setValue(
                        static_cast<int>(render::kDefaultFieldOfViewDeg));
                    if (*wasOrthographic && orthoAction_)
                        orthoAction_->setChecked(true);
                });
        // Re-read on open: the field of view can also change with a restored
        // point of view or a switch to a tab that carries its own camera.
        connect(menu, &QMenu::aboutToShow, this,
                [this, slider, syncReadout, wasOrthographic] {
                    *wasOrthographic = orthoAction_ && orthoAction_->isChecked();
                    const QSignalBlocker blocker(slider);
                    slider->setValue(
                        static_cast<int>(std::lround(viewport_->fieldOfView())));
                    syncReadout(slider->value());
                });
    }
    frameToolbar->addWidget(perspectiveButton);
    // Directly after the projection toggle: both are about how the scene is
    // LOOKED AT rather than what is drawn, and this is the one that makes a
    // framing reproducible instead of hand-dragged.
    QAction* povAction = frameToolbar->addAction(
        ui::IconManager::icon(QStringLiteral("eye-fill")),
        tr("Set point-of-view…"));
    povAction->setToolTip(
        tr("Set point-of-view… — read out and type the camera's zoom, "
           "rotation and pan, and save named views to reuse on other "
           "structures."));
    connect(povAction, &QAction::triggered, this, &MainWindow::showPointOfView);

    // Film mode, directly after the point-of-view it is built out of: a film
    // IS a sequence of saved points-of-view, so the button that authors one
    // belongs next to the button that saves them.
    filmModeAction_ = frameToolbar->addAction(
        ui::IconManager::icon(QStringLiteral("film-fill")), tr("Film mode"));
    filmModeAction_->setCheckable(true);
    filmModeAction_->setToolTip(
        tr("Film mode — swap the trajectory timeline for a film timeline in "
           "seconds, and drive the camera from the film.\n\n"
           "The camera you have now is restored when film mode is switched "
           "off, so previewing never costs you the view you set up."));
    connect(filmModeAction_, &QAction::toggled, this, &MainWindow::setFilmMode);

    filmProductionAction_ = frameToolbar->addAction(
        ui::IconManager::icon(QStringLiteral("clapperboard-fill")),
        tr("Film production…"));
    filmProductionAction_->setToolTip(
        tr("Film production… — build a film from the saved points-of-view: "
           "transitions between shots, total duration and frame rate, cast "
           "opacity keyframes, and — with a trajectory in the workspace — "
           "which of the two timelines sets the length."));
    connect(filmProductionAction_, &QAction::triggered, this,
            &MainWindow::showFilmProduction);

    frameToolbar->addSeparator();
    QAction* alignXy = frameToolbar->addAction(
        cameraToolbarIcon(QStringLiteral("xy")), tr("Align view with the XY plane"));
    connect(alignXy, &QAction::triggered, viewport_, &ViewportWidget::alignWithXY);
    QAction* alignXz = frameToolbar->addAction(
        cameraToolbarIcon(QStringLiteral("xz")), tr("Align view with the XZ plane"));
    connect(alignXz, &QAction::triggered, viewport_, &ViewportWidget::alignWithXZ);
    QAction* alignYz = frameToolbar->addAction(
        cameraToolbarIcon(QStringLiteral("yz")), tr("Align view with the YZ plane"));
    connect(alignYz, &QAction::triggered, viewport_, &ViewportWidget::alignWithYZ);


    // --- Fixed-angle axis rotations ---------------------------------------
    // X+/X− .. Z+/Z− rotate the scene about a world axis by the editable
    // step; the transform animates smoothly and clicks compose exactly.
    frameToolbar->addSeparator();
    auto* angleStepSpin = new QDoubleSpinBox(frameToolbar);
    angleStepSpin->setRange(1.0, 180.0);
    angleStepSpin->setValue(15.0);
    angleStepSpin->setDecimals(1);
    angleStepSpin->setSingleStep(5.0);
    angleStepSpin->setSuffix(QStringLiteral("°"));
    angleStepSpin->setToolTip(tr("Rotation step of the X/Y/Z axis buttons"));
    frameToolbar->addWidget(angleStepSpin);
    const std::array<QString, 3> axisNames{QStringLiteral("X"),
                                           QStringLiteral("Y"),
                                           QStringLiteral("Z")};
    for (int axis = 0; axis < 3; ++axis) {
        for (const int sign : {+1, -1}) {
            auto* button = new QToolButton(frameToolbar);
            button->setToolButtonStyle(Qt::ToolButtonTextOnly);
            button->setText(QStringLiteral("%1%2").arg(
                axisNames[static_cast<std::size_t>(axis)],
                sign > 0 ? QStringLiteral("+") : QStringLiteral("−")));
            button->setToolTip(sign > 0
                                   ? tr("Rotate counter-clockwise around the "
                                        "%1 axis by the angle step")
                                         .arg(axisNames[static_cast<std::size_t>(axis)])
                                   : tr("Rotate clockwise around the %1 axis "
                                        "by the angle step")
                                         .arg(axisNames[static_cast<std::size_t>(axis)]));
            connect(button, &QToolButton::clicked, this,
                    [this, axis, sign, angleStepSpin] {
                        viewport_->rotateSceneAxis(axis,
                                                   sign * angleStepSpin->value());
                    });
            frameToolbar->addWidget(button);
        }
    }

    // --- Display toggles ---------------------------------------------------
    // Four one-bit "is this drawn" switches, moved up from the Representation
    // dock. They are the settings flipped most often while READING a structure
    // — hide the hydrogens to see the skeleton, turn on the indices to find
    // the atom a log mentions — and a dock the user may have collapsed is the
    // wrong place for something reached that constantly.
    //
    // Unlike the rest of that dock they are also not per-cast: each one is a
    // single viewport-wide bit, which is exactly what a toolbar toggle models.
    frameToolbar->addSeparator();
    const auto addToggle = [frameToolbar](const QString& iconName,
                                          const QString& text,
                                          bool checked) {
        QAction* action = frameToolbar->addAction(text);
        ui::IconManager::bind(action, iconName);
        action->setCheckable(true);
        action->setChecked(checked);
        return action;
    };

    QAction* elementLabelsAction =
        addToggle(QStringLiteral("atom-line"), tr("Show element symbols"),
                  viewport_->showElementLabels());
    elementLabelsAction->setToolTip(
        tr("Show element symbols — overlay each atom's chemical symbol "
           "(Fe, O, Si…) on the 3D viewport."));
    connect(elementLabelsAction, &QAction::toggled, viewport_,
            &ViewportWidget::setShowElementLabels);

    QAction* indexLabelsAction =
        addToggle(QStringLiteral("price-tag-fill"), tr("Show atomic indices"),
                  viewport_->showAtomIndexLabels());
    indexLabelsAction->setToolTip(
        tr("Show atomic indices — overlay each atom's 1-based index "
           "(#1, #2…) on the 3D viewport."));
    connect(indexLabelsAction, &QAction::toggled, viewport_,
            &ViewportWidget::setShowAtomIndexLabels);

    // "H", literally — the heading glyph is an H.
    showHydrogensAction_ =
        addToggle(QStringLiteral("heading"), tr("Draw hydrogen atoms"),
                  viewport_->style().showHydrogens);
    showHydrogensAction_->setToolTip(
        tr("Draw hydrogen atoms, their bonds and the hydrogen-bond dashes.\n"
           "Off leaves the heavy-atom skeleton, which is how a crowded organic\n"
           "or protein structure is normally read.\n\n"
           "Display only — the hydrogens stay in the structure, so the formula "
           "and every\ncalculation and exported file are unchanged."));
    connect(showHydrogensAction_, &QAction::toggled, this, [this](bool on) {
        viewport_->style().showHydrogens = on;
        viewport_->styleChanged(true);
    });

    // A staircase: the stepped ramp from one colour to the next, which the
    // flat half-and-half split it replaces does not do.
    QAction* gradientBondsAction =
        addToggle(QStringLiteral("stairs-fill"), tr("Show bonds smoothly"),
                  viewport_->style().gradientBonds);
    gradientBondsAction->setToolTip(
        tr("Blend each bond smoothly from one atom's color to the other's\n"
           "instead of the classic half-and-half split."));
    connect(gradientBondsAction, &QAction::toggled, this, [this](bool on) {
        viewport_->style().gradientBonds = on;
        viewport_->styleChanged(true);
    });

    // --- Workspace duplication / frame extraction -------------------------
    // LAST on the toolbar, deliberately: it is the only entry that creates a
    // new workspace rather than changing this one, and anything added later
    // belongs before it.
    // Clones the on-screen geometry into a new tab (a trajectory yields just
    // its current frame as a static structure). Theme-tinted RemixIcon; also
    // reachable from the tab bar's right-click menu.
    frameToolbar->addSeparator();
    QAction* duplicateAction = frameToolbar->addAction(tr("Duplicate Workspace / Extract Frame to New Tab"));
    ui::IconManager::bind(duplicateAction,
                          QStringLiteral("split-cells-horizontal"));
    duplicateAction->setToolTip(
        tr("Duplicate the active workspace into a new tab. For a trajectory, "
           "extract the frame currently shown as a standalone structure "
           "(the original timeline stays in this tab)."));
    connect(duplicateAction, &QAction::triggered, this,
            &MainWindow::duplicateOrExtractFrame);


    // The Lattice Plane and Custom overlay buttons are gone from this toolbar.
    // Both opened a modeless dialog that owned its own private overlay list, so
    // a scene with a plane and a labelled sphere meant two floating windows and
    // no single place showing what was actually drawn. Both are now entries in
    // the "Additional overlays" dock, alongside text annotations.

    auto* central = new QWidget(this);
    auto* centralLayout = new QVBoxLayout(central);
    centralLayout->setContentsMargins(0, 0, 0, 0);
    centralLayout->setSpacing(0);
    centralLayout->addWidget(tabBar_);
    centralLayout->addWidget(frameToolbar);
    centralLayout->addWidget(viewport_, 1);
    centralLayout->addWidget(timeline_);
    centralLayout->addWidget(filmTimeline_);
    setCentralWidget(central);

    connect(tabBar_, &QTabBar::currentChanged, this, &MainWindow::onTabChanged);
    // Record every camera move into the tab it belongs to, so the outgoing
    // tab's view is already up to date by the time a switch happens. Capturing
    // here rather than at switch time also means there is no "previous tab"
    // index to track and get wrong when tabs are closed or reordered.
    connect(viewport_, &ViewportWidget::cameraChanged, this, [this] {
        if (restoringPointOfView_ || applyingFilm_)
            return; // mid-restore or mid-playback: not a user camera move
        if (Document* doc = currentDocument())
            doc->pointOfView = viewport_->camera().pointOfView();
    });
    connect(tabBar_, &QTabBar::tabCloseRequested, this, &MainWindow::onTabCloseRequested);
    connect(tabBar_, &QTabBar::tabMoved, this, &MainWindow::onTabMoved);
    // Workspace context menu (Duplicate / Extract Frame, Close) on right-click.
    tabBar_->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(tabBar_, &QTabBar::customContextMenuRequested, this,
            &MainWindow::showTabContextMenu);
    connect(timeline_, &TimelineWidget::frameChanged, this, &MainWindow::showFrame);
    connect(filmTimeline_, &FilmTimelineWidget::timeChanged, this,
            &MainWindow::showFilmTime);

    createMenusAndDocks();

    // The log widget shows the running job directly (it always clears on
    // start); its output/error/progress are routed through MainWindow so they
    // are buffered per process and only mirrored when that process is
    // selected. Metric samples are likewise routed for per-process isolation.
    connect(jobRunner_, &jobs::JobRunner::started,
            jobLogWidget_, &JobLogWidget::onJobStarted);
    connect(jobRunner_, &jobs::JobRunner::outputLine,
            this, &MainWindow::onJobOutputLine);
    connect(jobRunner_, &jobs::JobRunner::errorLine,
            this, &MainWindow::onJobErrorLine);
    connect(jobRunner_, &jobs::JobRunner::progress,
            this, &MainWindow::onJobProgress);
    connect(jobRunner_, &jobs::JobRunner::energySample,
            this, &MainWindow::onEnergySample);
    connect(jobRunner_, &jobs::JobRunner::temperatureSample,
            this, &MainWindow::onTemperatureSample);
    connect(jobRunner_, &jobs::JobRunner::targetTemperature,
            this, &MainWindow::onTargetTemperature);
    connect(jobRunner_, &jobs::JobRunner::maxForceSample,
            this, &MainWindow::onForceSample);
    connect(jobRunner_, &jobs::JobRunner::pressureSample,
            this, &MainWindow::onPressureSample);
    connect(jobRunner_, &jobs::JobRunner::targetPressure,
            this, &MainWindow::onTargetPressure);
    connect(jobRunner_, &jobs::JobRunner::finished,
            jobLogWidget_, &JobLogWidget::onJobFinished);
    connect(jobRunner_, &jobs::JobRunner::finished,
            this, &MainWindow::onJobFinished);
    // Connected AFTER onJobFinished, deliberately. Qt delivers to slots in
    // connection order, so the finished run has already persisted its metrics,
    // released currentTaskId_ and opened its result viewer by the time the
    // queue binds the next job as the current task — and onJobFinished has a
    // dozen early returns, so pumping from inside it would need the call
    // repeated on every one of them.
    connect(jobRunner_, &jobs::JobRunner::finished,
            this, [this](int, bool) { startNextQueuedJob(); });
    connect(jobRunner_, &jobs::JobRunner::frameStreamed,
            this, &MainWindow::onFrameStreamed);
    connect(jobLogWidget_, &JobLogWidget::terminateRequested,
            jobRunner_, &jobs::JobRunner::terminate);

    connect(viewport_, &ViewportWidget::selectionChanged, this, [this](int count) {
        if (count > 0)
            statusBar()->showMessage(tr("%n atom(s) selected", nullptr, count));
    });

    // Insertion mode edits: the viewport only *requests* — the document
    // (with its undo history) is mutated here.
    connect(viewport_, &ViewportWidget::atomInsertRequested, this,
            [this](const core::Vec3& position) {
                Document& doc = ensureDocument();
                pushUndo();
                doc.structure->addAtom({activeElementZ_, position});
                notifyStructureChanged(false);
                statusBar()->showMessage(
                    tr("Added %1 at (%2, %3, %4) Å")
                        .arg(QLatin1String(
                            core::Elements::data(activeElementZ_).symbol))
                        .arg(position.x, 0, 'f', 2)
                        .arg(position.y, 0, 'f', 2)
                        .arg(position.z, 0, 'f', 2));
            });
    // Measurements land in the status console (the viewport overlays the
    // same value on the canvas).
    connect(viewport_, &ViewportWidget::measurementMade, this,
            [this](const QString& text) { statusBar()->showMessage(text); });
    connect(viewport_, &ViewportWidget::deleteSelectionRequested,
            this, &MainWindow::deleteSelectedAtoms);
    connect(viewport_, &ViewportWidget::bondInsertRequested, this,
            [this](int i, int j) {
                Document* doc = currentDocument();
                if (!doc || !doc->structure)
                    return;
                pushUndo();
                doc->structure->addBondOverride(i, j);
                notifyStructureChanged(false);
                statusBar()->showMessage(tr("Bond %1–%2 created").arg(i).arg(j));
            });
    // Insert mode Shift+click: substitute the clicked atom's element.
    connect(viewport_, &ViewportWidget::atomReplaceRequested, this,
            [this](int index) {
                Document* doc = currentDocument();
                if (!doc || !doc->structure || index < 0
                    || index >= static_cast<int>(doc->structure->size()))
                    return;
                pushUndo();
                doc->structure->atoms()[static_cast<std::size_t>(index)]
                    .atomicNumber = activeElementZ_;
                notifyStructureChanged(false);
                statusBar()->showMessage(
                    tr("Replaced atom %1 with %2").arg(index).arg(
                        QLatin1String(core::Elements::data(activeElementZ_).symbol)));
            });
    // Translation (Pan) mode Shift+drag: move a single atom (one undo/drag).
    connect(viewport_, &ViewportWidget::atomTranslateRequested, this,
            [this](int index, const core::Vec3& position, bool begin) {
                Document* doc = currentDocument();
                if (!doc || !doc->structure || index < 0
                    || index >= static_cast<int>(doc->structure->size()))
                    return;
                if (begin)
                    pushUndo();
                doc->structure->atoms()[static_cast<std::size_t>(index)].position =
                    position;
                viewport_->refreshStructure(); // fast, keeps camera + selection
                if (begin)
                    statusBar()->showMessage(tr("Translating atom %1…").arg(index));
            });

    // Palette + Zone-1 logo for the persisted theme. main() already applied the
    // palette pre-construction; this seeds the logo variant and, for the
    // System theme, keeps the app in sync if the OS scheme changes at runtime.
    applyAppearanceTheme();
#if QT_VERSION >= QT_VERSION_CHECK(6, 5, 0)
    connect(QGuiApplication::styleHints(), &QStyleHints::colorSchemeChanged, this,
            [this] {
                if (ThemeManager::current() == ThemeManager::Theme::System)
                    applyAppearanceTheme();
            });
#endif

    statusBar()->showMessage(tr("Ready — open a structure to begin (File → Open)"));
}

MainWindow::~MainWindow() = default;

void MainWindow::createMenusAndDocks()
{
    // Docks can sit side-by-side (nested), stack as tabs, or float.
    setDockOptions(QMainWindow::AnimatedDocks | QMainWindow::AllowNestedDocks
                   | QMainWindow::AllowTabbedDocks);

    // Menu bar order is fixed: File, Edit, View, Build, Simulation,
    // Analysis, Modules (Help trails as is conventional). "Modules" collects
    // the MLIP and Alloys tool families between Analysis and Help.
    // ----- File: New | Open | Save | Import/Export | Workspace | Quit ------
    QMenu* fileMenu = menuBar()->addMenu(tr("&File"));
    fileMenu->addAction(tr("&New Workspace"), QKeySequence::New,
                        this, &MainWindow::newProject);
    QMenu* openMenu = fileMenu->addMenu(tr("&Open"));
    openMenu->addAction(tr("&Structure…"), QKeySequence::Open,
                        this, &MainWindow::openStructure);
    openMenu->addAction(tr("&Trajectory…"), QKeySequence(tr("Ctrl+T")),
                        this, &MainWindow::openTrajectory);
    recentMenu_ = openMenu->addMenu(tr("Open &Recent"));
    updateRecentFilesMenu();
    QMenu* saveMenu = fileMenu->addMenu(tr("&Save"));
    saveMenu->addAction(tr("Structure &As…"), QKeySequence::SaveAs,
                        this, &MainWindow::saveStructureAs);
    saveMenu->addAction(tr("Tra&jectory As…"), QKeySequence(tr("Ctrl+Shift+T")),
                        this, &MainWindow::saveTrajectoryAs);
    QMenu* exportMenu = fileMenu->addMenu(tr("&Import / Export"));
    exportMenu->addAction(tr("Export &Image…"), QKeySequence(tr("Ctrl+E")),
                          this, &MainWindow::exportImage);
    // The format is chosen inside the dialog (MP4, MOV, MKV, WebM, AVI, GIF),
    // so spelling two of them out in the menu title is both incomplete and
    // redundant.
    exportMenu->addAction(tr("Export Ani&mation…"),
                          this, &MainWindow::exportAnimation);
    exportMenu->addAction(tr("Export to &Alembic…"), this,
                          &MainWindow::exportAlembic)
        ->setToolTip(tr("Scene geometry (atoms, bonds, unit cell) as an "
                        "Alembic .abc cache for Blender / Houdini / Maya; a "
                        "trajectory exports as animated samples"));
    exportMenu->addAction(tr("&Ray-Traced Render…"),
                          this, &MainWindow::openRayTraceDialog);
    fileMenu->addSeparator();
    // Project workspace: one .calproj file restores the whole multi-tab
    // session (structures, trajectories, job console + metric series).
    QMenu* workspaceMenu = fileMenu->addMenu(tr("Project &Workspace"));
    workspaceMenu->addAction(tr("Open &Project…"),
                             QKeySequence(tr("Ctrl+Shift+O")),
                             this, &MainWindow::openProject);
    workspaceMenu->addAction(tr("Save P&roject"), QKeySequence::Save,
                             this, &MainWindow::saveProject);
    workspaceMenu->addAction(tr("Save Project As…"),
                             this, &MainWindow::saveProjectAs);
    fileMenu->addAction(tr("&Close Tab"), QKeySequence::Close, this, [this] {
        if (tabBar_->currentIndex() >= 0)
            onTabCloseRequested(tabBar_->currentIndex());
    });
    fileMenu->addSeparator();
    // Quit goes through closeAllWindows so closeEvent persists the window
    // geometry / dock layout (and runs the unsaved-changes guard); the
    // embedded interpreter is finalized by PythonEngine's destructor.
    fileMenu->addAction(tr("&Quit"), QKeySequence::Quit,
                        qApp, &QApplication::closeAllWindows);

    // ----- Edit: Selection | Deletion | Bonds | Cell | Preferences ---------
    QMenu* editMenu = menuBar()->addMenu(tr("&Edit"));
    undoAction_ = editMenu->addAction(tr("&Undo"), QKeySequence::Undo,
                                      this, &MainWindow::undo);
    redoAction_ = editMenu->addAction(tr("&Redo"), QKeySequence::Redo,
                                      this, &MainWindow::redo);
    editMenu->addSeparator();
    editMenu->addAction(tr("&Add Atom…"), QKeySequence(tr("Ctrl+Shift+A")),
                        this, &MainWindow::addAtom);
    QMenu* selectionMenu = editMenu->addMenu(tr("&Selection"));
    selectionMenu->addAction(tr("&Change Element of Selection…"),
                             this, &MainWindow::changeElementOfSelection);
    selectionMenu->addAction(tr("&Translate Selection…"),
                             this, &MainWindow::translateSelection);
    editMenu->addAction(tr("&Delete Selected Atoms"), QKeySequence::Delete,
                        this, &MainWindow::deleteSelectedAtoms);
    editMenu->addSeparator();
    editMenu->addAction(tr("&Bond Editor…"), QKeySequence(tr("Ctrl+B")),
                        this, &MainWindow::showBondEditor);
    // Supercell creation is unified under Build → Supercell (Transformation
    // Matrix); the old Edit → Unit Cell → Create Supercell entry was removed.
    editMenu->addSeparator();
    // Ctrl+P (⌘P on macOS — Qt maps the Ctrl modifier to Command there).
    // Spelled literally instead of QKeySequence::Preferences so the shortcut
    // is the same on every platform; nothing in the app prints, so the
    // conventional Print binding is free.
    QAction* preferencesAction =
        editMenu->addAction(tr("&Preferences…"), QKeySequence(tr("Ctrl+P")),
                            this, &MainWindow::showPreferences);
    // Application-wide, so Preferences opens even while a results window or
    // undocked panel has focus rather than only from the main window.
    preferencesAction->setShortcutContext(Qt::ApplicationShortcut);
    updateUndoActions();

#ifdef Q_OS_MACOS
    // Keep the Edit menu strictly developer-defined on macOS. Qt otherwise
    // auto-assigns Mac menu *roles* to actions it recognises by text (e.g.
    // "Preferences…" would be hoisted into the application menu); forcing
    // NoRole keeps every entry where it was declared. AppKit *also* injects
    // its own text-editing items ("Start Dictation…", "Emoji & Symbols", and
    // on Sequoia "Writing Tools"/"AutoFill") into any menu titled "Edit";
    // those are disabled via the user-defaults keys set in main() before the
    // menu bar is built (see disableMacEditMenuInjections()).
    for (QAction* action : editMenu->actions())
        action->setMenuRole(QAction::NoRole);
#endif

    // ----- View: effects | panels ------------------------------------------
    // Projection (perspective/orthographic) lives solely on the frame-panel
    // toolbar 'O' button; unit-cell visibility + wireframe styling live on the
    // Representation dock's "Unit cell" tab. Camera alignment (frame [F],
    // XY/XZ/YZ) lives entirely on the 3D-viewport toolbar — the View →
    // Alignment submenu was removed as redundant.
    QMenu* viewMenu = menuBar()->addMenu(tr("&View"));
    // Visual effects (fog, depth blur, lighting) live entirely in the Zone-9
    // "Visual Effects" dock; the dock's own toggle is in View → the docks.

    // ----- Build: generators and structure sources -------------------------
    // Ordered by workflow rather than by when each builder was added:
    // structure *sources* first (the database browser is where most sessions
    // start), then the builders that grow a structure from an existing one,
    // roughly by increasing dimensionality/complexity, with the reciprocal-
    // space tool last behind a separator.
    QMenu* buildMenu = menuBar()->addMenu(tr("&Build"));
    buildMenu->addAction(tr("From &Database…"), this, &MainWindow::openExamplesBrowser)
        ->setToolTip(tr("Bulk crystals (ase.build.bulk), Materials Project, PubChem"));
    buildMenu->addAction(tr("Nano&particle Builder…"),
                         this, &MainWindow::openNanoparticleBuilder);
    buildMenu->addAction(tr("&Surface Slab…"), this, &MainWindow::cleaveSurface);
    buildMenu->addAction(tr("&Nanomaterials…"), this, &MainWindow::openNanoBuilder);
    // Supercell moved to the Structure panel's action row: it is a
    // whole-structure transform like centring and vacuum padding, and having it
    // in a menu while its siblings were buttons made the grouping arbitrary.
    // Decorating a surface follows building one, so it sits under Surface Slab
    // and Nanoparticle rather than in Analysis with the site *statistics*.
    buildMenu->addAction(tr("Add &adsorbate…"), this,
                         &MainWindow::openAddAdsorbate)
        ->setToolTip(tr("Place one atom or one molecule/radical on the current "
                        "geometry, on a detected site or at an explicit "
                        "position; the result opens as a new tab"));
    buildMenu->addSeparator();
    // Molecular-system builders: both generate a periodic box of molecules
    // rather than cleaving or repeating a crystal, so they group together.
    buildMenu->addAction(tr("&Macromolecules…"),
                         this, &MainWindow::openMacromoleculeBuilder)
        ->setToolTip(tr("Polymer chains: monomer chemistry, tacticity, "
                        "conformation and amorphous multi-chain packing"));
    buildMenu->addAction(tr("&Water && Ice…"),
                         this, &MainWindow::openWaterIceBuilder)
        ->setToolTip(tr("Liquid water and the ice polymorphs, with "
                        "Bernal-Fowler proton disorder"));
    // Third in the molecular-systems block because it CONSUMES a structure
    // rather than generating one from nothing: it needs a slab (from Surface
    // Slab, above) to open a region on.
    buildMenu->addAction(tr("&Liquid / Gas Interface…"),
                         this, &MainWindow::openLiquidInterfaceBuilder)
        ->setToolTip(tr("Open a fluid region on the current structure and pack "
                        "it with a liquid, a gas, a mixture, or an ionic "
                        "solution — solid/liquid and solid/gas interfaces"));
    buildMenu->addSeparator();
    // Crystal defects: both CONSUME the current structure as a parent lattice
    // and both are pure geometry (no engine, no Python), so they group
    // together and sit after the molecular builders rather than among them.
    buildMenu->addAction(tr("&Dislocation…"), this,
                         &MainWindow::openDislocationBuilder)
        ->setToolTip(tr("Edge, screw, glide and climb dislocations, plus the "
                        "anisotropic (Stroh) solution for a mixed Burgers "
                        "vector — inserted by displacing the current "
                        "structure's atoms"));
    buildMenu->addAction(tr("Solid &Interface…"), this,
                         &MainWindow::openSolidInterfaceBuilder)
        ->setToolTip(tr("Stacking faults, twin boundaries, bicrystals and "
                        "(multi-phase) polycrystals built from the open "
                        "structures as parent lattices"));
    // Cluster Expansion, SQS and Warren-Cowley now live under Modules → Alloys;
    // the alloy toolchain is grouped there rather than split across Build /
    // Simulation / Analysis.
    buildMenu->addSeparator();
    buildMenu->addAction(tr("&Brillouin Zone Builder…"),
                         this, &MainWindow::showBrillouinZone);

    // ----- Simulation: local/remote jobs and ML datasets -------------------
    QMenu* simulationMenu = menuBar()->addMenu(tr("&Simulation"));
    simulationMenu->addAction(tr("&Single-point Calculation…"),
                              QKeySequence(tr("Ctrl+R")),
                              this, &MainWindow::singlePointCalculation);
    simulationMenu->addAction(tr("&Geometry Optimization…"),
                              this, &MainWindow::geometryOptimization);
    simulationMenu->addAction(tr("&Molecular Dynamics…"),
                              this, &MainWindow::molecularDynamics);
    simulationMenu->addAction(tr("&Phonon…"),
                              this, &MainWindow::openPhononBuilder);
    simulationMenu->addAction(tr("&Monte Carlo Simulation…"),
                              this, &MainWindow::openMonteCarlo);
    // Random noise moved here from Build: it no longer merely displaces a
    // structure, it runs a single point on every displaced copy — that is a
    // job, and jobs live on this menu.
    simulationMenu->addAction(tr("Random N&oise Setup…"),
                              this, &MainWindow::addRandomNoise);
    simulationMenu->addAction(tr("&Nudged Elastic Band (NEB)…"),
                              this, &MainWindow::openNudgedElasticBand);
    // Cluster Expansion Calculation moved to Modules → Alloys.
    // "New Remote Calculation…" was removed along with the legacy calculator
    // dialog it opened: remote execution is now chosen inside each wizard
    // (Stage 2 execution mode + the Stage-4 "Run (Remote)" button) and
    // monitored in the Zone-11 Remote Access manager, so a second, parallel
    // entry point would generate scripts the wizards no longer own.
    // Dataset Manager and Trainer moved to Modules → MLIP.

    // ----- Electronics: everything that reads out the electronic state ------
    // These all answer questions about the ELECTRONS of an already-solved
    // system (bands, dielectric response, quasiparticles, localized orbitals)
    // rather than about where the nuclei end up, which is what the Simulation
    // menu above is for. They also share a workflow — each inherits a completed
    // SCF baseline rather than converging its own — so grouping them puts the
    // whole post-processing chain in one place instead of scattering it down a
    // long Simulation list.
    QMenu* electronicsMenu = menuBar()->addMenu(tr("&Electronics"));
    electronicsMenu->addAction(tr("Electronic &Structure…"),
                               this, &MainWindow::showBandStructure);
    // Effective Bands (Popescu-Zunger unfolding) reads out of an electronic
    // structure run, so it sits immediately after "Electronic Structure…".
    electronicsMenu->addAction(tr("&Effective Bands (Unfolding)…"),
                               this, &MainWindow::effectiveBandsCalculation);
    // Linear optical response (dielectric function, absorption, reflectivity,
    // refractive index, energy loss) via GPAW's response module.
    electronicsMenu->addAction(tr("&Optics…"),
                               this, &MainWindow::showOptics);
    // Directly after the linear response it extends. Second order is where the
    // symmetry requirement appears — χ⁽²⁾ and the shift current are odd-rank
    // tensors and vanish identically in a centrosymmetric crystal — so the two
    // entries sit together and the tooltip says which question each answers.
    electronicsMenu
        ->addAction(tr("&Nonlinear Optics…"), this,
                    &MainWindow::showNonlinearOptics)
        ->setToolTip(tr("Second-harmonic generation χ⁽²⁾, the shift current "
                        "and the linear susceptibility tensor, via GPAW's "
                        "gpaw.nlopt. Needs a non-centrosymmetric crystal"));
    // G₀W₀ sits right after Optics, the other response-function calculation.
    electronicsMenu->addAction(tr("&GW Calculations…"), this,
                               &MainWindow::showGwCalculations)
        ->setToolTip(tr("One-shot G₀W₀ quasiparticle corrections on top of a "
                        "completed SCF (GPAW or Yambo)"));
    // MLWF is a DFT post-process staged & run through the standardized wizard
    // (engine selection + auto-bound Conda env); its result viewer opens from
    // the Processes panel when the job finishes.
    //
    // ELF used to sit beside it. It no longer needs a module of its own: the
    // Single-point wizard writes elf.cube from the same SCF as one of its six
    // density exports, and the grid renders in the main 3D viewport through the
    // Volumetric Data dock — so a separate wizard, a separate job and a
    // separate isosurface dialog were three copies of machinery that already
    // existed.
    electronicsMenu->addAction(
        tr("&Wannier Functions…"),
        this, &MainWindow::showWannier);
    // The three Wannier post-processes are standalone modules with a strict
    // prerequisite: each diagonalizes the localized H(R) a completed Wannier Functions run
    // produced, so each begins by selecting one (and refuses without it).
    electronicsMenu
        ->addAction(tr("Wannier &Interpolation…"), this,
                    &MainWindow::showWannierInterpolation)
        ->setToolTip(tr("Interpolated band structure + projected DOS "
                        "(H(R) → H(k)) from a completed Wannier Functions process"));
    electronicsMenu
        ->addAction(tr("&Fermi Surface…"), this, &MainWindow::showFermiSurface)
        ->setToolTip(tr("E_n(k) = E_F sheets on a dense interpolated k-grid, "
                        "from a completed Wannier Functions process"));
    electronicsMenu
        ->addAction(tr("&Topological Invariants…"), this,
                    &MainWindow::showTopologicalInvariants)
        ->setToolTip(tr("Chern number and Z₂ index from the hybrid Wannier "
                        "centre (Wilson loop) flow, from a completed Wannier Functions "
                        "process"));
    // Charged defects sit with the other post-processes that consume a
    // completed SCF. Its two inputs are both Single-Point runs, which is what
    // distinguishes it from everything above: the physics is in the
    // DIFFERENCE between a host cell and a defect cell.
    electronicsMenu->addAction(tr("&Charged defects…"), this,
                               &MainWindow::showChargedDefects)
        ->setToolTip(tr("Formation energies E_f(q, E_F), thermodynamic "
                        "transition levels and the charged-defect diagram, "
                        "with the Freysoldt-Neugebauer-Van de Walle "
                        "finite-size correction"));
    // Hubbard U from linear response. It sits with the post-processes because
    // it is one — the answer is a response function assembled from a queue of
    // single points — but it is the only entry here that produces a PARAMETER
    // rather than an observable: what comes out is fed back into the next
    // calculation's DFT+U, which is why it is worth computing rather than
    // borrowing from a paper about a different compound.
    electronicsMenu->addAction(tr("&Hubbard Parameter Calculation…"), this,
                               &MainWindow::showHubbardParameters)
        ->setToolTip(tr("U_eff from the linear response of the on-site "
                        "occupation to a localized perturbation "
                        "(Cococcioni & de Gironcoli) — VASP or Quantum "
                        "ESPRESSO"));
    electronicsMenu->addSeparator();
    // Born charges are the electronic response to a DISPLACEMENT rather than to
    // a field, which is why they sit slightly apart from the rest.
    electronicsMenu->addAction(tr("&Born Effective Charges…"), this,
                               &MainWindow::showBornCharges)
        ->setToolTip(tr("Z* tensors from the polarization response to atomic "
                        "displacements — the LO-TO splitting and IR "
                        "intensities depend on them"));
    // Directly after Born charges, which it consumes: the IR intensities ARE
    // the Z* tensors contracted with the phonon eigenvectors, so this is the
    // step that turns that run into a spectrum.
    // XAS sits with the other spectroscopies. Its own module rather than an
    // option on one of them: the core-hole dataset it has to generate first
    // has no analogue anywhere else in the application.
    electronicsMenu->addAction(tr("&X-ray Absorption Spectroscopy (XAS)…"),
                               this, &MainWindow::showXas);
    electronicsMenu->addAction(tr("&Raman and IR Spectroscopy…"), this,
                               &MainWindow::showRamanIrSpectroscopy)
        ->setToolTip(tr("Γ-point Raman and infrared spectra: IR from the "
                        "inherited Born effective charges, Raman from the "
                        "polarizability derivative ∂α/∂Q"));

    // ----- Analysis: spec order, reciprocal-space tools at the end ---------
    QMenu* analysisMenu = menuBar()->addMenu(tr("&Analysis"));
    // One entry, not two. "Raman Modes…" used to sit further down this menu
    // and opened a dialog that answered a question about the point group —
    // detected by the same call, at a tolerance it gave the user no way to
    // set. It is a tab of the Symmetry dialog now, computed from that dialog's
    // own detection.
    analysisMenu->addAction(tr("&Symmetry, Raman && IR Activity…"),
                            this, &MainWindow::showSymmetry);
    // Directly beneath the crystallographic symmetry it extends. A magnetic
    // space group is what the ordinary one becomes once the magnetic moments
    // are allowed to have a say: the operations that move a moment onto its
    // own reverse survive only in combination with time reversal, and which
    // ones those are is a different (and strictly lower) group.
    analysisMenu->addAction(tr("&Magnetic Space Group…"), this,
                            &MainWindow::showMagneticSpaceGroup)
        ->setToolTip(tr("Belov-Neronova-Smirnova classification among the "
                        "1651 magnetic space groups, from the coordinates "
                        "plus the atomic magnetic moments"));
    analysisMenu->addAction(tr("Structure &Factor S(q)…"),
                            this, &MainWindow::showStructureFactor);
    analysisMenu->addAction(tr("&X-Ray Diffraction (XRD)…"),
                            this, &MainWindow::showXrd);
    analysisMenu->addAction(tr("&Radial Distribution Function…"),
                            this, &MainWindow::showRdf);
    analysisMenu->addAction(tr("Bond &Length / Angle Distributions…"),
                            this, &MainWindow::showDistributions);
    analysisMenu->addAction(tr("&Coordination Numbers (CN / GCN)…"),
                            this, &MainWindow::showCoordination);
    // Warren-Cowley (short-range order) moved to Modules → Alloys.
    analysisMenu->addAction(tr("Local &Entropy Analysis…"),
                            this, &MainWindow::showLocalEntropy);
    analysisMenu->addAction(tr("Partial &Charge Analysis…"),
                            this, &MainWindow::showPartialCharge);
    analysisMenu->addAction(tr("&Velocity Autocorrelation Function (VACF)…"),
                            this, &MainWindow::showVacf);
    // "Raman Modes…" was here; it is now a tab of the Symmetry dialog at the
    // top of this menu. "Volumetric Data…" was here too, and is gone: the
    // Volumetric Data DOCK loads the same grids, renders them in the main
    // viewport rather than in a window of its own, and is where every other
    // part of the application already sends its output.
    // Charge density difference sits with the volumetric tools because that is
    // what it produces: one more grid in that dock, rendered in the main
    // viewport like any other.
    analysisMenu->addAction(tr("Charge &Density Difference (CDD)…"),
                            this, &MainWindow::showChargeDensityDifference)
        ->setToolTip(tr("Δρ = ρ(A+B) − ρ(A) − ρ(B) from a completed "
                        "single-point: where the charge went when two "
                        "fragments were brought together"));
    // MLWF is a DFT post-process: its setup + run lives in the Electronics
    // menu (as a multi-stage wizard); its result viewer opens automatically
    // when the job finishes.
    analysisMenu->addAction(tr("Adsorption && Catal&ysis…"),
                            this, &MainWindow::showAdsorption);
    // Brillouin Zone Builder moved to the Build menu.

    // The "Results" menu is gone. Its five entries all began by asking which
    // process the user meant — and then failed with "select a completed X in
    // the Processes panel first" when the answer was wrong. The viewers now
    // open FROM that process (its "Open Viewer" button, its context menu, or a
    // double-click), so the question is answered by the act of asking, and only
    // the viewers a given run actually produced are ever offered.
    //
    // Its five slots went with it: onProcessResultRequested() dispatches on
    // which result files a directory actually holds, which is the same decision
    // made once instead of five times.

    // ----- Modules: MLIP + Alloys tool families (between Analysis and Help) -
    // "Modules" gathers the machine-learning-potential workflow and the alloy
    // toolchain (cluster expansion, SQS, short-range order) that were formerly
    // scattered across Build / Simulation / Analysis.
    QMenu* modulesMenu = menuBar()->addMenu(tr("&Modules"));

    QMenu* mlipMenu = modulesMenu->addMenu(tr("&MLIP"));
    mlipMenu->addAction(tr("&Trainer…"), this, &MainWindow::openMaceTrainer);
    mlipMenu->addAction(tr("&Dataset Manager…"),
                        this, &MainWindow::showDatasetManager);

    // 2D Materials: workflows whose physics is specific to a sheet in vacuum,
    // where the supercell's arbitrary vacuum thickness has to be divided back
    // out before a quantity means anything.
    QMenu* twoDimensionalMenu = modulesMenu->addMenu(tr("&2D Materials"));
    twoDimensionalMenu->addAction(tr("2D &Optics…"), this,
                                  &MainWindow::show2DOptics)
        ->setToolTip(tr("Absorbance A(ω), 2D conductivity σ₂D and sheet "
                        "polarizability α₂D from an inherited ground state"));
    // Directly after 2D Optics: both are response properties of a sheet
    // evaluated over its own two-dimensional Brillouin zone, and both inherit
    // a converged ground state rather than computing one.
    twoDimensionalMenu->addAction(tr("2D &Bands…"), this,
                                  &MainWindow::show2DBands)
        ->setToolTip(tr("Band structure of a sheet as surfaces "
                        "E_n(kx, ky) over the 2D Brillouin zone, rather than "
                        "along a k-path"));
    // Third of the baseline-inheriting trio, before the builder: like the two
    // above it, this reads a completed ground state rather than computing one.
    twoDimensionalMenu->addAction(tr("2D &Workfunction…"), this,
                                  &MainWindow::show2DWorkfunction)
        ->setToolTip(tr("Work function Φ = E_vac − E_F of both slab faces, "
                        "from the planar-averaged electrostatic potential of "
                        "an inherited ground state"));
    // Fourth baseline-inheriting entry, and the reason it is here rather than
    // beside the bulk Charged Defects: what differs is not a parameter but the
    // correction's functional form, because a charged sheet has no scalar
    // epsilon and its energy diverges with vacuum instead of converging.
    twoDimensionalMenu->addAction(tr("&Charged Defects in 2D Materials…"), this,
                                  &MainWindow::show2DChargedDefects)
        ->setToolTip(tr("Formation energies E_f(q, E_F) and transition levels "
                        "for a monolayer, with the Komsa-Pasquarello 2D "
                        "image-charge correction in place of bulk FNV"));
    twoDimensionalMenu->addAction(tr("&Graphene Oxide…"), this,
                                  &MainWindow::openGrapheneOxideBuilder)
        ->setToolTip(tr("Functionalized graphene: epoxides, hydroxyls, "
                        "carboxyls and carbonyls at target coverages"));

    QMenu* alloysMenu = modulesMenu->addMenu(tr("&Alloys"));
    alloysMenu->addAction(tr("Cluster &Expansion Builder…"),
                          this, &MainWindow::openClusterExpansion);
    alloysMenu->addAction(tr("Cluster Expansion &Calculation…"),
                          this, &MainWindow::clusterExpansionCalculation);
    alloysMenu->addAction(tr("Special &Quasirandom Structure (SQS)…"),
                          this, &MainWindow::openSqsBuilder);
    alloysMenu->addAction(tr("&Warren-Cowley Analysis…"),
                          this, &MainWindow::showWarrenCowley);

    // Parameters Convergence: sweeps that answer "is this setting tight
    // enough?" with a curve instead of folklore. The plane-wave cutoff is the
    // first; a k-point sweep is the natural next tenant.
    QMenu* convergenceMenu =
        modulesMenu->addMenu(tr("&Parameters Convergence"));
    convergenceMenu
        ->addAction(tr("Plane-wave &Cutoff Convergence…"), this,
                    &MainWindow::planeWaveCutoffConvergence)
        ->setToolTip(tr("Single points over a range of PW cutoffs; energy "
                        "per atom and maximum force plotted against the "
                        "cutoff, referenced to the highest one"));
    convergenceMenu
        ->addAction(tr("&K-points Convergence…"), this,
                    &MainWindow::kPointsConvergence)
        ->setToolTip(tr("Single points over a sequence of Monkhorst-Pack "
                        "meshes; energy per atom and maximum force plotted "
                        "against the mesh density, referenced to the densest "
                        "one"));

    // The "Workflow" menu and its single "Add Workflow…" action are gone: the
    // node canvas is now a permanent dock in the bottom row (see below) rather
    // than a modeless window a menu item conjures up. A whole top-level menu
    // for one action that opened one window was the most prominent thing in
    // the menu bar and the least used; as a dock the canvas is simply THERE,
    // beside the Results panel that reports on the jobs it dispatches, and
    // View → Orchestration toggles it like every other panel.

    // Help trails the menu bar: online resources first, About last (as is
    // conventional). New documentation/support links belong in kHelpLinks.
    QMenu* helpMenu = menuBar()->addMenu(tr("&Help"));
    struct HelpLink {
        const char* title;
        const char* url;
    };
    static constexpr HelpLink kHelpLinks[] = {
        {QT_TR_NOOP("&Documentation"),
         "https://github.com/seixas-research/calango#readme"},
        {QT_TR_NOOP("&GitHub Repository"),
         "https://github.com/seixas-research/calango"},
    };
    for (const auto& link : kHelpLinks) {
        const QUrl url = QUrl(QLatin1String(link.url));
        helpMenu->addAction(tr(link.title),
                            [url] { QDesktopServices::openUrl(url); });
    }
    helpMenu->addSeparator();
    helpMenu->addAction(tr("&About Calango"), this, &MainWindow::about);

    // ----- 12-zone grid workspace (4 columns × 3 rows) ---------------------
    //
    //   | 1 Logo & Name | 2-3   Viewport | 4  Representation  |
    //   | 5 Structure   | 6-7   Viewport | 8  (Representation)|
    //   | 9 Lighting    | 10 Job | 11 Remote | 12 Cell & Axes |
    //
    // The bottom dock area owns both corners, so the third row spans the
    // full window width with four zones side by side. The left column
    // (branding over structure) and the right column (Representation
    // spanning two rows) flank the central widget (tab bar + viewport +
    // timeline) that fills zones 2-3/6-7. Every zone stays resizable via
    // the dock splitters, and panels remain re-dockable/floatable.
    // Corner ownership decides how far the bottom row runs, and it is the only
    // thing that does — nothing in the splitDockWidget calls expresses it.
    // Both bottom corners belong to the side columns here, so the left and
    // right columns run the FULL height and the bottom row spans only the
    // space between them (Results | Remote Access).
    setCorner(Qt::BottomLeftCorner, Qt::LeftDockWidgetArea);
    setCorner(Qt::BottomRightCorner, Qt::RightDockWidgetArea);

    auto* brandingDock = new QDockWidget(tr("Calango"), this); // zone 1
    brandingDock->setObjectName(QStringLiteral("brandingDock"));
    brandingPanel_ = new BrandingPanel(brandingDock);
    brandingDock->setWidget(brandingPanel_);
    // No title bar: zone 1 shows only the centered logo. (The dock title
    // still names the View-menu toggle; an empty title widget removes
    // the header without disabling the dock.)
    brandingDock->setTitleBarWidget(new QWidget(brandingDock));
    addDockWidget(Qt::LeftDockWidgetArea, brandingDock);
    // Visible by default: the branding card heads the left column in the
    // reference layout. Stated explicitly rather than left implicit — a dock
    // added to an area is shown by default, but this one was deliberately
    // hidden for a while, so a reader needs to see the default was changed
    // back rather than infer it from a missing line. This is only the
    // *default*: restoreState() below reinstates whatever the user left
    // behind, and View → Calango toggles it.
    brandingDock->setVisible(true);

    // Left column, top → bottom: Structure, Volumetric Data, Additional
    // overlays, then Processes.
    // (Structure and its related Volumetric Data panel sit at the prominent top
    // of the left column, directly under the branding card; the transient
    // Processes monitor moves to the foot of the column.)
    auto* infoDock = new QDockWidget(tr("Structure"), this); // zone 5
    infoDock->setObjectName(QStringLiteral("structureDock"));
    infoWidget_ = new StructureInfoWidget(infoDock);
    infoDock->setWidget(infoWidget_);
    splitDockWidget(brandingDock, infoDock, Qt::Vertical);
    connect(infoWidget_, &StructureInfoWidget::editStructureRequested,
            this, &MainWindow::editStructure);
    connect(infoWidget_, &StructureInfoWidget::centerStructureRequested,
            this, &MainWindow::centerStructure);
    connect(infoWidget_, &StructureInfoWidget::addVacuumRequested,
            this, &MainWindow::addVacuumLayer);
    connect(infoWidget_, &StructureInfoWidget::wrapIntoCellRequested,
            this, &MainWindow::wrapStructureIntoCell);
    connect(infoWidget_, &StructureInfoWidget::supercellRequested,
            this, &MainWindow::openSupercellBuilder);

    // Zone 13 — "Volumetric Data": stacked directly below Zone 5 "Structure" in
    // the left column. It renders 3D scalar fields (cube/xsf/CHGCAR) as
    // isosurface / color-slice overlays on the main viewport.
    auto* volumetricDock = new QDockWidget(tr("Volumetric Data"), this); // zone 13
    volumetricDock_ = volumetricDock;
    volumetricDock->setObjectName(QStringLiteral("volumetricDock"));
    volumetricPanel_ = new VolumetricPanel(viewport_, volumetricDock);
    volumetricDock->setWidget(volumetricPanel_);
    splitDockWidget(infoDock, volumetricDock, Qt::Vertical);

    // "Additional overlays": everything drawn OVER the structure that is not
    // the structure — lattice planes, text annotations, geometric primitives.
    // It sits under Volumetric Data because the two are neighbours in kind:
    // both add something to the scene rather than describing the atoms, and a
    // lattice plane routinely slices a field loaded in the panel above.
    auto* overlayDock = new QDockWidget(tr("Additional Overlays"), this);
    overlayDock->setObjectName(QStringLiteral("overlayDock"));
    overlayPanel_ = new OverlayPanel(viewport_, overlayDock);
    overlayDock->setWidget(overlayPanel_);
    splitDockWidget(volumetricDock, overlayDock, Qt::Vertical);
    // Hidden by default. Annotating a figure with planes, labels and
    // primitives is a finishing step, not something every session does, and
    // the panel was taking a quarter of the left column from the Structure and
    // Volumetric Data panels that ARE read on sight. View → Additional
    // Overlays brings it back; restoreState() below still reinstates whatever
    // the user left visible.
    overlayDock->setVisible(false);

    // Compact Process Manager at the foot of the left column.
    auto* processDock = new QDockWidget(tr("Processes"), this);
    processDock->setObjectName(QStringLiteral("processDock"));
    processPanel_ = new ProcessManagerPanel(processDock);
    processDock->setWidget(processPanel_);
    splitDockWidget(overlayDock, processDock, Qt::Vertical);
    connect(processPanel_, &ProcessManagerPanel::loadResultRequested,
            this, &MainWindow::onProcessResultRequested);
    connect(processPanel_, &ProcessManagerPanel::viewScriptRequested,
            this, &MainWindow::onViewScriptRequested);
    connect(processPanel_, &ProcessManagerPanel::deleteRequested,
            this, &MainWindow::onDeleteProcessRequested);
    connect(processPanel_, &ProcessManagerPanel::contextMenuRequested,
            this, &MainWindow::onProcessContextMenu);

    auto* reprDock = new QDockWidget(tr("Representation"), this); // zones 4 & 8
    reprDock->setObjectName(QStringLiteral("representationDock"));
    representationPanel_ = new RepresentationPanel(viewport_, reprDock);
    reprDock->setWidget(representationPanel_);
    addDockWidget(Qt::RightDockWidgetArea, reprDock);
    connect(representationPanel_, &RepresentationPanel::bondEditorRequested,
            this, &MainWindow::showBondEditor);

    // Zone 9. Constructed here (its panel is referenced below) but placed at
    // the END of the bottom row — see the splitDockWidget chain further down.
    visualEffectsDock_ = new QDockWidget(tr("Visual Effects"), this);
    visualEffectsDock_->setObjectName(QStringLiteral("visualEffectsDock"));
    visualEffectsDock_->setWidget(new VisualEffectsPanel(viewport_, visualEffectsDock_));

    // Results now TRAILS the bottom row (Orchestration | Remote Access | Results),
    // so it is built here but placed further down, once the dock that leads
    // the row exists — see the splitDockWidget chain after Remote Access.
    jobDock_ = new QDockWidget(tr("Results"), this); // zone 10
    jobDock_->setObjectName(QStringLiteral("resultsDock"));
    auto* jobTabs = new QTabWidget(jobDock_);
    jobLogWidget_ = new JobLogWidget(jobTabs);

    // One MetricPlotWidget per job observable; the specs carry all
    // labeling/units so the tabs stay behaviorally identical.
    MetricPlotWidget::MetricSpec energySpec;
    energySpec.quantity = tr("Energy");
    energySpec.yAxisLabel = tr("Total Energy (eV)");
    energySpec.xAxisLabel = tr("MD/optimization step");
    energySpec.valueSymbol = QStringLiteral("E");
    energySpec.unit = tr("eV");
    energySpec.placeholder = tr("Energy vs. step will appear here during a run");
    energySpec.marker = QStringLiteral("CALANGO_ENERGY");
    energySpec.csvColumn = QStringLiteral("total_energy_eV");
    energySpec.exportBaseName = QStringLiteral("energy.csv");
    // The four metric curves are drawn on the standardized white plot canvas
    // (gui/PlotPalette.hpp), so they are the saturated matplotlib "tab10"
    // family rather than the pastels the old dark canvas needed: a tint that
    // reads as a bright line on near-black washes out to nearly nothing on
    // white, and these plots are exported and printed.
    energySpec.lineColor = QColor(0x1f, 0x77, 0xb4); // blue
    energySpec.decimals = 3;

    MetricPlotWidget::MetricSpec temperatureSpec;
    temperatureSpec.quantity = tr("Temperature");
    temperatureSpec.yAxisLabel = tr("Temperature (K)");
    temperatureSpec.xAxisLabel = tr("MD step");
    temperatureSpec.valueSymbol = QStringLiteral("T");
    temperatureSpec.unit = tr("K");
    temperatureSpec.placeholder =
        tr("Temperature vs. step will appear here during an MD run");
    temperatureSpec.marker = QStringLiteral("CALANGO_TEMP");
    temperatureSpec.csvColumn = QStringLiteral("temperature_K");
    temperatureSpec.csvTargetColumn = QStringLiteral("target_K");
    temperatureSpec.exportBaseName = QStringLiteral("temperature.csv");
    temperatureSpec.lineColor = QColor(0xd6, 0x27, 0x28); // red
    temperatureSpec.decimals = 1;
    temperatureSpec.exportDecimals = 2;
    temperatureSpec.flatPadding = 5.0;
    // Absolute temperature is read against 0 K: an auto-scaled axis turns a
    // well-behaved 299–301 K thermostat into apparent wild oscillation.
    temperatureSpec.yAxisFromZero = true;
    temperatureSpec.targetLabelFormat = tr("T = %1 K");

    MetricPlotWidget::MetricSpec forceSpec;
    forceSpec.quantity = tr("Force");
    forceSpec.yAxisLabel = tr("Max |F| (eV/Å)");
    forceSpec.xAxisLabel = tr("MD/optimization step");
    forceSpec.valueSymbol = tr("max |F|");
    forceSpec.unit = tr("eV/Å");
    forceSpec.placeholder = tr("Maximum atomic force vs. step will appear here "
                               "during an optimization or MD run");
    forceSpec.marker = QStringLiteral("CALANGO_FMAX");
    forceSpec.csvColumn = QStringLiteral("max_force_eV_per_A");
    forceSpec.exportBaseName = QStringLiteral("max_force.csv");
    forceSpec.lineColor = QColor(0x2c, 0xa0, 0x2c); // green
    forceSpec.decimals = 3;
    forceSpec.flatPadding = 0.05;
    // max|F| is non-negative and converges *to* zero — the distance from the
    // origin is the whole signal during a relaxation.
    forceSpec.yAxisFromZero = true;

    MetricPlotWidget::MetricSpec pressureSpec;
    pressureSpec.quantity = tr("Pressure");
    pressureSpec.yAxisLabel = tr("Pressure (GPa)");
    pressureSpec.xAxisLabel = tr("MD step");
    pressureSpec.valueSymbol = QStringLiteral("P");
    pressureSpec.unit = tr("GPa");
    pressureSpec.placeholder =
        tr("Pressure vs. step will appear here during a constant-pressure "
           "(NPT) MD run");
    pressureSpec.marker = QStringLiteral("CALANGO_PRESSURE");
    pressureSpec.csvColumn = QStringLiteral("pressure_GPa");
    pressureSpec.csvTargetColumn = QStringLiteral("target_GPa");
    pressureSpec.exportBaseName = QStringLiteral("pressure.csv");
    pressureSpec.lineColor = QColor(0x94, 0x67, 0xbd); // purple
    pressureSpec.decimals = 3;
    pressureSpec.flatPadding = 0.1;
    // Pressure is signed (a cell under tension reports negative P), so its
    // axis stays auto-scaled — only the barostat setpoint gets annotated.
    pressureSpec.targetLabelFormat = tr("P = %1 GPa");

    energyPlot_ = new MetricPlotWidget(energySpec, jobTabs);
    temperaturePlot_ = new MetricPlotWidget(temperatureSpec, jobTabs);
    forcePlot_ = new MetricPlotWidget(forceSpec, jobTabs);
    pressurePlot_ = new MetricPlotWidget(pressureSpec, jobTabs);
    jobTabs->addTab(jobLogWidget_, tr("Log"));

    // Plot tabs carry their own Export Data… action for external analysis.
    const auto plotPage = [jobTabs](MetricPlotWidget* plot) {
        auto* page = new QWidget(jobTabs);
        auto* layout = new QVBoxLayout(page);
        layout->setContentsMargins(0, 0, 0, 2);
        layout->setSpacing(2);
        layout->addWidget(plot, 1);
        auto* row = new QHBoxLayout;
        row->addStretch(1);
        auto* exportButton = new QPushButton(QObject::tr("Export Data…"), page);
        row->addWidget(exportButton);
        layout->addLayout(row);
        QObject::connect(exportButton, &QPushButton::clicked,
                         plot, &MetricPlotWidget::exportData);
        return page;
    };
    jobTabs->addTab(plotPage(energyPlot_), tr("Energy"));
    jobTabs->addTab(plotPage(temperaturePlot_), tr("Temperature"));
    jobTabs->addTab(plotPage(forcePlot_), tr("Force"));
    jobTabs->addTab(plotPage(pressurePlot_), tr("Pressure"));

    // Convex Hull and Effective Bands no longer live in the Results dock:
    // each opens in its own standalone window (ConvexHullWindow /
    // EffectiveBandsWindow) from onProcessResultRequested, so the analytics
    // get a resizable canvas instead of a cramped zone-10 tab.

    // Wrap the tab widget with a small top margin: without it the tab
    // titles (Log/Energy/...) render flush against the dock's title bar
    // and visually collide with it (zone-10 overflow).
    auto* jobContainer = new QWidget(jobDock_);
    auto* jobLayout = new QVBoxLayout(jobContainer);
    jobLayout->setContentsMargins(4, 8, 4, 4);
    jobLayout->setSpacing(4);
    // Process selector: each background run keeps its own metric history, so
    // switching processes here repopulates every tab with that run's data.
    auto* selectorRow = new QHBoxLayout;
    selectorRow->setContentsMargins(0, 0, 0, 0);
    selectorRow->addWidget(new QLabel(tr("Process:"), jobContainer));
    processSelector_ = new QComboBox(jobContainer);
    processSelector_->setToolTip(
        tr("Select a background run to view its logged metrics."));
    selectorRow->addWidget(processSelector_, 1);
    jobLayout->addLayout(selectorRow);
    connect(processSelector_, &QComboBox::currentIndexChanged, this,
            &MainWindow::onProcessSelected);
    jobLayout->addWidget(jobTabs);
    jobTabs->setDocumentMode(true); // flat tab bar, no frame to overlap
    jobDock_->setWidget(jobContainer);

    remoteDock_ = new QDockWidget(tr("Remote Access"), this); // zone 11
    remoteDock_->setObjectName(QStringLiteral("remoteDock"));
    remotePanel_ = new RemoteAccessPanel(
        QString::fromStdString(pybridge::PythonEngine::instance().executable()),
        remoteDock_);
    remoteDock_->setWidget(remotePanel_);

    // Zone 14 — "Orchestration": the node canvas, formerly a modeless window behind
    // a top-level menu. It leads the bottom row, so it is the dock that
    // establishes the row and the two job panels split off it.
    //
    // The row reads left to right in the order the work happens: you build a
    // pipeline (Orchestration), choose where it runs (Remote Access), and read what
    // came back (Results).
    orchestrationDock_ = new QDockWidget(tr("Orchestration"), this);
    orchestrationDock_->setObjectName(QStringLiteral("orchestrationDock"));
    orchestrationPanel_ = createOrchestrationPanel(orchestrationDock_);
    orchestrationDock_->setWidget(orchestrationPanel_);
    addDockWidget(Qt::BottomDockWidgetArea, orchestrationDock_);
    splitDockWidget(orchestrationDock_, remoteDock_, Qt::Horizontal);
    splitDockWidget(remoteDock_, jobDock_, Qt::Horizontal);
    // Remote Access is hidden by default: submitting to a cluster is a
    // deliberate act a minority of sessions perform, and the panel is a login
    // form that says nothing until it is used — unlike Orchestration and Results,
    // which are useful on sight. View → Remote Access brings it back, and
    // restoreState() below still reinstates whatever the user left visible.
    remoteDock_->setVisible(false);

    // Zone 12 — "Spatial References": the cell wireframe, the orientation
    // triad and the per-atom vector arrows.
    //
    // The name is what the three have in common rather than a list of them:
    // each answers "where/which way is this?" about the scene, and none of
    // them draws the structure. That is also what separates the dock from
    // Representation, which is entirely about how the atoms themselves look.
    auto* overlaysDock = new QDockWidget(tr("Spatial References"), this);
    // The object name deliberately keeps its old spelling. It is the key Qt
    // stores saved layouts against, not a label anyone sees, and renaming it
    // would orphan the dock in every layout saved so far — forcing a version
    // bump and a Reset Layout on every user, for a cosmetic change.
    overlaysDock->setObjectName(QStringLiteral("cellAxesVectorsDock"));
    auto* overlayTabs = new QTabWidget(overlaysDock);
    overlayTabs->setUsesScrollButtons(false);
    overlayTabs->setElideMode(Qt::ElideNone);
    overlayTabs->tabBar()->setExpanding(false);
    overlayTabs->addTab(new UnitCellPanel(viewport_, overlayTabs), tr("Unit cell"));
    overlayTabs->addTab(new AxesTriadPanel(viewport_, overlayTabs), tr("Axes triad"));
    overlayTabs->addTab(new VectorsPanel(viewport_, overlayTabs), tr("Vectors"));
    overlayTabs->setMinimumWidth(overlayTabs->tabBar()->sizeHint().width() + 24);
    overlaysDock->setWidget(overlayTabs);
    // The right column, top → bottom: Representation, Spatial References,
    // Visual Effects. All three configure how the scene is DRAWN, so they read
    // as one column; the bottom row is left to the two job panels.
    splitDockWidget(reprDock, overlaysDock, Qt::Vertical);
    splitDockWidget(overlaysDock, visualEffectsDock_, Qt::Vertical);

    connect(remotePanel_, &RemoteAccessPanel::resultsReady,
            this, &MainWindow::onRemoteResultsReady);

    // Default grid proportions: side columns kColumnWidth px wide with a
    // compact branding card; the full-width bottom row is ~250 px tall.
    //
    // The right-hand column must be one width top to bottom or the layout
    // stops reading as a grid: Representation sits above "Spatial
    // References", whose three tab headers set a hard minimum. Letting each take
    // its own minimum would step the column, so it is the widest of the two
    // and both docks are pinned to it.
    //
    // resizeDocks() alone is only a *hint* — Qt re-solves it against each
    // widget's size hint on the first show and whenever a dock is toggled — so
    // the minimum width is what actually holds the column.
    constexpr int kColumnWidth = 290;
    // The branding card is a thin strip heading the left column. BrandingPanel
    // scales the logo to fit, so this is nearly a free choice — but it has to
    // stay at or above the panel's own minimum height or resizeDocks is
    // overridden, and that minimum is what now accounts for the version
    // caption drawn under the logo. Read it back rather than repeating the
    // number here.
    const int kBrandingHeight = brandingPanel_->minimumHeight();
    // All three right-column docks now share one width — Visual Effects has
    // joined them, and its five tab headers set the widest floor of the three.
    const QVector<QDockWidget*> rightColumn{reprDock, overlaysDock,
                                            visualEffectsDock_};
    int rightColumnWidth = kColumnWidth;
    for (QDockWidget* dock : rightColumn)
        if (QWidget* panel = dock->widget())
            rightColumnWidth = qMax(rightColumnWidth, panel->minimumWidth());
    for (QDockWidget* dock : rightColumn)
        if (QWidget* panel = dock->widget())
            panel->setMinimumWidth(rightColumnWidth);

    // The left column needs a hard minimum for the same reason the right one
    // does: resizeDocks is only a hint, so without it the left column is the
    // one thing in the window with no floor and absorbs every squeeze — it
    // collapsed to 198 px once Remote Access gained a minimum width.
    for (QDockWidget* dock : {brandingDock, infoDock, volumetricDock, processDock})
        if (QWidget* panel = dock->widget())
            panel->setMinimumWidth(qMax(kColumnWidth, panel->minimumWidth()));
    resizeDocks({brandingDock, infoDock, volumetricDock, processDock},
                {kColumnWidth, kColumnWidth, kColumnWidth, kColumnWidth},
                Qt::Horizontal);
    resizeDocks({reprDock, overlaysDock, visualEffectsDock_},
                {rightColumnWidth, rightColumnWidth, rightColumnWidth},
                Qt::Horizontal);
    // Right column heights, now three docks over the full window height:
    // Representation carries the most controls, Visual Effects the fewest
    // (five tabs of a handful of rows each).
    resizeDocks({reprDock, overlaysDock, visualEffectsDock_},
                {320, 280, 260}, Qt::Vertical);

    // Left column heights (top → bottom: Structure, Volumetric Data,
    // Processes): keep the compact Structure summary small (its ~7 property
    // rows fit comfortably) and hand the freed space to the Volumetric Data and
    // Processes panels below it.
    resizeDocks({brandingDock, infoDock, volumetricDock, processDock},
                {kBrandingHeight, 220, 300, 300}, Qt::Vertical);
    resizeDocks({orchestrationDock_, jobDock_, remoteDock_}, {250, 250, 250},
                Qt::Vertical);
    // The bottom row is Orchestration | Remote Access | Results. Remote Access is
    // held to the narrowest width that still shows its whole form — its own
    // minimum size hint, asked of the panel rather than guessed at — and the
    // other two split what is left.
    const int remoteWidth = remotePanel_->minimumSizeHint().width();
    remoteDock_->setMinimumWidth(remoteWidth);
    // Orchestration and Results are given plain preferred widths rather than
    // enormous ones: resizeDocks normalizes the numbers it is handed, and an
    // extreme ratio makes the solver claw the difference out of the LEFT
    // column, which is not part of this call at all. Orchestration gets the wider
    // share of the two — it is a canvas, and a canvas narrower than a couple
    // of nodes cannot show a pipeline.
    resizeDocks({orchestrationDock_, remoteDock_, jobDock_},
                {560, remoteWidth, 460}, Qt::Horizontal);

    // Dock titles at 1.2× the theme default across all zones (the earlier
    // 1.5× reduced by 0.8×). The font is set on the QDockWidget (whose
    // title bar renders with it) and reset on each content widget, since
    // fonts would otherwise propagate down.
    const QFont contentFont = QApplication::font();
    QFont dockTitleFont = contentFont;
    dockTitleFont.setPointSizeF(contentFont.pointSizeF() * 1.2);
    for (QDockWidget* dock : findChildren<QDockWidget*>()) {
        dock->setFont(dockTitleFont);
        if (dock->widget())
            dock->widget()->setFont(contentFont);
    }

    // Every dock is listed here, in the reading order of the layout: left
    // column, right column, then the bottom row. A dock missing from this list
    // is one the user cannot get back after closing it — which is what had
    // happened to Additional Overlays.
    viewMenu->addSeparator();
    viewMenu->addAction(brandingDock->toggleViewAction());
    viewMenu->addAction(infoDock->toggleViewAction());
    viewMenu->addAction(volumetricDock->toggleViewAction());
    viewMenu->addAction(overlayDock->toggleViewAction());
    viewMenu->addAction(processDock->toggleViewAction());
    viewMenu->addAction(reprDock->toggleViewAction());
    viewMenu->addAction(visualEffectsDock_->toggleViewAction());
    viewMenu->addAction(overlaysDock->toggleViewAction());
    viewMenu->addAction(orchestrationDock_->toggleViewAction());
    viewMenu->addAction(remoteDock_->toggleViewAction());
    viewMenu->addAction(jobDock_->toggleViewAction());

    // Bottom system status bar: Calango's own CPU / GPU / memory / threads,
    // plus the running job's. The runner is bound so the job group can sample
    // the subprocess tree — without it the widget simply shows Calango alone.
    systemStatusBar_ = new SystemStatusBar(this);
    systemStatusBar_->setJobRunner(jobRunner_);
    statusBar()->addPermanentWidget(systemStatusBar_);
    viewMenu->addSeparator();
    auto* statusBarAction = viewMenu->addAction(tr("&Status Bar"));
    statusBarAction->setCheckable(true);
    statusBarAction->setChecked(true);
    connect(statusBarAction, &QAction::toggled, statusBar(),
            &QWidget::setVisible);

    // Snapshot the default arrangement BEFORE any saved state is applied —
    // this is the only moment it exists. Rebuilding it later would mean
    // re-running the whole dock construction, so it is captured instead. Any
    // future change to the dock set must keep this after the last resizeDocks.
    defaultLayoutState_ = saveState(kLayoutVersion);

    viewMenu->addSeparator();
    viewMenu->addAction(tr("Reset &Layout"), this, &MainWindow::resetLayout);

    // Reapply the layout the user left behind last session. The version
    // tag rejects layouts saved before the 8-zone grid existed, so the
    // new default appears once and user rearrangements persist after.
    const QSettings settings;
    restoreGeometry(settings.value(QStringLiteral("window/geometry")).toByteArray());
    restoreState(settings.value(QStringLiteral("window/state")).toByteArray(),
                 kLayoutVersion);
}

void MainWindow::closeEvent(QCloseEvent* event)
{
    // File -> Quit funnels through closeAllWindows, so this guard covers
    // both the menu action and the window close button.
    if (isDirty_) {
        const auto choice = QMessageBox::warning(
            this, tr("Unsaved Changes"),
            tr("The project has unsaved changes.\n"
               "Do you want to save them before quitting?"),
            QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel,
            QMessageBox::Save);
        if (choice == QMessageBox::Cancel
            || (choice == QMessageBox::Save && !saveProject())) {
            event->ignore(); // stay open (save failed or was cancelled too)
            return;
        }
    }

    QSettings settings;
    settings.setValue(QStringLiteral("window/geometry"), saveGeometry());
    settings.setValue(QStringLiteral("window/state"), saveState(kLayoutVersion));
    QMainWindow::closeEvent(event);
}

bool MainWindow::ensureAseAvailable()
{
    const auto& python = pybridge::PythonEngine::instance();
    if (python.aseAvailable())
        return true;
    QMessageBox::warning(
        this, tr("ASE Not Available"),
        tr("The embedded Python interpreter cannot import ASE.\n\n"
           "Point Calango at an interpreter that has ASE, e.g.:\n"
           "    export CALANGO_PYTHON=/path/to/.venv/bin/python\n"
           "then restart. Diagnose with:  calango --probe-python\n\n"
           "Details:\n%1")
            .arg(QString::fromStdString(python.lastError())));
    return false;
}

// ---------------------------------------------------------------------------
// Document / tab management
// ---------------------------------------------------------------------------

MainWindow::Document* MainWindow::currentDocument()
{
    const int index = tabBar_->currentIndex();
    if (index < 0 || index >= static_cast<int>(documents_.size()))
        return nullptr;
    return documents_[static_cast<std::size_t>(index)].get();
}

MainWindow::Document& MainWindow::ensureDocument()
{
    if (Document* doc = currentDocument())
        return *doc;
    const int index =
        addDocument(std::make_shared<core::Structure>(), tr("Untitled"));
    return *documents_[static_cast<std::size_t>(index)];
}

int MainWindow::addDocument(std::shared_ptr<core::Structure> structure,
                            const QString& name,
                            std::vector<std::shared_ptr<core::Structure>> frames,
                            const QString& task)
{
    auto document = std::make_unique<Document>();
    document->id = nextWorkspaceId_++;
    document->structure = std::move(structure);
    document->frames = std::move(frames);
    document->fileName = name;
    document->task = task;
    documents_.push_back(std::move(document));

    // The tab text is derived by refreshTabTitles(); the placeholder is only
    // shown for the instant before it runs.
    const int index = tabBar_->addTab(name);
    refreshTabTitles();
    tabBar_->setCurrentIndex(index); // triggers onTabChanged -> sync
    if (tabBar_->currentIndex() == index)
        syncViewsToCurrent(true); // first tab: currentChanged may not fire
    return index;
}

int MainWindow::addTrajectoryDocument(
    std::vector<std::shared_ptr<core::Structure>> frames, const QString& name,
    const QString& task)
{
    if (frames.empty())
        return -1;
    // The one place `front()` and the move of the vector meet, and they are
    // sequenced here rather than left to a call's unspecified argument order.
    auto first = frames.front();
    return addDocument(std::move(first), name, std::move(frames), task);
}

void MainWindow::refreshTabTitles()
{
    for (std::size_t i = 0; i < documents_.size(); ++i) {
        const Document* doc = documents_[i].get();
        // Field 1: zero-padded two-digit sequence number (01, 02, …).
        QStringList fields{
            QStringLiteral("%1").arg(static_cast<int>(i) + 1, 2, 10,
                                     QLatin1Char('0'))};
        // Field 2: live chemical formula, falling back to the file/source name.
        QString formula;
        if (doc->structure && !doc->structure->empty())
            formula = QString::fromStdString(doc->structure->chemicalFormula());
        fields << (formula.isEmpty()
                       ? (doc->fileName.isEmpty() ? tr("Untitled") : doc->fileName)
                       : formula);
        // Field 3: process / task name, when the document has one.
        if (!doc->task.isEmpty())
            fields << doc->task;
        tabBar_->setTabText(static_cast<int>(i),
                            fields.join(QStringLiteral(" - ")));
    }
}

void MainWindow::onTabMoved(int from, int to)
{
    if (from < 0 || to < 0 || from >= static_cast<int>(documents_.size())
        || to >= static_cast<int>(documents_.size()) || from == to)
        return;
    // Mirror the drag in documents_ so index-based lookups stay aligned with
    // the tab bar, then re-number the sequence field.
    auto moved = std::move(documents_[static_cast<std::size_t>(from)]);
    documents_.erase(documents_.begin() + from);
    documents_.insert(documents_.begin() + to, std::move(moved));
    refreshTabTitles();
}

void MainWindow::onTabChanged(int index)
{
    if (index < 0 || index >= static_cast<int>(documents_.size())) {
        viewport_->setStructure(nullptr);
        infoWidget_->updateFromStructure(nullptr);
        // No workspace on screen — no volumetric record may render.
        if (volumetricPanel_)
            volumetricPanel_->setActiveWorkspace(-1);
        timeline_->stop();
        timeline_->hide();
        setWindowTitle(QStringLiteral("Calango"));
        updateUndoActions();
        return;
    }
    syncViewsToCurrent(true);
}

void MainWindow::onTabCloseRequested(int index)
{
    if (index < 0 || index >= static_cast<int>(documents_.size()))
        return;
    Document* closing = documents_[static_cast<std::size_t>(index)].get();
    if (closing == liveDoc_)
        liveDoc_ = nullptr; // stream continues, frames just aren't shown
    // Same for an orchestration node's live tab: the entry must go with the
    // document, or the next streamed frame appends to freed memory.
    std::erase_if(orchestrationLiveDocs_,
                  [closing](const auto& entry) { return entry.second == closing; });
    documents_.erase(documents_.begin() + index);
    tabBar_->removeTab(index); // currentChanged fires and re-syncs
    refreshTabTitles();        // re-number the remaining tabs (01, 02, …)
    isDirty_ = true;
}

void MainWindow::duplicateOrExtractFrame()
{
    Document* doc = currentDocument();
    if (!doc || !doc->structure || doc->structure->empty()) {
        QMessageBox::information(
            this, tr("Duplicate Workspace"),
            tr("Open a structure or trajectory in the active tab first."));
        return;
    }
    // Snapshot the geometry currently on screen. showFrame() keeps
    // doc->structure pointed at the displayed trajectory frame, so this one
    // clone covers both cases: a static structure is duplicated wholesale, and
    // a trajectory yields exactly its current frame as an independent static
    // structure. Either way the source tab keeps its own structure and (for a
    // trajectory) its full timeline — the extraction is non-destructive.
    const bool isTrajectory = doc->frames.size() > 1;
    auto snapshot = std::make_shared<core::Structure>(*doc->structure);
    const QString baseName =
        doc->fileName.isEmpty() ? tr("Untitled") : doc->fileName;
    const QString newName =
        isTrajectory
            ? tr("%1 (frame %2)").arg(baseName).arg(timeline_->currentFrame() + 1)
            : tr("%1 (copy)").arg(baseName);

    // A new tab with no frames: an independent, static workspace.
    addDocument(std::move(snapshot), newName);
    isDirty_ = true;
    statusBar()->showMessage(
        isTrajectory ? tr("Extracted frame into a new tab: %1").arg(newName)
                     : tr("Duplicated workspace into a new tab: %1").arg(newName));
}

void MainWindow::showTabContextMenu(const QPoint& pos)
{
    const int index = tabBar_->tabAt(pos);
    if (index < 0)
        return;
    // Act on the right-clicked tab, not merely the active one.
    tabBar_->setCurrentIndex(index);

    QMenu menu(this);
    menu.addAction(ui::IconManager::icon(QStringLiteral("file-copy-line")),
                   tr("Duplicate Workspace / Extract Frame to New Tab"), this,
                   &MainWindow::duplicateOrExtractFrame);
    menu.addSeparator();
    menu.addAction(tr("Close Tab"), this,
                   [this, index] { onTabCloseRequested(index); });
    menu.exec(tabBar_->mapToGlobal(pos));
}

void MainWindow::syncViewsToCurrent(bool frameCamera)
{
    Document* doc = currentDocument();
    if (!doc)
        return;
    // Read the stored view BEFORE setStructure: framing the camera emits
    // cameraChanged, which would otherwise overwrite it with the auto-framed
    // one before it could be restored.
    const render::PointOfView stored = doc->pointOfView;
    // Before setStructure: the colour mapping is recomputed inside it and the
    // Custom Gradient dialog re-reads its auto-scale from the trajectory, so
    // the viewport has to already know which trajectory this tab is.
    pushTrajectoryToViewport(doc);
    viewport_->setStructure(doc->structure, frameCamera);
    if (stored.valid) {
        restoringPointOfView_ = true;
        viewport_->setPointOfView(stored);
        restoringPointOfView_ = false;
        doc->pointOfView = stored;
    }
    infoWidget_->updateFromStructure(doc->structure.get());
    // Volumetric overlays are bound to the workspace that produced them: this
    // hides the previous tab's fields and draws this tab's. Cheap no-op when
    // the workspace is unchanged (a plain structure refresh).
    if (volumetricPanel_)
        volumetricPanel_->setActiveWorkspace(doc->id);
    refreshTabTitles(); // formula may have changed since the last sync
    setWindowTitle(doc->fileName.isEmpty()
                       ? QStringLiteral("Calango")
                       : QStringLiteral("Calango — %1").arg(doc->fileName));
    if (doc->frames.size() > 1
        && !(filmModeAction_ && filmModeAction_->isChecked())) {
        timeline_->setFrameCount(static_cast<int>(doc->frames.size()));
        timeline_->show();
    } else {
        timeline_->stop();
        timeline_->hide();
    }
    // The film is per tab, so a switch re-points the film timeline (and the
    // dialog, if open) at the incoming tab's film and trajectory.
    refreshFilmTimeline();
    updateUndoActions();
}

void MainWindow::notifyStructureChanged(bool frameCamera)
{
    Document* doc = currentDocument();
    if (!doc)
        return;
    // Read the stored view BEFORE setStructure: framing the camera emits
    // cameraChanged, which would otherwise overwrite it with the auto-framed
    // one before it could be restored.
    const render::PointOfView stored = doc->pointOfView;
    // An edit that replaces a frame (wrap, centering, a streamed arrival)
    // changes what the trajectory-wide colour scale spans.
    pushTrajectoryToViewport(doc);
    viewport_->setStructure(doc->structure, frameCamera);
    if (stored.valid) {
        restoringPointOfView_ = true;
        viewport_->setPointOfView(stored);
        restoringPointOfView_ = false;
        doc->pointOfView = stored;
    }
    infoWidget_->updateFromStructure(doc->structure.get());
}

MainWindow::Snapshot MainWindow::snapshotOf(const Document& doc)
{
    Snapshot snapshot;
    // The displayed structure is deep-copied because some edits mutate it in
    // place; the other frames are only ever replaced, so a pointer suffices.
    snapshot.structure = doc.structure
        ? std::make_shared<core::Structure>(*doc.structure)
        : nullptr;
    snapshot.frames.reserve(doc.frames.size());
    for (const auto& frame : doc.frames) {
        // The entry standing for the frame on screen has to point at the deep
        // copy, or restoring would put the trajectory back while leaving that
        // one frame aliased to the live (already edited) object.
        snapshot.frames.push_back(frame && frame == doc.structure
                                      ? snapshot.structure
                                      : frame);
    }
    return snapshot;
}

void MainWindow::restore(Document& doc, Snapshot snapshot)
{
    doc.structure = std::move(snapshot.structure);
    doc.frames = std::move(snapshot.frames);
}

void MainWindow::pushUndo()
{
    Document* doc = currentDocument();
    if (!doc)
        return;
    // Every undoable mutation funnels through here — the natural single
    // point to flag the workspace as having unsaved changes.
    isDirty_ = true;
    doc->undoStack.push_back(snapshotOf(*doc));
    if (doc->undoStack.size() > kMaxUndoDepth)
        doc->undoStack.pop_front();
    doc->redoStack.clear();
    updateUndoActions();
}

void MainWindow::updateUndoActions()
{
    Document* doc = currentDocument();
    undoAction_->setEnabled(doc && !doc->undoStack.empty());
    redoAction_->setEnabled(doc && !doc->redoStack.empty());
}

void MainWindow::undo()
{
    Document* doc = currentDocument();
    if (!doc || doc->undoStack.empty())
        return;
    doc->redoStack.push_back(snapshotOf(*doc));
    restore(*doc, std::move(doc->undoStack.back()));
    doc->undoStack.pop_back();
    updateUndoActions();
    notifyStructureChanged(false);
    statusBar()->showMessage(tr("Undo"), 2000);
}

void MainWindow::redo()
{
    Document* doc = currentDocument();
    if (!doc || doc->redoStack.empty())
        return;
    doc->undoStack.push_back(snapshotOf(*doc));
    restore(*doc, std::move(doc->redoStack.back()));
    doc->redoStack.pop_back();
    updateUndoActions();
    notifyStructureChanged(false);
    statusBar()->showMessage(tr("Redo"), 2000);
}

// ---------------------------------------------------------------------------
// File I/O
// ---------------------------------------------------------------------------

namespace {

/// Explicit ASE format hints for extensions ase.io cannot infer reliably.
QString formatHintFor(const QString& path)
{
    const QString suffix = QFileInfo(path).suffix().toLower();
    if (suffix == QLatin1String("data"))
        return QStringLiteral("lammps-data");
    if (suffix == QLatin1String("dump") || suffix == QLatin1String("lammpstrj"))
        return QStringLiteral("lammps-dump-text");
    if (suffix == QLatin1String("pwi") || suffix == QLatin1String("in"))
        return QStringLiteral("espresso-in");
    if (suffix == QLatin1String("pwo"))
        return QStringLiteral("espresso-out");
    if (suffix == QLatin1String("gjf") || suffix == QLatin1String("com"))
        return QStringLiteral("gaussian-in");
    if (suffix == QLatin1String("cell"))
        return QStringLiteral("castep-cell");
    if (suffix == QLatin1String("res"))
        return QStringLiteral("res");
    return {}; // .out and others: let ASE sniff the contents
}

} // namespace

namespace {
const auto kRecentFilesKey = QStringLiteral("recent/files");
constexpr int kMaxRecentFiles = 10;
} // namespace

void MainWindow::addRecentFile(const QString& path)
{
    const QString absolute = QFileInfo(path).absoluteFilePath();
    QSettings settings;
    QStringList recent = settings.value(kRecentFilesKey).toStringList();
    recent.removeAll(absolute); // de-duplicate; the newest goes to the front
    recent.prepend(absolute);
    while (recent.size() > kMaxRecentFiles)
        recent.removeLast();
    settings.setValue(kRecentFilesKey, recent);
    updateRecentFilesMenu();
}

void MainWindow::updateRecentFilesMenu()
{
    if (!recentMenu_)
        return;
    recentMenu_->clear();

    QStringList recent = QSettings().value(kRecentFilesKey).toStringList();
    // Drop entries whose files no longer exist so the list stays trustworthy.
    recent.erase(std::remove_if(recent.begin(), recent.end(),
                                [](const QString& p) { return !QFileInfo::exists(p); }),
                 recent.end());

    if (recent.isEmpty()) {
        QAction* empty = recentMenu_->addAction(tr("(no recent files)"));
        empty->setEnabled(false);
        return;
    }

    int index = 1;
    for (const QString& path : recent) {
        // "&1 name" gives Alt-number mnemonics for the first nine entries.
        const QString label = index <= 9
            ? tr("&%1  %2").arg(index).arg(QFileInfo(path).fileName())
            : QFileInfo(path).fileName();
        QAction* action = recentMenu_->addAction(label);
        action->setData(path);
        action->setToolTip(path);
        connect(action, &QAction::triggered, this,
                [this, path] { loadFile(path); });
        ++index;
    }
    recentMenu_->addSeparator();
    recentMenu_->addAction(tr("&Clear Recent Files"), this, [this] {
        QSettings().remove(kRecentFilesKey);
        updateRecentFilesMenu();
    });
}

void MainWindow::loadFile(const QString& path)
{
    // Project workspaces (double-click / "Open with" via the installer's
    // MIME association, or a CLI argument) restore the whole session
    // instead of loading a structure through ASE.
    if (path.endsWith(QStringLiteral(".calproj"), Qt::CaseInsensitive)) {
        if (readProject(path)) {
            projectPath_ = path;
            addRecentFile(path);
        }
        return;
    }
    if (!ensureAseAvailable())
        return;
    try {
        // Always read every frame: multi-frame files (trajectories,
        // animated XYZ) get a document with frames, which automatically
        // reveals and activates the timeline panel.
        const auto rawFrames = pybridge::AseBridge::readTrajectory(
            path.toStdString(), formatHintFor(path).toStdString());
        if (rawFrames.empty())
            throw std::runtime_error("File contains no structures");

        if (rawFrames.size() == 1) {
            auto structure = std::make_shared<core::Structure>(rawFrames.front());
            const auto atomCount = structure->size();
            addDocument(std::move(structure), QFileInfo(path).fileName());
            statusBar()->showMessage(tr("Loaded %1 (%2 atoms)").arg(path).arg(atomCount));
            addRecentFile(path);
        } else {
            std::vector<std::shared_ptr<core::Structure>> frames;
            frames.reserve(rawFrames.size());
            for (const auto& frame : rawFrames)
                frames.push_back(std::make_shared<core::Structure>(frame));
            const auto frameCount = frames.size();
            addTrajectoryDocument(std::move(frames), QFileInfo(path).fileName());
            statusBar()->showMessage(
                tr("Loaded %1 (%2 frames)").arg(path).arg(frameCount));
            addRecentFile(path);
        }
    } catch (const std::exception& e) {
        QMessageBox::critical(this, tr("Open Structure"),
                              QString::fromUtf8(e.what()));
    }
}

void MainWindow::openStructure()
{
    // Filters come from GuiUtils so every dialog in the application offers the
    // same list and pre-selects the same default (Extended XYZ).
    const QStringList paths = QFileDialog::getOpenFileNames(
        this, tr("Open Structure(s)"), QString(), structureOpenFilters());
    for (const QString& path : paths)
        loadFile(path);
}

void MainWindow::openTrajectory()
{
    const QString path = QFileDialog::getOpenFileName(
        this, tr("Open Trajectory"), QString(), trajectoryOpenFilters());
    if (!path.isEmpty())
        loadFile(path); // multi-frame aware — activates the timeline
}

void MainWindow::showFrame(int index)
{
    Document* doc = currentDocument();
    if (!doc || index < 0 || index >= static_cast<int>(doc->frames.size()))
        return;
    doc->structure = doc->frames[static_cast<std::size_t>(index)];
    notifyStructureChanged(false);
}

void MainWindow::pushTrajectoryToViewport(const Document* doc)
{
    std::vector<std::shared_ptr<const core::Structure>> frames;
    if (doc) {
        frames.reserve(doc->frames.size());
        for (const auto& frame : doc->frames)
            frames.push_back(frame);
    }
    viewport_->setTrajectory(std::move(frames));
}

void MainWindow::showFinalFrame(const Document* doc)
{
    if (!doc || doc->frames.size() < 2)
        return;
    const int index = indexOfDocument(doc);
    if (index < 0 || tabBar_->currentIndex() != index)
        return; // another tab is forward; switching to this one re-syncs it
    const int last = static_cast<int>(doc->frames.size()) - 1;
    timeline_->extendFrameCount(static_cast<int>(doc->frames.size()));
    timeline_->show();
    // setCurrentFrame() drives the slider, and a slider already sitting on
    // `last` emits nothing — which is exactly the case after a run the user
    // never scrubbed. Call showFrame() directly there.
    if (timeline_->currentFrame() == last)
        showFrame(last);
    else
        timeline_->setCurrentFrame(last);
}

void MainWindow::saveStructureAs()
{
    Document* doc = currentDocument();
    if (!doc || !doc->structure || !ensureAseAvailable())
        return;
    // Filter -> explicit ASE format, shared with every other save dialog in
    // the application; Extended XYZ is first and therefore pre-selected.
    const auto& saveFormats = structureSaveFormats();
    QStringList filters;
    for (const auto& entry : saveFormats)
        filters << entry.first;

    QString selectedFilter;
    // Suggest the document's own name with the default suffix, so the common
    // case — open a CIF, edit, save — writes "quartz.extxyz" rather than
    // leaving the name blank and the format to chance.
    QString path = QFileDialog::getSaveFileName(
        this, tr("Save Structure As"), defaultStructureFileName(doc->fileName),
        filters.join(QStringLiteral(";;")), &selectedFilter);
    if (path.isEmpty())
        return;
    // A typed name with no extension gets the selected filter's own, so the
    // file can be recognized again later.
    path = withFilterSuffix(path, selectedFilter);
    const QString format = formatForFilter(saveFormats, selectedFilter);
    try {
        // PDBx is written natively — ASE has no writer for it, and this is the
        // one format here that carries the residue/chain annotation.
        if (format == QStringLiteral("pdbx"))
            core::PdbxFile::write(*doc->structure, path.toStdString());
        else
            pybridge::AseBridge::writeStructure(*doc->structure,
                                                path.toStdString(),
                                                format.toStdString());
        statusBar()->showMessage(tr("Saved %1").arg(path));
    } catch (const std::exception& e) {
        QMessageBox::critical(this, tr("Save Structure"), QString::fromUtf8(e.what()));
    }
}

void MainWindow::saveTrajectoryAs()
{
    Document* doc = currentDocument();
    if (!doc || doc->frames.size() < 2) {
        QMessageBox::information(
            this, tr("Save Trajectory"),
            tr("The current tab has no multi-frame trajectory.\n"
               "Open a trajectory (or generate one) first."));
        return;
    }
    if (!ensureAseAvailable())
        return;

    const auto& trajectoryFormats = trajectorySaveFormats();
    QStringList filters;
    for (const auto& entry : trajectoryFormats)
        filters << entry.first;

    QString selectedFilter;
    QString path = QFileDialog::getSaveFileName(
        this, tr("Save Trajectory As"),
        defaultStructureFileName(QStringLiteral("trajectory")),
        filters.join(QStringLiteral(";;")), &selectedFilter);
    if (path.isEmpty())
        return;
    path = withFilterSuffix(path, selectedFilter);
    const QString format = formatForFilter(trajectoryFormats, selectedFilter);

    try {
        pybridge::AseBridge::writeTrajectory(doc->frames, path.toStdString(),
                                             format.toStdString());
        statusBar()->showMessage(
            tr("Saved %1 (%2 frames)").arg(path).arg(doc->frames.size()));
    } catch (const std::exception& e) {
        QMessageBox::critical(this, tr("Save Trajectory"), QString::fromUtf8(e.what()));
    }
}

// ---------------------------------------------------------------------------
// Project workspace persistence (.calproj)
// ---------------------------------------------------------------------------

namespace {

/// (step, value[, target]) series of one Job-panel metric tab.
QJsonObject metricToJson(const MetricPlotWidget* plot)
{
    QJsonObject metric;
    QJsonArray samples;
    for (const MetricPlotWidget::Sample& sample : plot->samples())
        samples.append(QJsonArray{sample.step, sample.value});
    metric[QStringLiteral("samples")] = samples;
    if (plot->hasTarget())
        metric[QStringLiteral("target")] = plot->targetValue();
    return metric;
}

void metricFromJson(const QJsonObject& metric, MetricPlotWidget* plot)
{
    plot->clear();
    std::vector<MetricPlotWidget::Sample> samples;
    const QJsonArray array = metric[QStringLiteral("samples")].toArray();
    samples.reserve(static_cast<std::size_t>(array.size()));
    for (const auto& entry : array) {
        const QJsonArray pair = entry.toArray();
        if (pair.size() == 2)
            samples.push_back({pair[0].toInt(), pair[1].toDouble()});
    }
    plot->setSamples(std::move(samples));
    if (metric.contains(QStringLiteral("target")))
        plot->setTarget(metric[QStringLiteral("target")].toDouble());
}

} // namespace

bool MainWindow::writeProject(const QString& path)
{
    QJsonObject root;
    root[QStringLiteral("application")] = QStringLiteral("calango");
    root[QStringLiteral("fileType")] = QStringLiteral("project");
    root[QStringLiteral("formatVersion")] = ProjectSerializer::kFormatVersion;
    root[QStringLiteral("calangoVersion")] = QStringLiteral(CALANGO_VERSION);

    QJsonArray docs;
    for (const auto& document : documents_) {
        QJsonObject docJson;
        docJson[QStringLiteral("fileName")] = document->fileName;
        if (!document->frames.empty()) {
            // Trajectory tab: persist every frame; the displayed structure
            // is one of them, recorded by index instead of duplicated.
            QJsonArray frames;
            int currentFrame = -1;
            for (std::size_t i = 0; i < document->frames.size(); ++i) {
                if (document->frames[i] == document->structure)
                    currentFrame = static_cast<int>(i);
                frames.append(
                    ProjectSerializer::structureToJson(*document->frames[i]));
            }
            docJson[QStringLiteral("frames")] = frames;
            docJson[QStringLiteral("currentFrame")] = currentFrame;
            if (currentFrame < 0 && document->structure)
                docJson[QStringLiteral("structure")]
                    = ProjectSerializer::structureToJson(*document->structure);
        } else if (document->structure) {
            docJson[QStringLiteral("structure")]
                = ProjectSerializer::structureToJson(*document->structure);
        }
        docs.append(docJson);
    }
    root[QStringLiteral("documents")] = docs;
    root[QStringLiteral("activeTab")] = tabBar_->currentIndex();

    QJsonObject viewportJson;
    viewportJson[QStringLiteral("colorMode")]
        = static_cast<int>(viewport_->colorMode());
    viewportJson[QStringLiteral("gradient")]
        = static_cast<int>(viewport_->style().gradient);
    viewportJson[QStringLiteral("customField")] = viewport_->customScalarField();
    viewportJson[QStringLiteral("background")]
        = viewport_->backgroundColor().name();
    root[QStringLiteral("viewport")] = viewportJson;

    QJsonObject job;
    job[QStringLiteral("lastJobDir")] = lastJobDir_;
    job[QStringLiteral("log")] = jobLogWidget_->logText();
    job[QStringLiteral("energy")] = metricToJson(energyPlot_);
    job[QStringLiteral("temperature")] = metricToJson(temperaturePlot_);
    job[QStringLiteral("maxForce")] = metricToJson(forcePlot_);
    job[QStringLiteral("pressure")] = metricToJson(pressurePlot_);
    root[QStringLiteral("job")] = job;

    // QSaveFile: the previous project survives a failed / interrupted save.
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        QMessageBox::critical(this, tr("Save Project"),
                              tr("Could not write %1:\n%2")
                                  .arg(path, file.errorString()));
        return false;
    }
    file.write(QJsonDocument(root).toJson(QJsonDocument::Compact));
    if (!file.commit()) {
        QMessageBox::critical(this, tr("Save Project"),
                              tr("Could not write %1:\n%2")
                                  .arg(path, file.errorString()));
        return false;
    }
    statusBar()->showMessage(tr("Project saved to %1 (%2 tab(s))")
                                 .arg(path)
                                 .arg(documents_.size()));
    return true;
}

bool MainWindow::readProject(const QString& path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        QMessageBox::critical(this, tr("Open Project"),
                              tr("Could not read %1:\n%2")
                                  .arg(path, file.errorString()));
        return false;
    }
    QJsonParseError parseError;
    const QJsonDocument json = QJsonDocument::fromJson(file.readAll(), &parseError);
    const QJsonObject root = json.object();
    if (parseError.error != QJsonParseError::NoError
        || root[QStringLiteral("application")].toString()
            != QLatin1String("calango")
        || root[QStringLiteral("fileType")].toString() != QLatin1String("project")) {
        QMessageBox::critical(this, tr("Open Project"),
                              tr("%1 is not a Calango project file.").arg(path));
        return false;
    }
    if (root[QStringLiteral("formatVersion")].toInt()
        > ProjectSerializer::kFormatVersion) {
        QMessageBox::critical(
            this, tr("Open Project"),
            tr("%1 was saved by a newer Calango (project format %2, this build "
               "reads up to %3). Update Calango to open it.")
                .arg(path)
                .arg(root[QStringLiteral("formatVersion")].toInt())
                .arg(ProjectSerializer::kFormatVersion));
        return false;
    }

    closeAllDocuments();

    for (const auto& entry : root[QStringLiteral("documents")].toArray()) {
        const QJsonObject docJson = entry.toObject();
        std::vector<std::shared_ptr<core::Structure>> frames;
        for (const auto& frameJson : docJson[QStringLiteral("frames")].toArray())
            frames.push_back(
                ProjectSerializer::structureFromJson(frameJson.toObject()));

        const int currentFrame = docJson[QStringLiteral("currentFrame")].toInt(-1);
        std::shared_ptr<core::Structure> structure;
        if (currentFrame >= 0 && currentFrame < static_cast<int>(frames.size()))
            structure = frames[static_cast<std::size_t>(currentFrame)];
        else if (docJson.contains(QStringLiteral("structure")))
            structure = ProjectSerializer::structureFromJson(
                docJson[QStringLiteral("structure")].toObject());
        else if (!frames.empty())
            structure = frames.front();
        else
            structure = std::make_shared<core::Structure>();

        addDocument(std::move(structure),
                    docJson[QStringLiteral("fileName")].toString(tr("Untitled")),
                    std::move(frames));
    }

    const int activeTab = root[QStringLiteral("activeTab")].toInt(0);
    if (activeTab >= 0 && activeTab < tabBar_->count())
        tabBar_->setCurrentIndex(activeTab);

    // Color mapping / viewport state — restore before the panels sync.
    const QJsonObject viewportJson = root[QStringLiteral("viewport")].toObject();
    if (!viewportJson.isEmpty()) {
        if (const QColor background(
                viewportJson[QStringLiteral("background")].toString());
            background.isValid())
            viewport_->setBackgroundColor(background);
        const int gradient = viewportJson[QStringLiteral("gradient")].toInt(0);
        if (gradient >= 0 && gradient <= static_cast<int>(render::ColorGradient::Afmhot))
            viewport_->setColorGradient(static_cast<render::ColorGradient>(gradient));
        const int colorMode = viewportJson[QStringLiteral("colorMode")].toInt(0);
        // Bound by the LAST enumerator (Cast), not a hand-picked one: the
        // mode is saved by numeric value, and a guard frozen at an older tail
        // would silently drop every mode appended after it.
        if (colorMode >= 0
            && colorMode <= static_cast<int>(render::ColorMode::Cast))
            viewport_->setColorMode(
                static_cast<render::ColorMode>(colorMode),
                viewportJson[QStringLiteral("customField")].toString());
    }

    // Job console + metric series of the last (unexported) run.
    const QJsonObject job = root[QStringLiteral("job")].toObject();
    if (!job.isEmpty()) {
        lastJobDir_ = job[QStringLiteral("lastJobDir")].toString();
        jobLogWidget_->restoreLog(job[QStringLiteral("log")].toString());
        metricFromJson(job[QStringLiteral("energy")].toObject(), energyPlot_);
        metricFromJson(job[QStringLiteral("temperature")].toObject(),
                       temperaturePlot_);
        metricFromJson(job[QStringLiteral("maxForce")].toObject(), forcePlot_);
        metricFromJson(job[QStringLiteral("pressure")].toObject(), pressurePlot_);
    }

    statusBar()->showMessage(tr("Project %1 restored (%2 tab(s))")
                                 .arg(path)
                                 .arg(documents_.size()));
    isDirty_ = false; // freshly restored — matches the file on disk
    return true;
}

void MainWindow::closeAllDocuments()
{
    documents_.clear();
    while (tabBar_->count() > 0)
        tabBar_->removeTab(0); // currentChanged fires; views reset gracefully
}

void MainWindow::openProject()
{
    if (jobRunner_->isRunning()) {
        QMessageBox::information(this, tr("Open Project"),
                                 tr("A calculation is running — kill it before "
                                    "switching projects."));
        return;
    }
    if (!documents_.empty()
        && QMessageBox::question(
               this, tr("Open Project"),
               tr("Opening a project replaces the current workspace "
                  "(%n open tab(s)). Continue?",
                  nullptr, static_cast<int>(documents_.size())))
            != QMessageBox::Yes)
        return;

    const QString path = QFileDialog::getOpenFileName(
        this, tr("Open Project"), QString(),
        tr("Calango project (*.calproj);;All files (*)"));
    if (path.isEmpty())
        return;
    if (readProject(path))
        projectPath_ = path;
}

bool MainWindow::saveProject()
{
    if (projectPath_.isEmpty())
        return saveProjectAs();
    const bool ok = writeProject(projectPath_);
    if (ok)
        isDirty_ = false;
    return ok;
}

bool MainWindow::saveProjectAs()
{
    QString path = QFileDialog::getSaveFileName(
        this, tr("Save Project As"), QStringLiteral("workspace.calproj"),
        tr("Calango project (*.calproj)"));
    if (path.isEmpty())
        return false; // user cancelled
    if (!path.endsWith(QStringLiteral(".calproj"), Qt::CaseInsensitive))
        path += QStringLiteral(".calproj");
    if (!writeProject(path))
        return false;
    projectPath_ = path;
    isDirty_ = false;
    return true;
}

// ---------------------------------------------------------------------------
// Image & animation export
// ---------------------------------------------------------------------------

namespace {

/// Attaches a resolution-preset combo (720p / 1080p / 4K / Custom) to a
/// width/height spinbox pair: picking a preset writes the spins, editing
/// a spin manually flips the combo back to Custom.
QComboBox* makeResolutionPresetCombo(QDialog* dialog, QSpinBox* widthSpin,
                                     QSpinBox* heightSpin)
{
    struct Preset {
        const char* label;
        int width;
        int height;
    };
    static constexpr Preset kPresets[] = {
        {"720p (1280 × 720)", 1280, 720},
        {"1080p (1920 × 1080)", 1920, 1080},
        {"4K UHD (3840 × 2160)", 3840, 2160},
    };

    auto* combo = new QComboBox(dialog);
    for (const Preset& preset : kPresets)
        combo->addItem(QObject::tr(preset.label), QSize(preset.width, preset.height));
    combo->addItem(QObject::tr("Custom"), QSize());
    combo->setCurrentIndex(combo->count() - 1); // dialogs open with custom sizes

    QObject::connect(combo, &QComboBox::currentIndexChanged, dialog,
                     [combo, widthSpin, heightSpin](int index) {
                         const QSize size = combo->itemData(index).toSize();
                         if (!size.isValid() || size.isEmpty())
                             return; // Custom: leave the spins alone
                         const QSignalBlocker blockWidth(widthSpin);
                         const QSignalBlocker blockHeight(heightSpin);
                         widthSpin->setValue(size.width());
                         heightSpin->setValue(size.height());
                     });
    const auto toCustom = [combo] {
        const QSignalBlocker blocker(combo);
        combo->setCurrentIndex(combo->count() - 1);
    };
    QObject::connect(widthSpin, &QSpinBox::valueChanged, dialog, toCustom);
    QObject::connect(heightSpin, &QSpinBox::valueChanged, dialog, toCustom);
    return combo;
}

} // namespace

void MainWindow::exportImage()
{
    Document* doc = currentDocument();
    if (!doc || !doc->structure || doc->structure->empty()) {
        statusBar()->showMessage(tr("Open a structure first."));
        return;
    }

    QDialog dialog(this);
    dialog.setWindowTitle(tr("Export Image"));
    auto* form = new QFormLayout(&dialog);

    auto* widthSpin = new QSpinBox(&dialog);
    widthSpin->setRange(64, 8192);
    widthSpin->setValue(viewport_->width() * 2); // 2x viewport = crisp default
    auto* heightSpin = new QSpinBox(&dialog);
    heightSpin->setRange(64, 8192);
    heightSpin->setValue(viewport_->height() * 2);
    form->addRow(tr("Resolution preset:"),
                 makeResolutionPresetCombo(&dialog, widthSpin, heightSpin));
    form->addRow(tr("Width (px):"), widthSpin);
    form->addRow(tr("Height (px):"), heightSpin);

    auto* backgroundCombo = new QComboBox(&dialog);
    backgroundCombo->addItems({tr("Transparent (PNG only)"), tr("Solid white"),
                               tr("Viewport color")});
    form->addRow(tr("Background:"), backgroundCombo);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel,
                                         &dialog);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    form->addRow(buttons);

    if (dialog.exec() != QDialog::Accepted)
        return;

    const QString path = QFileDialog::getSaveFileName(
        this, tr("Export Image"), QStringLiteral("calango.png"),
        tr("PNG image (*.png);;JPEG image (*.jpg *.jpeg)"));
    if (path.isEmpty())
        return;

    const bool transparent = backgroundCombo->currentIndex() == 0;
    const QColor background = transparent ? QColor(0, 0, 0, 0)
        : backgroundCombo->currentIndex() == 1 ? QColor(Qt::white)
                                               : viewport_->backgroundColor();

    QImage image =
        viewport_->renderToImage(widthSpin->value(), heightSpin->value(), background);

    const bool isJpeg = path.endsWith(QStringLiteral(".jpg"), Qt::CaseInsensitive)
        || path.endsWith(QStringLiteral(".jpeg"), Qt::CaseInsensitive);
    if (isJpeg && transparent) {
        // JPEG has no alpha channel — composite over white instead.
        QImage flattened(image.size(), QImage::Format_RGB32);
        flattened.fill(Qt::white);
        QPainter painter(&flattened);
        painter.drawImage(0, 0, image);
        painter.end();
        image = flattened;
    }

    QImageWriter writer(path);
    if (!writer.write(image)) {
        QMessageBox::critical(this, tr("Export Image"),
                              tr("Could not write %1:\n%2").arg(path, writer.errorString()));
        return;
    }
    statusBar()->showMessage(tr("Exported %1 (%2×%3)")
                                 .arg(path)
                                 .arg(image.width())
                                 .arg(image.height()));
}

QImage MainWindow::renderFilmFrame(const render::FilmScript& film, int frame,
                                   int frameCount, int width, int height,
                                   const QColor& background)
{
    Document* doc = currentDocument();
    const double time = frameCount > 1
        ? film.effectiveDuration() * static_cast<double>(frame)
            / static_cast<double>(frameCount - 1)
        : 0.0;
    const render::FilmSample sample = render::sampleFilm(film, time);

    // Geometry first: a camera moved onto a frame that is about to be replaced
    // would render one frame of the previous structure from the new angle.
    if (doc && sample.trajectoryFrame >= 0
        && sample.trajectoryFrame < static_cast<int>(doc->frames.size())) {
        viewport_->setStructure(
            doc->frames[static_cast<std::size_t>(sample.trajectoryFrame)], false);
    }

    // The exported film must show the same overlays the preview does — an
    // annotation that appears on screen but not in the rendered MP4 is the
    // kind of difference nobody notices until the talk.
    if (overlayPanel_) {
        overlayPanel_->setFilmOverlayFilter(
            sample.overridesOverlays ? &sample.overlayIds : nullptr);
    }

    const auto shoot = [&](const render::PointOfView& pov,
                           const std::vector<render::FilmCastOpacity>& casts) {
        applyFilmCastOpacities(casts);
        applyingFilm_ = true;
        viewport_->setPointOfView(pov);
        applyingFilm_ = false;
        return viewport_->renderToImage(width, height, background);
    };

    QImage image = shoot(sample.camera, sample.castOpacity);

    // A dissolve is a mix of two complete renders. Unlike the live preview,
    // which caches the outgoing side for the whole transition, the export
    // simply renders both: it is offline, and re-rendering keeps it correct
    // even when the trajectory advances underneath the dissolve.
    if (sample.crossfading && sample.crossfadeWeight < 1.0f) {
        const QImage outgoing =
            shoot(sample.crossfadeFrom, sample.crossfadeFromCastOpacity);
        QPainter painter(&image);
        painter.setOpacity(1.0 - static_cast<double>(sample.crossfadeWeight));
        painter.drawImage(0, 0, outgoing);
    }

    // The fade goes to black, matching the preview: a fade that resolved to
    // the export background would look different from what was previewed.
    if (sample.fade < 1.0f) {
        QPainter painter(&image);
        painter.setPen(Qt::NoPen);
        painter.setBrush(QColor(0, 0, 0,
                                static_cast<int>(std::lround(
                                    255.0f * (1.0f - sample.fade)))));
        painter.drawRect(image.rect());
    }
    return image;
}

void MainWindow::exportAnimation()
{
    Document* doc = currentDocument();
    if (!doc || !doc->structure || doc->structure->empty()) {
        statusBar()->showMessage(tr("Open a structure first."));
        return;
    }

    QDialog dialog(this);
    dialog.setWindowTitle(tr("Export Animation"));
    auto* form = new QFormLayout(&dialog);

    // The film is coupled to this workspace's trajectory exactly as the film
    // timeline couples it, so an export reproduces what the preview showed
    // rather than re-deriving the timing from scratch.
    const bool hasTrajectory = doc->frames.size() > 1;
    render::FilmScript film = doc->film;
    film.trajectoryFrames = hasTrajectory ? static_cast<int>(doc->frames.size()) : 0;
    if (film.trajectoryFps <= 0.0)
        film.trajectoryFps = kTrajectoryPlaybackFps;
    const bool hasFilm = film.isValid();

    auto* sourceCombo = new QComboBox(&dialog);
    // Identified by userData, not by row: which rows exist depends on what
    // this workspace happens to hold.
    sourceCombo->addItem(tr("Turntable rotation (360°)"),
                         static_cast<int>(AnimationSource::Turntable));
    if (hasTrajectory) {
        sourceCombo->addItem(tr("Trajectory frames (%1)").arg(doc->frames.size()),
                             static_cast<int>(AnimationSource::Trajectory));
    }
    if (hasFilm) {
        sourceCombo->addItem(
            tr("Film production (%1 shots, %2 s)")
                .arg(film.shots.size())
                .arg(film.effectiveDuration(), 0, 'f', 2),
            static_cast<int>(AnimationSource::Film));
    }
    form->addRow(tr("Source:"), sourceCombo);
    if (!hasFilm) {
        auto* filmHint = new QLabel(
            tr("<i>No film in this workspace — build one in Film "
               "production… to export it.</i>"),
            &dialog);
        filmHint->setWordWrap(true);
        form->addRow(QString(), filmHint);
    }

    auto* framesSpin = new QSpinBox(&dialog);
    framesSpin->setRange(8, 360);
    framesSpin->setValue(72);
    form->addRow(tr("Rotation frames:"), framesSpin);

    auto* widthSpin = new QSpinBox(&dialog);
    widthSpin->setRange(64, 4096); // up to 4K UHD
    widthSpin->setSingleStep(2); // H.264 yuv420p wants even dimensions
    widthSpin->setValue(640);
    auto* heightSpin = new QSpinBox(&dialog);
    heightSpin->setRange(64, 4096);
    heightSpin->setSingleStep(2);
    heightSpin->setValue(480);
    form->addRow(tr("Resolution preset:"),
                 makeResolutionPresetCombo(&dialog, widthSpin, heightSpin));
    form->addRow(tr("Width (px):"), widthSpin);
    form->addRow(tr("Height (px):"), heightSpin);

    auto* fpsSpin = new QSpinBox(&dialog);
    fpsSpin->setRange(1, 60);
    fpsSpin->setValue(24);
    form->addRow(tr("Frames per second:"), fpsSpin);

    // Format is picked here rather than inferred from the file extension the
    // user happens to type: .mp4 alone does not say H.264 or HEVC, and a
    // silently-chosen codec is what makes an export unplayable somewhere else.
    // The save dialog below follows this choice.
    auto* formatCombo = new QComboBox(&dialog);
    const auto& formats = pybridge::AnimationExporter::videoFormats();
    for (std::size_t i = 0; i < formats.size(); ++i) {
        formatCombo->addItem(tr(formats[i].label), static_cast<int>(i));
    }
    formatCombo->setCurrentIndex(0); // MP4 / H.264 — plays everywhere
    formatCombo->setToolTip(
        tr("H.264 MP4 is the default: it is the one file that plays "
           "everywhere.\n"
           "HEVC and VP9 encode the same picture smaller, at the cost of "
           "older players.\n"
           "Animated GIF is the only format here that carries transparency — "
           "and the only one limited to 256 colors per frame."));
    form->addRow(tr("Format:"), formatCombo);

    auto* countLabel = new QLabel(&dialog);
    form->addRow(tr("Frames to render:"), countLabel);
    // Rotation frames only mean anything for the turntable; the trajectory
    // brings its own count and the film derives one from its duration and
    // rate. Selecting the film also adopts its rate, so an exported file runs
    // at the length the Film production dialog promised — exporting a 10 s
    // film at a different fps would silently change its duration.
    const auto syncSourceControls = [&] {
        const auto source =
            static_cast<AnimationSource>(sourceCombo->currentData().toInt());
        framesSpin->setEnabled(source == AnimationSource::Turntable);
        if (source == AnimationSource::Film) {
            fpsSpin->setValue(std::clamp(film.fps, fpsSpin->minimum(),
                                         fpsSpin->maximum()));
        }
        const int count = source == AnimationSource::Turntable
            ? framesSpin->value()
            : source == AnimationSource::Trajectory
                ? static_cast<int>(doc->frames.size())
                : film.frameCount();
        countLabel->setText(
            source == AnimationSource::Film
                ? tr("%1  (%2 s at %3 fps)")
                      .arg(count)
                      .arg(film.effectiveDuration(), 0, 'f', 2)
                      .arg(film.fps)
                : QString::number(count));
    };
    connect(sourceCombo, &QComboBox::currentIndexChanged, &dialog,
            [&syncSourceControls] { syncSourceControls(); });
    connect(framesSpin, &QSpinBox::valueChanged, &dialog,
            [&syncSourceControls] { syncSourceControls(); });
    syncSourceControls();

    auto* backgroundCombo = new QComboBox(&dialog);
    backgroundCombo->addItems({tr("Solid white"), tr("Viewport color"),
                               tr("Custom color…"), tr("Transparent (GIF only)")});
    form->addRow(tr("Background:"), backgroundCombo);
    QColor customBackground = Qt::white;
    connect(backgroundCombo, &QComboBox::currentIndexChanged, &dialog,
            [this, &customBackground](int index) {
                if (index != 2)
                    return;
                const QColor chosen = QColorDialog::getColor(
                    customBackground, this, tr("Animation Background Color"));
                if (chosen.isValid())
                    customBackground = chosen;
            });

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel,
                                         &dialog);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    form->addRow(buttons);

    if (dialog.exec() != QDialog::Accepted)
        return;

    const pybridge::AnimationExporter::VideoFormat& format =
        formats[static_cast<std::size_t>(formatCombo->currentData().toInt())];
    const QString extension = QLatin1String(format.extension);
    const bool isGif = QLatin1String(format.codec).size() == 0;

    QString path = QFileDialog::getSaveFileName(
        this, tr("Export Animation"),
        QStringLiteral("calango.") + extension,
        tr("%1 (*.%2)").arg(tr(format.label), extension));
    if (path.isEmpty())
        return;
    // The chosen format decides the container, so an absent (or mismatched)
    // suffix is corrected rather than silently handing ffmpeg a name whose
    // extension contradicts the codec it was told to use.
    if (!path.endsWith(QLatin1Char('.') + extension, Qt::CaseInsensitive))
        path += QLatin1Char('.') + extension;

    bool transparent = backgroundCombo->currentIndex() == 3;
    if (transparent && !isGif) {
        QMessageBox::information(
            this, tr("Export Animation"),
            tr("%1 has no alpha channel — using a solid white background "
               "instead.")
                .arg(tr(format.label)));
        transparent = false;
        backgroundCombo->setCurrentIndex(0);
    }
    const QColor background = transparent ? QColor(0, 0, 0, 0)
        : backgroundCombo->currentIndex() == 1  ? viewport_->backgroundColor()
        : backgroundCombo->currentIndex() == 2  ? customBackground
                                                : QColor(Qt::white);

    const int width = widthSpin->value() & ~1;
    const int height = heightSpin->value() & ~1;
    const auto source =
        static_cast<AnimationSource>(sourceCombo->currentData().toInt());
    const int frameCount = source == AnimationSource::Turntable
        ? framesSpin->value()
        : source == AnimationSource::Trajectory
            ? static_cast<int>(doc->frames.size())
            : film.frameCount();

    QProgressDialog progress(tr("Rendering frames…"), tr("Cancel"), 0, frameCount, this);
    progress.setWindowModality(Qt::WindowModal);
    progress.setMinimumDuration(0);

    std::vector<QImage> images;
    images.reserve(static_cast<std::size_t>(frameCount));
    const int restoreFrame = hasTrajectory ? timeline_->currentFrame() : 0;
    // A film export drives the camera and the cast opacities, so both are put
    // back afterwards — exporting must not cost the user the view they had.
    const render::PointOfView restorePov = viewport_->camera().pointOfView();
    const bool filmSource = source == AnimationSource::Film;
    const bool ownBaseline = filmSource && filmCastBaseline_.empty();
    if (ownBaseline)
        rememberCastOpacities();

    for (int i = 0; i < frameCount; ++i) {
        progress.setValue(i);
        QApplication::processEvents();
        if (progress.wasCanceled())
            break;

        switch (source) {
        case AnimationSource::Turntable:
            images.push_back(viewport_->renderToImage(
                width, height, background,
                360.0f * static_cast<float>(i) / static_cast<float>(frameCount)));
            break;
        case AnimationSource::Trajectory:
            viewport_->setStructure(doc->frames[static_cast<std::size_t>(i)], false);
            images.push_back(viewport_->renderToImage(width, height, background));
            break;
        case AnimationSource::Film:
            images.push_back(
                renderFilmFrame(film, i, frameCount, width, height, background));
            break;
        }
    }

    if (source == AnimationSource::Trajectory)
        showFrame(restoreFrame); // put the live view back where it was
    if (filmSource) {
        if (hasTrajectory)
            showFrame(restoreFrame);
        applyFilmCastOpacities({});
        if (overlayPanel_)
            overlayPanel_->setFilmOverlayFilter(nullptr);
        if (ownBaseline)
            filmCastBaseline_.clear();
        applyingFilm_ = true;
        viewport_->setPointOfView(restorePov);
        applyingFilm_ = false;
    }
    if (progress.wasCanceled())
        return;
    progress.setValue(frameCount);

    try {
        if (isGif) {
            pybridge::AnimationExporter::exportGif(images, path, fpsSpin->value(),
                                                   transparent);
        } else {
            pybridge::AnimationExporter::exportVideo(
                images, path, fpsSpin->value(), QLatin1String(format.codec),
                QLatin1String(format.pixelFormat));
        }
        statusBar()->showMessage(tr("Exported %1 (%2 frames)").arg(path).arg(images.size()));
    } catch (const std::exception& e) {
        QMessageBox::critical(this, tr("Export Animation"), QString::fromUtf8(e.what()));
    }
}

void MainWindow::exportAlembic()
{
    Document* doc = currentDocument();
    if (!doc || !doc->structure || doc->structure->empty()) {
        statusBar()->showMessage(tr("Open a structure first."));
        return;
    }

    QDialog dialog(this);
    dialog.setWindowTitle(tr("Export to Alembic"));
    auto* form = new QFormLayout(&dialog);

    auto* intro = new QLabel(
        tr("<i>Alembic (.abc) is a baked geometry cache: the atoms, bonds and "
           "cell as polygon meshes, one object per element so materials can be "
           "assigned per species after import (Blender, Houdini, Maya, "
           "Cinema&nbsp;4D, Unreal).</i>"),
        &dialog);
    intro->setWordWrap(true);
    form->addRow(intro);

    const bool hasTrajectory = doc->frames.size() > 1;
    auto* sourceCombo = new QComboBox(&dialog);
    sourceCombo->addItem(tr("Current structure (single frame)"), 0);
    if (hasTrajectory) {
        sourceCombo->addItem(
            tr("Trajectory (%1 frames, animated)").arg(doc->frames.size()), 1);
        sourceCombo->setCurrentIndex(1);
    }
    form->addRow(tr("Source:"), sourceCombo);

    auto* fpsSpin = new QSpinBox(&dialog);
    fpsSpin->setRange(1, 120);
    fpsSpin->setValue(24);
    fpsSpin->setEnabled(hasTrajectory);
    form->addRow(tr("Frames per second:"), fpsSpin);

    // Tessellation is the one real trade-off here: an .abc is polygons, and a
    // 5 000-atom cell at high detail is a multi-hundred-megabyte file.
    auto* detailCombo = new QComboBox(&dialog);
    detailCombo->addItem(tr("Low (12 sides — large systems)"), 12);
    detailCombo->addItem(tr("Medium (24 sides)"), 24);
    detailCombo->addItem(tr("High (40 sides — close-ups)"), 40);
    detailCombo->setCurrentIndex(1);
    form->addRow(tr("Sphere detail:"), detailCombo);

    auto* bondsCheck = new QCheckBox(tr("Include bonds"), &dialog);
    bondsCheck->setChecked(true);
    form->addRow(bondsCheck);
    auto* cellCheck = new QCheckBox(tr("Include unit cell wireframe"), &dialog);
    cellCheck->setChecked(doc->structure->cell().isDefined());
    cellCheck->setEnabled(doc->structure->cell().isDefined());
    form->addRow(cellCheck);

    auto* buttons = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    form->addRow(buttons);

    if (dialog.exec() != QDialog::Accepted)
        return;

    QString path = QFileDialog::getSaveFileName(
        this, tr("Export to Alembic"), QStringLiteral("calango.abc"),
        tr("Alembic cache (*.abc)"));
    if (path.isEmpty())
        return;
    if (!path.endsWith(QStringLiteral(".abc"), Qt::CaseInsensitive))
        path += QStringLiteral(".abc");

    pybridge::AlembicExporter::Options options;
    // The exported cache must be the scene that is on screen — same radii,
    // same element colours, same bond perception — not a second set of
    // defaults that happens to look similar.
    options.style = viewport_->style();
    options.sphereSegments = detailCombo->currentData().toInt();
    options.cylinderSides = std::max(6, options.sphereSegments * 2 / 3);
    options.includeBonds = bondsCheck->isChecked();
    options.includeCell = cellCheck->isChecked();
    options.fps = fpsSpin->value();

    std::vector<std::shared_ptr<const core::Structure>> frames;
    if (sourceCombo->currentData().toInt() == 1)
        frames.assign(doc->frames.begin(), doc->frames.end());
    else
        frames.push_back(doc->structure);

    QProgressDialog progress(tr("Writing Alembic cache…"), QString(), 0, 0, this);
    progress.setWindowModality(Qt::WindowModal);
    progress.setMinimumDuration(0);
    progress.show();
    QApplication::processEvents();
    try {
        pybridge::AlembicExporter::exportScene(frames, path, options);
        progress.close();
        statusBar()->showMessage(
            tr("Exported %1 (%2 frame(s))").arg(path).arg(frames.size()));
    } catch (const std::exception& e) {
        progress.close();
        QMessageBox::critical(this, tr("Export to Alembic"),
                              QString::fromUtf8(e.what()));
    }
}

// ---------------------------------------------------------------------------
// Builder tools
// ---------------------------------------------------------------------------

namespace {

/// Prompt for how much vacuum to add and where. Lifted verbatim from the Edit
/// Structure dialog, which no longer owns the operation.
bool askVacuumOptions(QWidget* parent, core::VacuumOptions& options)
{
    QDialog dialog(parent);
    dialog.setWindowTitle(QObject::tr("Add Vacuum Layer"));
    auto* form = new QFormLayout(&dialog);

    auto* amountSpin = new QDoubleSpinBox(&dialog);
    amountSpin->setRange(0.0, 1000.0);
    amountSpin->setDecimals(3);
    amountSpin->setSingleStep(0.5);
    amountSpin->setValue(options.thickness);
    amountSpin->setSuffix(QStringLiteral(" Å"));
    form->addRow(QObject::tr("Vacuum thickness:"), amountSpin);

    auto* bothSidesCheck =
        new QCheckBox(QObject::tr("Split evenly on both sides"), &dialog);
    bothSidesCheck->setChecked(options.bothSides);
    bothSidesCheck->setToolTip(QObject::tr(
        "On: the thickness above is the *total* added length, and the "
        "structure ends up centered along that direction (the usual choice "
        "for slabs and clusters).\n"
        "Off: the full amount is added past the structure on the far side "
        "only."));
    form->addRow(QString(), bothSidesCheck);

    // Lattice directions rather than Cartesian axes: vacuum has to grow along
    // the cell vector to stay commensurate with a non-orthogonal cell.
    std::array<QCheckBox*, 3> axisChecks{};
    static const char* kAxisLabels[3] = {
        QT_TR_NOOP("a (v1)"), QT_TR_NOOP("b (v2)"), QT_TR_NOOP("c (v3)")};
    auto* axisRow = new QHBoxLayout;
    for (int i = 0; i < 3; ++i) {
        axisChecks[static_cast<std::size_t>(i)] =
            new QCheckBox(QObject::tr(kAxisLabels[i]), &dialog);
        axisChecks[static_cast<std::size_t>(i)]->setChecked(options.axes[i]);
        axisRow->addWidget(axisChecks[static_cast<std::size_t>(i)]);
    }
    form->addRow(QObject::tr("Along:"), axisRow);

    auto* clearPbcCheck = new QCheckBox(
        QObject::tr("Mark the padded directions non-periodic"), &dialog);
    clearPbcCheck->setChecked(options.clearPbc);
    clearPbcCheck->setToolTip(QObject::tr(
        "Vacuum is normally added precisely to decouple periodic images; "
        "clearing pbc along those directions makes that explicit for the "
        "calculators."));
    form->addRow(QString(), clearPbcCheck);

    auto* buttons = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog,
                     &QDialog::accept);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog,
                     &QDialog::reject);
    form->addRow(buttons);

    if (dialog.exec() != QDialog::Accepted)
        return false;
    options.thickness = amountSpin->value();
    options.bothSides = bothSidesCheck->isChecked();
    options.clearPbc = clearPbcCheck->isChecked();
    for (int i = 0; i < 3; ++i)
        options.axes[i] = axisChecks[static_cast<std::size_t>(i)]->isChecked();
    return true;
}

} // namespace

int MainWindow::applyToAllFrames(
    const std::function<bool(core::Structure&)>& transform)
{
    Document* doc = currentDocument();
    if (!doc || !doc->structure || doc->structure->empty())
        return -1;

    // No trajectory: the displayed structure IS the whole document.
    if (doc->frames.size() < 2) {
        auto edited = std::make_shared<core::Structure>(*doc->structure);
        if (!transform(*edited))
            return 0;
        installEditedStructure(std::move(edited), QString());
        return 1;
    }

    // Every frame is transformed into a fresh copy first and only committed
    // once they all succeeded, so a transform that throws part-way (spglib
    // failing on one frame, say) leaves the document exactly as it was rather
    // than half-converted.
    std::vector<std::shared_ptr<core::Structure>> updated;
    updated.reserve(doc->frames.size());
    std::shared_ptr<core::Structure> displayed;
    int changed = 0;
    for (const auto& frame : doc->frames) {
        if (!frame) {
            updated.push_back(frame);
            continue;
        }
        auto copy = std::make_shared<core::Structure>(*frame);
        if (transform(*copy))
            ++changed;
        // The frame the viewport is on has to become the same object the
        // trajectory now holds, or scrubbing away and back would revert it.
        if (frame == doc->structure)
            displayed = copy;
        updated.push_back(std::move(copy));
    }
    if (changed == 0)
        return 0;

    pushUndo();
    doc->frames = std::move(updated);
    // `displayed` is null only if the shown structure was not one of the frames
    // (an edited structure never re-inserted); leave it alone then.
    if (displayed)
        doc->structure = std::move(displayed);
    notifyStructureChanged(/*frameCamera=*/false);
    return changed;
}

void MainWindow::centerStructure()
{
    // Centred frame by frame, which is the point of centring a trajectory: it
    // removes the centre-of-mass drift that otherwise walks the whole system
    // across the viewport during an MD run.
    const int frames = applyToAllFrames([](core::Structure& structure) {
        core::centerInCell(structure);
        return true;
    });
    if (frames <= 0)
        return;
    statusBar()->showMessage(
        frames > 1
            ? tr("Centered every atom in the cell across %n trajectory frame(s)",
                 nullptr, frames)
            : tr("Structure centered in the cell"));
}

void MainWindow::addVacuumLayer()
{
    Document* doc = currentDocument();
    if (!doc || !doc->structure || doc->structure->empty())
        return;
    if (!doc->structure->cell().isDefined()) {
        QMessageBox::information(
            this, tr("Add Vacuum"),
            tr("Define a unit cell first — vacuum padding extends an existing "
               "cell."));
        return;
    }
    core::VacuumOptions options;
    if (!askVacuumOptions(this, options))
        return;
    if (options.thickness <= 0.0)
        return;
    if (!options.axes[0] && !options.axes[1] && !options.axes[2]) {
        QMessageBox::information(this, tr("Add Vacuum"),
                                 tr("Select at least one direction."));
        return;
    }
    auto edited = std::make_shared<core::Structure>(*doc->structure);
    if (!core::addVacuum(*edited, options))
        return;
    installEditedStructure(std::move(edited),
                           tr("Vacuum layer added (%1 Å)")
                               .arg(options.thickness, 0, 'f', 2));
}

void MainWindow::wrapStructureIntoCell()
{
    Document* doc = currentDocument();
    if (!doc || !doc->structure || doc->structure->empty())
        return;
    if (!doc->structure->cell().isDefined()) {
        QMessageBox::information(
            this, tr("Wrap Into Cell"),
            tr("Define a unit cell first — without a lattice there are no cell "
               "boundaries to wrap into."));
        return;
    }
    // The viewport selection is the analogue of the atom table's selection the
    // Edit Structure dialog used; nothing selected still means "all atoms",
    // which is the common case for a freshly imported structure.
    std::vector<std::size_t> indices;
    for (const int index : viewport_->selection()) {
        if (index >= 0)
            indices.push_back(static_cast<std::size_t>(index));
    }

    // A trajectory is wrapped WHOLE. Wrapping only the displayed frame would
    // make the atoms jump between lattice images as the timeline is scrubbed —
    // the very artifact the button exists to remove — and an MD run is
    // precisely where atoms diffuse out of the box in the first place.
    const int frameCount = static_cast<int>(doc->frames.size());
    int movedTotal = 0;
    const int framesChanged =
        applyToAllFrames([&indices, &movedTotal](core::Structure& structure) {
            const int moved = core::wrapIntoCell(structure, indices);
            movedTotal += moved;
            return moved > 0;
        });
    if (framesChanged < 0)
        return;
    if (framesChanged == 0) {
        // Nothing moved and nothing moved look identical on screen, so say so
        // rather than pushing an undo step that changes nothing.
        if (frameCount > 1) {
            statusBar()->showMessage(
                indices.empty()
                    ? tr("Every atom of every frame already lies inside the "
                         "unit cell.")
                    : tr("Every selected atom already lies inside the unit "
                         "cell, in every frame."));
        } else {
            statusBar()->showMessage(
                indices.empty()
                    ? tr("Every atom already lies inside the unit cell.")
                    : tr("Every selected atom already lies inside the unit cell."));
        }
        return;
    }
    if (frameCount > 1) {
        statusBar()->showMessage(
            tr("Wrapped %n atom(s) into the cell across %1 of %2 trajectory "
               "frames", nullptr, movedTotal)
                .arg(framesChanged)
                .arg(frameCount));
        return;
    }
    statusBar()->showMessage(
        tr("Wrapped %n atom(s) into the cell", nullptr, movedTotal));
}

void MainWindow::openSupercellBuilder()
{
    Document* doc = currentDocument();
    if (!doc || !doc->structure || doc->structure->empty()
        || !doc->structure->cell().isDefined()) {
        QMessageBox::information(
            this, tr("Supercell"),
            tr("Open a periodic structure with a defined unit cell first."));
        return;
    }
    if (!ensureAseAvailable())
        return;

    SupercellDialog dialog(doc->structure, this);
    if (dialog.exec() != QDialog::Accepted)
        return;

    int p[3][3];
    dialog.matrix(p);
    try {
        auto transformed = std::make_shared<core::Structure>(
            pybridge::AseBridge::makeSupercellMatrix(*doc->structure, p));
        // Non-destructive: the supercell opens as an independent workspace tab
        // and the original unit-cell tab is left untouched, so P can be
        // re-applied or compared against the primitive cell.
        const std::size_t atoms = transformed->size();
        const QString baseName = doc->fileName.isEmpty() ? tr("Untitled")
                                                         : doc->fileName;
        addDocument(std::move(transformed),
                    tr("%1 (supercell)").arg(baseName));
        isDirty_ = true;
        statusBar()->showMessage(
            tr("Supercell created in a new tab: %1 atoms").arg(atoms));
    } catch (const std::exception& e) {
        QMessageBox::critical(this, tr("Supercell"), QString::fromUtf8(e.what()));
    }
}

void MainWindow::cleaveSurface()
{
    Document* doc = currentDocument();
    if (!doc || !doc->structure || !doc->structure->cell().isDefined()) {
        QMessageBox::information(this, tr("Surface Slab"),
                                 tr("Open a bulk structure with a unit cell first."));
        return;
    }
    if (!ensureAseAvailable())
        return;

    // Three-stage wizard: orientation (draggable in-plane vectors) →
    // cut / terminations (cross-section) → vacuum + 3D preview.
    SlabWizard wizard(doc->structure, this);
    if (wizard.exec() != QDialog::Accepted || !wizard.result())
        return;

    // The slab opens in its own tab — the bulk structure the wizard was
    // fed stays untouched in the original tab (with its undo history).
    const auto atomCount = wizard.result()->size();
    addDocument(wizard.result(),
                tr("%1 %2").arg(doc->fileName, wizard.resultLabel()));
    statusBar()->showMessage(tr("Slab created in a new tab: %1 atoms").arg(atomCount));
}

// ---------------------------------------------------------------------------
// Editing tools
// ---------------------------------------------------------------------------

void MainWindow::addAtom()
{
    QDialog dialog(this);
    dialog.setWindowTitle(tr("Add Atom"));
    auto* form = new QFormLayout(&dialog);

    // Element choice via the graphical periodic table.
    int selectedZ = 6; // carbon default
    auto* elementButton = new QPushButton(&dialog);
    const auto updateElementButton = [&selectedZ, elementButton] {
        elementButton->setText(
            QStringLiteral("%1  (Z = %2)")
                .arg(QLatin1String(core::Elements::data(selectedZ).symbol))
                .arg(selectedZ));
    };
    updateElementButton();
    form->addRow(tr("Element:"), elementButton);
    connect(elementButton, &QPushButton::clicked, &dialog,
            [&dialog, &selectedZ, updateElementButton] {
                if (const int z = PeriodicTableDialog::pickElement(&dialog, selectedZ)) {
                    selectedZ = z;
                    updateElementButton();
                }
            });

    QDoubleSpinBox* coords[3];
    const char* names[3] = {"x", "y", "z"};
    for (int i = 0; i < 3; ++i) {
        coords[i] = new QDoubleSpinBox(&dialog);
        coords[i]->setRange(-1000.0, 1000.0);
        coords[i]->setDecimals(4);
        coords[i]->setSuffix(tr(" Å"));
        form->addRow(QStringLiteral("%1:").arg(QLatin1String(names[i])), coords[i]);
    }

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel,
                                         &dialog);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    form->addRow(buttons);

    if (dialog.exec() != QDialog::Accepted)
        return;

    Document& doc = ensureDocument();
    pushUndo();
    const bool firstAtom = !doc.structure || doc.structure->empty();
    if (!doc.structure)
        doc.structure = std::make_shared<core::Structure>();
    doc.structure->addAtom(
        {selectedZ, {coords[0]->value(), coords[1]->value(), coords[2]->value()}});
    notifyStructureChanged(firstAtom);
    statusBar()->showMessage(
        tr("Added %1 atom").arg(QLatin1String(core::Elements::data(selectedZ).symbol)));
}

void MainWindow::changeElementOfSelection()
{
    Document* doc = currentDocument();
    const auto& selection = viewport_->selection();
    if (!doc || !doc->structure || selection.empty()) {
        statusBar()->showMessage(tr("Select atoms first (click / Ctrl+click)."));
        return;
    }

    // Highlight the current element when the selection is homogeneous.
    const int firstZ = doc->structure
                           ->atoms()[static_cast<std::size_t>(*selection.begin())]
                           .atomicNumber;
    const int z = PeriodicTableDialog::pickElement(this, firstZ);
    if (z == 0)
        return;

    pushUndo();
    for (const int index : selection)
        doc->structure->atoms()[static_cast<std::size_t>(index)].atomicNumber = z;
    notifyStructureChanged(false);
    statusBar()->showMessage(
        tr("Changed %n atom(s) to %1", nullptr, static_cast<int>(selection.size()))
            .arg(QLatin1String(core::Elements::data(z).symbol)));
}

void MainWindow::translateSelection()
{
    Document* doc = currentDocument();
    const auto& selection = viewport_->selection();
    if (!doc || !doc->structure || selection.empty()) {
        statusBar()->showMessage(tr("Select atoms first (click / Ctrl+click)."));
        return;
    }

    QDialog dialog(this);
    dialog.setWindowTitle(tr("Translate Selection"));
    auto* form = new QFormLayout(&dialog);
    QDoubleSpinBox* delta[3];
    const char* names[3] = {"Δx", "Δy", "Δz"};
    for (int i = 0; i < 3; ++i) {
        delta[i] = new QDoubleSpinBox(&dialog);
        delta[i]->setRange(-100.0, 100.0);
        delta[i]->setDecimals(4);
        delta[i]->setSuffix(tr(" Å"));
        form->addRow(QString::fromUtf8(names[i]) + QStringLiteral(":"), delta[i]);
    }
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel,
                                         &dialog);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    form->addRow(buttons);

    if (dialog.exec() != QDialog::Accepted)
        return;

    pushUndo();
    const core::Vec3 shift{delta[0]->value(), delta[1]->value(), delta[2]->value()};
    for (const int index : selection)
        doc->structure->atoms()[static_cast<std::size_t>(index)].position += shift;
    notifyStructureChanged(false);
}

void MainWindow::deleteSelectedAtoms()
{
    Document* doc = currentDocument();
    const auto& selection = viewport_->selection();
    if (!doc || !doc->structure || selection.empty()) {
        statusBar()->showMessage(tr("Select atoms first (click / Ctrl+click)."));
        return;
    }

    pushUndo();
    // Remove in descending index order so indices stay valid.
    std::vector<int> indices(selection.begin(), selection.end());
    std::sort(indices.rbegin(), indices.rend());
    for (const int index : indices)
        doc->structure->removeAtom(static_cast<std::size_t>(index));
    notifyStructureChanged(false);
    statusBar()->showMessage(tr("Deleted %n atom(s)", nullptr,
                                static_cast<int>(indices.size())));
}

void MainWindow::showBondEditor()
{
    Document* doc = currentDocument();
    if (!doc || !doc->structure || doc->structure->empty()) {
        statusBar()->showMessage(tr("Open a structure first."));
        return;
    }

    // One undo snapshot per editing session (the dialog applies live).
    pushUndo();
    // The whole trajectory goes in, not just the displayed frame: bond rules
    // describe the system's chemistry, which does not change between two
    // samples of the same run. See BondEditorDialog's class comment.
    BondEditorDialog dialog(doc->structure, doc->frames, viewport_, this);
    connect(&dialog, &BondEditorDialog::bondsEdited, this,
            [this] { notifyStructureChanged(false); });
    dialog.exec();
}

void MainWindow::completeWithHydrogens()
{
    Document* doc = currentDocument();
    if (!doc || !doc->structure || doc->structure->empty()) {
        statusBar()->showMessage(tr("Open a structure first."));
        return;
    }

    // Built on a copy so a run that turns out to add nothing leaves the undo
    // stack untouched — an undo entry that restores an identical structure is
    // worse than no entry at all.
    auto updated = std::make_shared<core::Structure>(*doc->structure);
    core::HydrogenCompletionOptions options;
    // Perceive bonds exactly as the viewport draws them: an atom's valence is
    // counted against the bonds the user can see, not a second opinion.
    options.bondTolerance = viewport_->style().bondTolerance;
    options.autoBonds = viewport_->style().autoBonds;
    const core::HydrogenCompletionResult result =
        core::completeWithHydrogens(*updated, options);

    if (result.added == 0) {
        statusBar()->showMessage(
            result.skippedAtoms == static_cast<int>(doc->structure->size())
                ? tr("No hydrogens added — none of these elements has a "
                     "standard valence to complete.")
                : tr("No hydrogens added — every atom already carries its "
                     "full valence."));
        return;
    }

    pushUndo();
    // Building hydrogens the user cannot see is a no-op as far as they can
    // tell, so asking for them turns the display back on. Set through the
    // toolbar action rather than on the style directly, so the toggle beside
    // this button follows instead of going stale.
    if (showHydrogensAction_)
        showHydrogensAction_->setChecked(true);
    // A trajectory's displayed frame IS one of doc->frames; replace it there
    // too, or scrubbing away and back would silently drop the hydrogens.
    const auto previous = doc->structure;
    for (auto& frame : doc->frames) {
        if (frame == previous)
            frame = updated;
    }
    doc->structure = std::move(updated);
    // Keep the camera put: the structure grew slightly, and re-framing it
    // would move the view out from under the user for no reason.
    notifyStructureChanged(/*frameCamera=*/false);

    QString message = tr("Added %1 hydrogen(s) to %2 atom(s).")
                          .arg(result.added)
                          .arg(result.completedAtoms);
    if (result.skippedAtoms > 0) {
        message += tr(" %1 atom(s) skipped — no standard valence for their "
                      "element.")
                       .arg(result.skippedAtoms);
    }
    statusBar()->showMessage(message);
}

void MainWindow::editStructure()
{
    Document* doc = currentDocument();
    if (!doc || !doc->structure || doc->structure->empty()) {
        statusBar()->showMessage(tr("Open a structure first."));
        return;
    }

    StructureEditorDialog dialog(*doc->structure, this);
    if (dialog.exec() != QDialog::Accepted)
        return;
    auto edited = dialog.result();
    if (!edited)
        return;

    const auto transform = dialog.cellTransform();
    if (transform != core::CellTransform::None
        && doc->frames.size() > 1) {
        propagateCellTransform(std::move(edited), transform);
        return;
    }

    const auto atomCount = edited->size();
    installEditedStructure(std::move(edited),
                           tr("Structure updated (%1 atoms)").arg(atomCount));
}

void MainWindow::propagateCellTransform(
    std::shared_ptr<core::Structure> edited,
    core::CellTransform transform)
{
    Document* doc = currentDocument();
    if (!doc || !edited)
        return;
    const bool toPrimitive =
        transform == core::CellTransform::Primitive;
    const QString title = toPrimitive ? tr("Reduce to Primitive Cell")
                                      : tr("Standardize Cell");

    // Every frame is standardized independently, then the whole set is checked
    // for the one property that makes it a trajectory: frame k's atom i has to
    // be the same atom in every frame. spglib does not promise that across
    // frames — a thermally displaced MD frame usually resolves to a LOWER
    // symmetry than the ideal one, so it can come back with a different atom
    // count or a different ordering, and the "trajectory" would silently stop
    // corresponding to itself. Better to convert nothing and say so.
    std::vector<std::shared_ptr<core::Structure>> converted;
    converted.reserve(doc->frames.size());
    const auto signatureOf = [](const core::Structure& structure) {
        std::vector<int> signature;
        signature.reserve(structure.size());
        for (const core::Atom& atom : structure.atoms())
            signature.push_back(atom.atomicNumber);
        return signature;
    };
    const std::vector<int> reference = signatureOf(*edited);

    int diverged = 0;
    try {
        for (const auto& frame : doc->frames) {
            if (!frame) {
                converted.push_back(frame);
                continue;
            }
            // The displayed frame is already done — and carries whatever else
            // the dialog edited, which must not be thrown away.
            if (frame == doc->structure) {
                converted.push_back(edited);
                continue;
            }
            auto result = std::make_shared<core::Structure>(
                pybridge::AseBridge::standardizeCell(*frame, /*symprec=*/1e-3,
                                                     toPrimitive,
                                                     /*idealize=*/!toPrimitive));
            if (signatureOf(*result) != reference)
                ++diverged;
            converted.push_back(std::move(result));
        }
    } catch (const std::exception& e) {
        QMessageBox::critical(
            this, title,
            tr("%1\n\nNo frame was changed — the trajectory is unchanged.")
                .arg(QString::fromUtf8(e.what())));
        return;
    }

    if (diverged > 0) {
        QMessageBox::warning(
            this, title,
            tr("%1 of %2 frames came back with a different atom count or "
               "ordering than the frame on screen, so the frames would no "
               "longer correspond to each other and nothing measured across "
               "them (an MSD, an RDF, a diffusion coefficient) would mean "
               "anything.\n\n"
               "This is the normal outcome for a finite-temperature "
               "trajectory: displaced frames resolve to lower symmetry than "
               "the ideal one, and spglib answers each frame on its own "
               "merits.\n\n"
               "Nothing was changed. Standardize the reference structure in "
               "its own tab instead, or loosen the symmetry tolerance so every "
               "frame resolves the same way.")
                .arg(diverged)
                .arg(doc->frames.size() - 1));
        return;
    }

    pushUndo();
    doc->frames = std::move(converted);
    doc->structure = std::move(edited);
    notifyStructureChanged(/*frameCamera=*/false);
    statusBar()->showMessage(
        toPrimitive
            ? tr("Reduced every one of %n trajectory frame(s) to the primitive "
                 "cell (%1 atoms)", nullptr, static_cast<int>(doc->frames.size()))
                  .arg(doc->structure->size())
            : tr("Standardized the cell of every one of %n trajectory frame(s) "
                 "(%1 atoms)", nullptr, static_cast<int>(doc->frames.size()))
                  .arg(doc->structure->size()));
}

void MainWindow::installEditedStructure(
    std::shared_ptr<core::Structure> edited, const QString& message)
{
    Document* doc = currentDocument();
    if (!doc || !edited)
        return;
    pushUndo();
    // In a trajectory the displayed frame *is* one of doc->frames — replace
    // it there too, or scrubbing away and back would silently revert the
    // edit. (frames holds shared_ptrs to the same objects; identity, not
    // index, is what identifies the current one.)
    const auto previous = doc->structure;
    for (auto& frame : doc->frames) {
        if (frame == previous)
            frame = edited;
    }
    doc->structure = std::move(edited);
    // Keep the camera where the user left it: an edited cell or a vacuum
    // layer would otherwise jump the view.
    notifyStructureChanged(/*frameCamera=*/false);
    if (!message.isEmpty())
        statusBar()->showMessage(message);
}

void MainWindow::showPreferences()
{
    PreferencesDialog dialog(this);
    dialog.exec();
    // Persist the curated settings to ~/.calango/settings.json and apply any
    // appearance/thread changes live (theme palette + Zone-1 logo + status bar).
    SettingsManager::save();
    applyAppearanceTheme();
    // A shader-profile change takes effect on the next redraw. rebuildGeometry
    // is true because a profile MAY declare a different vertex layout (the
    // impostor profiles will); rebuilding is cheap next to the surprise of a
    // stale buffer being drawn by a program that expects another format.
    if (viewport_)
        viewport_->styleChanged(/*rebuildGeometry=*/true);
    // Preferences no longer edits the shader profiles — the "Rendering" page
    // is gone — so the Representation panel's "Shading" row is now the only
    // control on them and has nothing to re-read.
}

// ---------------------------------------------------------------------------
// Analysis
// ---------------------------------------------------------------------------

void MainWindow::showBrillouinZone()
{
    Document* doc = currentDocument();
    if (!doc || !doc->structure || !doc->structure->cell().isDefined()) {
        QMessageBox::information(this, tr("Brillouin Zone"),
                                 tr("Open a periodic structure with a unit cell first."));
        return;
    }
    if (!ensureAseAvailable())
        return;

    try {
        const auto zone = core::computeBrillouinZone(doc->structure->cell());
        const auto bandPath = pybridge::AseBridge::bandPathInfo(*doc->structure);
        BrillouinZoneDialog dialog(zone, bandPath, this);
        dialog.exec();
    } catch (const std::exception& e) {
        QMessageBox::critical(this, tr("Brillouin Zone"), QString::fromUtf8(e.what()));
    }
}

void MainWindow::showRdf()
{
    Document* doc = currentDocument();
    if (!doc || !doc->structure || doc->structure->empty()) {
        QMessageBox::information(this, tr("Radial Distribution Function"),
                                 tr("Open a structure first."));
        return;
    }
    RdfDialog dialog(doc->structure, doc->frames, this);
    dialog.exec();
}

void MainWindow::showDistributions()
{
    Document* doc = currentDocument();
    if (!doc || !doc->structure || doc->structure->empty()) {
        QMessageBox::information(this, tr("Bond Distributions"),
                                 tr("Open a structure first."));
        return;
    }
    DistributionDialog dialog(doc->structure, this);
    dialog.exec();
}

void MainWindow::showStructureFactor()
{
    Document* doc = currentDocument();
    if (!doc || !doc->structure || doc->structure->empty()) {
        QMessageBox::information(this, tr("Structure Factor"),
                                 tr("Open a structure first."));
        return;
    }
    StructureFactorDialog dialog(doc->structure, doc->frames, this);
    dialog.exec();
}

void MainWindow::showXrd()
{
    Document* doc = currentDocument();
    if (!doc || !doc->structure || doc->structure->empty()) {
        QMessageBox::information(this, tr("X-Ray Diffraction"),
                                 tr("Open a structure first."));
        return;
    }
    if (!ensureAseAvailable())
        return;
    XrdDialog dialog(doc->structure, this);
    dialog.exec();
}

void MainWindow::showBandStructure()
{
    Document* doc = currentDocument();
    if (!doc || !doc->structure || doc->structure->empty()
        || !doc->structure->cell().isDefined()) {
        QMessageBox::information(this, tr("Electronic Structure"),
                                 tr("Open a periodic structure first."));
        return;
    }
    if (!ensureAseAvailable())
        return;

    // Electronic Structure runs strictly non-self-consistently off a completed
    // Single-Point Calculation's saved charge density (single_point.gpw), so a
    // baseline SCF is mandatory. Gather the candidates first — from BOTH
    // engines, because a band structure is non-self-consistent either way and
    // the file it reads back is simply named differently: GPAW restarts from
    // single_point.gpw, VASP reads a CHGCAR with ICHARG = 11.
    QList<QPair<QString, QString>> baselines = gpawDensityFiles();
    baselines += vaspChargeDensityFiles();
    if (baselines.isEmpty()) {
        QMessageBox::critical(
            this, tr("Electronic Structure"),
            tr("Error: Electronic Structure calculations require a completed "
               "baseline SCF process with a saved charge density. Please run a "
               "Single-Point Calculation first.\n\n"
               "GPAW writes single_point.gpw; VASP writes CHGCAR (leave "
               "\"CHGCAR\" ticked under Write in the VASP settings)."));
        return;
    }

    ElectronicBandsWizard wizard(doc->structure, this);
    wizard.setDensityBaselines(baselines);
    runSimulationWizard(wizard, tr("Electronic Structure"), /*expectFrames=*/false);
}

void MainWindow::openBandResults(const QString& directory)
{
    auto* window = new BandPdosWindow(directory, this);
    if (!window->hasData()) {
        delete window;
        QMessageBox::information(this, tr("Electronic Structure Viewer"),
                                 tr("No bands.json found in %1").arg(directory));
        return;
    }
    window->setAttribute(Qt::WA_DeleteOnClose);
    window->show();
}



void MainWindow::openMolecularDynamicsResults(const QString& directory)
{
    auto* viewer = new MolecularDynamicsViewer(this);
    viewer->setAttribute(Qt::WA_DeleteOnClose);
    if (!viewer->loadDirectory(directory)) {
        delete viewer;
        QMessageBox::information(
            this, tr("Molecular Dynamics Viewer"),
            tr("No MD metrics or trajectory found in %1.").arg(directory));
        return;
    }

    // Playback lives on the main viewport's timeline: the run's frames open
    // as a scrubbable workspace tab, and the global slider + play/pause
    // drive the 3D rendering — the viewer itself only analyzes. The tab is
    // labeled by job directory so re-opening the same results re-focuses it
    // instead of stacking copies.
    if (!viewer->frames().empty()) {
        const QString label = tr("%1 — MD trajectory")
                                  .arg(QFileInfo(directory).fileName());
        int existing = -1;
        for (std::size_t i = 0; i < documents_.size(); ++i)
            if (documents_[i]->fileName == label) {
                existing = static_cast<int>(i);
                break;
            }
        if (existing >= 0) {
            tabBar_->setCurrentIndex(existing);
        } else {
            const auto& frames = viewer->frames();
            const int tab = addDocument(
                std::make_shared<core::Structure>(*frames.front()), label,
                frames, tr("Molecular Dynamics"));
            tabBar_->setCurrentIndex(tab);
            // A finished run is read from its end; land the playhead there.
            showFinalFrame(documents_[static_cast<std::size_t>(tab)].get());
            isDirty_ = true;
        }
    }
    viewer->show();
}

void MainWindow::openModeTrajectory(
    const std::vector<std::shared_ptr<core::Structure>>& frames,
    const QString& label)
{
    if (frames.empty())
        return;
    // A scrubbable multi-frame document, exactly like a loaded trajectory: the
    // timeline, the vector overlay and the export tooling all work on it
    // without a special case for "this one came from a phonon mode".
    const int tab = addDocument(
        std::make_shared<core::Structure>(*frames.front()), label, frames,
        tr("Vibrational Mode"));
    tabBar_->setCurrentIndex(tab);
    isDirty_ = true;
    statusBar()->showMessage(
        tr("Opened %1 as a %2-frame trajectory — enable Representation → "
           "Vector overlay → Force to see the restoring forces")
            .arg(label)
            .arg(static_cast<int>(frames.size())));
}

void MainWindow::openPhononResults(const QString& directory)
{
    // The active document's structure is what the phonons were computed for,
    // so hand it (and the viewport) over — that is what "Vibrational
    // Analysis…" animates on.
    Document* doc = currentDocument();
    auto* window = new PhononPlotWindow(
        directory, this, doc ? doc->structure : nullptr, viewport_);
    connect(window, &PhononPlotWindow::modeTrajectoryRequested, this,
            &MainWindow::openModeTrajectory);
    if (!window->hasData()) {
        delete window;
        QMessageBox::information(
            this, tr("Phonon Viewer"),
            tr("No phonon_band.json found in %1").arg(directory));
        return;
    }
    window->setAttribute(Qt::WA_DeleteOnClose);
    window->show();
}


// Completed Quantum ESPRESSO calculations that saved a `.save` directory — the
// baselines Yambo can convert with p2y.
QList<QPair<QString, QString>> MainWindow::espressoBaselines() const
{
    QList<QPair<QString, QString>> baselines;
    for (const auto& [id, record] : processRecords_) {
        if (record.directory.isEmpty())
            continue;
        const QDir dir(record.directory);
        if (!dir.entryList({QStringLiteral("*.save")}, QDir::Dirs).isEmpty())
            baselines.append(
                {tr("#%1 — %2 [Quantum ESPRESSO]").arg(id).arg(record.label),
                 record.directory});
    }
    return baselines;
}

void MainWindow::showGwCalculations()
{
    if (!prepareSimulation(tr("GW Calculations")))
        return;
    // The GPAW engine restarts from the .gpw file itself; Yambo works from the
    // QE .save directory, so the two lists carry different kinds of path.
    const auto gpaw = gpawDensityFiles();
    const auto espresso = espressoBaselines();
    // G0W0 corrects a specific DFT solution, so without one there is nothing
    // to correct — this is a missing prerequisite, not a configuration error.
    if (gpaw.isEmpty() && espresso.isEmpty()) {
        QMessageBox::critical(
            this, tr("GW Calculations"),
            tr("G₀W₀ is a perturbative correction on top of a completed DFT "
               "ground state, so it needs a baseline to correct.\n\n"
               "Run a GPAW Single-Point Calculation (saving its .gpw) for the "
               "GPAW engine, or a Quantum ESPRESSO one (saving its .save "
               "directory) for Yambo."));
        return;
    }

    GwWizard wizard(this);
    wizard.setBaselines(gpaw, espresso);
    runSimulationWizard(wizard, tr("GW Calculations"), /*expectFrames=*/false);
}

void MainWindow::showOptics()
{
    openOpticsWizard(/*twoDimensional=*/false);
}

void MainWindow::show2DOptics()
{
    openOpticsWizard(/*twoDimensional=*/true);
}

void MainWindow::openOpticsWizard(bool twoDimensional)
{
    const QString label =
        twoDimensional ? tr("2D Optics") : tr("Optical Properties");
    Document* doc = currentDocument();
    if (!doc || !doc->structure || doc->structure->empty()
        || !doc->structure->cell().isDefined()) {
        QMessageBox::information(this, label,
                                 tr("Open a periodic structure first."));
        return;
    }
    if (!ensureAseAvailable())
        return;

    // The GPAW path inherits a completed single point's .gpw; without one
    // the wizard opens on the self-contained VASP engine instead of refusing
    // (it disables the GPAW option and says why).
    const auto baselines = gpawDensityFiles();

    OpticsWizard wizard(doc->structure, twoDimensional, this);
    wizard.setDensityBaselines(baselines);
    runSimulationWizard(wizard, label, /*expectFrames=*/false);
}

void MainWindow::showNonlinearOptics()
{
    if (!prepareSimulation(tr("Nonlinear Optics")))
        return;
    Document* doc = currentDocument();
    // A periodic cell is a hard requirement, not a preference: χ⁽²⁾ is a bulk
    // susceptibility obtained from a sum over Brillouin-zone k-points, and
    // there is no zone without a lattice. Cheaper to say so here than after
    // the ground state has converged.
    if (doc && doc->structure && !doc->structure->cell().isDefined()) {
        QMessageBox::information(
            this, tr("Nonlinear Optics"),
            tr("The nonlinear susceptibility is a Brillouin-zone integral over "
               "the band structure, so it is defined only for a periodic "
               "crystal — this structure has no unit cell.\n\n"
               "Build or import a periodic cell first."));
        return;
    }
    // No baseline selector: this module converges its own ground state,
    // because gpaw.nlopt asserts that point-group symmetry is off and its band
    // sums need a converged empty manifold — neither of which a Single-Point
    // baseline generally has.
    NonlinearOpticsWizard wizard(doc ? doc->structure : nullptr, this);
    runSimulationWizard(wizard, tr("Nonlinear Optics"),
                        /*expectFrames=*/false);
}

void MainWindow::openNonlinearOpticsResults(const QString& directory)
{
    auto* window = new NonlinearOpticsResultsWindow(this);
    window->setAttribute(Qt::WA_DeleteOnClose);
    if (!window->loadResults(directory)) {
        delete window;
        QMessageBox::information(
            this, tr("Nonlinear Optics"),
            tr("No nlopt.json with a usable spectrum found in %1.")
                .arg(directory));
        return;
    }
    window->show();
}

void MainWindow::openOpticsResults(const QString& directory)
{
    auto* window = new OpticsResultsWindow(directory, this);
    if (!window->hasData()) {
        delete window;
        QMessageBox::information(this, tr("Optical Properties"),
                                 tr("No optics.json found in %1").arg(directory));
        return;
    }
    window->setAttribute(Qt::WA_DeleteOnClose);
    window->show();
}

void MainWindow::openGwResults(const QString& directory)
{
    auto* window = new GwResultsWindow(this);
    if (!window->loadResults(directory + QStringLiteral("/gw.json"))) {
        delete window;
        QMessageBox::information(this, tr("GW Calculations"),
                                 tr("No gw.json found in %1").arg(directory));
        return;
    }
    window->setAttribute(Qt::WA_DeleteOnClose);
    window->show();
}

void MainWindow::show2DBands()
{
    Document* doc = currentDocument();
    if (!doc || !doc->structure || doc->structure->empty()
        || !doc->structure->cell().isDefined()) {
        QMessageBox::information(this, tr("2D Bands"),
                                 tr("Open a periodic structure first."));
        return;
    }
    if (!ensureAseAvailable())
        return;

    // GPAW only, and a saved .gpw specifically: the method is
    // calc.fixed_density() on the stored wavefunctions. A VASP CHGCAR — which
    // the 1D Electronic Structure wizard accepts, since VASP can restart from
    // one with ICHARG = 11 — cannot serve here.
    const QList<QPair<QString, QString>> baselines = gpawDensityFiles();
    if (baselines.isEmpty()) {
        QMessageBox::critical(
            this, tr("2D Bands"),
            tr("2D band surfaces are evaluated non-self-consistently on a "
               "converged density, so a completed GPAW Single-Point "
               "Calculation is required first.\n\n"
               "Run one with wavefunction export enabled — it writes "
               "single_point.gpw, which is what this restarts from."));
        return;
    }

    TwoDBandsWizard wizard(doc->structure, this);
    wizard.setDensityBaselines(baselines);
    runSimulationWizard(wizard, tr("2D Bands"), /*expectFrames=*/false);
}

void MainWindow::showChargedDefects()
{
    if (!prepareSimulation(tr("Charged Defects")))
        return;
    Document* doc = currentDocument();

    // TWO completed single points are needed, and they must be different runs:
    // the pristine host and the neutral defect. One is not enough, and the
    // wizard cannot tell them apart on its own — nothing in a .gpw says
    // "this one has the vacancy".
    const auto baselines = gpawDensityFiles();
    if (baselines.size() < 2) {
        QMessageBox::critical(
            this, tr("Charged Defects"),
            tr("A defect formation energy is a difference between two "
               "supercells, so this needs <b>two</b> completed GPAW "
               "Single-Point Calculations that saved their wavefunctions:\n\n"
               "  • the pristine host supercell;\n"
               "  • the same supercell containing the neutral defect.\n\n"
               "%1 found. Run the missing one first — same cell, same "
               "settings, so that everything except the defect is identical.")
                .arg(baselines.isEmpty() ? tr("None was")
                                         : tr("Only one was")));
        return;
    }

    DefectWizard wizard(doc ? doc->structure : nullptr, this);
    wizard.setDensityBaselines(baselines);
    runSimulationWizard(wizard, tr("Charged Defects"), /*expectFrames=*/false);
}

void MainWindow::show2DChargedDefects()
{
    if (!prepareSimulation(tr("Charged Defects in 2D Materials")))
        return;
    Document* doc = currentDocument();

    // Same two inherited runs as the bulk module, for the same reason: a
    // formation energy is a difference between two supercells, and nothing in
    // a .gpw says which one has the vacancy.
    const auto baselines = gpawDensityFiles();
    if (baselines.size() < 2) {
        QMessageBox::critical(
            this, tr("Charged Defects in 2D Materials"),
            tr("A defect formation energy is a difference between two "
               "supercells, so this needs <b>two</b> completed GPAW "
               "Single-Point Calculations that saved their wavefunctions:\n\n"
               "  • the pristine monolayer supercell;\n"
               "  • the same supercell containing the neutral defect.\n\n"
               "%1 found. Run the missing one first — same cell, same vacuum, "
               "same settings, so that everything except the defect is "
               "identical.")
                .arg(baselines.isEmpty() ? tr("None was")
                                         : tr("Only one was")));
        return;
    }

    Defect2dWizard wizard(doc ? doc->structure : nullptr, this);
    wizard.setDensityBaselines(baselines);
    runSimulationWizard(wizard, tr("Charged Defects in 2D Materials"),
                        /*expectFrames=*/false);
}

void MainWindow::open2DChargedDefectResults(const QString& directory)
{
    // The 2D run writes the bulk module's schema under its own file name, so
    // the same diagram window renders it; only the correction block differs,
    // and it names its own scheme.
    auto* viewer = new DefectDiagramWindow(this);
    viewer->setAttribute(Qt::WA_DeleteOnClose);
    if (!viewer->loadResults(directory
                             + QStringLiteral("/charged_defects_2d.json"))) {
        delete viewer;
        QMessageBox::warning(
            this, tr("Charged Defects in 2D Materials"),
            tr("Could not read charged_defects_2d.json in %1.").arg(directory));
        return;
    }
    viewer->show();
}

void MainWindow::openChargedDefectResults(const QString& directory)
{
    auto* viewer = new DefectDiagramWindow(this);
    viewer->setAttribute(Qt::WA_DeleteOnClose);
    if (!viewer->loadResults(directory
                             + QStringLiteral("/charged_defects.json"))) {
        delete viewer;
        QMessageBox::warning(
            this, tr("Charged Defects"),
            tr("Could not read charged_defects.json in %1.").arg(directory));
        return;
    }
    viewer->show();
}

void MainWindow::openFermiSurfaceResults(const QString& directory)
{
    auto* viewer = new FermiSurfaceWindow(this);
    viewer->setAttribute(Qt::WA_DeleteOnClose);
    if (!viewer->loadResults(directory + QStringLiteral("/fermi_surface.json"))) {
        delete viewer;
        QMessageBox::warning(
            this, tr("Fermi Surface"),
            tr("Could not read fermi_surface.json in %1.").arg(directory));
        return;
    }
    viewer->show();
}

void MainWindow::openTopologyResults(const QString& directory)
{
    auto* viewer = new TopologyWindow(this);
    viewer->setAttribute(Qt::WA_DeleteOnClose);
    if (!viewer->loadResults(directory + QStringLiteral("/topology.json"))) {
        delete viewer;
        QMessageBox::warning(
            this, tr("Topological Invariants"),
            tr("Could not read topology.json in %1.").arg(directory));
        return;
    }
    viewer->show();
}

void MainWindow::open2DBandsResults(const QString& directory)
{
    auto* viewer = new TwoDBandsWindow(this);
    viewer->setAttribute(Qt::WA_DeleteOnClose);
    if (!viewer->loadResults(directory + QStringLiteral("/bands_2d.json"))) {
        delete viewer;
        QMessageBox::warning(
            this, tr("2D Bands"),
            tr("Could not read bands_2d.json in %1.").arg(directory));
        return;
    }
    viewer->show();
}

void MainWindow::show2DWorkfunction()
{
    Document* doc = currentDocument();
    if (!doc || !doc->structure || doc->structure->empty()
        || !doc->structure->cell().isDefined()) {
        QMessageBox::information(this, tr("2D Workfunction"),
                                 tr("Open a periodic structure first."));
        return;
    }
    if (!ensureAseAvailable())
        return;

    // GPAW only, and a saved .gpw specifically: the method is reading
    // calc.get_electrostatic_potential() and the Fermi level back off the
    // stored ground state — there is nothing to compute without one.
    const QList<QPair<QString, QString>> baselines = gpawDensityFiles();
    if (baselines.isEmpty()) {
        QMessageBox::critical(
            this, tr("2D Workfunction"),
            tr("The work function Φ = E_vac − E_F is read off a completed "
               "ground state — its electrostatic potential and Fermi level — "
               "so a completed GPAW Single-Point Calculation is required "
               "first.\n\n"
               "Run one with wavefunction export enabled — it writes "
               "single_point.gpw, which is what this reads."));
        return;
    }

    WorkfunctionWizard wizard(doc->structure, this);
    wizard.setDensityBaselines(baselines);
    runSimulationWizard(wizard, tr("2D Workfunction"), /*expectFrames=*/false);
}

void MainWindow::openWorkfunctionResults(const QString& directory)
{
    auto* viewer = new WorkfunctionWindow(this);
    viewer->setAttribute(Qt::WA_DeleteOnClose);
    if (!viewer->loadResults(directory + QStringLiteral("/workfunction.json"))) {
        delete viewer;
        QMessageBox::warning(
            this, tr("2D Workfunction"),
            tr("Could not read workfunction.json in %1.").arg(directory));
        return;
    }
    viewer->show();
}

void MainWindow::adoptSinglePointResults(const QString& directory)
{
    Document* doc = currentDocument();
    if (!doc || !doc->structure)
        return;
    const QString path = directory + QStringLiteral("/single_point.extxyz");
    if (!QFile::exists(path))
        return; // a run from an older Calango, or a non-ASE backend

    core::Structure converged;
    try {
        converged = pybridge::AseBridge::readStructure(path.toStdString());
    } catch (const std::exception&) {
        return; // the summary viewer still opens; the overlay simply stays as-is
    }
    // Same system, or nothing doing. Writing one run's per-atom results onto a
    // different structure would be silently, confidently wrong.
    if (converged.size() != doc->structure->size())
        return;

    // Only the computed columns. `initial_magmoms` is deliberately NOT copied:
    // it is the user's input guess, it is already on the structure, and
    // overwriting it with what ASE echoed back would quietly redefine the seed
    // for the next run.
    int adopted = 0;
    for (const auto& [name, values] : converged.scalarFields()) {
        if (name == "initial_magmoms")
            continue;
        doc->structure->setScalarField(name, values);
        ++adopted;
    }
    for (const auto& [name, vectors] : converged.vectorFields()) {
        if (name == "initial_magmoms")
            continue;
        doc->structure->setVectorField(name, vectors);
    }
    if (adopted == 0)
        return;

    // frameCamera=false: the atoms have not moved, so re-framing the view
    // after a single point would be an unexplained camera jump.
    notifyStructureChanged(/*frameCamera=*/false);
    if (converged.hasVectorData("magmoms"))
        statusBar()->showMessage(
            tr("Converged magnetic moments loaded — draw them with Spatial "
               "References → Vectors → Magnetic moment"),
            8000);
}

void MainWindow::openSinglePointResults(const QString& directory)
{
    auto* viewer = new SinglePointViewer(this);
    viewer->setAttribute(Qt::WA_DeleteOnClose);
    if (!viewer->loadResults(directory + QStringLiteral("/single_point.json"))) {
        delete viewer;
        return;
    }
    connect(viewer, &SinglePointViewer::getVolumetricDataRequested, this,
            &MainWindow::onGetVolumetricData);
    viewer->show();
}

void MainWindow::openCutoffConvergenceResults(const QString& directory)
{
    auto* window = new ConvergenceResultsWindow(
        ConvergenceResultsWindow::Sweep::PlaneWaveCutoff, directory, this);
    if (!window->hasData()) {
        delete window;
        statusBar()->showMessage(
            tr("Could not read cutoff_convergence.json in %1.")
                .arg(directory));
        return;
    }
    window->setAttribute(Qt::WA_DeleteOnClose);
    window->show();
}

void MainWindow::openKpointsConvergenceResults(const QString& directory)
{
    auto* window = new ConvergenceResultsWindow(
        ConvergenceResultsWindow::Sweep::KpointGrid, directory, this);
    if (!window->hasData()) {
        delete window;
        statusBar()->showMessage(
            tr("Could not read kpoints_convergence.json in %1.")
                .arg(directory));
        return;
    }
    window->setAttribute(Qt::WA_DeleteOnClose);
    window->show();
}

void MainWindow::openRandomNoiseResults(const QString& directory)
{
    auto* window = new RandomNoiseViewer(directory, this);
    if (!window->hasData()) {
        delete window;
        statusBar()->showMessage(
            tr("Could not read random_noise.json in %1.").arg(directory));
        return;
    }
    window->setAttribute(Qt::WA_DeleteOnClose);
    window->show();
    // The ensemble itself opens in a tab as well: the distributions say how
    // wide the spread is, and scrubbing the trajectory is how you see what a
    // member at the edge of it actually looks like.
    if (const QString trajectory =
            directory + QStringLiteral("/noise_singlepoint.extxyz");
        QFile::exists(trajectory)) {
        loadFile(trajectory);
    }
}

int MainWindow::registerDensityCubes(const QString& directory)
{
    if (!volumetricPanel_)
        return 0;
    // The file -> display-label mapping lives in GuiUtils, keyed off the same
    // core::densityFiles constants the generators emit, so the two ends cannot
    // drift apart again. Anything unrecognized keeps its own file name rather
    // than being skipped — a hand-dropped cube is still a grid the user wants
    // to see.
    Document* doc = currentDocument();
    const QString structLabel = (doc && doc->structure)
        ? QString::fromStdString(doc->structure->chemicalFormula())
        : QString();

    const QDir dir(directory);
    int added = 0;
    // Cube files by extension, VASP grids by name. CHG (the coarse
    // every-step dump) is deliberately not offered: it is the same field as
    // CHGCAR at lower resolution, and two near-identical entries in the dock
    // is worse than one.
    QStringList names =
        dir.entryList({QStringLiteral("*.cube")}, QDir::Files, QDir::Name);
    for (const QString& vasp : {QStringLiteral("CHGCAR"),
                                QStringLiteral("AECCAR0"),
                                QStringLiteral("AECCAR2"),
                                QStringLiteral("LOCPOT"),
                                QStringLiteral("ELFCAR")}) {
        // Non-empty: VASP creates the file whether or not the tag asked for
        // it, and a zero-byte CHGCAR is not a density.
        if (QFileInfo(dir.filePath(vasp)).size() > 0)
            names << vasp;
    }
    for (const QString& name : names) {
        // The Wannier orbitals have their own registration path (they are
        // named and numbered from wannier.json), so they are not swept up here
        // as a pile of anonymous grids.
        if (name.startsWith(QStringLiteral("wannier_")))
            continue;
        volumetricPanel_->registerResultFile(dir.filePath(name),
                                             volumetricDisplayName(name),
                                             structLabel);
        ++added;
    }
    return added;
}

QString MainWindow::pythonForEngine(core::CalculatorKind kind) const
{
    QString env = EnginePresets::envFor(kind);
    if (env.trimmed().isEmpty())
        env = QSettings().value(QLatin1String(SettingsManager::kEnvironmentPath))
                  .toString();
    const QString resolved = CondaEnvs::resolvePython(env);
    if (!resolved.isEmpty())
        return resolved;
    return QString::fromStdString(pybridge::PythonEngine::instance().executable());
}

void MainWindow::onGetVolumetricData(const QString& directory)
{
    if (!volumetricPanel_)
        return;

    // Whatever the run already wrote goes straight in. A single-point with the
    // density exports enabled produces up to six cubes, and re-deriving any of
    // them from the .gpw would be a needless second GPAW start-up.
    if (const int added = registerDensityCubes(directory); added > 0) {
        statusBar()->showMessage(
            tr("%n volumetric field(s) added to the Volumetric Data dock.",
               nullptr, added),
            6000);
        volumetricDock_->show();
        volumetricDock_->raise();
        return;
    }

    // Otherwise export it from the saved GPAW wavefunctions (.gpw) as a job;
    // onJobFinished registers the resulting density.cube.
    const QDir dir(directory);
    if (dir.entryList({QStringLiteral("*.gpw")}, QDir::Files).isEmpty()) {
        QMessageBox::information(
            this, tr("Get Volumetric Data"),
            tr("This run saved no charge density. Re-run the Single-Point with "
               "\"Export Charge Density\" enabled (GPAW), or from a run that "
               "wrote a .gpw file."));
        return;
    }
    // All-electron by default; the pseudo/all-electron choice was made at run
    // time and is not recoverable post-hoc, so use the fuller density.
    const QString script = QString::fromStdString(
        core::AseScriptGenerator::densityCubeScript(directory.toStdString(),
                                                    /*allElectron=*/true));
    // The GPAW environment, NOT the embedded interpreter: this script does
    // `from gpaw import GPAW`, and the embedded runtime has no gpaw in it.
    runScript(script, pythonForEngine(core::CalculatorKind::Gpaw),
              tr("Charge Density Export"), /*expectFrames=*/false);
}

void MainWindow::registerWannierOrbitals(const QString& directory)
{
    if (!volumetricPanel_)
        return;
    QFile file(directory + QStringLiteral("/wannier.json"));
    if (!file.open(QIODevice::ReadOnly))
        return;
    const QJsonObject root = QJsonDocument::fromJson(file.readAll()).object();
    Document* doc = currentDocument();
    const QString structLabel = (doc && doc->structure)
        ? QString::fromStdString(doc->structure->chemicalFormula())
        : QString();

    // Prefer the cube filenames recorded in wannier.json; fall back to the
    // conventional wannier_<n>.cube naming keyed off the centres count.
    QStringList cubes;
    for (const QJsonValue& c : root.value(QStringLiteral("cubes")).toArray())
        cubes << c.toString();
    if (cubes.isEmpty()) {
        const int n = root.value(QStringLiteral("centers")).toArray().size();
        for (int i = 0; i < n; ++i)
            cubes << QStringLiteral("wannier_%1.cube").arg(i);
    }
    int index = 0;
    for (const QString& name : cubes) {
        if (name.isEmpty())
            continue;
        const QString path = QFileInfo(name).isAbsolute()
            ? name
            : directory + QLatin1Char('/') + name;
        if (QFile::exists(path))
            volumetricPanel_->registerResultFile(
                path, tr("Wannier ψ%1").arg(index), structLabel);
        ++index;
    }
}

void MainWindow::openMlwfResults(const QString& directory)
{
    // Auto-register each Wannier orbital's real-space mesh in the Volumetric
    // Data dock so it can be visualized on demand (spec: MLWF volume pipeline).
    registerWannierOrbitals(directory);

    // The MLWF viewer overlays orbital isosurfaces on the main viewport. The
    // post-processes that consume the run (Wannier Interpolation, Fermi
    // Surface, Topological Invariants) are standalone Electronics-menu modules.
    Document* doc = currentDocument();
    auto* viewer = new MlwfViewer(doc ? doc->structure : nullptr, viewport_,
                                  this);
    viewer->setAttribute(Qt::WA_DeleteOnClose);
    viewer->show();
    viewer->loadResults(directory + QStringLiteral("/wannier.json"));
}

std::vector<MainWindow::ViewerEntry> MainWindow::viewersFor(
    const QString& directory) const
{
    if (directory.isEmpty())
        return {};
    // The marker file IS the test: a run that wrote gw.json produced
    // quasiparticle data, whatever the process was labelled. That keeps this
    // honest when a directory is reused or a job is renamed.
    static const std::vector<ViewerEntry> kAll = {
        {"single_point.json", tr("Single-Point Viewer"),
         &MainWindow::openSinglePointResults},
        {"cutoff_convergence.json", tr("Cutoff Convergence Viewer"),
         &MainWindow::openCutoffConvergenceResults},
        {"kpoints_convergence.json", tr("K-points Convergence Viewer"),
         &MainWindow::openKpointsConvergenceResults},
        {"geometry_optimization.json", tr("Geometry Optimization Viewer"),
         &MainWindow::openGeometryOptimizationResults},
        {"born_charges.json", tr("Born Effective Charges Viewer"),
         &MainWindow::openBornChargesResults},
        {"raman_ir.json", tr("Raman / IR Spectroscopy Viewer"),
         &MainWindow::openRamanIrResults},
        {"nlopt.json", tr("Nonlinear Optics Viewer"),
         &MainWindow::openNonlinearOpticsResults},
        {"wannier.json", tr("Wannier Functions Viewer"), &MainWindow::openMlwfResults},
        {"bands_2d.json", tr("2D Band Surfaces Viewer"),
         &MainWindow::open2DBandsResults},
        {"workfunction.json", tr("2D Workfunction Viewer"),
         &MainWindow::openWorkfunctionResults},
        {"charged_defects.json", tr("Charged Defect Diagram"),
         &MainWindow::openChargedDefectResults},
        {"charged_defects_2d.json", tr("2D Charged Defect Diagram"),
         &MainWindow::open2DChargedDefectResults},
        {"fermi_surface.json", tr("Fermi Surface Viewer"),
         &MainWindow::openFermiSurfaceResults},
        {"topology.json", tr("Topological Invariants Viewer"),
         &MainWindow::openTopologyResults},
        {"gw.json", tr("GW Viewer"), &MainWindow::openGwResults},
    };
    std::vector<ViewerEntry> available;
    for (const ViewerEntry& entry : kAll)
        if (QFile::exists(directory + QLatin1Char('/')
                          + QLatin1String(entry.resultFile)))
            available.push_back(entry);

    // Molecular dynamics has no single summary file — the viewer takes the
    // directory and finds metrics or a trajectory in it — so it is offered
    // whenever one of those is present.
    for (const auto* marker : {"metrics.json", "md.traj", "md.extxyz"}) {
        if (QFile::exists(directory + QLatin1Char('/')
                          + QLatin1String(marker))) {
            available.push_back({"", tr("Molecular Dynamics Viewer"),
                                 &MainWindow::openMolecularDynamicsResults});
            break;
        }
    }
    return available;
}

void MainWindow::onProcessContextMenu(const QString& directory,
                                      const QPoint& globalPos)
{
    QMenu menu(this);
    // Viewers first: they are the reason the menu exists, and a completed run
    // is far more often opened than deleted.
    for (const ViewerEntry& entry : viewersFor(directory)) {
        const auto opener = entry.open;
        menu.addAction(entry.label, this,
                       [this, opener, directory] { (this->*opener)(directory); });
    }
    if (!menu.isEmpty())
        menu.addSeparator();
    menu.addAction(tr("Load Result into Workspace"), this,
                   [this, directory] { onProcessResultRequested(directory); });
    menu.addAction(tr("View ASE Script…"), this,
                   [this, directory] { onViewScriptRequested(directory); });
    menu.addAction(tr("Open Folder"), this, [directory] {
        QDesktopServices::openUrl(QUrl::fromLocalFile(directory));
    });
    menu.exec(globalPos);
}

void MainWindow::openGeometryOptimizationResults(const QString& directory)
{
    auto* viewer = new GeometryOptimizationViewer(this);
    viewer->setAttribute(Qt::WA_DeleteOnClose);
    if (!viewer->loadResults(directory
                             + QStringLiteral("/geometry_optimization.json"))) {
        delete viewer;
        return;
    }
    connect(viewer, &GeometryOptimizationViewer::getVolumetricDataRequested,
            this, &MainWindow::onGetVolumetricData);
    viewer->show();
}

void MainWindow::resetCamera()
{
    // A stored default wins over auto-framing: a user who has said "this is
    // how I want structures to look" means it for every structure, and the
    // fitted view they would otherwise get is exactly what they overrode.
    const render::PointOfView pov = PointOfViewDialog::defaultPointOfView();
    if (!pov.valid) {
        viewport_->frameStructure();
        return;
    }
    viewport_->setPointOfView(pov);
    // The stored view carries its projection, so the toolbar's Orthographic
    // toggle has to follow it — a checked box over a perspective view is worse
    // than no box at all. Blocked: the toggle drives the camera the other way.
    const bool ortho =
        pov.projection == render::CameraProjection::Orthographic;
    if (orthoAction_->isChecked() != ortho) {
        const QSignalBlocker blocker(orthoAction_);
        orthoAction_->setChecked(ortho);
    }
    statusBar()->showMessage(tr("Default point-of-view restored"), 2000);
}

void MainWindow::showPointOfView()
{
    // Modeless and single-instance: it edits the camera live, so it has to stay
    // open while the user orbits, and a second copy would fight the first over
    // the same controls.
    if (povDialog_) {
        povDialog_->show();
        povDialog_->raise();
        povDialog_->activateWindow();
        return;
    }
    povDialog_ = new PointOfViewDialog(viewport_, this);
    povDialog_->setAttribute(Qt::WA_DeleteOnClose);
    connect(povDialog_, &QObject::destroyed, this,
            [this] { povDialog_ = nullptr; });
    povDialog_->show();
}

void MainWindow::rememberCastOpacities()
{
    // Snapshot of what the Representation panel had set, so the film can
    // animate opacity without owning it. Taken when film mode starts and
    // handed back when it ends.
    filmCastBaseline_.clear();
    const auto& style = viewport_->style();
    for (int cast = 0; cast < style.castCount(); ++cast)
        filmCastBaseline_[cast] = style.castStyle(cast).opacity;
}

void MainWindow::restoreCastOpacities()
{
    if (filmCastBaseline_.empty())
        return;
    auto& style = viewport_->style();
    for (const auto& [cast, opacity] : filmCastBaseline_) {
        if (cast >= style.castCount())
            continue; // the structure changed under us; nothing to restore
        render::StructureRenderer::CastStyle value = style.castStyle(cast);
        value.opacity = opacity;
        style.setCastStyle(cast, value);
    }
    filmCastBaseline_.clear();
    viewport_->styleChanged(true);
}

void MainWindow::showFilmProduction()
{
    // Modeless and single-instance, like the point-of-view dialog it feeds
    // from: every edit republishes the film, and watching the transition in
    // the viewport is the only way to judge it.
    if (filmDialog_) {
        filmDialog_->show();
        filmDialog_->raise();
        filmDialog_->activateWindow();
        return;
    }
    Document* doc = currentDocument();
    if (!doc) {
        statusBar()->showMessage(tr("Open a structure first."));
        return;
    }
    filmDialog_ = new FilmProductionDialog(viewport_, doc->film, this);
    filmDialog_->setAttribute(Qt::WA_DeleteOnClose);
    connect(filmDialog_, &QObject::destroyed, this,
            [this] { filmDialog_ = nullptr; });
    // Shots pick their overlays from the dock's list by id, so the dialog has
    // to be told what is in it — now and whenever it changes underneath.
    if (overlayPanel_) {
        filmDialog_->setAvailableOverlays(overlayPanel_->entries());
        connect(overlayPanel_, &OverlayPanel::overlaysChanged, filmDialog_,
                [this] {
                    if (filmDialog_ && overlayPanel_)
                        filmDialog_->setAvailableOverlays(
                            overlayPanel_->entries());
                });
    }
    connect(filmDialog_, &FilmProductionDialog::scriptChanged, this,
            [this](const render::FilmScript& script) {
                if (Document* current = currentDocument())
                    current->film = script;
                filmTimeline_->setFilm(script.effectiveDuration(), script.fps);
            });
    connect(filmDialog_, &FilmProductionDialog::previewRequested, this, [this] {
        // Previewing implies film mode: the film cannot drive a camera the
        // trajectory timeline is still steering.
        if (!filmModeAction_->isChecked())
            filmModeAction_->setChecked(true);
        filmTimeline_->stop(); // rewind, so Preview always plays from the top
        filmTimeline_->play();
    });
    refreshFilmTimeline();
    filmDialog_->show();
}

void MainWindow::refreshFilmTimeline()
{
    Document* doc = currentDocument();
    if (!doc)
        return;
    // The trajectory's natural rate is the one the trajectory timeline plays
    // it at, so "Trajectory priority" means the length the user already sees
    // when they scrub it — not a rate invented here.
    const int frames = doc->frames.size() > 1
        ? static_cast<int>(doc->frames.size())
        : 0;
    doc->film.trajectoryFrames = frames;
    if (doc->film.trajectoryFps <= 0.0)
        doc->film.trajectoryFps = kTrajectoryPlaybackFps;
    if (filmDialog_) {
        // Order matters: the film first (it is what a tab switch changes),
        // then the trajectory it has to be reconciled against.
        filmDialog_->setScript(doc->film);
        filmDialog_->setTrajectory(frames, doc->film.trajectoryFps);
    }
    filmTimeline_->setFilm(doc->film.effectiveDuration(), doc->film.fps);
}

void MainWindow::setFilmMode(bool on)
{
    Document* doc = currentDocument();
    if (on) {
        if (!doc || !doc->film.isValid()) {
            // Nothing to play. Bounce the toggle rather than showing an empty
            // timeline that moves nothing, and point at the dialog that fixes
            // it — this is the state every first-time user arrives in.
            const QSignalBlocker blocker(filmModeAction_);
            filmModeAction_->setChecked(false);
            statusBar()->showMessage(
                tr("No film in this workspace yet — add at least one shot in "
                   "Film production…"));
            showFilmProduction();
            return;
        }
        // Remember the working view so switching film mode off puts it back.
        preFilmPov_ = viewport_->camera().pointOfView();
        rememberCastOpacities();
        timeline_->stop();
        timeline_->hide();
        refreshFilmTimeline();
        filmTimeline_->show();
        showFilmTime(filmTimeline_->currentTime());
        statusBar()->showMessage(
            tr("Film mode — scrub or play the timeline to preview the film."));
        return;
    }

    filmTimeline_->stop();
    filmTimeline_->hide();
    viewport_->setFilmFade(1.0f);
    viewport_->clearFilmCrossfade();
    filmCrossfadeCache_ = QImage();
    filmCrossfadeKey_ = CrossfadeKey{};
    // Cast opacities are the film's to animate but the panel's to own, so they
    // are handed back exactly as they were rather than left where the last
    // previewed frame put them. Overlays are handed back the same way.
    restoreCastOpacities();
    if (overlayPanel_)
        overlayPanel_->setFilmOverlayFilter(nullptr);
    if (preFilmPov_.valid) {
        restoringPointOfView_ = true;
        viewport_->setPointOfView(preFilmPov_);
        restoringPointOfView_ = false;
    }
    if (doc && doc->frames.size() > 1) {
        timeline_->setFrameCount(static_cast<int>(doc->frames.size()));
        timeline_->show();
    }
}

void MainWindow::showFilmTime(double seconds)
{
    Document* doc = currentDocument();
    if (!doc || !filmModeAction_->isChecked() || !doc->film.isValid())
        return;

    const render::FilmSample sample = render::sampleFilm(doc->film, seconds);

    // Trajectory first: moving the camera onto a frame that is about to be
    // replaced would show one frame of the previous geometry from the new
    // angle.
    if (sample.trajectoryFrame >= 0
        && sample.trajectoryFrame < static_cast<int>(doc->frames.size())) {
        const auto& frame =
            doc->frames[static_cast<std::size_t>(sample.trajectoryFrame)];
        if (doc->structure != frame) {
            doc->structure = frame;
            notifyStructureChanged(/*frameCamera=*/false);
        }
    }

    // Cast opacities: the film only names the casts it animates, so the rest
    // keep whatever the Representation panel set.
    // A dissolve needs the OUTGOING side rendered before the live view is
    // moved onto the incoming shot, and both sides carry their own cast
    // opacities — so it is captured first, while the cache key still describes
    // what is on screen.
    if (sample.crossfading) {
        const CrossfadeKey key{
            sample.trajectoryFrame, doc->structure.get(),
            viewport_->size() * viewport_->devicePixelRatioF()};
        if (!(key == filmCrossfadeKey_) || filmCrossfadeCache_.isNull()) {
            filmCrossfadeCache_ =
                renderFilmShot(sample.crossfadeFrom, sample.crossfadeFromCastOpacity);
            filmCrossfadeKey_ = key;
        }
        viewport_->setFilmCrossfade(filmCrossfadeCache_, sample.crossfadeWeight);
    } else if (!filmCrossfadeCache_.isNull()) {
        filmCrossfadeCache_ = QImage();
        filmCrossfadeKey_ = CrossfadeKey{};
        viewport_->clearFilmCrossfade();
    }

    // Every frame starts from the panel's values and applies the film's
    // overrides on top, rather than editing whatever the previous frame left
    // behind: a ramp that only ever wrote would never come back up.
    applyFilmCastOpacities(sample.castOpacity);

    // Per-shot overlays. A film that never sets them leaves the dock alone,
    // so nothing changes for a film authored before this existed.
    if (overlayPanel_) {
        overlayPanel_->setFilmOverlayFilter(
            sample.overridesOverlays ? &sample.overlayIds : nullptr);
    }

    applyingFilm_ = true;
    viewport_->setPointOfView(sample.camera);
    applyingFilm_ = false;
    viewport_->setFilmFade(sample.fade);
}

bool MainWindow::applyFilmCastOpacities(
    const std::vector<render::FilmCastOpacity>& casts)
{
    auto& style = viewport_->style();
    std::map<int, float> wanted = filmCastBaseline_;
    for (const render::FilmCastOpacity& entry : casts) {
        if (entry.cast >= 0 && entry.cast < style.castCount())
            wanted[entry.cast] = entry.opacity;
    }
    // styleChanged(true) rebuilds every instance buffer, so it must not fire on
    // frames where nothing moved — most of a film's frames only move the
    // camera, and a rebuild per frame would cost the playback its frame rate
    // for no visible difference.
    bool changed = false;
    for (const auto& [cast, opacity] : wanted) {
        if (cast >= style.castCount())
            continue;
        render::StructureRenderer::CastStyle value = style.castStyle(cast);
        if (qFuzzyCompare(value.opacity + 1.0f, opacity + 1.0f))
            continue;
        value.opacity = opacity;
        style.setCastStyle(cast, value);
        changed = true;
    }
    if (changed)
        viewport_->styleChanged(true);
    return changed;
}

QImage MainWindow::renderFilmShot(
    const render::PointOfView& pov,
    const std::vector<render::FilmCastOpacity>& casts)
{
    // Off-screen render of one dissolve endpoint. The camera has to be moved
    // there and back because renderToImage() shoots from the live camera; the
    // round trip is invisible because nothing repaints in between.
    const render::PointOfView restore = viewport_->camera().pointOfView();
    const bool castsChanged = applyFilmCastOpacities(casts);

    applyingFilm_ = true;
    viewport_->setPointOfView(pov);
    // Capture at DEVICE resolution, not logical: on a HiDPI display a
    // logical-size grab would be scaled up when composited and every dissolve
    // would go visibly soft halfway through.
    const qreal dpr = viewport_->devicePixelRatioF();
    const QSize size = viewport_->size() * dpr;
    QImage image = viewport_->renderToImage(std::max(1, size.width()),
                                            std::max(1, size.height()),
                                            viewport_->backgroundColor());
    image.setDevicePixelRatio(dpr);
    viewport_->setPointOfView(restore);
    applyingFilm_ = false;

    if (castsChanged) {
        // Put the casts back to the baseline; the caller applies the incoming
        // shot's own values immediately afterwards.
        applyFilmCastOpacities({});
    }
    return image;
}

void MainWindow::resetLayout()
{
    if (defaultLayoutState_.isEmpty())
        return;
    restoreState(defaultLayoutState_, kLayoutVersion);
    // restoreState reinstates each dock's recorded visibility, so the branding
    // card returns with the rest. Asserting it makes the guarantee independent
    // of what the snapshot happened to capture — a reset that left the primary
    // panel hidden would read as the reset having failed.
    if (auto* branding = findChild<QDockWidget*>(QStringLiteral("brandingDock")))
        branding->setVisible(true);
    statusBar()->showMessage(tr("Dock layout reset to the default."), 4000);
}

void MainWindow::onDeleteProcessRequested(int id)
{
    const auto it = processRecords_.find(id);
    const QString label = it != processRecords_.end() ? it->second.label
                                                       : tr("this process");
    const bool running = id == currentTaskId_;
    const bool queued =
        std::any_of(jobQueue_.begin(), jobQueue_.end(),
                    [id](const QueuedJob& job) { return job.processId == id; });

    const auto choice = QMessageBox::question(
        this, tr("Delete Process"),
        running
            ? tr("Process #%1 (%2) is still running. Stop it and permanently "
                 "delete its data folder?").arg(id).arg(label)
            : queued
            ? tr("Process #%1 (%2) is queued and has not started. Remove it "
                 "from the queue and permanently delete its data folder?")
                  .arg(id).arg(label)
            : tr("Permanently delete process #%1 (%2) and its data folder?")
                  .arg(id).arg(label),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
    if (choice != QMessageBox::Yes)
        return;

    // Stop the subprocess first so it releases the directory before we purge.
    if (running) {
        jobRunner_->terminate();
        currentTaskId_ = -1; // its finish must not write metrics back
        liveDoc_ = nullptr;
    }
    // Cancelling a job that has not started yet: drop it from the queue, or
    // startNextQueuedJob() would later launch a shell in the directory this
    // function is about to delete.
    if (queued) {
        jobQueue_.erase(std::remove_if(jobQueue_.begin(), jobQueue_.end(),
                                       [id](const QueuedJob& job) {
                                           return job.processId == id;
                                       }),
                        jobQueue_.end());
    }

    // Purge the managed .calango_tmp/proc_<id>/ folder.
    QString directory = it != processRecords_.end() ? it->second.directory
                                                    : QString();
    if (!directory.isEmpty())
        QDir(directory).removeRecursively();

    processRecords_.erase(id);

    // Drop the selector entry; if it was showing, fall back to another process
    // or clear the tabs when none remain.
    if (processSelector_) {
        const int comboIndex = processSelector_->findData(id);
        if (comboIndex >= 0) {
            const QSignalBlocker block(processSelector_);
            processSelector_->removeItem(comboIndex);
        }
        if (processSelector_->count() == 0) {
            selectedProcessId_ = -1;
            energyPlot_->clear();
            temperaturePlot_->clear();
            forcePlot_->clear();
            pressurePlot_->clear();
            jobLogWidget_->restoreLog(QString());
        } else if (selectedProcessId_ == id) {
            processSelector_->setCurrentIndex(processSelector_->count() - 1);
            onProcessSelected(processSelector_->currentIndex());
        }
    }

    processPanel_->removeTask(id);
    statusBar()->showMessage(tr("Deleted process #%1").arg(id));
}

void MainWindow::onProcessResultRequested(const QString& directory)
{
    // A cluster-expansion run produces both a hull and a trajectory: open the
    // hull in its standalone window, then fall through so the optimized
    // structures open in a tab too.
    if (QFile::exists(directory + QStringLiteral("/cluster_expansion.json"))) {
        auto* window = new ConvexHullWindow(directory, this);
        if (window->hasData()) {
            // Double-clicking a configuration jumps the viewport to that frame
            // of the optimized trajectory (once it has been loaded into a tab).
            connect(window, &ConvexHullWindow::frameActivated, this,
                    [this](int frame) {
                        if (Document* doc = currentDocument();
                            doc && frame >= 0
                            && frame < static_cast<int>(doc->frames.size())) {
                            timeline_->setCurrentFrame(frame);
                        } else {
                            statusBar()->showMessage(
                                tr("Load the optimized ensemble (Process panel → "
                                   "Load Result) to inspect configuration %1")
                                    .arg(frame));
                        }
                    });
            window->setAttribute(Qt::WA_DeleteOnClose);
            window->show();
            statusBar()->showMessage(tr("Convex hull analytics opened"));
        } else {
            delete window;
        }
    }
    if (QFile::exists(directory + QStringLiteral("/effective_bands.json"))) {
        auto* window = new EffectiveBandsWindow(directory, this);
        if (window->hasData()) {
            window->setAttribute(Qt::WA_DeleteOnClose);
            window->show();
            statusBar()->showMessage(
                tr("Effective band structure opened from %1").arg(directory));
            return;
        }
        delete window;
    }
    if (QFile::exists(directory + QStringLiteral("/bands.json"))) {
        openBandResults(directory);
        return;
    }
    if (QFile::exists(directory + QStringLiteral("/phonon_band.json"))) {
        openPhononResults(directory);
        return;
    }
    if (QFile::exists(directory + QStringLiteral("/optics.json"))) {
        openOpticsResults(directory);
        return;
    }
    if (QFile::exists(directory + QStringLiteral("/gw.json"))) {
        openGwResults(directory);
        return;
    }
    if (QFile::exists(directory + QStringLiteral("/wannier.json"))) {
        openMlwfResults(directory);
        return;
    }
    if (QFile::exists(directory + QStringLiteral("/bands_2d.json"))) {
        open2DBandsResults(directory);
        return;
    }
    if (QFile::exists(directory + QStringLiteral("/workfunction.json"))) {
        openWorkfunctionResults(directory);
        return;
    }
    if (QFile::exists(directory + QStringLiteral("/charged_defects.json"))) {
        openChargedDefectResults(directory);
        return;
    }
    if (QFile::exists(directory
                      + QStringLiteral("/charged_defects_2d.json"))) {
        open2DChargedDefectResults(directory);
        return;
    }
    if (QFile::exists(directory + QStringLiteral("/topology.json"))) {
        openTopologyResults(directory);
        return;
    }
    if (QFile::exists(directory + QStringLiteral("/fermi_surface.json"))) {
        openFermiSurfaceResults(directory);
        return;
    }
    if (QFile::exists(directory + QStringLiteral("/xas.json"))) {
        openXasResults(directory);
        return;
    }
    if (QFile::exists(directory + QStringLiteral("/cutoff_convergence.json"))) {
        openCutoffConvergenceResults(directory);
        return;
    }
    if (QFile::exists(directory
                      + QStringLiteral("/kpoints_convergence.json"))) {
        openKpointsConvergenceResults(directory);
        return;
    }
    // Before the single-point branch below: a Random Noise run IS a batch of
    // single points, and its directory can carry both files. The ensemble's
    // distributions are what the run was for, and its viewer opens the
    // trajectory in a tab as well, so nothing is lost by preferring it.
    if (QFile::exists(directory + QStringLiteral("/random_noise.json"))) {
        openRandomNoiseResults(directory);
        return;
    }
    if (QFile::exists(directory + QStringLiteral("/single_point.json"))) {
        openSinglePointResults(directory);
        return;
    }
    // Before the raw-trajectory fallback below: a relaxation's own summary is
    // strictly more informative than opening opt.traj as an anonymous
    // trajectory, and the viewer scrubs the same frames anyway.
    if (QFile::exists(directory
                      + QStringLiteral("/geometry_optimization.json"))) {
        openGeometryOptimizationResults(directory);
        return;
    }
    // An MD run is identified by its trajectory: metrics.json alone is written
    // by every task, so keying off that would open this viewer on relaxations
    // too. Placed before the generic trajectory fallback below, which would
    // otherwise open md.extxyz as an anonymous trajectory and drop the metrics.
    for (const auto* mdFile : {"md.extxyz", "md.traj"}) {
        if (QFile::exists(directory + QLatin1Char('/') + QLatin1String(mdFile))) {
            openMolecularDynamicsResults(directory);
            return;
        }
    }
    for (const auto* candidate :
         // md.extxyz first: it carries the per-atom forces and velocities
         // the Vector overlay needs, which the binary md.traj only exposes
         // where the calculator left results attached.
         {"optimized_configs.extxyz", "md.extxyz", "md.traj", "opt.traj",
          "optimized.extxyz", "md_final.extxyz", "perturbed.extxyz"}) {
        const QString path = directory + QLatin1Char('/')
            + QLatin1String(candidate);
        if (QFile::exists(path)) {
            loadFile(path);
            return;
        }
    }
    statusBar()->showMessage(
        tr("No loadable result in %1 yet — try Open Folder").arg(directory));
}

void MainWindow::onViewScriptRequested(const QString& directory)
{
    const QString path = directory + QStringLiteral("/run.py");
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QMessageBox::information(
            this, tr("View ASE Script"),
            tr("No run.py script was found in\n%1").arg(directory));
        return;
    }
    const QString script = QString::fromUtf8(file.readAll());
    auto* viewer = new ScriptViewerDialog(tr("ASE Script Viewer"), script, this);
    viewer->setAttribute(Qt::WA_DeleteOnClose);
    viewer->show();
}

void MainWindow::applyAppearanceTheme()
{
    const ThemeManager::Theme theme = ThemeManager::current();
    const bool dark = ThemeManager::apply(theme);
    if (brandingPanel_)
        brandingPanel_->setDarkVariant(dark);
    if (systemStatusBar_)
        systemStatusBar_->refreshThreads();
    // Icon tints are baked into pixmaps at bind time, so the whole registry is
    // re-rendered against the new palette. The application event filter also
    // catches this, but palette-change events are delivered asynchronously and
    // a theme applied from Preferences should be visible on the way out of
    // this call, not one event loop later.
    ui::IconManager::refreshAll();
}

void MainWindow::showWelcomeScreen()
{
    // Split the shared recent-files list into the two things it actually holds
    // — saved workspaces and bare geometries — dropping entries whose file has
    // since gone, so nothing on the welcome screen fails when clicked.
    QStringList recentProjects;
    QStringList recentStructures;
    for (const QString& path : QSettings().value(kRecentFilesKey).toStringList()) {
        if (!QFileInfo::exists(path))
            continue;
        const bool project =
            path.endsWith(QStringLiteral(".calproj"), Qt::CaseInsensitive)
            || path.endsWith(QStringLiteral(".calango"), Qt::CaseInsensitive);
        (project ? recentProjects : recentStructures) << path;
    }

    WelcomeDialog dialog(recentProjects, recentStructures, this);
    if (dialog.exec() != QDialog::Accepted)
        return; // dismissed — start with the empty workspace
    switch (dialog.choice()) {
    case WelcomeDialog::Choice::NewProject:
        newProject();
        break;
    case WelcomeDialog::Choice::OpenProject:
        openProject();
        break;
    case WelcomeDialog::Choice::OpenGeometry:
        openStructure();
        break;
    case WelcomeDialog::Choice::OpenRecent:
        if (!dialog.selectedPath().isEmpty())
            loadFile(dialog.selectedPath());
        break;
    case WelcomeDialog::Choice::None:
        break;
    }
}

void MainWindow::newProject()
{
    if (isDirty_) {
        const auto choice = QMessageBox::warning(
            this, tr("New Workspace"),
            tr("The project has unsaved changes.\n"
               "Save them before starting a new workspace?"),
            QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel,
            QMessageBox::Save);
        if (choice == QMessageBox::Cancel
            || (choice == QMessageBox::Save && !saveProject()))
            return;
    }
    closeAllDocuments();
    projectPath_.clear();
    isDirty_ = false;
    statusBar()->showMessage(tr("New workspace"));
}

void MainWindow::showDatasetManager()
{
    if (!ensureAseAvailable())
        return;
    DatasetManagerDialog dialog(this);
    dialog.exec();
}

void MainWindow::openMaceTrainer()
{
    // MACE training runs in the user-selected environment (needs mace-torch),
    // reads its own dataset file, and needs no open structure.
    MaceTrainerDialog dialog(this);
    if (dialog.exec() != QDialog::Accepted)
        return; // Close, or an Export (handled inside the dialog)

    const QString label = tr("MACE training");
    if (dialog.action() == MaceTrainerDialog::Action::RunRemote) {
        const QString jobDir = stageJob(dialog.runnerScript());
        if (jobDir.isEmpty())
            return;
        remoteDock_->show();
        remoteDock_->raise();
        const int taskId =
            processPanel_->registerTask(tr("Remote %1").arg(label), jobDir);
        processPanel_->setTaskStatus(taskId, ProcessManagerPanel::Status::Running);
        remotePanel_->submitStagedJob(jobDir, label);
        statusBar()->showMessage(tr("Submitting %1 to the cluster…").arg(label));
        return;
    }
    runScript(dialog.runnerScript(), dialog.pythonExecutable(), label,
              /*expectFrames=*/false);
}

void MainWindow::showWarrenCowley()
{
    Document* doc = currentDocument();
    if (!doc || !doc->structure || doc->structure->empty()) {
        QMessageBox::information(this, tr("Warren-Cowley analysis"),
                                 tr("Open a structure first."));
        return;
    }
    WarrenCowleyDialog dialog(doc->structure, this);
    dialog.exec();
}

void MainWindow::showLocalEntropy()
{
    Document* doc = currentDocument();
    if (!doc || !doc->structure || doc->structure->empty()) {
        QMessageBox::information(this, tr("Local Entropy Analysis"),
                                 tr("Open a structure first."));
        return;
    }
    LocalEntropyDialog dialog(doc->structure, viewport_, this);
    // The computed field must reach every view (property combo, info
    // panel) — same refresh path as the Bond Editor.
    connect(&dialog, &LocalEntropyDialog::fieldStored, this,
            [this] { notifyStructureChanged(false); });
    dialog.exec();
}

void MainWindow::showPartialCharge()
{
    Document* doc = currentDocument();
    if (!doc || !doc->structure || doc->structure->empty()) {
        QMessageBox::information(this, tr("Partial Charge Analysis"),
                                 tr("Open a structure first."));
        return;
    }
    if (!ensureAseAvailable())
        return;
    // Modeless: the analysis runs as a background job, and the dialog is used
    // again afterwards to load the results and colour the atoms.
    auto* dialog = new PartialChargeDialog(doc->structure, viewport_, this);
    dialog->setAttribute(Qt::WA_DeleteOnClose);

    // Offer every completed process whose directory holds a charge density as a
    // density source, tagging the auto-detected engine.
    QList<QPair<QString, QString>> baselines;
    for (const auto& [id, record] : processRecords_) {
        if (record.directory.isEmpty())
            continue;
        const QDir dir(record.directory);
        QString engine;
        if (!dir.entryList({QStringLiteral("*.gpw")}, QDir::Files).isEmpty())
            engine = QStringLiteral("GPAW");
        // VASP before the generic cube sniff: a VASP directory often also
        // holds an exported cube, and the CHGCAR/AECCAR pair is the better
        // source (AECCAR0+AECCAR2 is the all-electron density Bader needs).
        else if (dir.exists(QStringLiteral("AECCAR2")))
            engine = QStringLiteral("VASP (all-electron)");
        else if (dir.exists(QStringLiteral("CHGCAR")))
            engine = QStringLiteral("VASP");
        else if (!dir.entryList({QStringLiteral("*.cube")}, QDir::Files).isEmpty())
            engine = QStringLiteral("cube");
        else if (!dir.entryList({QStringLiteral("*.save"),
                                 QStringLiteral("data-file-schema.xml")},
                                QDir::Files | QDir::Dirs).isEmpty())
            engine = QStringLiteral("Quantum ESPRESSO");
        else if (!dir.entryList({QStringLiteral("*.RHO*")}, QDir::Files).isEmpty())
            engine = QStringLiteral("SIESTA");
        if (!engine.isEmpty())
            baselines.append({tr("#%1 — %2 [%3]").arg(id).arg(record.label, engine),
                              record.directory});
    }
    dialog->setDensityBaselines(baselines);
    dialog->setTrajectoryFrameCount(static_cast<int>(doc->frames.size()));
    connect(dialog, &PartialChargeDialog::runRequested, this,
            [this](const QString& script, const QString& label) {
                runScript(script,
                          QString::fromStdString(
                              pybridge::PythonEngine::instance().executable()),
                          label, /*expectFrames=*/false);
            });
    // Writing the charges into the document is an EDIT, so it goes through the
    // undo stack like any other. The dialog holds a structure and no business
    // saving anything; the window owns both the stack and the file.
    connect(dialog, &PartialChargeDialog::chargesApplied, this,
            [this](const QVector<double>& charges, bool wholeTrajectory) {
                Document* document = currentDocument();
                if (!document || !document->structure
                    || charges.size()
                        != static_cast<int>(document->structure->size()))
                    return;
                pushUndo();
                const std::vector<double> values(charges.begin(), charges.end());
                document->structure->setScalarField("initial_charges", values);
                int written = 1;
                if (wholeTrajectory)
                    for (auto& frame : document->frames) {
                        if (!frame || frame == document->structure
                            || frame->size() != values.size())
                            continue;
                        frame->setScalarField("initial_charges", values);
                        ++written;
                    }
                isDirty_ = true;
                notifyStructureChanged(false);
                statusBar()->showMessage(
                    tr("Partial charges stored as `initial_charges` on %n "
                       "frame(s) — save as .extxyz to write them out",
                       nullptr, written));
            });
    // Recolouring by charge writes a scalar field on the structure; make sure
    // the property combo / info panel pick it up.
    connect(dialog, &QDialog::finished, this,
            [this] { notifyStructureChanged(false); });
    dialog->show();
}

// Completed processes that saved GPAW wavefunctions (.gpw) — the baselines the
// the MLWF post-process can restart from.
QList<QPair<QString, QString>> MainWindow::gpawBaselines() const
{
    QList<QPair<QString, QString>> baselines;
    for (const auto& [id, record] : processRecords_) {
        if (record.directory.isEmpty())
            continue;
        const QDir dir(record.directory);
        if (!dir.entryList({QStringLiteral("*.gpw")}, QDir::Files).isEmpty())
            baselines.append({tr("#%1 — %2 [GPAW]").arg(id).arg(record.label),
                              record.directory});
    }
    return baselines;
}

void MainWindow::showChargeDensityDifference()
{
    // Deliberately not prepareSimulation(): that requires an open structure,
    // and this one gets its geometry from the completed run it differences.
    // A project reopened with finished jobs in the Processes panel but no tab
    // in front is a perfectly good starting point.
    if (!ensureAseAvailable())
        return;
    const QList<QPair<QString, QString>> baselines = gpawBaselines();
    if (baselines.isEmpty()) {
        QMessageBox::information(
            this, tr("Charge Density Difference"),
            tr("No completed calculation has saved wavefunctions to build the "
               "difference from.\n\n"
               "Run a Single-point Calculation with GPAW first — it writes "
               "single_point.gpw, and both fragments are rebuilt from that "
               "same calculator so the three densities cannot drift apart."));
        return;
    }

    // Each baseline carries the geometry the run was staged with, read here
    // rather than in the wizard: the atom indices the generated script names
    // must be the ones inside that run's .gpw, and the current document may
    // have been edited since — or be a different system entirely.
    QList<CddWizard::Baseline> sources;
    for (const auto& [label, directory] : baselines) {
        CddWizard::Baseline source;
        source.label = label;
        source.directory = directory;
        try {
            source.structure = std::make_shared<const core::Structure>(
                pybridge::AseBridge::readStructure(
                    (directory + QStringLiteral("/structure.extxyz"))
                        .toStdString()));
        } catch (const std::exception&) {
            // Readable job, unreadable input: still offered, with the
            // partition stage saying it has nothing to split. Dropping it
            // silently would be worse — the user would wonder where their run
            // went.
        }
        sources.append(std::move(source));
    }

    CddWizard wizard(this);
    wizard.setDensityBaselines(std::move(sources));
    runSimulationWizard(wizard, tr("Charge Density Difference"),
                        /*expectFrames=*/false);
}

QList<QPair<QString, QString>> MainWindow::completedMlwfRuns() const
{
    // The marker is wannier.json, which the MLWF script writes only on
    // success — so a directory that has one is a run that finished.
    //
    // Validity beyond that (are the wavefunctions still on disk?) is NOT
    // decided here: the dialogs' MlwfSourceSelector re-checks whichever entry
    // is selected and says so in place, which is also the only way a browsed
    // directory outside this list could be checked at all.
    QList<QPair<QString, QString>> runs;
    for (const auto& [label, jsonPath] :
         processResults(QStringLiteral("wannier.json")))
        runs.append({label, QFileInfo(jsonPath).absolutePath()});
    return runs;
}

QString MainWindow::pythonForMlwfRun(const QString& mlwfDir) const
{
    // These three modules all restart GPAW from the .gpw the MLWF run
    // localized, so they must run WHERE GPAW IS. That is the environment the
    // MLWF run itself used — recorded in its calculator.json when it was
    // staged — and it is very rarely the embedded interpreter: the bundled
    // .venv carries ASE (so the script imports fine, and the structure loads)
    // but no GPAW, which is why hardcoding it here failed every one of these
    // modules on `from gpaw import GPAW`, a hundred lines in.
    if (const auto provenance =
            SimulationWizardBase::readCalculatorProvenance(mlwfDir);
        provenance && !provenance->pythonExecutable.isEmpty()
        && QFileInfo::exists(provenance->pythonExecutable)) {
        return provenance->pythonExecutable;
    }
    // No provenance (a run from before Calango recorded it), or the
    // interpreter it named has since been removed: fall back to the GPAW
    // environment configured in Preferences, which pythonForEngine resolves —
    // and which itself falls back to the embedded interpreter, so this is
    // still best-effort rather than a refusal.
    return pythonForEngine(core::CalculatorKind::Gpaw);
}

bool MainWindow::requireMlwfPrerequisite(const QString& title)
{
    // Only the "nothing at all" case is refused up front. Anything else is the
    // dialog's business now — opening a source picker with an empty list and a
    // disabled OK button would be a worse way to say "run MLWF first".
    if (!completedMlwfRuns().isEmpty())
        return true;
    QMessageBox::information(
        this, title,
        tr("%1 requires a successfully completed Wannier Functions process — "
           "it post-processes the localized Hamiltonian that run "
           "produces.\n\nRun Electronics → Wannier Functions… first; once it "
           "completes, this module will find it. A finished job from an "
           "earlier session can also be picked with the dialog's Browse… "
           "button.")
            .arg(title));
    return false;
}

void MainWindow::showWannierInterpolation()
{
    if (!requireMlwfPrerequisite(tr("Wannier Interpolation")))
        return;
    Document* doc = currentDocument();
    // The MLWF run is now chosen in the dialog's first step rather than in a
    // prompt before it, so the choice sits beside the settings that depend on
    // it and can be revised without starting over.
    WannierInterpolationDialog dialog(completedMlwfRuns(),
                                      doc ? doc->structure : nullptr, this);
    if (dialog.exec() != QDialog::Accepted)
        return;
    runScript(QString::fromStdString(core::generateWannierInterpolationScript(
                  dialog.mlwfDirectory().toStdString(), dialog.config())),
              pythonForMlwfRun(dialog.mlwfDirectory()),
              tr("Wannier Interpolation"), /*expectFrames=*/false);
}

void MainWindow::showFermiSurface()
{
    if (!requireMlwfPrerequisite(tr("Fermi Surface")))
        return;
    FermiSurfaceDialog dialog(completedMlwfRuns(), this);
    if (dialog.exec() != QDialog::Accepted)
        return;
    // config() now carries mlwfDir — the dialog owns that choice.
    const core::FermiSurfaceConfig cfg = dialog.config();
    runScript(QString::fromStdString(core::generateFermiSurfaceScript(cfg)),
              pythonForMlwfRun(dialog.mlwfDirectory()),
              tr("Fermi Surface"), /*expectFrames=*/false);
}

void MainWindow::showTopologicalInvariants()
{
    if (!requireMlwfPrerequisite(tr("Topological Invariants")))
        return;
    TopologyDialog dialog(completedMlwfRuns(), this);
    if (dialog.exec() != QDialog::Accepted)
        return;
    const core::TopologyConfig cfg = dialog.config();
    runScript(QString::fromStdString(core::generateTopologyScript(cfg)),
              pythonForMlwfRun(dialog.mlwfDirectory()),
              tr("Topological Invariants"), /*expectFrames=*/false);
}

QList<QPair<QString, QString>> MainWindow::processResults(
    const QString& resultFile) const
{
    // Processes that left a particular result file behind, as
    // label -> absolute path. Keyed on the FILE rather than on the task name:
    // what makes a run usable as an input is that it actually produced the
    // data, not what it was launched as.
    QList<QPair<QString, QString>> matches;
    for (const auto& [id, record] : processRecords_) {
        if (record.directory.isEmpty())
            continue;
        const QDir dir(record.directory);
        if (!dir.exists(resultFile))
            continue;
        matches.append({tr("#%1 — %2").arg(id).arg(record.label),
                        dir.absoluteFilePath(resultFile)});
    }
    return matches;
}

QList<QPair<QString, QString>> MainWindow::gpawDensityFiles() const
{
    QList<QPair<QString, QString>> baselines;
    for (const auto& [id, record] : processRecords_) {
        if (record.directory.isEmpty())
            continue;
        const QDir dir(record.directory);
        // single_point.gpw is what the Single-Point wizard writes; accept any
        // other .gpw the directory holds so a hand-run job still qualifies.
        const QStringList files =
            dir.entryList({QStringLiteral("*.gpw")}, QDir::Files, QDir::Name);
        if (files.isEmpty())
            continue;
        const QString preferred =
            files.contains(QStringLiteral("single_point.gpw"))
                ? QStringLiteral("single_point.gpw")
                : files.first();
        baselines.append({tr("#%1 — %2 [GPAW]").arg(id).arg(record.label),
                          dir.absoluteFilePath(preferred)});
    }
    return baselines;
}

QList<QPair<QString, QString>> MainWindow::vaspChargeDensityFiles() const
{
    QList<QPair<QString, QString>> baselines;
    for (const auto& [id, record] : processRecords_) {
        if (record.directory.isEmpty())
            continue;
        const QDir dir(record.directory);
        const QString chgcar = dir.absoluteFilePath(QStringLiteral("CHGCAR"));
        // Non-empty: VASP writes the file whether or not LCHARG asked for it,
        // and a zero-byte CHGCAR would fail ICHARG=11 at run time rather than
        // here, where it can still be explained.
        if (QFileInfo(chgcar).size() <= 0)
            continue;
        baselines.append({tr("#%1 — %2 [VASP]").arg(id).arg(record.label),
                          chgcar});
    }
    return baselines;
}

void MainWindow::openXasResults(const QString& directory)
{
    auto* window = new XasResultsWindow(this);
    window->setAttribute(Qt::WA_DeleteOnClose);
    if (!window->loadResults(directory + QStringLiteral("/xas.json"))) {
        delete window;
        QMessageBox::information(this, tr("X-ray Absorption Spectroscopy"),
                                 tr("No readable xas.json in %1").arg(directory));
        return;
    }
    window->show();
}

void MainWindow::showHubbardParameters()
{
    if (!prepareSimulation(tr("Hubbard Parameter Calculation")))
        return;
    HubbardUWizard wizard(currentDocument()->structure, this);
    runSimulationWizard(wizard, tr("Hubbard Parameter Calculation"),
                        /*expectFrames=*/false);
}

void MainWindow::showXas()
{
    if (!prepareSimulation(tr("X-ray Absorption Spectroscopy")))
        return;
    XasWizard wizard(currentDocument()->structure, this);
    runSimulationWizard(wizard, tr("X-ray Absorption Spectroscopy"),
                        /*expectFrames=*/false);
}

void MainWindow::showBornCharges()
{
    if (!prepareSimulation(tr("Born Effective Charges")))
        return;
    Document* doc = currentDocument();
    // The Berry-phase polarization is only defined for a periodic crystal, and
    // the check is cheap enough to make here rather than after the user has
    // configured a job that cannot run.
    if (doc && doc->structure && !doc->structure->cell().isDefined()) {
        QMessageBox::information(
            this, tr("Born Effective Charges"),
            tr("Z* is obtained from the macroscopic polarization, which is "
               "defined only for a periodic crystal — this structure has no "
               "unit cell.\n\nBuild or import a periodic insulator first."));
        return;
    }
    // Like Electronic Structure and Optics, this starts from a completed
    // Single-Point Calculation — it supplies the converged geometry and the
    // calculator every displaced run is rebuilt from.
    const auto baselines = gpawDensityFiles();
    if (baselines.isEmpty()) {
        QMessageBox::critical(
            this, tr("Born Effective Charges"),
            tr("This calculation starts from a converged ground state, so it "
               "needs a completed GPAW Single-Point Calculation that saved its "
               "wavefunctions (.gpw).\n\n"
               "Run one on this structure first."));
        return;
    }

    BornChargesWizard wizard(doc ? doc->structure : nullptr, this);
    wizard.setDensityBaselines(baselines);
    runSimulationWizard(wizard, tr("Born Effective Charges"),
                        /*expectFrames=*/false);
}

void MainWindow::openBornChargesResults(const QString& directory)
{
    auto* viewer = new BornChargesViewer(this);
    viewer->setAttribute(Qt::WA_DeleteOnClose);
    if (!viewer->loadResults(directory
                             + QStringLiteral("/born_charges.json"))) {
        delete viewer;
        QMessageBox::information(
            this, tr("Born Effective Charges"),
            tr("No born_charges.json found in %1.").arg(directory));
        return;
    }
    viewer->show();
}

void MainWindow::showRamanIrSpectroscopy()
{
    if (!prepareSimulation(tr("Raman and IR Spectroscopy")))
        return;
    Document* doc = currentDocument();

    // No hard precondition any more. A baseline .gpw is what the GPAW route
    // displaces about, but VASP and Quantum ESPRESSO converge their own ground
    // state and solve the linear response on top of it — so refusing to open
    // without one would withhold the module from every user of those two
    // engines. The wizard disables the GPAW entry (with the reason on it) when
    // the list is empty and steers to a self-contained engine instead.
    const auto baselines = gpawDensityFiles();
    // Born charges are OPTIONAL. They are the only route to an infrared
    // intensity in a periodic crystal, but nothing else here needs them: the
    // Γ-point phonons come from finite displacements and the Raman activities
    // from ∂χ/∂Q. Refusing to open without them made a second, expensive run
    // the price of admission for results that do not depend on it — and for a
    // material studied by Raman alone, a price with nothing behind it. The
    // wizard offers "(none)" and the generated script reports every IR
    // intensity as zero with `ir.computed = false`, rather than as a
    // plausible-looking number.
    const auto bornCharges =
        processResults(QStringLiteral("born_charges.json"));

    RamanIrWizard wizard(doc ? doc->structure : nullptr, this);
    wizard.setDensityBaselines(baselines);
    wizard.setBornChargesResults(bornCharges);
    // Optional: an Optics run contributes the broadening its dielectric
    // response was validated with, nothing more.
    wizard.setOpticsResults(processResults(QStringLiteral("optics.json")));
    runSimulationWizard(wizard, tr("Raman and IR Spectroscopy"),
                        /*expectFrames=*/false);
}

void MainWindow::openRamanIrResults(const QString& directory)
{
    auto* viewer = new RamanIrViewer(this);
    viewer->setAttribute(Qt::WA_DeleteOnClose);
    if (!viewer->loadResults(directory + QStringLiteral("/raman_ir.json"))) {
        delete viewer;
        QMessageBox::information(this, tr("Raman and IR Spectroscopy"),
                                 tr("No raman_ir.json found in %1.")
                                     .arg(directory));
        return;
    }
    viewer->show();
}

void MainWindow::showWannier()
{
    if (!prepareSimulation(tr("Wannier Functions")))
        return;
    // MLWF localization is set up + launched through the standardized wizard
    // (engine selection + per-engine Conda env). It writes wannier.json (+
    // per-orbital cubes); onJobFinished() opens the centres table + viewer.
    WannierWizard wizard(currentDocument()->structure, this);
    wizard.setDensityBaselines(gpawBaselines());
    runSimulationWizard(wizard, tr("Wannier Functions"),
                        /*expectFrames=*/false);
}

void MainWindow::showVacf()
{
    Document* doc = currentDocument();
    if (!doc || doc->frames.size() < 2) {
        QMessageBox::information(
            this, tr("Velocity Autocorrelation Function (VACF)"),
            tr("Open a molecular-dynamics trajectory (at least two frames) "
               "first."));
        return;
    }
    // Extract per-frame, per-atom velocities. MD trajectories carry them as the
    // "velocities" vector field; require every frame to have them.
    std::vector<std::vector<core::Vec3>> velocities;
    velocities.reserve(doc->frames.size());
    for (const auto& frame : doc->frames) {
        if (!frame)
            continue;
        const auto& fields = frame->vectorFields();
        const auto it = fields.find("velocities");
        if (it == fields.end() || it->second.empty()) {
            QMessageBox::information(
                this, tr("Velocity Autocorrelation Function (VACF)"),
                tr("This trajectory has no per-atom velocities. Run Molecular "
                   "Dynamics (which records velocities) and analyze that "
                   "trajectory."));
            return;
        }
        velocities.push_back(it->second);
    }

    auto* dialog = new VacfDialog(std::move(velocities), /*defaultDtFs=*/1.0, this);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->show();
}

void MainWindow::openNanoparticleBuilder()
{
    if (!ensureAseAvailable())
        return;
    NanoparticleDialog dialog(this);
    if (dialog.exec() != QDialog::Accepted || !dialog.result())
        return;
    const int tab = addDocument(
        std::make_shared<core::Structure>(*dialog.result()),
        dialog.resultName());
    tabBar_->setCurrentIndex(tab);
    statusBar()->showMessage(tr("%1 generated").arg(dialog.resultName()));
}

void MainWindow::openAddAdsorbate()
{
    Document* doc = currentDocument();
    if (!doc || !doc->structure || doc->structure->empty()) {
        QMessageBox::information(
            this, tr("Add Adsorbate"),
            tr("Open or build a structure first — the adsorbate is placed on "
               "the geometry in the active tab."));
        return;
    }
    // ASE supplies the molecule database on the second tab. Site detection and
    // the placement geometry are native, so a missing ASE would still leave a
    // usable single-atom builder — but the dialog would then be half-broken in
    // a way that is only discovered after opening it.
    if (!ensureAseAvailable())
        return;

    AddAdsorbateDialog dialog(doc->structure, this);
    if (dialog.exec() != QDialog::Accepted || !dialog.result())
        return;
    // A NEW tab, carrying the previous tab's geometry plus the adsorbate: the
    // clean surface is the reference an adsorption energy is measured against,
    // so mutating it in place would destroy half of the calculation.
    const int tab = addDocument(dialog.result(), dialog.resultName());
    tabBar_->setCurrentIndex(tab);
    isDirty_ = true;
    statusBar()->showMessage(tr("%1 — %2 atoms")
                                 .arg(dialog.resultName())
                                 .arg(dialog.result()->size()));
}

void MainWindow::showAdsorption()
{
    Document* doc = currentDocument();
    if (!doc || !doc->structure || doc->structure->empty()) {
        QMessageBox::information(
            this, tr("Adsorption & Catalysis"),
            tr("Open a surface first — a periodic slab (e.g. from the Surface "
               "Slab wizard) or a nanoparticle/cluster."));
        return;
    }
    if (!ensureAseAvailable())
        return;

    AdsorptionDialog dialog(doc->structure, this);
    if (dialog.exec() != QDialog::Accepted || dialog.outputs().empty())
        return;
    const auto& outputs = dialog.outputs();
    int lastTab = -1;
    if (dialog.outputMode() == AdsorptionDialog::OutputMode::SingleTrajectory
        && outputs.size() > 1) {
        // One tab whose frames are the generated geometries, scrubbable from
        // the timeline. The first frame is also the tab's own structure, so a
        // trajectory tab still behaves like a structure tab everywhere else.
        std::vector<std::shared_ptr<core::Structure>> frames;
        frames.reserve(outputs.size());
        for (const auto& output : outputs)
            frames.push_back(output.structure);
        lastTab = addDocument(outputs.front().structure,
                              tr("Adsorption sites"), frames,
                              tr("Adsorption"));
    } else {
        for (const auto& output : outputs)
            lastTab = addDocument(output.structure, output.name);
    }
    tabBar_->setCurrentIndex(lastTab);
    statusBar()->showMessage(
        tr("%n adsorption structure(s) generated", nullptr,
           static_cast<int>(outputs.size())));
}


void MainWindow::openMacromoleculeBuilder()
{
    MacromoleculeWizard wizard(this);
    if (wizard.exec() != QDialog::Accepted || !wizard.result())
        return;
    const auto& generated = *wizard.result();
    const int tab = addDocument(
        std::make_shared<core::Structure>(generated.structure),
        QString::fromStdString(generated.description));
    tabBar_->setCurrentIndex(tab);
    isDirty_ = true;
    statusBar()->showMessage(
        tr("%1 — %2 atoms, %3 g/cm³")
            .arg(QString::fromStdString(generated.description))
            .arg(static_cast<int>(generated.structure.size()))
            .arg(generated.densityGCm3, 0, 'f', 3));
}

void MainWindow::openLiquidInterfaceBuilder()
{
    Document* doc = currentDocument();
    if (!doc || !doc->structure || doc->structure->empty()) {
        QMessageBox::information(
            this, tr("Liquid / Gas Interface"),
            tr("Open or build a structure first.\n\nThis builder opens a fluid "
               "region on an existing one — typically a surface slab from "
               "Build → Surface Slab…"));
        return;
    }
    if (!doc->structure->cell().isDefined()) {
        QMessageBox::information(
            this, tr("Liquid / Gas Interface"),
            tr("This structure has no periodic cell.\n\nThe fluid region is "
               "opened along a lattice vector, so the substrate needs one. Add "
               "a cell from the Structure panel, or start from a surface "
               "slab."));
        return;
    }

    LiquidInterfaceWizard wizard(doc->structure, this);
    if (wizard.exec() != QDialog::Accepted || !wizard.result())
        return;
    const auto& generated = *wizard.result();
    const int tab = addDocument(
        std::make_shared<core::Structure>(generated.structure),
        QString::fromStdString(generated.description));
    tabBar_->setCurrentIndex(tab);
    isDirty_ = true;
    statusBar()->showMessage(
        tr("%1 molecules packed — %2 g/cm³ over %3 Å³")
            .arg(generated.totalMolecules)
            .arg(generated.density, 0, 'f', 3)
            .arg(generated.regionVolume, 0, 'f', 0));

    // Warnings are shown AFTER the tab opens, not instead of it: a saturated
    // packing still produces a usable cell, and hiding it behind a modal that
    // looks like a failure would be worse than the shortfall it reports.
    if (!generated.warnings.empty()) {
        QStringList lines;
        for (const std::string& warning : generated.warnings)
            lines << QStringLiteral("• ") + QString::fromStdString(warning);
        QMessageBox::warning(this, tr("Liquid / Gas Interface"),
                             lines.join(QStringLiteral("\n\n")));
    }
}

void MainWindow::openDislocationBuilder()
{
    Document* doc = currentDocument();
    if (!doc || !doc->structure || doc->structure->empty()) {
        QMessageBox::information(
            this, tr("Dislocation"),
            tr("Open or build a crystal first.\n\nA dislocation is inserted by "
               "displacing an existing lattice — there is nothing to displace "
               "yet."));
        return;
    }
    if (!doc->structure->cell().isDefined()) {
        QMessageBox::information(
            this, tr("Dislocation"),
            tr("This structure has no periodic cell.\n\nThe dislocation line "
               "runs along a lattice direction and the field is periodic along "
               "it, so the host needs a cell."));
        return;
    }

    DislocationWizard wizard(doc->structure, this);
    if (wizard.exec() != QDialog::Accepted || !wizard.result())
        return;
    const auto& generated = *wizard.result();
    const int tab = addDocument(
        std::make_shared<core::Structure>(generated.structure),
        QString::fromStdString(generated.description));
    tabBar_->setCurrentIndex(tab);
    isDirty_ = true;
    statusBar()->showMessage(
        tr("%1 — largest displacement %2 Å, closest pair %3 Å")
            .arg(QString::fromStdString(generated.description))
            .arg(generated.maxDisplacement, 0, 'f', 3)
            .arg(generated.minSeparation, 0, 'f', 3));

    // Shown after the tab opens: an unrelaxed core and a cell that is periodic
    // only along the line are both expected outcomes, not failures, and hiding
    // the result behind a modal that reads like one would be worse than the
    // caveat it carries.
    if (!generated.warnings.empty()) {
        QStringList lines;
        for (const std::string& warning : generated.warnings)
            lines << QStringLiteral("• ") + QString::fromStdString(warning);
        QMessageBox::warning(this, tr("Dislocation"),
                             lines.join(QStringLiteral("\n\n")));
    }
}

void MainWindow::openSolidInterfaceBuilder()
{
    Document* doc = currentDocument();
    if (!doc || !doc->structure || doc->structure->empty()) {
        QMessageBox::information(
            this, tr("Solid Interface"),
            tr("Open or build a crystal first.\n\nEvery construction here "
               "fills space by repeating a parent lattice."));
        return;
    }

    // Every open tab is offered as a phase, with the active one first: a
    // multi-phase polycrystal needs a second lattice, and the only place one
    // can come from is another workspace.
    std::vector<PhaseSource> phases;
    phases.emplace_back(tabBar_->tabText(tabBar_->currentIndex()),
                        doc->structure);
    for (const auto& other : documents_) {
        if (!other || !other->structure || other->structure->empty()
            || other->structure == doc->structure
            || !other->structure->cell().isDefined())
            continue;
        phases.emplace_back(other->fileName.isEmpty()
                                ? QString::fromStdString(
                                      other->structure->chemicalFormula())
                                : other->fileName,
                            other->structure);
    }

    SolidInterfaceWizard wizard(std::move(phases), this);
    if (wizard.exec() != QDialog::Accepted || !wizard.result())
        return;
    const auto& generated = *wizard.result();
    const int tab = addDocument(
        std::make_shared<core::Structure>(generated.structure),
        QString::fromStdString(generated.description));
    tabBar_->setCurrentIndex(tab);
    isDirty_ = true;
    // One cast per grain, each its own colour, so the tessellation is visible
    // the moment the structure opens. A polycrystal drawn in element colours
    // is a uniform block of atoms: the grains are the whole point of the
    // structure and are the one thing element colouring cannot show.
    const int grainCasts = applyGrainCasts();
    QString status =
        tr("%1 — %2 atoms, %3 g/cm³, %4 merged at the seams")
            .arg(QString::fromStdString(generated.description))
            .arg(static_cast<int>(generated.structure.size()))
            .arg(generated.density, 0, 'f', 3)
            .arg(generated.mergedAtoms);
    if (grainCasts > 0)
        status += tr(" — %n grain(s) coloured as casts", nullptr, grainCasts);
    statusBar()->showMessage(status);

    if (!generated.warnings.empty()) {
        QStringList lines;
        for (const std::string& warning : generated.warnings)
            lines << QStringLiteral("• ") + QString::fromStdString(warning);
        QMessageBox::warning(this, tr("Solid Interface"),
                             lines.join(QStringLiteral("\n\n")));
    }
}

void MainWindow::openWaterIceBuilder()
{
    WaterIceWizard wizard(this);
    if (wizard.exec() != QDialog::Accepted || !wizard.result())
        return;
    const auto& generated = *wizard.result();
    const int tab = addDocument(
        std::make_shared<core::Structure>(generated.structure),
        QString::fromStdString(generated.description));
    tabBar_->setCurrentIndex(tab);
    isDirty_ = true;
    statusBar()->showMessage(
        tr("%1 — %2 molecules, %3 g/cm³")
            .arg(QString::fromStdString(generated.description))
            .arg(generated.moleculeCount)
            .arg(generated.densityGCm3, 0, 'f', 3));
}

int MainWindow::applyFunctionalGroupCasts()
{
    Document* doc = currentDocument();
    if (!doc || !doc->structure || doc->structure->empty() || !viewport_)
        return 0;

    const std::vector<int> labels =
        core::GrapheneOxideBuilder::functionalGroupLabels(*doc->structure);
    if (labels.size() != doc->structure->size())
        return 0;

    // One cast per group KIND, not per group instance. A flake carries dozens
    // of epoxides; giving each its own cast would produce a list nobody can
    // work with, whereas "all the epoxides" is a thing a reader of the figure
    // actually wants to pick out, hide or colour.
    //
    // Only the kinds that are present get a cast — an empty "Carbonyl" cast on
    // a structure with no carbonyls is a control that does nothing.
    using Builder = core::GrapheneOxideBuilder;
    std::array<int, Builder::kGroupCount> castOfGroup{};
    castOfGroup.fill(0);
    // Distinct hues, in the order the groups are declared. Chosen against the
    // element colours the sheet is drawn in (grey C, red O, white H) so a
    // group reads as a group rather than blending into the substrate.
    static const QColor kGroupColors[Builder::kGroupCount] = {
        QColor(0xE6, 0x55, 0x0D), // epoxide      — orange
        QColor(0x1F, 0x77, 0xB4), // hydroxyl     — blue
        QColor(0x9E, 0x4C, 0xC4), // carboxyl     — purple
        QColor(0x2C, 0xA0, 0x2C), // carbonyl     — green
    };

    auto& style = viewport_->style();
    style.castStyles.clear();
    style.castName = tr("Pristine framework");
    for (std::size_t group = 0; group < Builder::kGroupCount; ++group) {
        const int slot = static_cast<int>(group);
        if (std::find(labels.begin(), labels.end(), slot) == labels.end())
            continue;
        render::StructureRenderer::CastStyle cast = style.castStyle(0);
        cast.castColor = kGroupColors[group];
        // Capitalized: it is a name in a list, not a word in a sentence.
        QString name = QString::fromLatin1(
            Builder::name(static_cast<Builder::Group>(group)));
        name[0] = name[0].toUpper();
        cast.name = name;
        style.castStyles.push_back(cast);
        castOfGroup[group] = static_cast<int>(style.castStyles.size());
    }
    if (style.castStyles.empty()) {
        // Nothing was found: leave the scene in the single-cast state rather
        // than renaming cast 0 after an analysis that came back empty.
        style.castName.clear();
        return 0;
    }

    style.atomCasts.assign(labels.size(), 0);
    for (std::size_t i = 0; i < labels.size(); ++i) {
        if (labels[i] >= 0 && labels[i] < static_cast<int>(Builder::kGroupCount))
            style.atomCasts[i] =
                castOfGroup[static_cast<std::size_t>(labels[i])];
    }
    // Cast colouring is the whole point of having made the casts; leaving the
    // scene on Element colouring would produce the groups and show none of
    // them.
    style.colorMode = render::ColorMode::Cast;
    for (auto& cast : style.castStyles)
        cast.colorMode = render::ColorMode::Cast;
    viewport_->styleChanged(/*rebuildGeometry=*/true);
    return static_cast<int>(style.castStyles.size());
}

int MainWindow::applyGrainCasts()
{
    Document* doc = currentDocument();
    if (!doc || !doc->structure || doc->structure->empty() || !viewport_)
        return 0;

    // The builder tags every atom with the grain it was carved from — see
    // SolidInterfaceBuilder, which writes the field as it fills the Voronoi
    // regions. Reading it back here rather than re-deriving the tessellation
    // is the whole point: the assignment the geometry was BUILT from is the
    // only one guaranteed to agree with it, and a second nearest-seed pass
    // would disagree exactly at the seams, which is where it matters.
    const auto& fields = doc->structure->scalarFields();
    const auto it = fields.find("grain");
    if (it == fields.end() || it->second.size() != doc->structure->size())
        return 0;
    const std::vector<double>* field = &it->second;

    int grainCount = 0;
    for (const double value : *field)
        grainCount = std::max(grainCount, static_cast<int>(value) + 1);
    // One grain is not a polycrystal, and a single "Grain 1" cast covering
    // every atom is a control that does nothing.
    if (grainCount < 2)
        return 0;

    auto& style = viewport_->style();
    style.castStyles.clear();
    // Cast 0 is what an atom falls back to; nothing should, since every atom
    // carries a grain, but naming it honestly beats leaving the previous
    // structure's label in place.
    style.castName = tr("Ungrouped");
    for (int grain = 0; grain < grainCount; ++grain) {
        render::StructureRenderer::CastStyle cast = style.castStyle(0);
        // Golden-angle hue rotation. Consecutive grains land ~137.5° apart, so
        // any PREFIX of the sequence is well separated — which matters because
        // the grain count is whatever the user asked for, from 2 to dozens,
        // and a fixed palette would either run out or waste its best colours.
        // Saturation and value alternate slightly as well: past roughly a
        // dozen grains hue alone starts to repeat perceptually, and two
        // touching grains of the same apparent colour is exactly the thing
        // this feature exists to prevent.
        const double hue = std::fmod(grain * 137.508, 360.0);
        const int saturation = (grain % 3 == 1) ? 165 : 220;
        const int value = (grain % 3 == 2) ? 195 : 245;
        cast.castColor = QColor::fromHsv(static_cast<int>(hue), saturation,
                                         value);
        cast.name = tr("Grain %1").arg(grain + 1);
        style.castStyles.push_back(cast);
    }

    style.atomCasts.assign(doc->structure->size(), 0);
    for (std::size_t i = 0; i < field->size(); ++i) {
        const int grain = static_cast<int>((*field)[i]);
        if (grain >= 0 && grain < grainCount)
            style.atomCasts[i] = grain + 1; // cast 0 is the fallback
    }
    viewport_->update();
    return grainCount;
}

void MainWindow::openGrapheneOxideBuilder()
{
    GrapheneOxideWizard wizard(this);
    if (wizard.exec() != QDialog::Accepted || !wizard.result())
        return;
    const auto& report = wizard.report();
    const int tab = addDocument(
        std::make_shared<core::Structure>(*wizard.result()),
        tr("Graphene oxide"));
    tabBar_->setCurrentIndex(tab);
    isDirty_ = true;
    const int castCount = applyFunctionalGroupCasts();

    // Report what was actually placed, not what was asked for. The two differ
    // whenever the substrate ran out of free carbons, and a silent shortfall
    // leaves the user believing they have a composition they do not have.
    QString message =
        tr("Graphene oxide — %1, %2 basal / %3 edge carbons, ")
            .arg(QString::fromStdString(wizard.result()->chemicalFormula()))
            .arg(report.basalCarbonCount)
            .arg(report.edgeCarbonCount);
    // C/O is meaningless without oxygen, and printing "0.00" for a pristine
    // substrate reads as an infinitely oxidized one.
    message += report.oxygenAtoms > 0
        ? tr("C/O = %1").arg(report.carbonToOxygenRatio(), 0, 'f', 2)
        : tr("no oxygen placed");
    if (castCount > 0) {
        message += tr(" — %n functional-group cast(s) ready to colour",
                      nullptr, castCount);
    }
    if (!report.note.empty()) {
        message += tr("  ⚠ %1").arg(QString::fromStdString(report.note));
        QMessageBox::information(this, tr("Graphene Oxide"),
                                 QString::fromStdString(report.note));
    }
    statusBar()->showMessage(message);
}

void MainWindow::openSqsBuilder()
{
    Document* doc = currentDocument();
    if (!doc || !doc->structure || doc->structure->empty()) {
        QMessageBox::information(this, tr("Special Quasirandom Structure"),
                                 tr("Open or build a base structure first."));
        return;
    }
    // No ensureAseAvailable() here any more: SQS generation is native C++
    // (core::SqsGenerator), so it runs with or without a working Python
    // environment.
    SqsDialog dialog(doc->structure, this);
    if (dialog.exec() != QDialog::Accepted || !dialog.result())
        return;

    const auto& generated = *dialog.result();
    const int tab =
        addDocument(std::make_shared<core::Structure>(generated.structure),
                    tr("SQS"));
    tabBar_->setCurrentIndex(tab);
    statusBar()->showMessage(dialog.resultSummary());
}

void MainWindow::effectiveBandsCalculation()
{
    if (!prepareSimulation(tr("Effective Bands")))
        return;
    Document* doc = currentDocument();
    if (!doc || !doc->structure)
        return;
    if (!doc->structure->cell().isDefined()) {
        QMessageBox::information(
            this, tr("Effective Bands"),
            tr("Band unfolding needs a periodic supercell — this structure "
               "has no unit cell."));
        return;
    }

    // Every OTHER open document is a candidate primitive reference. Excluding
    // the active one is deliberate: a cell cannot be its own primitive cell,
    // and offering it would only invite an identity matrix.
    std::vector<EffectiveBandsWizard::NamedStructure> candidates;
    for (std::size_t i = 0; i < documents_.size(); ++i) {
        const Document* other = documents_[i].get();
        if (other == doc || !other->structure
            || !other->structure->cell().isDefined()) {
            continue;
        }
        candidates.push_back(
            {other->fileName.isEmpty() ? tr("Tab %1").arg(i + 1) : other->fileName,
             other->structure});
    }
    if (candidates.empty()) {
        QMessageBox::information(
            this, tr("Effective Bands"),
            tr("Open the pristine primitive cell in another tab first — "
               "unfolding projects the supercell's bands onto its Brillouin "
               "zone, so both structures are needed."));
        return;
    }

    EffectiveBandsWizard wizard(doc->structure, std::move(candidates), this);
    // stageJob writes this as primitive.extxyz next to run.py. Set before the
    // wizard runs so both the local and remote paths pick it up.
    if (wizard.exec() != QDialog::Accepted)
        return;
    stagedPrimitive_ = wizard.primitiveStructure();
    if (wizard.action() == SimulationWizardBase::Action::RunRemote) {
        const QString jobDir = stageJob(wizard.script());
        if (jobDir.isEmpty()) {
            stagedPrimitive_.reset();
            return;
        }
        remoteDock_->show();
        remoteDock_->raise();
        const int taskId =
            processPanel_->registerTask(tr("Remote effective bands"), jobDir);
        processPanel_->setTaskStatus(taskId, ProcessManagerPanel::Status::Running);
        remotePanel_->submitStagedJob(
            jobDir, QFileInfo(doc->fileName).completeBaseName());
        statusBar()->showMessage(tr("Submitting unfolding run to the cluster…"));
        return;
    }
    runScript(wizard.script(), wizard.pythonExecutable(), tr("Effective Bands"),
              /*expectFrames=*/false, wizard.calculatorKind(),
              wizard.runCommand());
    stagedPrimitive_.reset(); // consumed by stageJob, or dropped on failure
}

void MainWindow::clusterExpansionCalculation()
{
    if (!prepareSimulation(tr("Cluster Expansion Calculation")))
        return;
    Document* doc = currentDocument();
    if (!doc)
        return;

    // The ensemble is the document's trajectory when it has one; a plain
    // single structure is still accepted (the wizard says why that limits the
    // hull) so the workflow can be tried on one configuration.
    std::vector<std::shared_ptr<core::Structure>> ensemble = doc->frames;
    if (ensemble.empty() && doc->structure)
        ensemble.push_back(doc->structure);
    if (ensemble.empty()) {
        QMessageBox::information(this, tr("Cluster Expansion Calculation"),
                                 tr("Open the configuration ensemble first "
                                    "(Build → Cluster Expansion…)."));
        return;
    }

    ClusterExpansionWizard wizard(ensemble, this);
    // stageJob writes these as configs.extxyz, which the generated script
    // reads; set it before running the wizard's action so both the local and
    // the remote path pick it up.
    stagedEnsembleFrames_ = std::move(ensemble);
    runSimulationWizard(wizard, tr("Cluster Expansion"), /*expectFrames=*/false);
    // Not consumed (the user cancelled) — do not leak the staging into the
    // next unrelated job.
    stagedEnsembleFrames_.clear();
}

void MainWindow::openClusterExpansion()
{
    Document* doc = currentDocument();
    if (!doc || !doc->structure || doc->structure->empty()
        || !doc->structure->cell().isDefined()) {
        QMessageBox::information(
            this, tr("Cluster Expansion"),
            tr("Open a periodic parent structure with a defined unit cell first."));
        return;
    }

    ClusterExpansionDialog dialog(doc->structure, this);
    if (dialog.exec() != QDialog::Accepted || !dialog.result())
        return;

    const auto& res = *dialog.result();
    // Present the whole inequivalent ensemble as one scrubbable multi-frame
    // document (Save Trajectory As… exports it for the Dataset Manager).
    std::vector<std::shared_ptr<core::Structure>> frames;
    frames.reserve(res.configs.size());
    for (const auto& cfg : res.configs)
        frames.push_back(std::make_shared<core::Structure>(cfg.structure));

    const int tab = addTrajectoryDocument(std::move(frames),
                                          tr("Cluster Expansion (%1 configs)")
                                              .arg(res.configs.size()));
    tabBar_->setCurrentIndex(tab);

    int pairO = 0, tripO = 0, quadO = 0;
    for (const auto& o : res.orbits) {
        if (o.order == 2) ++pairO;
        else if (o.order == 3) ++tripO;
        else if (o.order == 4) ++quadO;
    }
    statusBar()->showMessage(
        tr("Cluster Expansion: %1 inequivalent configs from %2 decorations "
           "(%3 active sites; orbits %4 pair / %5 triplet / %6 quad)%7")
            .arg(res.configs.size())
            .arg(res.enumerated)
            .arg(res.activeSites)
            .arg(pairO).arg(tripO).arg(quadO)
            .arg(res.sampled ? tr(" — occupation space sub-sampled") : QString()));
}

void MainWindow::showCoordination()
{
    Document* doc = currentDocument();
    if (!doc || !doc->structure || doc->structure->empty()) {
        QMessageBox::information(this, tr("Coordination Numbers"),
                                 tr("Open a structure first."));
        return;
    }
    CoordinationDialog dialog(doc->structure, viewport_, this);
    dialog.exec();
}

void MainWindow::showSymmetry()
{
    Document* doc = currentDocument();
    if (!doc || !doc->structure || doc->structure->empty()
        || !doc->structure->cell().isDefined()) {
        QMessageBox::information(
            this, tr("Symmetry"),
            tr("Open a periodic structure with a defined unit cell first."));
        return;
    }
    if (!ensureAseAvailable())
        return;

    // Inspection only: the cell transforms moved to Edit Structure, so the
    // dialog no longer returns a structure.
    SymmetryDialog dialog(doc->structure, this);
    dialog.exec();
}

void MainWindow::showMagneticSpaceGroup()
{
    Document* doc = currentDocument();
    if (!doc || !doc->structure || doc->structure->empty()
        || !doc->structure->cell().isDefined()) {
        QMessageBox::information(
            this, tr("Magnetic Space Group"),
            tr("Open a periodic structure with a defined unit cell first."));
        return;
    }
    if (!ensureAseAvailable())
        return;

    // The moments are an input the dialog owns rather than something read
    // once here: it loads whichever the structure carries (converged or
    // seeded) into an editable table, because "what would the magnetic space
    // group be if this sublattice flipped?" is the normal way to use it.
    MagneticSpaceGroupDialog dialog(doc->structure, this);
    dialog.exec();
}

void MainWindow::openPhononBuilder()
{
    Document* doc = currentDocument();
    if (!doc || !doc->structure || doc->structure->empty()) {
        QMessageBox::information(this, tr("Phonon"),
                                 tr("Open or build a structure first."));
        return;
    }
    if (!ensureAseAvailable())
        return;
    const auto pbc = doc->structure->cell().pbc();
    const bool periodic = doc->structure->cell().isDefined()
        && (pbc[0] || pbc[1] || pbc[2]);
    PhononWizard wizard(periodic, doc->structure, this);
    // LO-TO splitting is assembled from two earlier runs: Z* from a Born
    // Effective Charges job, and eps_inf from an Optics one (or typed).
    wizard.setBornChargeProcesses(processResults(QStringLiteral("born_charges.json")));
    wizard.setOpticsProcesses(processResults(QStringLiteral("optics.json")));
    runSimulationWizard(wizard, tr("Phonon Calculation"));
}

void MainWindow::openNanoBuilder()
{
    if (!ensureAseAvailable())
        return;
    NanoBuilderDialog dialog(this);
    if (dialog.exec() != QDialog::Accepted || !dialog.result())
        return;
    const auto atomCount = dialog.result()->size();
    addDocument(dialog.result(), dialog.resultName());
    statusBar()->showMessage(
        tr("Built %1 (%2 atoms)").arg(dialog.resultName()).arg(atomCount));
}

void MainWindow::addRandomNoise()
{
    if (!prepareSimulation(tr("Random Noise Setup")))
        return;
    Document* doc = currentDocument();
    if (!doc || !doc->structure || doc->structure->empty()) {
        statusBar()->showMessage(tr("Open a structure first."));
        return;
    }

    // Captured before any tab is added: regenerating would otherwise name the
    // second ensemble after the first one's tab, giving "X (noise) (noise)".
    const QString baseName = doc->fileName;

    RandomNoiseWizard wizard(doc->structure, this);
    // The ensemble is generated DURING the wizard, so it is captured as it
    // arrives rather than read back after exec() — stageJob() consumes
    // stagedEnsembleFrames_ as configs.extxyz, which is what the generated
    // script reads.
    connect(&wizard, &RandomNoiseWizard::structuresGenerated, this,
            [this, baseName](
                const std::vector<std::shared_ptr<core::Structure>>& frames) {
                stagedEnsembleFrames_ = frames;
                if (frames.empty())
                    return;
                // Open it as a scrubbable trajectory, so the displacement can
                // be judged by eye before any compute time is spent on it.
                addTrajectoryDocument(frames,
                                      tr("%1 (noise ×%2)")
                                          .arg(baseName)
                                          .arg(frames.size() - 1));
                statusBar()->showMessage(
                    tr("Generated %1 perturbed structures — scrub the new tab.")
                        .arg(frames.size() - 1));
            });

    runSimulationWizard(wizard, tr("Random Noise Single-point"),
                        /*expectFrames=*/false);
    // Not consumed (the user cancelled) — do not leak the staging into the
    // next unrelated job.
    stagedEnsembleFrames_.clear();
}

void MainWindow::openExamplesBrowser()
{
    if (!ensureAseAvailable())
        return;
    ExamplesDialog dialog(this);
    connect(&dialog, &ExamplesDialog::structureFetched, this,
            [this](std::shared_ptr<core::Structure> structure, const QString& name) {
                const auto atomCount = structure->size();
                addDocument(std::move(structure), name);
                statusBar()->showMessage(
                    tr("Fetched %1 (%2 atoms) from the database")
                        .arg(name)
                        .arg(atomCount));
            });
    // "Group Selected into Single Trajectory File": several database entries
    // land in one multi-frame document, scrubable on the timeline and
    // saveable via File → Save Trajectory As.
    connect(&dialog, &ExamplesDialog::trajectoryFetched, this,
            [this](std::vector<std::shared_ptr<core::Structure>> frames,
                   const QString& name) {
                const auto frameCount = frames.size();
                if (addTrajectoryDocument(std::move(frames), name) < 0)
                    return;
                statusBar()->showMessage(
                    tr("Grouped %1 structures into one trajectory").arg(frameCount));
            });
    dialog.exec();
}

void MainWindow::openRayTraceDialog()
{
    Document* doc = currentDocument();
    if (!doc || !doc->structure || doc->structure->empty()) {
        statusBar()->showMessage(tr("Open a structure first."));
        return;
    }
    RayTraceDialog dialog(viewport_, this);
    // Frames are borrowed for the dialog's lifetime; it is modal, so the
    // document cannot be closed underneath it.
    dialog.setTrajectory(doc->frames);
    dialog.exec();
    // The dialog scrubs the viewport through the trajectory while rendering
    // and restores what it found; re-binding the document's own frame here
    // is the belt-and-braces version (and covers an aborted run). Deliberately
    // not syncViewsToCurrent(), which would reset the timeline playhead.
    viewport_->setStructure(doc->structure, /*frameCamera=*/false);
}

// ---------------------------------------------------------------------------
// Simulation
// ---------------------------------------------------------------------------

bool MainWindow::prepareSimulation(const QString& title)
{
    Document* doc = currentDocument();
    if (!doc || !doc->structure || doc->structure->empty()) {
        QMessageBox::information(this, title,
                                 tr("Open or build a structure first."));
        return false;
    }
    if (!ensureAseAvailable())
        return false;
    return true;
}

namespace {

/// Build the setup wizard for one orchestration node.
///
/// This is the catalogue the canvas deliberately does not own: OrchestrationWindow
/// schedules runs and stages their inputs, and knows nothing about which
/// modules exist. Keeping the switch here is what lets that panel be linked
/// into a headless test without dragging in fifteen wizards and their
/// generators.
///
/// The one rule every branch obeys: the baselines handed in are RELATIVE paths
/// that do not exist yet. They are the names the canvas guarantees to stage
/// before the node runs, so a wizard configured now against a parent that has
/// never executed still generates a script that will find its inputs. Nothing
/// here may test them for existence.
std::unique_ptr<SimulationWizardBase> makeOrchestrationWizard(
    const OrchestrationWindow::WizardRequest& request)
{
    // Several wizards take a mutable structure. The node holds a const
    // snapshot, so they get a copy rather than a const_cast: a wizard that
    // edited the node's structure in place would silently change what every
    // other node built from the same document computes.
    const auto mutableStructure = [&request] {
        return request.structure
            ? std::make_shared<core::Structure>(*request.structure)
            : nullptr;
    };
    const auto path = [&request](int slot) {
        return slot < request.baselines.size() ? request.baselines[slot].second
                                               : QString();
    };
    // One slot as the (label, path) list a setDensityBaselines() expects.
    const auto slotList = [&request](int slot) {
        QList<QPair<QString, QString>> list;
        if (slot < request.baselines.size())
            list.append(request.baselines[slot]);
        return list;
    };

    switch (request.task) {
    case OrchestrationTask::GeometryOptimization:
        return std::make_unique<GeometryOptimizationWizard>(request.structure);
    case OrchestrationTask::MolecularDynamics:
        return std::make_unique<MolecularDynamicsWizard>(request.structure);
    case OrchestrationTask::Phonon:
        return std::make_unique<PhononWizard>(/*periodic=*/true,
                                              request.structure);
    case OrchestrationTask::SinglePoint:
        return std::make_unique<SinglePointWizard>();

    case OrchestrationTask::ElectronicBands: {
        auto wizard = std::make_unique<ElectronicBandsWizard>(request.structure);
        wizard->setDensityBaselines(slotList(0));
        return wizard;
    }
    case OrchestrationTask::Optics: {
        auto wizard = std::make_unique<OpticsWizard>(mutableStructure());
        wizard->setDensityBaselines(slotList(0));
        return wizard;
    }
    case OrchestrationTask::Workfunction: {
        auto wizard = std::make_unique<WorkfunctionWizard>(mutableStructure());
        wizard->setDensityBaselines(slotList(0));
        return wizard;
    }
    case OrchestrationTask::TwoDBands: {
        auto wizard = std::make_unique<TwoDBandsWizard>(request.structure);
        wizard->setDensityBaselines(slotList(0));
        return wizard;
    }
    case OrchestrationTask::Wannier: {
        auto wizard = std::make_unique<WannierWizard>(mutableStructure());
        wizard->setDensityBaselines(slotList(0));
        return wizard;
    }
    case OrchestrationTask::BornCharges: {
        auto wizard = std::make_unique<BornChargesWizard>(request.structure);
        wizard->setDensityBaselines(slotList(0));
        return wizard;
    }
    case OrchestrationTask::Gw: {
        auto wizard = std::make_unique<GwWizard>();
        // GPAW list only, Yambo's empty: its baseline is a Quantum ESPRESSO
        // `.save` directory, and no node on this canvas produces one.
        wizard->setBaselines(slotList(0), {});
        return wizard;
    }
    case OrchestrationTask::ChargeDensityDifference: {
        auto wizard = std::make_unique<CddWizard>();
        QList<CddWizard::Baseline> baselines;
        if (!request.baselines.isEmpty())
            baselines.append({request.baselines.front().first, path(0),
                              request.structure});
        wizard->setDensityBaselines(std::move(baselines));
        return wizard;
    }
    case OrchestrationTask::RamanIr: {
        auto wizard = std::make_unique<RamanIrWizard>(request.structure);
        wizard->setDensityBaselines(slotList(0));
        // Both optional, and offered only when a parent is linked for them:
        // the wizard's own "(none)" entry is what an absent Born run means,
        // and the generated script then reports every IR intensity as zero
        // with ir.computed = false rather than as a plausible number.
        wizard->setBornChargesResults(slotList(1));
        wizard->setOpticsResults(slotList(2));
        return wizard;
    }
    case OrchestrationTask::ChargedDefects: {
        auto wizard = std::make_unique<DefectWizard>(request.structure);
        wizard->setDensityBaselines(request.baselines);
        return wizard;
    }
    case OrchestrationTask::ChargedDefects2d: {
        auto wizard = std::make_unique<Defect2dWizard>(request.structure);
        wizard->setDensityBaselines(request.baselines);
        return wizard;
    }

    case OrchestrationTask::Container:
    case OrchestrationTask::Supercell:
    case OrchestrationTask::DefectGenerator:
        // The transforms configure themselves on the canvas — a structure
        // list, three spin boxes, a table of edits. There is no engine to
        // pick and no script to generate, so there is no wizard to build and
        // OrchestrationWindow never asks for one.
        break;
    }
    return nullptr;
}

} // namespace

OrchestrationWindow* MainWindow::createOrchestrationPanel(QWidget* parent)
{
    // The panel is built once and lives in the dock, so the material list is
    // supplied by a callback rather than snapshotted: it must reflect the tabs
    // open at the moment "Add Process…" is pressed, not the (empty) set that
    // existed while the window was being constructed.
    const auto materialsNow = [this] {
        OrchestrationWindow::MaterialList materials;
        for (const auto& doc : documents_)
            if (doc && doc->structure && !doc->structure->empty())
                materials.append({doc->fileName, doc->structure});
        return materials;
    };
    // The global Processes panel rides along: every dispatched node shows
    // up there (Queued → Running → Completed/Failed) with its directory, so
    // "Load Result" works on orchestration jobs like on any wizard run.
    auto* window = new OrchestrationWindow(
        materialsNow(),
        [this](core::CalculatorKind kind) { return pythonForEngine(kind); },
        processPanel_, parent);
    window->setMaterialsProvider(materialsNow);
    window->setWizardFactory(&makeOrchestrationWizard);
    // Filling a Structure Container from the database. A dedicated picker,
    // not ExamplesDialog: that one exists to open documents in TABS, so its
    // actions are worded for that, and stacking its window-modal progress
    // dialog on top of the container editor left the window unresponsive.
    // The canvas asks through a callback so it never has to include any of it.
    window->setDatabaseImporter(&DatabaseImportDialog::pick);

    // Results-panel integration: an orchestration node's job is a process like any
    // other. Register its record and selector entry when it starts, poll its
    // metrics.json while it runs, finalize and persist when it ends — the
    // same lifecycle runScript()/onJobFinished() give a standalone job.
    connect(window, &OrchestrationWindow::nodeStarted, this,
            [this](int id, const QString& label, const QString& directory) {
                ProcessRecord record;
                record.label = label;
                record.directory = directory;
                processRecords_[id] = std::move(record);
                addProcessToSelector(id, label);
                orchestrationRunningIds_.insert(id);
                if (!metricsTimer_->isActive())
                    metricsTimer_->start();
            });
    // Live geometries from a running node. The trajectory tab is opened HERE,
    // on the first frame, rather than when the node starts: only relaxations
    // and MD runs stream anything, and the panel does not have to say in
    // advance which nodes those are — a node that streams nothing simply never
    // triggers this.
    connect(window, &OrchestrationWindow::nodeFrameStreamed, this,
            [this](int id, const std::shared_ptr<core::Structure>& frame) {
                if (!frame)
                    return;
                auto it = orchestrationLiveDocs_.find(id);
                if (it == orchestrationLiveDocs_.end()) {
                    const auto record = processRecords_.find(id);
                    const QString label = record != processRecords_.end()
                        ? record->second.label
                        : tr("Orchestration run");
                    // Seeded empty: the first streamed frame becomes frame 0,
                    // matching a standalone run — the input geometry carries no
                    // evaluated forces, so scrubbing onto it would blank the
                    // vector overlay every other frame has.
                    const int tab = addDocument(
                        std::make_shared<core::Structure>(*frame),
                        tr("%1 (live)").arg(label), {}, label);
                    it = orchestrationLiveDocs_
                             .emplace(id,
                                      documents_[static_cast<std::size_t>(tab)]
                                          .get())
                             .first;
                    tabBar_->setCurrentIndex(tab);
                }
                appendStreamedFrame(it->second, frame);
            });
    connect(window, &OrchestrationWindow::nodeFinished, this,
            [this](int id, bool success) {
                finalizeOrchestrationTrajectory(id, success);
                orchestrationRunningIds_.erase(id);
                if (auto it = processRecords_.find(id);
                    it != processRecords_.end()
                    && !it->second.directory.isEmpty()) {
                    // Final read so the last steps are captured, then persist
                    // like any finished process, and refresh the plots if
                    // this is the process the Results tabs are showing.
                    readMetricsJson(it->second.directory, it->second);
                    writeProcessMetrics(id);
                    if (id == selectedProcessId_)
                        syncResultsToProcess(id);
                }
                if (orchestrationRunningIds_.empty() && currentTaskId_ < 0)
                    metricsTimer_->stop();
            });

    // No WA_DeleteOnClose: the dock owns the panel for the life of the window.
    // Closing the dock hides it, and the pipeline on the canvas survives to be
    // reopened — which is the point of it being a panel rather than a dialog.
    return window;
}

void MainWindow::singlePointCalculation()
{
    if (!prepareSimulation(tr("Single-point Calculation")))
        return;
    SinglePointWizard wizard(this);

    // Calango's own engine is not a generated script — it runs here, in this
    // process. The dispatch has to happen before runSimulationWizard(), which
    // would otherwise stage a run.py and launch an interpreter against a
    // calculator that does not exist in Python.
    //
    // Hooked to the wizard's own accept rather than duplicating its setup:
    // exec() is called by runSimulationWizard, and this callback fires from
    // inside it when the chosen engine is ours.
    runSimulationWizard(wizard, tr("Single-Point Calculation"), /*expectFrames=*/false);
}

void MainWindow::runNativeDftSinglePoint(const core::Structure& structure)
{
    dft::Parameters parameters;
    dft::CalangoDFTEngine engine(parameters);
    const dft::CalangoDFTEngine::Result result = engine.run(structure);

    if (result.outcome.ok()) {
        // Unreachable today — the engine reports NotImplemented — and written
        // anyway so the success path exists and is obvious when it starts
        // being taken. Nothing here invents a number: everything shown comes
        // from the engine's own breakdown.
        statusBar()->showMessage(
            tr("Calango DFT — total %1 eV (%2 SCF iterations)")
                .arg(result.energy.total, 0, 'f', 6)
                .arg(result.scfIterations));
        return;
    }

    // Reported as a message box, not a status line. The engine produced NO
    // result, and a run that quietly leaves the status bar unchanged reads as
    // a run that worked.
    QString text = tr("The built-in Calango DFT engine did not produce a "
                      "result.\n\n%1")
                       .arg(QString::fromStdString(result.outcome.message));
    if (!result.log.empty()) {
        QStringList lines;
        for (const std::string& line : result.log)
            lines << QString::fromStdString(line);
        text += tr("\n\nRun context:\n%1").arg(lines.join(QLatin1Char('\n')));
    }
    QMessageBox::information(this, tr("Calango DFT"), text);
    statusBar()->showMessage(
        tr("Calango DFT: %1")
            .arg(QString::fromLatin1(dft::toString(result.outcome.status))));
}

void MainWindow::planeWaveCutoffConvergence()
{
    if (!prepareSimulation(tr("Plane-wave Cutoff Convergence")))
        return;
    CutoffConvergenceWizard wizard(this);
    runSimulationWizard(wizard, tr("Cutoff Convergence"),
                        /*expectFrames=*/false);
}

void MainWindow::kPointsConvergence()
{
    if (!prepareSimulation(tr("K-points Convergence")))
        return;
    KpointsConvergenceWizard wizard(this);
    runSimulationWizard(wizard, tr("K-points Convergence"),
                        /*expectFrames=*/false);
}

void MainWindow::runSimulationWizard(SimulationWizardBase& wizard,
                                     const QString& label, bool expectFrames)
{
    // Hand the wizard the active structure's chemistry before it opens, so
    // the per-element suggested defaults (~/.calango/calculator_parameters
    // .json) resolve for wizards that do not hold a structure themselves.
    if (const Document* doc = currentDocument(); doc && doc->structure) {
        wizard.setStructureElements(structureElements(doc->structure.get()));
        // xTB is the one engine for which this is not a hint but a hard
        // constraint — see SimulationWizardBase::setStructurePeriodic().
        const auto pbc = doc->structure->cell().pbc();
        wizard.setStructurePeriodic(doc->structure->cell().isDefined()
                                    && (pbc[0] || pbc[1] || pbc[2]));
    }

    if (wizard.exec() != QDialog::Accepted)
        return;

    // Persist the calculator provenance next to the job so a downstream
    // post-process (the MLWF wizard) can inherit the engine + parameters from
    // this completed run. stageJob() writes it as calculator.json and clears
    // the pending value.
    pendingCalculatorProvenance_ = wizard.calculatorProvenanceJson();

    // Calango's own engine runs HERE, not as a staged script. Handled before
    // every other action so nothing downstream has to know the difference.
    if (wizard.action() == SimulationWizardBase::Action::RunNativeEngine) {
        const Document* doc = currentDocument();
        if (doc && doc->structure)
            runNativeDftSinglePoint(*doc->structure);
        return;
    }

    if (wizard.action() == SimulationWizardBase::Action::RunRemote) {
        // Zone-11 Remote Access manager: stage the script and submit it.
        const QString jobDir = stageJob(wizard.script());
        if (jobDir.isEmpty())
            return;
        Document* doc = currentDocument();
        remoteDock_->show();
        remoteDock_->raise();
        const int taskId =
            processPanel_->registerTask(tr("Remote %1").arg(label), jobDir);
        processPanel_->setTaskStatus(taskId, ProcessManagerPanel::Status::Running);
        remotePanel_->submitStagedJob(
            jobDir, doc ? QFileInfo(doc->fileName).completeBaseName() : label);
        statusBar()->showMessage(tr("Submitting %1 run to the cluster…").arg(label));
        return;
    }
    runScript(wizard.script(), wizard.pythonExecutable(), label, expectFrames,
              wizard.calculatorKind(), wizard.runCommand());
}

void MainWindow::geometryOptimization()
{
    Document* doc = currentDocument();
    if (!doc || !doc->structure || doc->structure->empty()) {
        QMessageBox::information(this, tr("Geometry Optimization"),
                                 tr("Open or build a structure first."));
        return;
    }
    if (!ensureAseAvailable())
        return;
    // The structure goes in so the wizard's "Geometry constraints…" editor can
    // list the actual atoms (and the Hubbard editor can complete against the
    // species present).
    GeometryOptimizationWizard wizard(doc->structure, this);
    runSimulationWizard(wizard, tr("Geometry Optimization"));
}

void MainWindow::molecularDynamics()
{
    Document* doc = currentDocument();
    if (!doc || !doc->structure || doc->structure->empty()) {
        QMessageBox::information(this, tr("Molecular Dynamics"),
                                 tr("Open or build a structure first."));
        return;
    }
    if (!ensureAseAvailable())
        return;
    // The structure goes in so the wizard's "Geometry constraints…" editor can
    // list the actual atoms (and the Hubbard editor can complete against the
    // species present).
    MolecularDynamicsWizard wizard(doc->structure, this);
    runSimulationWizard(wizard, tr("Molecular Dynamics"));
}

void MainWindow::openMonteCarlo()
{
    Document* doc = currentDocument();
    if (!doc || !doc->structure || doc->structure->empty()) {
        QMessageBox::information(this, tr("Monte Carlo Simulation"),
                                 tr("Open or build a structure first."));
        return;
    }
    if (!ensureAseAvailable())
        return;
    MonteCarloWizard wizard(this);
    runSimulationWizard(wizard, tr("Monte Carlo Simulation"));
}

void MainWindow::openNudgedElasticBand()
{
    if (!ensureAseAvailable())
        return;
    if (documents_.empty()) {
        QMessageBox::information(
            this, tr("Nudged Elastic Band"),
            tr("Open the reactant and product structures first (as tabs), "
               "or load them from files inside the dialog."));
        return;
    }
    if (nebDialog_) { // non-modal: one instance, just resurface it
        nebDialog_->raise();
        nebDialog_->activateWindow();
        return;
    }

    std::vector<NebDialog::NamedStructure> docs;
    for (const auto& d : documents_)
        if (d->structure)
            docs.push_back({d->fileName, d->structure});

    nebDialog_ = new NebDialog(std::move(docs), this);
    connect(nebDialog_, &NebDialog::previewRequested, this,
            [this](const std::vector<std::shared_ptr<core::Structure>>& band) {
                if (band.empty())
                    return;
                std::vector<std::shared_ptr<core::Structure>> frames = band;
                const int tab = addDocument(
                    frames.front(),
                    tr("NEB preview (%1 images)").arg(frames.size()), frames);
                tabBar_->setCurrentIndex(tab);
            });
    connect(nebDialog_, &NebDialog::runRequested, this, [this] {
        if (!nebDialog_)
            return;
        stagedBandFrames_ = nebDialog_->band();
        runScript(nebDialog_->script(), nebDialog_->pythonExecutable(),
                  tr("NEB"), /*expectFrames=*/true,
                  nebDialog_->calculatorKind());
    });
    connect(nebDialog_, &QObject::destroyed, this, [this] { nebDialog_ = nullptr; });
    nebDialog_->show();
}

void MainWindow::addProcessToSelector(int id, const QString& label)
{
    if (!processSelector_)
        return;
    const QSignalBlocker block(processSelector_);
    processSelector_->addItem(QStringLiteral("#%1: %2").arg(id).arg(label), id);
    processSelector_->setCurrentIndex(processSelector_->count() - 1);
    selectedProcessId_ = id;
    syncResultsToProcess(id);
}

void MainWindow::onProcessSelected(int comboIndex)
{
    if (!processSelector_ || comboIndex < 0)
        return;
    selectedProcessId_ = processSelector_->itemData(comboIndex).toInt();
    syncResultsToProcess(selectedProcessId_);
}

void MainWindow::syncResultsToProcess(int id)
{
    auto it = processRecords_.find(id);
    if (it == processRecords_.end())
        return;
    ProcessRecord& r = it->second;
    // Lazily hydrate a record with no in-memory samples from its proc_<id>
    // directory (e.g. after reopening a project).
    if (r.energy.empty() && r.temperature.empty() && r.force.empty()
        && r.pressure.empty() && r.log.isEmpty() && !r.directory.isEmpty())
        loadProcessMetrics(id);

    const auto toSamples = [](const std::vector<std::pair<int, double>>& v) {
        std::vector<MetricPlotWidget::Sample> s;
        s.reserve(v.size());
        for (const auto& [step, value] : v)
            s.push_back({step, value});
        return s;
    };
    energyPlot_->clear();
    energyPlot_->setSamples(toSamples(r.energy));
    temperaturePlot_->clear();
    temperaturePlot_->setSamples(toSamples(r.temperature));
    if (r.hasTempTarget)
        temperaturePlot_->setTarget(r.tempTarget);
    forcePlot_->clear();
    forcePlot_->setSamples(toSamples(r.force));
    pressurePlot_->clear();
    pressurePlot_->setSamples(toSamples(r.pressure));
    if (r.hasPressTarget)
        pressurePlot_->setTarget(r.pressTarget);
    jobLogWidget_->restoreLog(r.log);
}

void MainWindow::onEnergySample(int step, double value)
{
    if (auto it = processRecords_.find(currentTaskId_); it != processRecords_.end())
        it->second.energy.emplace_back(step, value);
    if (currentTaskId_ == selectedProcessId_)
        energyPlot_->addSample(step, value);
}

void MainWindow::onTemperatureSample(int step, double value)
{
    if (auto it = processRecords_.find(currentTaskId_); it != processRecords_.end())
        it->second.temperature.emplace_back(step, value);
    if (currentTaskId_ == selectedProcessId_)
        temperaturePlot_->addSample(step, value);
}

void MainWindow::onForceSample(int step, double value)
{
    if (auto it = processRecords_.find(currentTaskId_); it != processRecords_.end())
        it->second.force.emplace_back(step, value);
    if (currentTaskId_ == selectedProcessId_)
        forcePlot_->addSample(step, value);
}

void MainWindow::onPressureSample(int step, double value)
{
    if (auto it = processRecords_.find(currentTaskId_); it != processRecords_.end())
        it->second.pressure.emplace_back(step, value);
    if (currentTaskId_ == selectedProcessId_)
        pressurePlot_->addSample(step, value);
}

void MainWindow::onTargetTemperature(double value)
{
    if (auto it = processRecords_.find(currentTaskId_); it != processRecords_.end()) {
        it->second.hasTempTarget = true;
        it->second.tempTarget = value;
    }
    if (currentTaskId_ == selectedProcessId_)
        temperaturePlot_->setTarget(value);
}

void MainWindow::onTargetPressure(double value)
{
    if (auto it = processRecords_.find(currentTaskId_); it != processRecords_.end()) {
        it->second.hasPressTarget = true;
        it->second.pressTarget = value;
    }
    if (currentTaskId_ == selectedProcessId_)
        pressurePlot_->setTarget(value);
}

void MainWindow::onJobOutputLine(const QString& line)
{
    if (auto it = processRecords_.find(currentTaskId_); it != processRecords_.end())
        it->second.log += line + QLatin1Char('\n');
    if (currentTaskId_ == selectedProcessId_)
        jobLogWidget_->onOutputLine(line);
}

void MainWindow::onJobErrorLine(const QString& line)
{
    // Keep the Results "Log" tab clean: Python runtime warnings (UserWarning,
    // DeprecationWarning, ResourceWarning, … from ASE/PyTorch/SciPy/GPAW) are
    // redirected to warnings.log rather than shown as errors. The generated
    // scripts already route the `warnings` module to warnings.log; this is the
    // backstop for warnings other libraries write straight to stderr.
    static const QRegularExpression warningRe(
        QStringLiteral(R"((?:\bUserWarning\b|\bDeprecationWarning\b|)"
                       R"(\bResourceWarning\b|\bFutureWarning\b|)"
                       R"(\bRuntimeWarning\b|\bPendingDeprecationWarning\b|)"
                       R"(\bImportWarning\b|Warning:|warnings\.warn))"));
    if (warningRe.match(line).hasMatch()) {
        if (auto it = processRecords_.find(currentTaskId_);
            it != processRecords_.end() && !it->second.directory.isEmpty()) {
            QFile warnFile(it->second.directory + QStringLiteral("/warnings.log"));
            if (warnFile.open(QIODevice::Append | QIODevice::Text))
                warnFile.write((line + QLatin1Char('\n')).toUtf8());
        }
        return; // never render warnings in the Log tab
    }

    if (auto it = processRecords_.find(currentTaskId_); it != processRecords_.end())
        it->second.log += line + QLatin1Char('\n');
    if (currentTaskId_ == selectedProcessId_)
        jobLogWidget_->onErrorLine(line);
}

void MainWindow::onJobProgress(int step, int total)
{
    if (currentTaskId_ == selectedProcessId_)
        jobLogWidget_->onProgress(step, total);
}

void MainWindow::writeProcessMetrics(int id)
{
    auto it = processRecords_.find(id);
    if (it == processRecords_.end() || it->second.directory.isEmpty())
        return;
    const ProcessRecord& r = it->second;
    const auto writeCsv = [&](const QString& name, const char* column,
                              const std::vector<std::pair<int, double>>& v) {
        if (v.empty())
            return;
        QFile file(r.directory + QLatin1Char('/') + name);
        if (!file.open(QIODevice::WriteOnly | QIODevice::Text))
            return;
        QTextStream out(&file);
        out << "step," << column << "\n";
        for (const auto& [step, value] : v)
            out << step << ',' << QString::number(value, 'g', 8) << '\n';
    };
    writeCsv(QStringLiteral("energy.csv"), "total_energy_eV", r.energy);
    writeCsv(QStringLiteral("temperature.csv"), "temperature_K", r.temperature);
    writeCsv(QStringLiteral("max_force.csv"), "max_force_eV_per_A", r.force);
    writeCsv(QStringLiteral("pressure.csv"), "pressure_GPa", r.pressure);
    if (!r.log.isEmpty()) {
        QFile file(r.directory + QStringLiteral("/log.txt"));
        if (file.open(QIODevice::WriteOnly | QIODevice::Text))
            QTextStream(&file) << r.log;
    }
}

bool MainWindow::readMetricsJson(const QString& directory,
                                 ProcessRecord& record) const
{
    QFile file(directory + QStringLiteral("/metrics.json"));
    if (!file.open(QIODevice::ReadOnly))
        return false;
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    if (!doc.isObject())
        return false;
    const QJsonObject root = doc.object();
    const QJsonArray metrics = root.value(QStringLiteral("metrics")).toArray();

    // Progress (step counts / percentage completion) now lives in metrics.json
    // rather than a CALANGO_PROGRESS stdout line.
    if (const QJsonObject p = root.value(QStringLiteral("progress")).toObject();
        !p.isEmpty()) {
        record.progressStep = p.value(QStringLiteral("step")).toInt();
        record.progressTotal = p.value(QStringLiteral("total")).toInt();
    }

    // Rebuild the series from scratch (metrics.json is the full history).
    record.energy.clear();
    record.temperature.clear();
    record.force.clear();
    record.pressure.clear();
    for (const QJsonValue& value : metrics) {
        const QJsonObject entry = value.toObject();
        const int step = entry.value(QStringLiteral("step")).toInt();
        const auto add = [&](const char* key,
                             std::vector<std::pair<int, double>>& series) {
            const QJsonValue v = entry.value(QLatin1String(key));
            if (v.isDouble())
                series.emplace_back(step, v.toDouble());
        };
        add("energy", record.energy);
        add("temperature", record.temperature);
        add("max_force", record.force);
        add("pressure", record.pressure);
    }
    return true;
}

void MainWindow::pollLiveMetrics()
{
    // Every live job feeds its record: the main window's own run plus any
    // orchestration-driven node jobs. Only the SELECTED process repaints the
    // plots, but the others' records still accumulate, so switching the
    // Results selector to an orchestration process mid-run shows its history.
    std::vector<int> liveIds;
    if (currentTaskId_ >= 0)
        liveIds.push_back(currentTaskId_);
    liveIds.insert(liveIds.end(), orchestrationRunningIds_.begin(),
                   orchestrationRunningIds_.end());

    auto it = processRecords_.end();
    for (int id : liveIds) {
        auto record = processRecords_.find(id);
        if (record == processRecords_.end()
            || record->second.directory.isEmpty())
            continue;
        if (!readMetricsJson(record->second.directory, record->second))
            continue;
        if (id == selectedProcessId_)
            it = record;
    }
    if (it == processRecords_.end())
        return;
    // Repaint the four metric plots + progress bar from the freshly-read data.
    const ProcessRecord& r = it->second;
    if (r.progressTotal > 0)
        jobLogWidget_->onProgress(r.progressStep, r.progressTotal);
    const auto toSamples = [](const std::vector<std::pair<int, double>>& v) {
        std::vector<MetricPlotWidget::Sample> s;
        s.reserve(v.size());
        for (const auto& [step, value] : v)
            s.push_back({step, value});
        return s;
    };
    energyPlot_->setSamples(toSamples(r.energy));
    temperaturePlot_->setSamples(toSamples(r.temperature));
    forcePlot_->setSamples(toSamples(r.force));
    pressurePlot_->setSamples(toSamples(r.pressure));
}

void MainWindow::loadProcessMetrics(int id)
{
    auto it = processRecords_.find(id);
    if (it == processRecords_.end() || it->second.directory.isEmpty())
        return;
    ProcessRecord& r = it->second;
    // metrics.json (written by the generated scripts) is the primary source;
    // fall back to the legacy per-series CSVs for older/other job types.
    if (readMetricsJson(r.directory, r)) {
        QFile logFile(r.directory + QStringLiteral("/log.txt"));
        if (logFile.open(QIODevice::ReadOnly | QIODevice::Text))
            r.log = QString::fromUtf8(logFile.readAll());
        return;
    }
    const auto readCsv = [&](const QString& name,
                             std::vector<std::pair<int, double>>& v) {
        QFile file(r.directory + QLatin1Char('/') + name);
        if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
            return;
        QTextStream in(&file);
        in.readLine(); // header
        while (!in.atEnd()) {
            const QStringList parts = in.readLine().split(QLatin1Char(','));
            if (parts.size() >= 2)
                v.emplace_back(parts[0].toInt(), parts[1].toDouble());
        }
    };
    readCsv(QStringLiteral("energy.csv"), r.energy);
    readCsv(QStringLiteral("temperature.csv"), r.temperature);
    readCsv(QStringLiteral("max_force.csv"), r.force);
    readCsv(QStringLiteral("pressure.csv"), r.pressure);
    QFile logFile(r.directory + QStringLiteral("/log.txt"));
    if (logFile.open(QIODevice::ReadOnly | QIODevice::Text))
        r.log = QString::fromUtf8(logFile.readAll());
}

QString MainWindow::stageJob(const QString& script, int procId)
{
    // Most jobs stage the current structure as structure.extxyz; a few
    // (e.g. MACE training, which reads its own dataset) run without one.
    Document* doc = currentDocument();

    // Managed session storage: jobs of a saved project live in a
    // .calango_tmp/ folder next to the .calproj (checkpoints, trajectory
    // dumps and logs stay with the project, reachable from the Process
    // panel); unsaved sessions fall back to the per-user app-data store.
    // A SAVED project keeps its jobs beside the .calproj so the project stays
    // self-contained and movable. An unsaved session has no such anchor, and
    // its runs now go to the user's own simulations folder (Preferences →
    // General) rather than to the platform's application-data location —
    // which is the right place for application state and the wrong one for a
    // trajectory somebody wants to keep.
    const QString jobsRoot = projectPath_.isEmpty()
        ? SettingsManager::simulationsDirectory()
        : QFileInfo(projectPath_).absolutePath()
            + QStringLiteral("/.calango_tmp");
    // Per-process metric store: proc_<id> keeps each run's outputs (energy.csv,
    // max_force.csv, temperature.csv, pressure.csv, run.py, log.txt) isolated;
    // paths without a process id (e.g. remote submissions) keep a timestamp.
    //
    // The Process-panel id counter resets to 0 each launch, so on a project
    // reopened with proc_0/, proc_1/ already on disk a fresh run would reuse
    // proc_0 and overwrite the earlier run's CSVs. Guard against that: if the
    // proc_<id> directory already exists, advance to the next free proc_<n> so
    // no prior run's metrics are ever clobbered.
    QString jobDir;
    if (procId >= 0) {
        int suffix = procId;
        do {
            jobDir = jobsRoot + QStringLiteral("/proc_%1").arg(suffix);
            ++suffix;
        } while (QDir(jobDir).exists());
    } else {
        jobDir = jobsRoot + QStringLiteral("/job_")
            + QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd_HHmmss"));
    }
    if (!QDir().mkpath(jobDir)) {
        QMessageBox::critical(this, tr("Run Calculation"),
                              tr("Could not create run directory %1").arg(jobDir));
        return {};
    }

    try {
        // Stage inputs: structure (extxyz round-trips everything) + script.
        if (doc && doc->structure)
            pybridge::AseBridge::writeStructure(
                *doc->structure,
                (jobDir + QStringLiteral("/structure.extxyz")).toStdString(),
                "extxyz");

        // NEB and other band jobs also stage the full image band as
        // band.extxyz; the member is consumed (cleared) per staging.
        if (!stagedBandFrames_.empty()) {
            pybridge::AseBridge::writeTrajectory(
                stagedBandFrames_,
                (jobDir + QStringLiteral("/band.extxyz")).toStdString(), "extxyz");
            stagedBandFrames_.clear();
        }

        // Band unfolding: the pristine primitive cell the supercell's bands
        // are projected back onto.
        if (stagedPrimitive_) {
            pybridge::AseBridge::writeStructure(
                *stagedPrimitive_,
                (jobDir + QStringLiteral("/primitive.extxyz")).toStdString(),
                "extxyz");
            stagedPrimitive_.reset();
        }

        // Cluster-expansion ensemble: the unrelaxed decorated configurations
        // the batch job iterates over.
        if (!stagedEnsembleFrames_.empty()) {
            pybridge::AseBridge::writeTrajectory(
                stagedEnsembleFrames_,
                (jobDir + QStringLiteral("/configs.extxyz")).toStdString(),
                "extxyz");
            stagedEnsembleFrames_.clear();
        }

        const QString scriptPath = jobDir + QStringLiteral("/run.py");
        QFile scriptFile(scriptPath);
        if (!scriptFile.open(QIODevice::WriteOnly | QIODevice::Text))
            throw std::runtime_error("Could not write " + scriptPath.toStdString());
        QTextStream(&scriptFile) << script;
        scriptFile.close();

        // Nothing else to stage for the script to run: its logging block is
        // embedded, so run.py alone is the whole program. That is what lets a
        // remote submission upload this directory and have the job work on a
        // machine where Calango has never been installed.

        // Calculator provenance sidecar: lets the MLWF wizard inherit the
        // engine + parameters + Conda env from this completed baseline. Written
        // only when the launcher supplied it (simulation wizards); consumed
        // (cleared) up front so an unrelated job never carries a stale copy.
        const QString provenance = pendingCalculatorProvenance_;
        pendingCalculatorProvenance_.clear();
        if (!provenance.isEmpty()) {
            QFile provenanceFile(jobDir + QStringLiteral("/calculator.json"));
            if (provenanceFile.open(QIODevice::WriteOnly | QIODevice::Text))
                QTextStream(&provenanceFile) << provenance;
        }
    } catch (const std::exception& e) {
        QMessageBox::critical(this, tr("Run Calculation"), QString::fromUtf8(e.what()));
        return {};
    }
    return jobDir;
}

void MainWindow::runScript(const QString& script, const QString& pythonExe,
                           const QString& taskLabel, bool expectFrames,
                           core::CalculatorKind kind, const QString& runCommand)
{
    const QString label = taskLabel.isEmpty() ? tr("Local calculation") : taskLabel;
    // Stamp the task onto the tab the run launches from. For frame-producing
    // runs (MD/relaxation) the live trajectory tab created below carries the
    // task instead, so the input tab keeps its own identity.
    if (!expectFrames) {
        if (Document* launchDoc = currentDocument()) {
            launchDoc->task = label;
            refreshTabTitles();
        }
    }
    // Allocate the process id first so the run stages into proc_<id>/ and its
    // metrics are recorded under that id.
    const int procId = processPanel_->registerTask(label, QString());
    const QString jobDir = stageJob(script, procId);
    if (jobDir.isEmpty()) {
        processPanel_->setTaskStatus(procId, ProcessManagerPanel::Status::Failed);
        return;
    }
    processPanel_->setTaskDirectory(procId, jobDir);

    isDirty_ = true; // the run console + metric series persist in .calproj
    ProcessRecord record;
    record.label = label;
    record.directory = jobDir;
    processRecords_[procId] = std::move(record);

    // Resolve the launch command: the engine's template from Preferences →
    // "Run", or the wizard's hand-edited "Running:" line when it supplied one.
    // Resolved NOW, not at launch: it depends on the engine and the wizard's
    // "Running:" line, neither of which survives to the moment a queued job
    // reaches the front.
    RunCommands::Context context;
    context.pythonExecutable = pythonExe;
    context.scriptFile = QStringLiteral("run.py");
    context.cores = RunCommands::cores();
    const RunCommands::Resolved resolved =
        RunCommands::resolve(kind, context, runCommand);

    QueuedJob job;
    job.processId = procId;
    job.label = label;
    job.jobDir = jobDir;
    job.pythonExecutable = pythonExe;
    job.commandLine = resolved.commandLine;
    job.environment = resolved.environment;
    job.expectFrames = expectFrames;
    // Snapshot the geometry the live tab would be seeded from. Deferring the
    // lookup to launch time would seed it from whatever tab happens to be
    // current then, which for a queued job is routinely a different structure
    // than the one the run was launched against.
    if (expectFrames) {
        if (Document* doc = currentDocument(); doc && doc->structure)
            job.liveSeed = std::make_shared<core::Structure>(*doc->structure);
    }

    if (jobRunner_->isRunning() || !jobQueue_.empty()) {
        // Queued, not refused. The job is fully staged — its directory exists
        // and its script is on disk, so it can be inspected (or deleted) while
        // it waits, and nothing about it depends on the state of the GUI when
        // its turn comes.
        //
        // The `!jobQueue_.empty()` half matters as much as the first: without
        // it a submission made in the window between one job finishing and the
        // queue being pumped would jump ahead of everything already waiting.
        jobQueue_.push_back(std::move(job));
        processPanel_->setTaskStatus(procId,
                                     ProcessManagerPanel::Status::Queued);
        jobDock_->show();
        jobDock_->raise();
        statusBar()->showMessage(
            tr("Queued %1 as process #%2 — %n job(s) waiting", nullptr,
               static_cast<int>(jobQueue_.size()))
                .arg(label)
                .arg(procId));
        return;
    }

    launchJob(job);
}

void MainWindow::launchJob(const QueuedJob& job)
{
    lastJobDir_ = job.jobDir;
    currentTaskId_ = job.processId;
    // Adding + selecting the process repopulates (clears) the tabs for the
    // fresh run; its live samples then flow into the now-selected process.
    addProcessToSelector(job.processId, job.label);
    processPanel_->setTaskStatus(job.processId,
                                 ProcessManagerPanel::Status::Running);

    // Live viewport streaming: MD/relaxation scripts emit CALANGO_FRAME
    // blocks — open the trajectory tab NOW and let frames pour in.
    liveDoc_ = nullptr;
    if (job.expectFrames && job.liveSeed) {
        // The input geometry is shown while the first frame is computed,
        // but it is NOT seeded as trajectory frame 0: it carries no
        // evaluated forces or velocities, so scrubbing onto it blanked the
        // vector overlay that every other frame has. The trajectory starts
        // empty and the run's first streamed frame becomes frame 0.
        auto first = std::make_shared<core::Structure>(*job.liveSeed);
        const int tab = addDocument(
            first, tr("%1 (live)").arg(job.label), {}, job.label);
        liveDoc_ = documents_[static_cast<std::size_t>(tab)].get();
        tabBar_->setCurrentIndex(tab);
    }

    jobDock_->show();
    jobDock_->raise();
    jobRunner_->start(job.commandLine, job.pythonExecutable, job.jobDir,
                      job.environment);
    metricsTimer_->start(); // poll metrics.json for live Results-graph updates
    statusBar()->showMessage(tr("Running in %1 — %2")
                                 .arg(job.jobDir, job.commandLine));
}

void MainWindow::startNextQueuedJob()
{
    if (jobQueue_.empty() || jobRunner_->isRunning())
        return;
    const QueuedJob job = std::move(jobQueue_.front());
    jobQueue_.pop_front();
    // The staging directory is the job. If it has gone — the user deleted the
    // process while it waited, or cleared the simulations folder — there is
    // nothing to run, so drop it and move on rather than starting a shell in a
    // directory that does not exist.
    if (!QDir(job.jobDir).exists()) {
        processPanel_->setTaskStatus(job.processId,
                                     ProcessManagerPanel::Status::Failed);
        startNextQueuedJob();
        return;
    }
    launchJob(job);
    if (!jobQueue_.empty()) {
        statusBar()->showMessage(
            tr("Started %1 — %n job(s) still queued", nullptr,
               static_cast<int>(jobQueue_.size()))
                .arg(job.label));
    }
}

int MainWindow::indexOfDocument(const Document* document) const
{
    for (std::size_t i = 0; i < documents_.size(); ++i)
        if (documents_[i].get() == document)
            return static_cast<int>(i);
    return -1;
}

void MainWindow::onFrameStreamed(const std::shared_ptr<core::Structure>& frame)
{
    appendStreamedFrame(liveDoc_, frame);
}

void MainWindow::finalizeOrchestrationTrajectory(int processId, bool success)
{
    // The streamed case: the node already owns a trajectory tab, so all that
    // is left is to drop the "(live)" marker and land the playhead on the
    // answer — the same finish a standalone run gets in onJobFinished().
    if (const auto it = orchestrationLiveDocs_.find(processId);
        it != orchestrationLiveDocs_.end()) {
        Document* streamed = it->second;
        orchestrationLiveDocs_.erase(it);
        const int index = indexOfDocument(streamed);
        if (index >= 0 && streamed->frames.size() > 1) {
            streamed->fileName.replace(tr(" (live)"), QString());
            tabBar_->setTabText(index, streamed->fileName);
            if (tabBar_->currentIndex() == index)
                syncViewsToCurrent(false);
            showFinalFrame(streamed);
            if (success)
                statusBar()->showMessage(
                    tr("Orchestration node finished — %n streamed frame(s)", nullptr,
                       static_cast<int>(streamed->frames.size())));
            return;
        }
        // One frame is not a trajectory; drop the placeholder tab rather than
        // leaving a timeline with a single position on it.
        if (index >= 0 && streamed->frames.size() <= 1)
            onTabCloseRequested(index);
    }

    if (!success)
        return;

    // Nothing streamed. A relaxation or MD node still WROTE its trajectory, so
    // load that instead of leaving the timeline empty — this is the half of the
    // bug that survives even when live streaming is unavailable (a remote run,
    // or a script whose engine writes frames only at the end).
    const auto record = processRecords_.find(processId);
    if (record == processRecords_.end() || record->second.directory.isEmpty())
        return;
    // Same precedence as onProcessResultRequested's trajectory fallback:
    // md.extxyz before md.traj because it carries the per-atom forces and
    // velocities the vector overlay reads.
    for (const auto* candidate : {"md.extxyz", "md.traj", "opt.traj",
                                  "optimized.extxyz", "md_final.extxyz"}) {
        const QString path =
            record->second.directory + QLatin1Char('/') + QLatin1String(candidate);
        if (QFile::exists(path)) {
            loadFile(path);
            if (Document* doc = currentDocument(); doc && doc->frames.size() > 1)
                showFinalFrame(doc);
            return;
        }
    }
}

void MainWindow::appendStreamedFrame(
    Document* target, const std::shared_ptr<core::Structure>& frame)
{
    const int index = target ? indexOfDocument(target) : -1;
    if (index < 0 || !frame)
        return;
    Document& doc = *target;
    const bool followTail = static_cast<std::size_t>(timeline_->currentFrame())
        + 1 >= doc.frames.size();
    doc.frames.push_back(frame);
    isDirty_ = true;

    if (tabBar_->currentIndex() != index)
        return; // tab exists and accumulates; views update on switch
    timeline_->extendFrameCount(static_cast<int>(doc.frames.size()));
    timeline_->show();
    if (doc.frames.size() == 1) {
        // First frame of the run. The playhead is already at 0, so
        // setCurrentFrame(0) would emit nothing and the viewport would keep
        // showing the (unevaluated) input geometry until frame 2 arrived —
        // visible now that the input is no longer seeded as frame 0.
        showFrame(0);
        return;
    }
    if (followTail) // keep tracking the newest frame unless the user scrubbed
        timeline_->setCurrentFrame(static_cast<int>(doc.frames.size()) - 1);
}

void MainWindow::onRemoteResultsReady(const QString& localDir)
{
    // Same convention as local jobs: trajectories first (they activate
    // the timeline), then a bare optimized structure.
    for (const auto* candidate :
         {"md.traj", "opt.traj", "optimized.extxyz"}) {
        const QString path = localDir + QLatin1Char('/') + QLatin1String(candidate);
        if (QFile::exists(path)) {
            loadFile(path);
            // Same rule as a local run: a finished trajectory is shown at its
            // final step, not rewound to the input geometry.
            showFinalFrame(currentDocument());
            statusBar()->showMessage(
                tr("Remote results loaded from %1").arg(localDir));
            return;
        }
    }
    statusBar()->showMessage(
        tr("Remote run finished — results in %1").arg(localDir));
}

void MainWindow::onJobFinished(int exitCode, bool crashed)
{
    // The timer also serves any orchestration node still executing — it only
    // rests when nothing at all is live.
    if (orchestrationRunningIds_.empty())
        metricsTimer_->stop();
    const bool failed = crashed || exitCode != 0;
    if (currentTaskId_ >= 0) {
        processPanel_->setTaskStatus(currentTaskId_,
                                     failed ? ProcessManagerPanel::Status::Failed
                                            : ProcessManagerPanel::Status::Completed);
        // Final read of metrics.json so the last steps are captured, then
        // persist this process's metrics + log to proc_<id>/ so they survive
        // subsequent runs and can be reloaded from the Results selector.
        pollLiveMetrics();
        writeProcessMetrics(currentTaskId_);
        currentTaskId_ = -1;
    }

    // A live-streamed run already owns its trajectory tab — finalize its
    // title and skip the legacy end-of-job trajectory load.
    if (liveDoc_) {
        const int index = indexOfDocument(liveDoc_);
        Document* streamed = liveDoc_;
        liveDoc_ = nullptr;
        if (index >= 0 && streamed->frames.size() > 1) {
            streamed->fileName.replace(tr(" (live)"), QString());
            tabBar_->setTabText(index, streamed->fileName);
            if (tabBar_->currentIndex() == index)
                syncViewsToCurrent(false);
            // The run is over, so its answer is the last frame — and
            // syncViewsToCurrent() above has just rewound the timeline to 0.
            showFinalFrame(streamed);
            if (!failed)
                statusBar()->showMessage(
                    tr("Run finished — %n streamed frame(s)", nullptr,
                       static_cast<int>(streamed->frames.size())));
            return;
        }
        // Nothing streamed (e.g. single-point) — drop the placeholder tab.
        if (index >= 0 && streamed->frames.size() <= 1)
            onTabCloseRequested(index);
    }

    if (failed || lastJobDir_.isEmpty())
        return;

    // Electronic-structure runs: open the band/PDOS viewer directly.
    if (QFile::exists(lastJobDir_ + QStringLiteral("/bands.json"))) {
        openBandResults(lastJobDir_);
        return;
    }
    // Phonon runs: open the Phonon Viewer (dispersion + PhDOS).
    if (QFile::exists(lastJobDir_ + QStringLiteral("/phonon_band.json"))) {
        openPhononResults(lastJobDir_);
        return;
    }
    // Optics runs: open the optical-spectra viewer.
    if (QFile::exists(lastJobDir_ + QStringLiteral("/optics.json"))) {
        openOpticsResults(lastJobDir_);
        return;
    }
    // Nonlinear optics: χ⁽²⁾ / shift current / χ⁽¹⁾ spectra.
    if (QFile::exists(lastJobDir_ + QStringLiteral("/nlopt.json"))) {
        openNonlinearOpticsResults(lastJobDir_);
        return;
    }
    // XAS runs: open the spectrum viewer.
    if (QFile::exists(lastJobDir_ + QStringLiteral("/xas.json"))) {
        openXasResults(lastJobDir_);
        return;
    }
    // MLWF runs: open the dedicated MLWF viewer (centres/spreads table, orbital
    // isosurface overlays on the viewport, band-interpolation launcher).
    if (QFile::exists(lastJobDir_ + QStringLiteral("/wannier.json"))) {
        openMlwfResults(lastJobDir_);
        return;
    }
    // 2D Bands: open the surface viewer.
    if (QFile::exists(lastJobDir_ + QStringLiteral("/bands_2d.json"))) {
        open2DBandsResults(lastJobDir_);
        return;
    }
    // 2D Workfunction: open the V̄(z) viewer with the Φ headline.
    if (QFile::exists(lastJobDir_ + QStringLiteral("/workfunction.json"))) {
        openWorkfunctionResults(lastJobDir_);
        return;
    }
    // Charged defects: open the formation-energy diagram.
    if (QFile::exists(lastJobDir_ + QStringLiteral("/charged_defects.json"))) {
        openChargedDefectResults(lastJobDir_);
        return;
    }
    if (QFile::exists(lastJobDir_
                      + QStringLiteral("/charged_defects_2d.json"))) {
        open2DChargedDefectResults(lastJobDir_);
        return;
    }
    if (QFile::exists(lastJobDir_ + QStringLiteral("/topology.json"))) {
        openTopologyResults(lastJobDir_);
        return;
    }
    if (QFile::exists(lastJobDir_ + QStringLiteral("/fermi_surface.json"))) {
        openFermiSurfaceResults(lastJobDir_);
        return;
    }
    // Born effective charges: open the Z* tensor table.
    if (QFile::exists(lastJobDir_ + QStringLiteral("/born_charges.json"))) {
        openBornChargesResults(lastJobDir_);
        return;
    }
    // Charge density difference: the cube is registered by the sweep below like
    // any other grid; what needs saying here is the integrated result, because
    // "how much charge moved" is the number the isosurface is qualitative
    // about.
    if (QFile::exists(lastJobDir_ + QStringLiteral("/cdd.json"))) {
        QFile file(lastJobDir_ + QStringLiteral("/cdd.json"));
        if (file.open(QIODevice::ReadOnly)) {
            const QJsonObject o =
                QJsonDocument::fromJson(file.readAll()).object();
            statusBar()->showMessage(
                tr("Δρ(%1 | %2): %3 e redistributed — loaded into the "
                   "Volumetric Data dock.")
                    .arg(o.value(QStringLiteral("formula_a")).toString(),
                         o.value(QStringLiteral("formula_b")).toString())
                    .arg(o.value(QStringLiteral("charge_transferred")).toDouble(),
                         0, 'f', 4),
                12000);
        }
    }

    // Volumetric fields: every .cube the run wrote goes into the Volumetric
    // Data dock, which is where they are now viewed — ELF included, since it
    // renders in the main viewport like any other grid rather than in a dialog
    // of its own.
    //
    // A single-point with the density exports enabled writes up to six of
    // them; the old code looked for one hard-coded name and left the rest in
    // the job directory, which is exactly the "generated but not transferred"
    // symptom.
    if (const int cubes = registerDensityCubes(lastJobDir_); cubes > 0) {
        statusBar()->showMessage(
            tr("%n volumetric field(s) added to the Volumetric Data dock.",
               nullptr, cubes),
            6000);
        if (volumetricDock_) {
            volumetricDock_->show();
            volumetricDock_->raise();
        }
        // A grid-only export has nothing else to open.
        if (!QFile::exists(lastJobDir_ + QStringLiteral("/single_point.json")))
            return;
    }
    // Convergence sweeps: open the two-panel convergence window. Before the
    // single-point check — a sweep writes no single_point.json, but its
    // metrics.json must not route it into the MD/trajectory fallbacks either.
    if (QFile::exists(lastJobDir_
                      + QStringLiteral("/cutoff_convergence.json"))) {
        openCutoffConvergenceResults(lastJobDir_);
        return;
    }
    if (QFile::exists(lastJobDir_
                      + QStringLiteral("/kpoints_convergence.json"))) {
        openKpointsConvergenceResults(lastJobDir_);
        return;
    }
    // Single-point runs: open the dedicated summary viewer.
    if (QFile::exists(lastJobDir_ + QStringLiteral("/single_point.json"))) {
        // First, hand the converged per-atom results to the structure on
        // screen, so the viewport's "Magnetic moment" and "Force" overlays
        // draw the RESULT rather than staying empty (or, worse, showing only
        // the initial guess). A single point writes no trajectory, so this
        // file is the only place those columns ever appear.
        adoptSinglePointResults(lastJobDir_);
        openSinglePointResults(lastJobDir_);
        return;
    }

    // MD / optimization runs: open the trajectory automatically in a new
    // tab — the timeline comes pre-loaded and ready to scrub.
    for (const auto* trajectory : {"opt.traj", "md.traj"}) {
        const QString trajectoryPath = lastJobDir_ + QLatin1Char('/')
            + QLatin1String(trajectory);
        if (!QFile::exists(trajectoryPath))
            continue;
        loadFile(trajectoryPath);
        // Opened at frame 0 like any trajectory; a finished relaxation or MD
        // run is read from its end, so land the playhead there.
        showFinalFrame(currentDocument());
        statusBar()->showMessage(
            tr("Run finished — trajectory %1 opened in a new tab at its final "
               "step")
                .arg(QLatin1String(trajectory)),
            8000);
        return;
    }

    // Otherwise offer the final structure, if the job produced one.
    for (const auto* candidate : {"optimized.extxyz", "md_final.extxyz"}) {
        const QString resultPath = lastJobDir_ + QLatin1Char('/')
            + QLatin1String(candidate);
        if (!QFile::exists(resultPath))
            continue;
        const auto answer = QMessageBox::question(
            this, tr("Run Finished"),
            tr("The run produced %1.\nLoad it into a new tab?")
                .arg(QLatin1String(candidate)));
        if (answer == QMessageBox::Yes)
            loadFile(resultPath);
        return;
    }
}


void MainWindow::about()
{
    const auto& python = pybridge::PythonEngine::instance();

    // Runtime diagnostics as aligned key/value rows. An HTML table keeps the
    // values column-aligned regardless of key length (a <pre> block would need
    // hand-padding); the labels below match the requested matrix exactly.
    //
    // sys.version reads e.g. "3.12.2 (main, ...) [Clang ...]" — take just the
    // leading whitespace-delimited token so only the clean semantic version is
    // shown (the compiler/build metadata is redundant with the Compiler row).
    const QString pythonVersion = QString::fromStdString(python.pythonVersion())
                                      .section('\n', 0, 0)
                                      .simplified()
                                      .section(' ', 0, 0);
    const QString aseVersion = python.aseAvailable()
                                   ? QString::fromStdString(python.aseVersion())
                                   : tr("not available");

    const auto row = [](const QString& key, const QString& value) {
        return QStringLiteral(
                   "<tr><td style='padding-right:14px; white-space:nowrap;'>"
                   "<b>%1</b></td><td>%2</td></tr>")
            .arg(key.toHtmlEscaped(), value.toHtmlEscaped());
    };
    const QString diagnostics =
        QStringLiteral("<table cellspacing='0' cellpadding='0'>%1%2%3%4</table>")
            .arg(row(tr("Python"), pythonVersion),
                 row(tr("C/C++ Compiler"), compilerVersionString()),
                 row(tr("Qt Framework"), QStringLiteral(QT_VERSION_STR)),
                 row(tr("ASE (Atomic Simulation Environment)"), aseVersion));

    // Open-source dependencies and their licenses. This is license
    // transparency, not a build manifest: everything Calango links against,
    // embeds, or drives at runtime is named together with the terms it is
    // distributed under. Keep this list in step with CMakeLists.txt (linked
    // libraries) and the packages the generated scripts import.
    struct Dependency {
        const char* name;
        QString role;
        const char* license;
    };
    const Dependency dependencies[] = {
        {"Qt 6", tr("cross-platform GUI, OpenGL viewport and concurrency"),
         "LGPL v3"},
        {"Python", tr("embedded interpreter driving every simulation"),
         "PSF License"},
        {"pybind11", tr("C++ ↔ Python bridge for the embedded interpreter"),
         "BSD 3-Clause"},
        {"ASE", tr("atomistic structures, calculators and dynamics"),
         "LGPL v2.1+"},
        {"GPAW", tr("density-functional theory engine (PAW / plane waves)"),
         "GPL v3+"},
        {"NumPy", tr("numerical arrays underpinning the Python tool-chain"),
         "BSD 3-Clause"},
        {"spglib", tr("crystal-symmetry detection and space groups"),
         "BSD 3-Clause"},
        {"phonopy", tr("phonon band structures, thermodynamics and "
                       "irreducible representations"),
         "BSD 3-Clause"},
        {"dftd4", tr("Grimme DFT-D4 van der Waals dispersion (dynamically "
                     "linked shared library)"),
         "LGPL v3+"},
        {"torch-dftd", tr("PyTorch DFT-D dispersion backing MACE-MP's "
                          "dispersion option"),
         "MIT"},
        {"MACE", tr("machine-learning interatomic potentials"), "MIT"},
        {"icet", tr("cluster expansions and special quasirandom structures"),
         "MIT"},
        {"Remix Icon", tr("the icon set used throughout the interface"),
         "Apache 2.0"},
    };
    QString dependencyRows;
    for (const auto& dependency : dependencies) {
        dependencyRows += QStringLiteral(
                              "<tr><td style='padding-right:14px; "
                              "white-space:nowrap;'><b>%1</b></td>"
                              "<td style='padding-right:14px;'>%2</td>"
                              "<td style='white-space:nowrap;'>%3</td></tr>")
                              .arg(QLatin1String(dependency.name),
                                   dependency.role.toHtmlEscaped(),
                                   QLatin1String(dependency.license));
    }
    const QString dependencyTable =
        QStringLiteral("<table cellspacing='0' cellpadding='1'>%1</table>")
            .arg(dependencyRows);

    // A plain QDialog rather than QMessageBox: a message box sizes itself to
    // its text with no upper bound, and with the dependency table the About
    // content outgrew smaller screens, pushing the bottom rows and the OK
    // button past the screen edge. Here the long content scrolls inside a
    // dialog clamped to the available screen, so every row stays reachable
    // however small the display — and on a normal one nothing scrolls at all.
    QDialog dialog(this);
    dialog.setWindowTitle(tr("About Calango"));
    auto* layout = new QVBoxLayout(&dialog);

    // Brand banner: the bare mark rather than either platform app icon, so
    // the dialog does not show a second rounded plate inside the dialog's
    // own. 96 px — the previous 140 px banner was a third of a small screen's
    // height on its own.
    auto* header = new QHBoxLayout;
    auto* logo = new QLabel(&dialog);
    logo->setPixmap(
        QPixmap(QStringLiteral(":/assets/calango/logo.png"))
            .scaled(96, 96, Qt::KeepAspectRatio, Qt::SmoothTransformation));
    logo->setAlignment(Qt::AlignTop);
    header->addWidget(logo);
    auto* heading = new QLabel(
        tr("<h3>Calango %1</h3>"
           "<p>For visual atomistic modeling</p>"
           "<p><b>Developed by</b><br>Leandro Seixas Rocha</p>")
            .arg(QStringLiteral(CALANGO_VERSION)),
        &dialog);
    heading->setTextFormat(Qt::RichText);
    heading->setWordWrap(true);
    header->addWidget(heading, 1);
    layout->addLayout(header);

    // Runtime diagnostics stay with the header: they describe THIS install
    // (which Python, which Qt, which ASE), which is the same kind of fact as
    // the version number above them, and putting them in a tab would hide the
    // first thing anyone opening About is usually looking for.
    auto* diagnosticsLabel = new QLabel(
        tr("<p><b>Runtime &amp; build diagnostics</b></p>%1").arg(diagnostics),
        &dialog);
    diagnosticsLabel->setTextFormat(Qt::RichText);
    diagnosticsLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    layout->addWidget(diagnosticsLabel);

    // Two tabs, because they answer two different questions and one of them is
    // a legal document. "What may I do with Calango itself?" is the licence on
    // the left; "what is Calango built out of, and under what terms?" is the
    // inventory on the right. They used to run together in one scrolling
    // column, where the project's own terms were a heading among a dozen
    // others — which is precisely the item a reader is most likely to want on
    // its own.
    auto* tabs = new QTabWidget(&dialog);

    // --- Tab 1: Calango's own licence, and nothing else -------------------
    auto* licenseView = new QPlainTextEdit(&dialog);
    licenseView->setReadOnly(true);
    // Monospaced and unwrapped: a licence is a legal text whose line breaks
    // are part of the document, and a proportional font reflowing it makes it
    // read as prose someone paraphrased.
    licenseView->setFont(
        QFontDatabase::systemFont(QFontDatabase::FixedFont));
    licenseView->setLineWrapMode(QPlainTextEdit::NoWrap);
    QString licenseText;
    if (QFile licenseFile(QStringLiteral(":/LICENSE"));
        licenseFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
        licenseText = QString::fromUtf8(licenseFile.readAll());
    } else {
        // The resource is compiled in, so this is a build-configuration fault
        // rather than a missing file — say so instead of showing a blank tab
        // that reads as "Calango has no licence".
        licenseText = tr("The bundled LICENSE resource could not be read. "
                         "The project's licence is the LICENSE file at the "
                         "root of the source tree.");
    }
    licenseView->setPlainText(licenseText);
    tabs->addTab(licenseView, tr("License"));

    // --- Tab 2: everything Calango is built on ----------------------------
    auto* dependencyBody = new QLabel(
        tr("<p>Calango is built on the following open-source software; each "
           "component remains under its own license:</p>"
           "%1"
           "<p style='margin-top:8px;'>GPL/LGPL components are dynamically "
           "linked or invoked as separate tools; their source code is "
           "available from the respective upstream projects.</p>")
            .arg(dependencyTable),
        &dialog);
    dependencyBody->setTextFormat(Qt::RichText);
    dependencyBody->setWordWrap(true);
    dependencyBody->setTextInteractionFlags(Qt::TextSelectableByMouse);
    dependencyBody->setAlignment(Qt::AlignTop);

    auto* dependencyScroll = new QScrollArea(&dialog);
    dependencyScroll->setWidget(dependencyBody);
    dependencyScroll->setWidgetResizable(true);
    dependencyScroll->setFrameShape(QFrame::NoFrame);
    dependencyScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    tabs->addTab(dependencyScroll, tr("Third-Party Licenses"));

    // --- Tab 3: funding acknowledgements ----------------------------------
    // A tab of its own rather than a line in the header: a funding agency's
    // acknowledgement has to be discoverable by name, and the header is
    // already the densest block of the dialog.
    auto* acknowledgements = new QWidget(&dialog);
    auto* acknowledgementsLayout = new QVBoxLayout(acknowledgements);
    auto* inctLogo = new QLabel(acknowledgements);
    const QPixmap inctPixmap(QStringLiteral(
        ":/assets/acknowledgements/inct_materials_informatics.png"));
    if (!inctPixmap.isNull()) {
        // Scale by width only: the source art's aspect ratio is preserved and
        // 420 px sits comfortably inside the dialog's clamped width.
        inctLogo->setPixmap(inctPixmap.scaledToWidth(
            420, Qt::SmoothTransformation));
    }
    inctLogo->setAlignment(Qt::AlignHCenter);
    acknowledgementsLayout->addWidget(inctLogo);
    auto* acknowledgementsText = new QLabel(
        tr("We thank financial support from INCT Materials Informatics "
           "(Grant No. 406447/2022-5)."),
        acknowledgements);
    acknowledgementsText->setWordWrap(true);
    acknowledgementsText->setAlignment(Qt::AlignHCenter);
    acknowledgementsText->setTextInteractionFlags(Qt::TextSelectableByMouse);
    acknowledgementsLayout->addWidget(acknowledgementsText);
    acknowledgementsLayout->addStretch(1);
    tabs->addTab(acknowledgements, tr("Acknowledgements"));

    layout->addWidget(tabs, 1);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok, &dialog);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    layout->addWidget(buttons);

    // Fit the content, clamped to the screen the dialog will appear on. The
    // width is fixed first so the word-wrapped body's height is knowable;
    // the +150 covers header, button row and layout margins.
    const QScreen* screen =
        dialog.screen() ? dialog.screen() : QGuiApplication::primaryScreen();
    const QSize available = screen->availableGeometry().size();
    const int width = std::min(680, available.width() * 9 / 10);
    // Sized on the dependency table, which is the taller of the two tabs; the
    // licence scrolls inside its own view. +260 covers the header, the
    // diagnostics block, the tab bar and the button row.
    const int contentHeight =
        dependencyBody->heightForWidth(width - 60) + 260;
    dialog.resize(width,
                  std::min(contentHeight, available.height() * 85 / 100));
    dialog.exec();
}

} // namespace calango::gui
