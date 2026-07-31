cmake_minimum_required(VERSION 3.21)

project(calango
    VERSION 26.7.80
    DESCRIPTION "Calango — For visual atomistic modeling."
    LANGUAGES CXX
)

# The single source of truth for the user-facing version is the
# project(VERSION ...) above: it is baked into the binary as the
# CALANGO_VERSION compile definition (About dialog, project files) and
# reused by CPack for the installer file names.

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Without an explicit build type CMake passes no optimization flags at all, so
# a plain `cmake -B build` produced an unoptimized binary that looked like a
# performance problem in the app (isosurface extraction and neighbor searches
# are several times slower). Default to Release; multi-config generators
# (Xcode / Visual Studio) pick per-build and must not be overridden here.
get_property(_calango_multi_config GLOBAL PROPERTY GENERATOR_IS_MULTI_CONFIG)
if(NOT _calango_multi_config AND NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING
        "Build type (Debug, Release, RelWithDebInfo, MinSizeRel)" FORCE)
    message(STATUS "No CMAKE_BUILD_TYPE set — defaulting to Release")
endif()
set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS
    Debug Release RelWithDebInfo MinSizeRel)

# ---------------------------------------------------------------------------
# Dependencies
# ---------------------------------------------------------------------------

find_package(Qt6 6.4 REQUIRED COMPONENTS Widgets OpenGLWidgets Concurrent Svg)
qt_standard_project_setup()   # enables AUTOMOC / AUTORCC / AUTOUIC

# Recent macOS SDKs removed the ancient AGL framework, but Qt's WrapOpenGL
# helper can still pick up the /System/Library stub and break the link.
if(APPLE AND TARGET WrapOpenGL::WrapOpenGL)
    get_target_property(_wrap_opengl_libs WrapOpenGL::WrapOpenGL INTERFACE_LINK_LIBRARIES)
    if(_wrap_opengl_libs)
        list(FILTER _wrap_opengl_libs EXCLUDE REGEX "AGL")
        set_target_properties(WrapOpenGL::WrapOpenGL PROPERTIES
            INTERFACE_LINK_LIBRARIES "${_wrap_opengl_libs}")
    endif()
endif()

# On macOS, FindOpenGL can reference the AGL framework, which recent SDKs
# no longer ship — link the OpenGL framework directly instead.
if(NOT APPLE)
    find_package(OpenGL REQUIRED)
endif()

# The interpreter found here is the one Calango embeds at runtime.
# Point CMake at a virtualenv that has ASE installed:
#   cmake -B build -DPython3_EXECUTABLE=/path/to/.venv/bin/python
find_package(Python3 3.9 REQUIRED COMPONENTS Interpreter Development.Embed)

# Make pybind11 reuse the interpreter/library found by FindPython3 above
# (its new-style FindPython path) instead of its legacy FindPythonLibs
# search, which ignores Python3_EXECUTABLE and picks up whatever `python`
# is first on PATH (e.g. a Conda/Homebrew libpython). Without this the
# binary links a non-system libpython, which breaks dpkg-shlibdeps and
# makes the .deb non-portable.
set(PYBIND11_FINDPYTHON ON)

# 2.12+ is needed for scoped_interpreter(PyConfig*).
find_package(pybind11 2.12 CONFIG QUIET)
if(NOT pybind11_FOUND)
    message(STATUS "System pybind11 not found — fetching it")
    include(FetchContent)
    FetchContent_Declare(pybind11
        GIT_REPOSITORY https://github.com/pybind/pybind11.git
        GIT_TAG        v2.13.6
    )
    FetchContent_MakeAvailable(pybind11)
endif()

# Embedded Python helper modules: there are none.
#
# assets/calango/scripts/calango_log.py used to be baked into a generated
# header so the Qt-free core/ layer could stage it beside every generated
# script, which imported it. That made a generated script a two-file bundle
# and broke the one thing the generator exists to promise — a .py you can
# copy to an HPC box and run under stock ASE. The logging block is now
# embedded in the scripts themselves (AseScriptGenerator::jsonLoggerPreamble),
# so the module, the generated header and this whole configure step are gone.

# ---------------------------------------------------------------------------
# Target
# ---------------------------------------------------------------------------

qt_add_executable(calango
    src/main.cpp

    # core — pure C++ data model (no Qt / no OpenGL): the "Model" of MVC
    src/core/Vec3.hpp
    src/core/Element.hpp
    src/core/Element.cpp
    src/core/Atom.hpp
    src/core/UnitCell.hpp
    src/core/UnitCell.cpp
    src/core/PeriodicImages.hpp
    src/core/Structure.hpp
    src/core/Structure.cpp
    src/core/StructureTransforms.hpp
    src/core/StructureTransforms.cpp
    src/core/CalculatorConfig.hpp
    src/core/AseScriptGenerator.hpp
    src/core/AseScriptGenerator.cpp
    src/core/BrillouinZone.hpp
    src/core/BrillouinZone.cpp
    src/core/KPath.hpp
    src/core/KPath.cpp
    src/core/Rdf.hpp
    src/core/Rdf.cpp
    src/core/Coordination.hpp
    src/core/Coordination.cpp
    src/core/HydrogenBonds.hpp
    src/core/HydrogenBonds.cpp
    src/core/HydrogenCompletion.hpp
    src/core/HydrogenCompletion.cpp
    src/core/Distributions.hpp
    src/core/Distributions.cpp
    src/core/StructureFactor.hpp
    src/core/StructureFactor.cpp
    src/core/Noise.hpp
    src/core/Noise.cpp
    src/core/PhononScriptGenerator.hpp
    src/core/PhononScriptGenerator.cpp
    src/core/PhononThermodynamics.hpp
    src/core/PhononThermodynamics.cpp
    src/core/SchedulerScript.hpp
    src/core/SchedulerScript.cpp
    src/core/ChemicalOrder.hpp
    src/core/ChemicalOrder.cpp
    src/core/LocalEntropy.hpp
    src/core/LocalEntropy.cpp
    src/core/VolumetricData.hpp
    src/core/VolumetricData.cpp
    src/core/MarchingCubes.hpp
    src/core/MarchingCubes.cpp
    src/core/GridInterpolation.hpp
    src/core/GridInterpolation.cpp
    src/core/DatasetSplit.hpp
    src/core/DatasetSplit.cpp
    src/core/ElectronicScriptGenerator.hpp
    src/core/ElectronicScriptGenerator.cpp
    src/core/BandSymmetryScriptGenerator.hpp
    src/core/BandSymmetryScriptGenerator.cpp
    src/core/OpticsScriptGenerator.hpp
    src/core/OpticsScriptGenerator.cpp
    src/core/GwScriptGenerator.hpp
    src/core/GwScriptGenerator.cpp
    src/core/GrapheneOxideBuilder.hpp
    src/core/GrapheneOxideBuilder.cpp
    src/core/BornChargesScriptGenerator.hpp
    src/core/BornChargesScriptGenerator.cpp
    src/core/RamanIrScriptGenerator.hpp
    src/core/RamanIrScriptGenerator.cpp
    src/core/PdbxFile.hpp
    src/core/PdbxFile.cpp
    src/core/WannierScriptGenerator.hpp
    src/core/DefectScriptGenerator.cpp
    src/core/FermiSurfaceScriptGenerator.cpp
    src/core/TopologyScriptGenerator.cpp
    src/core/TwoDBandsScriptGenerator.cpp
    src/core/WannierScriptGenerator.cpp
    src/core/AdsorptionSites.hpp
    src/core/AdsorptionSites.cpp
    src/core/ClusterExpansion.hpp
    src/core/ClusterExpansion.cpp
    src/core/BandGap.hpp
    src/core/BandGap.cpp
    src/core/BandUnfolding.hpp
    src/core/BandUnfolding.cpp
    src/core/UnfoldingScriptGenerator.hpp
    src/core/UnfoldingScriptGenerator.cpp
    src/core/ConvexHull.hpp
    src/core/ConvexHull.cpp
    src/core/ClusterExpansionScriptGenerator.hpp
    src/core/ClusterExpansionScriptGenerator.cpp
    src/core/RandomNoiseScriptGenerator.hpp
    src/core/RandomNoiseScriptGenerator.cpp
    src/core/CutoffConvergenceScriptGenerator.hpp
    src/core/CutoffConvergenceScriptGenerator.cpp
    src/core/KpointsConvergenceScriptGenerator.hpp
    src/core/KpointsConvergenceScriptGenerator.cpp
    src/core/CddScriptGenerator.hpp
    src/core/CddScriptGenerator.cpp
    src/core/XasScriptGenerator.hpp
    src/core/HubbardScriptGenerator.cpp
    src/core/XasScriptGenerator.cpp
    src/core/MonteCarlo.hpp
    src/core/MonteCarlo.cpp
    src/core/SqsGenerator.hpp
    src/core/SqsGenerator.cpp
    src/core/IceBuilder.hpp
    src/core/IceBuilder.cpp
    src/core/PolymerBuilder.hpp
    src/core/PolymerBuilder.cpp
    src/core/Vacf.hpp
    src/core/Vacf.cpp

    # python_bridge — embedded CPython + ASE interop
    src/python_bridge/PythonEngine.hpp
    src/python_bridge/PythonEngine.cpp
    src/python_bridge/AseBridge.hpp
    src/python_bridge/AseBridge.cpp
    src/python_bridge/AnimationExporter.hpp
    src/python_bridge/AnimationExporter.cpp
    src/python_bridge/AlembicExporter.hpp
    src/python_bridge/AlembicExporter.cpp
    src/python_bridge/MaterialsProject.hpp
    src/python_bridge/MaterialsProject.cpp
    src/python_bridge/BulkBuilder.hpp
    src/python_bridge/BulkBuilder.cpp
    src/python_bridge/PubChem.hpp
    src/python_bridge/PubChem.cpp
    src/python_bridge/SqsBuilder.hpp
    src/python_bridge/SqsBuilder.cpp
    src/python_bridge/RamanAnalysis.hpp
    src/python_bridge/RamanAnalysis.cpp
    src/python_bridge/MagneticSpaceGroup.hpp
    src/python_bridge/MagneticSpaceGroup.cpp
    src/python_bridge/SurfaceScience.hpp
    src/python_bridge/SurfaceScience.cpp
    src/python_bridge/NebBuilder.hpp
    src/python_bridge/NebBuilder.cpp

    # render — OpenGL "View" over core::Structure (read-only access)
    src/render/Camera.hpp
    src/render/Camera.cpp
    src/render/Film.hpp
    src/render/Film.cpp
    src/render/ColorMap.hpp
    src/render/ColorMap.cpp
    src/render/RenderGeometry.hpp
    src/render/StructureRenderer.hpp
    src/render/ShaderProfile.cpp
    src/render/StructureRenderer.cpp
    src/render/RayTraceExporter.hpp
    src/render/RayTraceExporter.cpp

    # jobs — local subprocess management
    src/jobs/JobRunner.hpp
    src/jobs/JobRunner.cpp

    # remote — SSH/SFTP HPC job submission (paramiko helper subprocess)
    src/remote/RemoteClient.hpp
    src/remote/RemoteClient.cpp

    # gui — Qt widgets: "View + Controller"
    src/gui/MainWindow.hpp
    src/gui/MainWindow.cpp
    src/gui/ViewportWidget.hpp
    src/gui/ViewportWidget.cpp
    src/gui/StructureInfoWidget.hpp
    src/gui/StructureInfoWidget.cpp
    src/gui/StructureEditorDialog.hpp
    src/gui/StructureEditorDialog.cpp
    src/gui/JobLogWidget.hpp
    src/gui/JobLogWidget.cpp
    src/gui/TimelineWidget.hpp
    src/gui/TimelineWidget.cpp
    src/gui/FilmTimelineWidget.hpp
    src/gui/FilmTimelineWidget.cpp
    src/gui/FilmProductionDialog.hpp
    src/gui/FilmProductionDialog.cpp
    src/gui/MetricPlotWidget.hpp
    src/gui/MetricPlotWidget.cpp
    src/gui/ProjectSerializer.hpp
    src/gui/ProjectSerializer.cpp
    src/gui/ScriptStaging.hpp
    src/gui/ScriptStaging.cpp
    src/gui/GuiUtils.hpp
    src/gui/GuiUtils.cpp
    src/gui/RepresentationPanel.hpp
    src/gui/RepresentationPanel.cpp
    src/gui/CollapsibleSection.hpp
    src/gui/CollapsibleSection.cpp
    src/gui/CellAxesTabs.hpp
    src/gui/CellAxesTabs.cpp
    src/gui/NeighborCellsDialog.cpp
    src/gui/TwoDBandsWizard.cpp
    src/gui/TwoDBandsWindow.cpp
    src/gui/DefectWizard.cpp
    src/gui/DefectDiagramWindow.cpp
    src/gui/FermiSurfaceDialog.cpp
    src/gui/FermiSurfaceWindow.cpp
    src/gui/TopologyDialog.cpp
    src/gui/TopologyWindow.cpp
    # The "Source MLWF process" step shared by the three Wannier
    # post-processes (interpolation, Fermi surface, topological invariants).
    src/gui/MlwfSourceSelector.hpp
    src/gui/MlwfSourceSelector.cpp
    src/gui/LightingPanel.hpp
    src/gui/LightingPanel.cpp
    src/gui/ElementSettingsDialog.hpp
    src/gui/ElementSettingsDialog.cpp
    src/gui/PolyhedralSettingsDialog.hpp
    src/gui/PolyhedralSettingsDialog.cpp
    src/gui/CustomGradientColoringDialog.hpp
    src/gui/CustomGradientColoringDialog.cpp
    src/gui/PythonHighlighter.hpp
    src/gui/PythonHighlighter.cpp
    src/gui/BrillouinZoneView.hpp
    src/gui/BrillouinZoneView.cpp
    src/gui/BrillouinZoneDialog.hpp
    src/gui/BrillouinZoneDialog.cpp
    src/gui/BrillouinZoneWidget.hpp
    src/gui/BrillouinZoneWidget.cpp
    src/gui/EmbeddedKPathEditor.hpp
    src/gui/EmbeddedKPathEditor.cpp
    src/gui/BrillouinZoneStyleDialog.hpp
    src/gui/BrillouinZoneStyleDialog.cpp
    src/gui/LinePlotWidget.hpp
    src/gui/LinePlotWidget.cpp
    src/gui/RdfDialog.hpp
    src/gui/RdfDialog.cpp
    src/gui/CoordinationDialog.hpp
    src/gui/CoordinationDialog.cpp
    src/gui/DistributionDialog.hpp
    src/gui/DistributionDialog.cpp
    src/gui/StructureFactorDialog.hpp
    src/gui/StructureFactorDialog.cpp
    src/gui/XrdDialog.hpp
    src/gui/XrdDialog.cpp
    src/gui/NanoBuilderDialog.hpp
    src/gui/NanoBuilderDialog.cpp
    src/gui/WaterIceWizard.hpp
    src/gui/WaterIceWizard.cpp
    src/gui/MacromoleculeWizard.hpp
    src/gui/MacromoleculeWizard.cpp
    src/gui/PhononBuilderDialog.hpp
    src/gui/PhononBuilderDialog.cpp
    src/gui/BondEditorDialog.hpp
    src/gui/BondEditorDialog.cpp
    src/gui/PeriodicTableDialog.hpp
    src/gui/PeriodicTableDialog.cpp
    src/gui/SlabWizard.hpp
    src/gui/SlabWizard.cpp
    src/gui/PreferencesDialog.hpp
    src/gui/PreferencesDialog.cpp
    src/gui/EnginePresets.hpp
    src/gui/EnginePresets.cpp
    src/gui/RunCommands.hpp
    src/gui/RunCommands.cpp
    src/gui/EnvFile.hpp
    src/gui/EnvFile.cpp
    src/gui/ExamplesDialog.hpp
    src/gui/ExamplesDialog.cpp
    src/gui/RayTraceDialog.hpp
    src/gui/RayTraceDialog.cpp
    src/gui/BrandingPanel.hpp
    src/gui/BrandingPanel.cpp
    src/gui/RemoteAccessPanel.hpp
    src/gui/RemoteAccessPanel.cpp
    src/gui/SqsDialog.hpp
    src/gui/SqsDialog.cpp
    src/gui/WarrenCowleyDialog.hpp
    src/gui/WarrenCowleyDialog.cpp
    src/gui/LocalEntropyDialog.hpp
    src/gui/LocalEntropyDialog.cpp
    src/gui/VolumeViewWidget.hpp
    src/gui/VolumeViewWidget.cpp
    src/gui/VisualEffectsPanel.hpp
    src/gui/VisualEffectsPanel.cpp
    src/gui/DatasetManagerDialog.hpp
    src/gui/DatasetManagerDialog.cpp
    src/gui/ProcessManagerPanel.hpp
    src/gui/ProcessManagerPanel.cpp
    src/gui/ScriptViewerDialog.hpp
    src/gui/ScriptViewerDialog.cpp
    src/gui/WelcomeDialog.hpp
    src/gui/WelcomeDialog.cpp
    src/gui/SettingsManager.hpp
    src/gui/SettingsManager.cpp
    src/gui/CalculatorParameters.hpp
    src/gui/CalculatorParameters.cpp
    src/gui/CalculatorParametersDialog.hpp
    src/gui/CalculatorParametersDialog.cpp
    src/gui/ThemeManager.hpp
    src/gui/ThemeManager.cpp
    src/ui/IconManager.hpp
    src/ui/IconManager.cpp
    src/gui/ConvexHullWindow.hpp
    src/gui/ConvexHullWindow.cpp
    src/gui/EffectiveBandsWindow.hpp
    src/gui/EffectiveBandsWindow.cpp
    src/gui/SystemStatusBar.hpp
    src/gui/SystemStatusBar.cpp
    src/gui/CondaEnvs.hpp
    src/gui/CondaEnvs.cpp
    src/gui/MaceTrainerDialog.hpp
    src/gui/MaceTrainerDialog.cpp
    src/gui/BandPdosView.hpp
    src/gui/BandPdosView.cpp
    src/gui/BandPdosWindow.hpp
    src/gui/BandPdosWindow.cpp
    src/gui/PlotStyleDialog.hpp
    src/gui/PlotStyleDialog.cpp
    src/gui/PhononPlotWindow.hpp
    src/gui/PhononPlotWindow.cpp
    src/gui/PhononThermodynamicsDialog.hpp
    src/gui/PhononThermodynamicsDialog.cpp
    src/gui/VibrationalAnalysisDialog.hpp
    src/gui/VibrationalAnalysisDialog.cpp
    src/gui/NanoparticleDialog.hpp
    src/gui/NanoparticleDialog.cpp
    src/gui/AdsorptionDialog.hpp
    src/gui/AdsorptionDialog.cpp
    src/gui/SupercellDialog.hpp
    src/gui/SupercellDialog.cpp
    src/gui/SymmetryDialog.hpp
    src/gui/SymmetryDialog.cpp
    src/gui/MagneticSpaceGroupDialog.hpp
    src/gui/MagneticSpaceGroupDialog.cpp
    src/gui/PartialChargeDialog.hpp
    src/gui/PartialChargeDialog.cpp
    src/gui/WannierDialog.hpp
    src/gui/WannierDialog.cpp
    src/gui/SinglePointViewer.hpp
    src/gui/SinglePointViewer.cpp
    src/gui/GeometryOptimizationViewer.hpp
    src/gui/GeometryOptimizationViewer.cpp
    src/gui/MolecularDynamicsViewer.hpp
    src/gui/MolecularDynamicsViewer.cpp
    src/gui/MlwfViewer.hpp
    src/gui/MlwfViewer.cpp
    src/gui/WannierInterpolationDialog.hpp
    src/gui/WannierInterpolationDialog.cpp
    src/gui/VolumetricStyle.hpp
    src/gui/VolumetricStyle.cpp
    src/gui/VolumetricPanel.hpp
    src/gui/VolumetricPanel.cpp
    src/gui/VolumetricMetadataDialog.hpp
    src/gui/VolumetricMetadataDialog.cpp
    src/gui/EditVolumetricRenderDialog.hpp
    src/gui/EditVolumetricRenderDialog.cpp
    src/gui/OverlayModel.hpp
    src/gui/OverlayModel.cpp
    src/gui/OverlayEditDialog.hpp
    src/gui/OverlayEditDialog.cpp
    src/gui/OverlayPanel.hpp
    src/gui/OverlayPanel.cpp
    src/gui/VacfDialog.hpp
    src/gui/VacfDialog.cpp
    src/gui/SimulationWizardBase.hpp
    src/gui/SimulationWizardBase.cpp
    src/gui/GpawElectronicRows.hpp
    src/gui/GpawElectronicRows.cpp
    src/gui/MolecularDynamicsWizard.hpp
    src/gui/MolecularDynamicsWizard.cpp
    src/gui/SinglePointWizard.hpp
    src/gui/SinglePointWizard.cpp
    src/gui/CutoffConvergenceWizard.hpp
    src/gui/CutoffConvergenceWizard.cpp
    src/gui/KpointsConvergenceWizard.hpp
    src/gui/KpointsConvergenceWizard.cpp
    src/gui/ConvergenceResultsWindow.hpp
    src/gui/ConvergenceResultsWindow.cpp
    src/gui/WorkflowWindow.hpp
    src/gui/WorkflowWindow.cpp
    src/gui/RandomNoiseWizard.hpp
    src/gui/RandomNoiseWizard.cpp
    src/gui/RandomNoiseViewer.hpp
    src/gui/RandomNoiseViewer.cpp
    src/gui/CddWizard.hpp
    src/gui/CddWizard.cpp
    src/gui/XasWizard.hpp
    src/gui/HubbardUWizard.cpp
    src/gui/XasWizard.cpp
    src/gui/SpectrumPlotWidget.hpp
    src/gui/SpectrumPlotWidget.cpp
    src/gui/XasResultsWindow.hpp
    src/gui/XasResultsWindow.cpp
    src/gui/ElectronicBandsWizard.hpp
    src/gui/ElectronicBandsWizard.cpp
    src/gui/OpticsWizard.hpp
    src/gui/OpticsWizard.cpp
    src/gui/GwWizard.hpp
    src/gui/GwWizard.cpp
    src/gui/WannierWizard.hpp
    src/gui/WannierWizard.cpp
    src/gui/OpticsResultsWindow.hpp
    src/gui/OpticsResultsWindow.cpp
    src/gui/GwResultsWindow.hpp
    src/gui/GwResultsWindow.cpp
    src/gui/HubbardParametersDialog.hpp
    src/gui/HubbardParametersDialog.cpp
    src/gui/GeometryConstraintsDialog.hpp
    src/gui/GeometryConstraintsDialog.cpp
    src/gui/BornChargesWizard.hpp
    src/gui/BornChargesWizard.cpp
    src/gui/BornChargesViewer.hpp
    src/gui/BornChargesViewer.cpp
    src/gui/RamanIrWizard.hpp
    src/gui/RamanIrWizard.cpp
    src/gui/RamanIrViewer.hpp
    src/gui/RamanIrViewer.cpp
    src/gui/PointOfViewDialog.hpp
    src/gui/PointOfViewDialog.cpp
    src/gui/CastSetupDialog.hpp
    src/gui/CastSetupDialog.cpp
    src/gui/AddAdsorbateDialog.hpp
    src/gui/AddAdsorbateDialog.cpp
    src/gui/GrapheneOxideWizard.hpp
    src/gui/GrapheneOxideWizard.cpp
    src/gui/OpticsPlotStyleDialog.hpp
    src/gui/OpticsPlotStyleDialog.cpp
    src/gui/EffectiveBandsWizard.hpp
    src/gui/EffectiveBandsWizard.cpp
    src/gui/GeometryOptimizationWizard.hpp
    src/gui/GeometryOptimizationWizard.cpp
    src/gui/PhononWizard.hpp
    src/gui/PhononWizard.cpp
    src/gui/ClusterExpansionDialog.hpp
    src/gui/ClusterExpansionDialog.cpp
    src/gui/ClusterExpansionWizard.hpp
    src/gui/ClusterExpansionWizard.cpp
    src/gui/ConvexHullPlotWidget.hpp
    src/gui/ConvexHullPlotWidget.cpp
    src/gui/SpectralHeatmapWidget.hpp
    src/gui/SpectralHeatmapWidget.cpp
    src/gui/MonteCarloDialog.hpp
    src/gui/MonteCarloDialog.cpp
    src/gui/MonteCarloWizard.hpp
    src/gui/MonteCarloWizard.cpp
    src/gui/NebDialog.hpp
    src/gui/NebDialog.cpp
)

qt_add_resources(calango "shaders"
    PREFIX "/"
    FILES
        assets/shaders/mesh.vert
        assets/shaders/mesh.frag
        assets/shaders/shadow.vert
        assets/shaders/shadow.frag
        assets/shaders/wire.vert
        assets/shaders/wire.frag
        assets/shaders/isosurface.vert
        assets/shaders/isosurface.frag
        assets/shaders/impostor_sphere.vert
        assets/shaders/impostor_sphere.frag
        assets/shaders/impostor_cylinder.vert
        assets/shaders/impostor_cylinder.frag
        assets/shaders/raymarch.vert
        assets/shaders/raymarch.frag
        assets/shaders/volume.vert
        assets/shaders/volume.frag
        assets/shaders/dof.vert
        assets/shaders/dof.frag
        # SSAO reuses dof.vert for its fullscreen triangle.
        assets/shaders/ssao.frag
        assets/shaders/ssao_blur.frag
        assets/shaders/ssao_composite.frag
)

# Brand artwork. The two app icons are platform-specific by design and are NOT
# interchangeable: icon_osx.png is a squircle carrying the ~80% macOS icon
# margin, icon_linux.png is a full-bleed circular badge as freedesktop expects.
# logo.png is the bare mark with no plate behind it, for in-app use (About).
# The project's own licence, embedded so the About dialog shows the REAL text
# rather than a copy that can drift from the file at the repo root.
qt_add_resources(calango "license"
    PREFIX "/"
    BASE "."
    FILES
        LICENSE
)

qt_add_resources(calango "branding"
    PREFIX "/"
    FILES
        assets/calango/icon_osx.png
        assets/calango/icon_linux.png
        assets/calango/logo.png
        assets/calango/logo_dark.png
        assets/calango/logo_light.png
)

# RemixIcon UI icons (monochrome SVG, runtime-tinted by calango::ui::IconManager
# for theme + widget state). One asset per glyph; the file stem is the logical
# name passed to IconManager::icon(), so this list is the registry of what
# IconManager can resolve. Add the new file's path here when bundling another
# RemixIcon.
qt_add_resources(calango "icons"
    PREFIX "/"
    FILES
        assets/icons/add-circle-fill.svg
        assets/icons/add-circle-line.svg
        assets/icons/align-item-vertical-center-line.svg
        assets/icons/anticlockwise-2-line.svg
        assets/icons/arrow-go-back-line.svg
        assets/icons/atom-line.svg
        assets/icons/box-1-fill.svg
        assets/icons/box-3-line.svg
        assets/icons/brush-fill.svg
        assets/icons/camera-lens-line.svg
        assets/icons/checkbox-circle-line.svg
        assets/icons/close-circle-line.svg
        assets/icons/clapperboard-fill.svg
        assets/icons/close-line.svg
        assets/icons/code-box-fill.svg
        assets/icons/color-filter-fill.svg
        assets/icons/compasses-2-line.svg
        assets/icons/computer-line.svg
        assets/icons/contract-left-right-line.svg
        assets/icons/cpu-line.svg
        assets/icons/cursor-line.svg
        assets/icons/delete.svg
        assets/icons/delete-bin-line.svg
        assets/icons/drag-move-2-line.svg
        assets/icons/edit-box-fill.svg
        assets/icons/edit-fill.svg
        assets/icons/expand-height-line.svg
        assets/icons/eye-fill.svg
        assets/icons/file-code-line.svg
        assets/icons/film-fill.svg
        assets/icons/file-copy-line.svg
        assets/icons/focus-3-line.svg
        assets/icons/folder-open-line.svg
        assets/icons/folder-received-line.svg
        assets/icons/font-size-2.svg
        assets/icons/grid-line.svg
        assets/icons/group-fill.svg
        assets/icons/hard-drive-2-line.svg
        assets/icons/hashtag.svg
        assets/icons/heading.svg
        assets/icons/indeterminate-circle-fill.svg
        assets/icons/information-line.svg
        assets/icons/loader-4-line.svg
        assets/icons/magic-line.svg
        assets/icons/map-2-fill.svg
        assets/icons/paint-fill.svg
        assets/icons/palette-line.svg
        assets/icons/play-circle-fill.svg
        assets/icons/price-tag-fill.svg
        assets/icons/puzzle-fill.svg
        assets/icons/line-chart-line.svg
        assets/icons/links-line.svg
        assets/icons/arrow-right-up-line.svg
        assets/icons/save-line.svg
        assets/icons/box-1-line.svg
        assets/icons/gradient.svg
        assets/icons/stairs-fill.svg
        assets/icons/hexagon-fill.svg
        assets/icons/home-2-fill.svg
        assets/icons/home-office-fill.svg
        assets/icons/community-fill.svg
        assets/icons/ram-line.svg
        assets/icons/ruler-2-line.svg
        assets/icons/scissors-cut-line.svg
        assets/icons/shape-line.svg
        assets/icons/share-fill.svg
        assets/icons/slideshow-3-fill.svg
        assets/icons/split-cells-horizontal.svg
        assets/icons/stack-fill.svg
        assets/icons/stack-line.svg
        assets/icons/stacked-view.svg
        assets/icons/time-line.svg
        assets/icons/triangle-fill.svg
)

qt_add_resources(calango "remote"
    PREFIX "/"
    FILES
        assets/remote/calango_remote.py
)

# Distributable macOS bundle: configure with -DCALANGO_MACOS_BUNDLE=ON.
# The default plain binary keeps the familiar ./build/calango developer
# workflow, with the window/dock icon set at runtime from the embedded
# resource. The bundle icon uses a committed assets/calango/calango.icns
# when present, otherwise it is rendered at build time from
# icon_osx.png with the stock sips/iconutil tools.
option(CALANGO_MACOS_BUNDLE "Build calango as a macOS .app bundle" OFF)
if(APPLE AND CALANGO_MACOS_BUNDLE)
    set_target_properties(calango PROPERTIES
        MACOSX_BUNDLE TRUE
        MACOSX_BUNDLE_BUNDLE_NAME "Calango"
        MACOSX_BUNDLE_GUI_IDENTIFIER "org.seixasresearch.calango"
        MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
        MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}")

    set(CALANGO_ICNS "")
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/assets/calango/calango.icns")
        set(CALANGO_ICNS "${CMAKE_CURRENT_SOURCE_DIR}/assets/calango/calango.icns")
    else()
        find_program(SIPS_EXECUTABLE sips)
        find_program(ICONUTIL_EXECUTABLE iconutil)
        if(SIPS_EXECUTABLE AND ICONUTIL_EXECUTABLE)
            set(CALANGO_ICNS "${CMAKE_CURRENT_BINARY_DIR}/calango.icns")
            # Run via bash: cloud-synced checkouts (Dropbox) may strip the
            # executable bit from the script.
            add_custom_command(OUTPUT "${CALANGO_ICNS}"
                COMMAND bash "${CMAKE_CURRENT_SOURCE_DIR}/packaging/macos/make_icns.sh"
                        "${CMAKE_CURRENT_SOURCE_DIR}/assets/calango/icon_osx.png"
                        "${CALANGO_ICNS}"
                DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/assets/calango/icon_osx.png"
                        "${CMAKE_CURRENT_SOURCE_DIR}/packaging/macos/make_icns.sh"
                COMMENT "Rendering calango.icns from icon_osx.png")
        else()
            message(WARNING "sips/iconutil not found — the .app bundle will "
                            "have no Finder icon")
        endif()
    endif()
    if(CALANGO_ICNS)
        set_target_properties(calango PROPERTIES
            MACOSX_BUNDLE_ICON_FILE "calango.icns")
        target_sources(calango PRIVATE "${CALANGO_ICNS}")
        set_source_files_properties("${CALANGO_ICNS}" PROPERTIES
            MACOSX_PACKAGE_LOCATION "Resources"
            GENERATED TRUE)
    endif()
endif()

qt_add_resources(calango "examples"
    PREFIX "/"
    FILES
        examples/diamond.vasp
        examples/mos2_2h_bulk.vasp
        examples/graphene.vasp
        examples/mos2_1h_monolayer.vasp
        examples/benzene.xyz
        examples/naphthalene.xyz
        examples/coronene.xyz
)

target_include_directories(calango PRIVATE src
    "${CMAKE_CURRENT_BINARY_DIR}/generated")

# QT_NO_KEYWORDS: pybind11/CPython headers and Qt's lowercase `slots`/`signals`
# macros collide. We therefore use Q_SIGNALS / Q_SLOTS / Q_EMIT everywhere.
# Bake in the configure-time interpreter as the fallback for the embedded
# runtime (overridable at runtime via CALANGO_PYTHON or VIRTUAL_ENV).
file(TO_CMAKE_PATH "${Python3_EXECUTABLE}" CALANGO_DEFAULT_PYTHON_PATH)

target_compile_definitions(calango PRIVATE
    QT_NO_KEYWORDS
    CALANGO_VERSION="${PROJECT_VERSION}"
    CALANGO_DEFAULT_PYTHON="${CALANGO_DEFAULT_PYTHON_PATH}"
)

target_link_libraries(calango PRIVATE
    Qt6::Widgets
    Qt6::OpenGLWidgets
    Qt6::Concurrent
    Qt6::Svg
    pybind11::embed
)
if(APPLE)
    target_link_libraries(calango PRIVATE "-framework OpenGL")
else()
    target_link_libraries(calango PRIVATE OpenGL::GL)
endif()

set_target_properties(calango PROPERTIES
    WIN32_EXECUTABLE TRUE   # no console window on Windows
)

if(MSVC)
    target_compile_options(calango PRIVATE /W4 /permissive-)
else()
    target_compile_options(calango PRIVATE -Wall -Wextra -Wpedantic)
endif()

# ---------------------------------------------------------------------------
# Integration tests (GUI-free: core + embedded Python bridge only)
#   cmake -B build -DCALANGO_BUILD_TESTS=ON ... && ctest --test-dir build
# ---------------------------------------------------------------------------
option(CALANGO_BUILD_TESTS "Build the integration test executables" OFF)
if(CALANGO_BUILD_TESTS)
    enable_testing()
    add_executable(calango_sqs_test
        tests/SqsSiGeTest.cpp
        src/core/Structure.cpp
        src/core/UnitCell.cpp
        src/core/Element.cpp
        src/python_bridge/PythonEngine.cpp
        src/python_bridge/AseBridge.cpp
        src/core/PdbxFile.cpp
        src/python_bridge/SqsBuilder.cpp
    )
    target_include_directories(calango_sqs_test PRIVATE src)
    target_compile_definitions(calango_sqs_test PRIVATE
        CALANGO_DEFAULT_PYTHON="${CALANGO_DEFAULT_PYTHON_PATH}")
    target_link_libraries(calango_sqs_test PRIVATE pybind11::embed)
    add_test(NAME sqs_sige_alloy COMMAND calango_sqs_test)

    add_executable(calango_adsorption_test
        tests/AdsorptionTest.cpp
        src/core/Structure.cpp
        src/core/UnitCell.cpp
        src/core/Element.cpp
        src/core/AdsorptionSites.cpp
        src/python_bridge/PythonEngine.cpp
        src/python_bridge/AseBridge.cpp
        src/core/PdbxFile.cpp
        src/python_bridge/SurfaceScience.cpp
    )
    target_include_directories(calango_adsorption_test PRIVATE src)
    target_compile_definitions(calango_adsorption_test PRIVATE
        CALANGO_DEFAULT_PYTHON="${CALANGO_DEFAULT_PYTHON_PATH}")
    target_link_libraries(calango_adsorption_test PRIVATE pybind11::embed)
    add_test(NAME adsorption_nanoparticle COMMAND calango_adsorption_test)

    add_executable(calango_phonon_test
        tests/PhononDiamondTest.cpp
        src/core/Structure.cpp
        src/core/UnitCell.cpp
        src/core/Element.cpp
        src/python_bridge/PythonEngine.cpp
        src/python_bridge/AseBridge.cpp
        src/core/PdbxFile.cpp
    )
    target_include_directories(calango_phonon_test PRIVATE src)
    target_compile_definitions(calango_phonon_test PRIVATE
        CALANGO_DEFAULT_PYTHON="${CALANGO_DEFAULT_PYTHON_PATH}")
    target_link_libraries(calango_phonon_test PRIVATE pybind11::embed)
    add_test(NAME phonon_diamond COMMAND calango_phonon_test)

    # Native cores (no embedded Python needed).
    add_executable(calango_cluster_expansion_test
        tests/ClusterExpansionTest.cpp
        src/core/Structure.cpp
        src/core/UnitCell.cpp
        src/core/Element.cpp
        src/core/ClusterExpansion.cpp
    )
    target_include_directories(calango_cluster_expansion_test PRIVATE src)
    add_test(NAME cluster_expansion COMMAND calango_cluster_expansion_test)

    add_executable(calango_monte_carlo_test
        tests/MonteCarloTest.cpp
        src/core/Structure.cpp
        src/core/UnitCell.cpp
        src/core/Element.cpp
        src/core/MonteCarlo.cpp
    )
    target_include_directories(calango_monte_carlo_test PRIVATE src)
    add_test(NAME monte_carlo_swap COMMAND calango_monte_carlo_test)

    add_executable(calango_symmetry_test
        tests/SymmetrySiTest.cpp
        src/core/Structure.cpp
        src/core/UnitCell.cpp
        src/core/Element.cpp
        src/python_bridge/PythonEngine.cpp
        src/python_bridge/AseBridge.cpp
        src/core/PdbxFile.cpp
    )
    target_include_directories(calango_symmetry_test PRIVATE src)
    target_compile_definitions(calango_symmetry_test PRIVATE
        CALANGO_DEFAULT_PYTHON="${CALANGO_DEFAULT_PYTHON_PATH}")
    target_link_libraries(calango_symmetry_test PRIVATE pybind11::embed)
    add_test(NAME symmetry_si_diamond
             COMMAND calango_symmetry_test
                     "${CMAKE_CURRENT_SOURCE_DIR}/examples/Si_diamond.vasp")

    # Live network test — skipped (exit 77) when MP_API_KEY is neither in
    # the environment nor in ~/.env.
    add_executable(calango_mp_test
        tests/MaterialsProjectTest.cpp
        src/core/Structure.cpp
        src/core/UnitCell.cpp
        src/core/Element.cpp
        src/python_bridge/PythonEngine.cpp
        src/python_bridge/AseBridge.cpp
        src/core/PdbxFile.cpp
        src/python_bridge/MaterialsProject.cpp
    )
    target_include_directories(calango_mp_test PRIVATE src)
    target_compile_definitions(calango_mp_test PRIVATE
        CALANGO_DEFAULT_PYTHON="${CALANGO_DEFAULT_PYTHON_PATH}")
    target_link_libraries(calango_mp_test PRIVATE pybind11::embed)
    add_test(NAME materials_project_mp1434 COMMAND calango_mp_test)
    set_tests_properties(materials_project_mp1434 PROPERTIES
        SKIP_RETURN_CODE 77)

    add_executable(calango_raman_test
        tests/RamanSiTest.cpp
        src/core/Structure.cpp
        src/core/UnitCell.cpp
        src/core/Element.cpp
        src/python_bridge/PythonEngine.cpp
        src/python_bridge/AseBridge.cpp
        src/core/PdbxFile.cpp
        src/python_bridge/RamanAnalysis.cpp
    )
    target_include_directories(calango_raman_test PRIVATE src)
    target_compile_definitions(calango_raman_test PRIVATE
        CALANGO_DEFAULT_PYTHON="${CALANGO_DEFAULT_PYTHON_PATH}")
    target_link_libraries(calango_raman_test PRIVATE pybind11::embed)
    add_test(NAME raman_si_diamond
             COMMAND calango_raman_test
                     "${CMAKE_CURRENT_SOURCE_DIR}/examples/Si_diamond.vasp")

    # Magnetic space group: one cell, three magnetic configurations, three
    # BNS types. The crystallography is identical in all three, so nothing but
    # the moments can produce the difference — which is exactly the module's
    # job. Skips (77) when spglib carries no magnetic tables.
    add_executable(calango_magnetic_test
        tests/MagneticSpaceGroupTest.cpp
        src/core/Structure.cpp
        src/core/UnitCell.cpp
        src/core/Element.cpp
        src/python_bridge/PythonEngine.cpp
        src/python_bridge/AseBridge.cpp
        src/core/PdbxFile.cpp
        src/python_bridge/MagneticSpaceGroup.cpp
    )
    target_include_directories(calango_magnetic_test PRIVATE src)
    target_compile_definitions(calango_magnetic_test PRIVATE
        CALANGO_DEFAULT_PYTHON="${CALANGO_DEFAULT_PYTHON_PATH}")
    target_link_libraries(calango_magnetic_test PRIVATE pybind11::embed)
    add_test(NAME magnetic_space_group COMMAND calango_magnetic_test)
    set_tests_properties(magnetic_space_group PROPERTIES SKIP_RETURN_CODE 77)

    add_executable(calango_stream_test
        tests/StreamingTest.cpp
        src/jobs/JobRunner.cpp
        src/core/Structure.cpp
        src/core/UnitCell.cpp
        src/core/Element.cpp
    )
    target_include_directories(calango_stream_test PRIVATE src)
    target_compile_definitions(calango_stream_test PRIVATE
        QT_NO_KEYWORDS
        CALANGO_DEFAULT_PYTHON="${CALANGO_DEFAULT_PYTHON_PATH}")
    target_link_libraries(calango_stream_test PRIVATE Qt6::Core)
    add_test(NAME frame_streaming COMMAND calango_stream_test)

    # Ray-tracer scene generation. No Python and no renderer binary needed:
    # it asserts the emitted POV-Ray/Tachyon text (keywords, camera zoom and
    # FOV conversion, numeric hygiene) — the layer where the Tachyon parse
    # failure and the 2x zoom regression lived.
    add_executable(calango_raytrace_test
        tests/RayTraceSceneTest.cpp
        src/render/RayTraceExporter.cpp
        src/render/StructureRenderer.cpp
        # The renderer builds the Voronoi (Wigner-Seitz) cell outline from
        # the same half-space intersection the Brillouin zone uses.
        src/core/BrillouinZone.cpp
        src/render/ShaderProfile.cpp
        src/core/MarchingCubes.cpp
        src/core/VolumetricData.cpp
        src/render/ColorMap.cpp
        src/render/Camera.cpp
        src/core/Structure.cpp
        src/core/UnitCell.cpp
        src/core/Element.cpp
    )
    target_include_directories(calango_raytrace_test PRIVATE src)
    target_compile_definitions(calango_raytrace_test PRIVATE QT_NO_KEYWORDS)
    # Qt6::OpenGL only for StructureRenderer's headers; no GL context is
    # created (only its static color/radius helpers are exercised).
    target_link_libraries(calango_raytrace_test PRIVATE Qt6::Gui Qt6::OpenGL)
    add_test(NAME raytrace_scene_files COMMAND calango_raytrace_test)

    # Film timing and sampling: the transition semantics (a hard cut must not
    # drift, a fade must reach black exactly at the cut) and the timeline
    # priority rule that re-times a trajectory against the film. Pure
    # arithmetic on Qt's math types — no widgets and no GL context.
    add_executable(calango_film_test
        tests/FilmTest.cpp
        src/render/Film.cpp
        src/render/Camera.cpp
    )
    target_include_directories(calango_film_test PRIVATE src)
    target_compile_definitions(calango_film_test PRIVATE QT_NO_KEYWORDS)
    target_link_libraries(calango_film_test PRIVATE Qt6::Gui)
    add_test(NAME film_timeline COMMAND calango_film_test)

    # Polymer builder: measures the chemistry of the generated cell (bond
    # lengths, valence angles, composition, self-avoidance) rather than the
    # builder's own bookkeeping.
    add_executable(calango_polymer_test
        tests/PolymerBuilderTest.cpp
        src/core/PolymerBuilder.cpp
        src/core/Structure.cpp
        src/core/UnitCell.cpp
        src/core/Element.cpp
    )
    target_include_directories(calango_polymer_test PRIVATE src)
    add_test(NAME polymer_builder COMMAND calango_polymer_test)

    # Native water/ice builder: the Bernal-Fowler ice rules are re-derived from
    # the generated COORDINATES, not read back from the solver's bookkeeping.
    add_executable(calango_ice_test
        tests/IceBuilderTest.cpp
        src/core/IceBuilder.cpp
        src/core/Structure.cpp
        src/core/UnitCell.cpp
        src/core/Element.cpp
    )
    target_include_directories(calango_ice_test PRIVATE src)
    add_test(NAME ice_builder COMMAND calango_ice_test)

    # Harmonic phonon thermodynamics: checked against the closed-form Einstein
    # oscillator and the limits that are laws (Third Law, Dulong-Petit).
    add_executable(calango_phonon_thermo_test
        tests/PhononThermodynamicsTest.cpp
        src/core/PhononThermodynamics.cpp
    )
    target_include_directories(calango_phonon_thermo_test PRIVATE src)
    add_test(NAME phonon_thermodynamics COMMAND calango_phonon_thermo_test)

    # Hydrogen-bond perception: the geometric criteria that separate a real
    # D-H...A contact from any two nearby polar atoms.
    add_executable(calango_hbond_test
        tests/HydrogenBondTest.cpp
        src/core/HydrogenBonds.cpp
        src/core/Structure.cpp
        src/core/UnitCell.cpp
        src/core/Element.cpp
    )
    target_include_directories(calango_hbond_test PRIVATE src)
    add_test(NAME hydrogen_bonds COMMAND calango_hbond_test)

    # Periodic bond perception: that an atom bonds to EVERY lattice image
    # within reach, not just the nearest one (the MoS2 two-bonds-instead-of-six
    # bug), and to its own images when the cell holds a single atom.
    add_executable(calango_bond_test
        tests/BondPerceptionTest.cpp
        src/core/Structure.cpp
        src/core/UnitCell.cpp
        src/core/Element.cpp
    )
    target_include_directories(calango_bond_test PRIVATE src)
    add_test(NAME bond_perception COMMAND calango_bond_test)

    # Hydrogen completion: the valence arithmetic that decides HOW MANY
    # hydrogens an atom is missing, and the repulsion relaxation that decides
    # WHERE they go.
    add_executable(calango_hcomplete_test
        tests/HydrogenCompletionTest.cpp
        src/core/HydrogenCompletion.cpp
        src/core/Structure.cpp
        src/core/UnitCell.cpp
        src/core/Element.cpp
    )
    target_include_directories(calango_hcomplete_test PRIVATE src)
    add_test(NAME hydrogen_completion COMMAND calango_hcomplete_test)

    # Shader compilation: a GLSL error renders nothing instead of crashing, so
    # without this a broken shader ships as a black viewport. Needs a real GL
    # 3.3 context and self-skips when there is none.
    qt_add_executable(calango_shader_test tests/ShaderCompileTest.cpp
        src/render/ShaderProfile.cpp)
    target_include_directories(calango_shader_test PRIVATE src)
    qt_add_resources(calango_shader_test "test_shaders"
        PREFIX "/"
        FILES
            assets/shaders/mesh.vert
            assets/shaders/mesh.frag
            assets/shaders/shadow.vert
            assets/shaders/shadow.frag
            assets/shaders/wire.vert
            assets/shaders/wire.frag
            assets/shaders/volume.vert
            assets/shaders/volume.frag
            assets/shaders/isosurface.vert
            assets/shaders/isosurface.frag
            assets/shaders/impostor_sphere.vert
            assets/shaders/impostor_sphere.frag
            assets/shaders/impostor_cylinder.vert
            assets/shaders/impostor_cylinder.frag
            assets/shaders/raymarch.vert
            assets/shaders/raymarch.frag
            assets/shaders/dof.vert
            assets/shaders/dof.frag
            assets/shaders/ssao.frag
            assets/shaders/ssao_blur.frag
            assets/shaders/ssao_composite.frag
    )
    target_link_libraries(calango_shader_test PRIVATE Qt6::Gui Qt6::OpenGL)
    add_test(NAME shader_compilation COMMAND calango_shader_test)

    # Translucent rendering: renders real frames offscreen and compares PIXELS.
    # Opacity below 1 used to make the unit cell vanish and every bond jump in
    # front of its atoms — a wrong frame is still a frame, so nothing else here
    # could see it. Self-skips when no GL 3.3 context exists.
    qt_add_executable(calango_translucent_render_test
        tests/TranslucentRenderTest.cpp
        src/render/StructureRenderer.cpp
        # The renderer builds the Voronoi (Wigner-Seitz) cell outline from
        # the same half-space intersection the Brillouin zone uses.
        src/core/BrillouinZone.cpp
        src/render/ShaderProfile.cpp
        src/render/Camera.cpp
        src/render/ColorMap.cpp
        src/core/Structure.cpp
        src/core/UnitCell.cpp
        src/core/Element.cpp
        src/core/Coordination.cpp
        src/core/MarchingCubes.cpp
        src/core/VolumetricData.cpp
    )
    qt_add_resources(calango_translucent_render_test "render_test_shaders"
        PREFIX "/"
        FILES
            assets/shaders/mesh.vert
            assets/shaders/mesh.frag
            assets/shaders/shadow.vert
            assets/shaders/shadow.frag
            assets/shaders/wire.vert
            assets/shaders/wire.frag
            # The filled unit cell is shaded through the lit surface program.
            # Without these the renderer silently falls back to the unlit path
            # and the test measures the fallback rather than the feature.
            assets/shaders/isosurface.vert
            assets/shaders/isosurface.frag)
    target_include_directories(calango_translucent_render_test PRIVATE src)
    target_link_libraries(calango_translucent_render_test
        PRIVATE Qt6::Gui Qt6::OpenGL)
    add_test(NAME translucent_render COMMAND calango_translucent_render_test)

    # Dialog construction: a wizard whose constructor emitted a signal into a
    # slot reading widgets it had not created yet crashed the moment its menu
    # item was clicked. The application smoke test never opens dialogs and a
    # compile cannot see a signal-ordering hazard, so each dialog is built and
    # driven here. Offscreen, no display.
    qt_add_executable(calango_dialog_test
        tests/DialogConstructionTest.cpp
        src/gui/FilmTimelineWidget.cpp
        src/gui/GrapheneOxideWizard.cpp
        src/gui/HubbardParametersDialog.cpp
        src/gui/GeometryConstraintsDialog.cpp
        src/gui/OpticsPlotStyleDialog.cpp
        src/gui/OverlayEditDialog.cpp
        src/gui/OverlayModel.cpp
        src/gui/GuiUtils.cpp
        src/core/VolumetricData.cpp
        src/render/ColorMap.cpp
        # One concrete simulation wizard, so the shared per-engine group
        # show/hide logic in SimulationWizardBase is actually driven. That is
        # where a newly added engine re-arms the construction-order hazard this
        # test exists for.
        src/gui/SinglePointWizard.cpp
        src/gui/TwoDBandsWizard.cpp
        src/core/TwoDBandsScriptGenerator.cpp
        src/gui/RandomNoiseWizard.cpp
        # Its results window: the JSON schema the generated script writes and
        # the viewer reads is a contract between two files that never see each
        # other, so the first GUI test to touch it pins the keys.
        src/gui/RandomNoiseViewer.cpp
        src/gui/CddWizard.cpp
        src/gui/HubbardUWizard.cpp
        src/gui/XasWizard.cpp
        src/gui/XasResultsWindow.cpp
        src/gui/SpectrumPlotWidget.cpp
        src/core/HubbardScriptGenerator.cpp
        src/core/XasScriptGenerator.cpp
        src/gui/EditVolumetricRenderDialog.cpp
        src/gui/VolumetricStyle.cpp
        # The isosurface "Lit surface" checkbox reads and writes the shader
        # profile registry. It queries no GL context here — ShaderRegistry
        # answers "unsupported is unknown, assume yes" with none current —
        # so the headless dialog test constructs it fine.
        src/render/ShaderProfile.cpp
        src/core/RandomNoiseScriptGenerator.cpp
        src/gui/CutoffConvergenceWizard.cpp
        src/core/CutoffConvergenceScriptGenerator.cpp
        src/gui/KpointsConvergenceWizard.cpp
        src/core/KpointsConvergenceScriptGenerator.cpp
        src/core/CddScriptGenerator.cpp
        src/core/Noise.cpp
        src/gui/SimulationWizardBase.cpp
        src/gui/GpawElectronicRows.cpp
        src/gui/HubbardParametersDialog.cpp
        src/gui/CondaEnvs.cpp
        # The MACE trainer: its YAML has to name keys mace.tools.arg_parser
        # accepts and the reference-data keys the exported dataset actually
        # uses, and its launcher has to be valid Python. All three fail at run
        # time on someone else's cluster, hours in, so they are pinned here.
        src/gui/MaceTrainerDialog.cpp
        # The Magnetic Space Group dialog: it determines the group inside its
        # own constructor and writes the answer back into widgets that
        # constructor created, which is precisely the hazard this test exists
        # for. Brings AseBridge with it (the determination goes through
        # ase.Atoms).
        src/gui/MagneticSpaceGroupDialog.cpp
        src/python_bridge/MagneticSpaceGroup.cpp
        src/python_bridge/AseBridge.cpp
        src/core/PdbxFile.cpp
        # The "Source MLWF process" step and two of the three dialogs that
        # carry it. What is pinned is the refusal: a run whose wavefunctions
        # have gone must be caught in the dialog, not after a job is staged
        # against it.
        #
        # Wannier Interpolation is deliberately NOT here. Its source step is
        # the same MlwfSourceSelector wired the same way, but reaching it means
        # linking the embedded k-path editor, the Brillouin-zone view (a
        # QOpenGLWidget) and AseBridge — a dozen translation units and a GL
        # dependency added to a construction smoke test, for a third copy of
        # three lines already covered twice.
        src/gui/MlwfSourceSelector.cpp
        # The Electronic Structure wizard itself. It is the most control-dense
        # settings page in the application and the one that seeds rows into a
        # table during construction, so it is worth the extra translation units
        # it drags in (the embedded k-path editor and its Brillouin-zone view).
        src/gui/ElectronicBandsWizard.cpp
        src/gui/EmbeddedKPathEditor.cpp
        src/gui/BrillouinZoneWidget.cpp
        src/gui/BrillouinZoneView.cpp
        src/gui/BrillouinZoneStyleDialog.cpp
        src/core/BrillouinZone.cpp
        src/core/KPath.cpp
        src/core/ElectronicScriptGenerator.cpp
        src/core/BandSymmetryScriptGenerator.cpp
        src/ui/IconManager.cpp
        src/gui/ThemeManager.cpp
        src/render/Camera.cpp
        src/gui/FermiSurfaceDialog.cpp
        src/gui/TopologyDialog.cpp
        src/gui/EnginePresets.cpp
        src/gui/PythonHighlighter.cpp
        src/gui/RunCommands.cpp
        src/gui/ScriptStaging.cpp
        src/gui/SettingsManager.cpp
        src/gui/CalculatorParameters.cpp
        src/core/AseScriptGenerator.cpp
        src/core/GrapheneOxideBuilder.cpp
        src/core/Structure.cpp
        src/core/UnitCell.cpp
        src/core/Element.cpp
        src/python_bridge/PythonEngine.cpp
    )
    target_include_directories(calango_dialog_test PRIVATE src
        "${CMAKE_CURRENT_BINARY_DIR}/generated")
    # QT_NO_KEYWORDS for the same reason the main target sets it: PythonEngine
    # pulls in CPython headers, whose `slots` member collides with Qt's
    # lowercase keyword. The GUI sources already use the Q_SLOTS spelling.
    target_compile_definitions(calango_dialog_test PRIVATE
        CALANGO_VERSION="${PROJECT_VERSION}"
        QT_NO_KEYWORDS)
    # Qt6::OpenGL for StructureRenderer.hpp, which OverlayModel.hpp pulls in
    # for the OverlayRange type the geometry builders emit.
    target_link_libraries(calango_dialog_test
        PRIVATE Qt6::Widgets Qt6::OpenGL Qt6::OpenGLWidgets Qt6::Svg
                pybind11::embed)
    add_test(NAME dialog_construction COMMAND calango_dialog_test)

    # Workflow pipeline validation on silicon: two linked nodes (geometry
    # optimization with cell relaxation → phonon) sent to processes for
    # real, with Lennard-Jones so the ASE runs finish in seconds. Pins the
    # Waiting/Running/Done lifecycle and that the relaxed structure flows
    # along the link into the phonon node. Self-skips without ASE.
    qt_add_executable(calango_workflow_si_test
        tests/WorkflowSiTest.cpp
        src/gui/WorkflowWindow.cpp
        src/gui/SimulationWizardBase.cpp
        src/gui/SinglePointWizard.cpp
        src/gui/GeometryOptimizationWizard.cpp
        src/gui/MolecularDynamicsWizard.cpp
        src/gui/PhononWizard.cpp
        src/gui/GeometryConstraintsDialog.cpp
        src/gui/EmbeddedKPathEditor.cpp
        src/gui/BrillouinZoneWidget.cpp
        src/gui/BrillouinZoneView.cpp
        src/gui/BrillouinZoneStyleDialog.cpp
        src/ui/IconManager.cpp
        src/gui/ThemeManager.cpp
        src/gui/ProcessManagerPanel.cpp
        src/render/Camera.cpp
        src/core/BrillouinZone.cpp
        src/core/KPath.cpp
        src/gui/GpawElectronicRows.cpp
        src/gui/HubbardParametersDialog.cpp
        src/gui/CondaEnvs.cpp
        src/gui/EnginePresets.cpp
        src/gui/PythonHighlighter.cpp
        src/gui/RunCommands.cpp
        src/gui/ScriptStaging.cpp
        src/gui/SettingsManager.cpp
        src/gui/CalculatorParameters.cpp
        src/gui/GuiUtils.cpp
        src/gui/OverlayModel.cpp
        src/render/ColorMap.cpp
        src/render/ShaderProfile.cpp
        src/core/VolumetricData.cpp
        src/core/AseScriptGenerator.cpp
        src/core/PhononScriptGenerator.cpp
        src/core/Structure.cpp
        src/core/UnitCell.cpp
        src/core/Element.cpp
        src/core/PdbxFile.cpp
        src/python_bridge/AseBridge.cpp
        src/python_bridge/PythonEngine.cpp
        src/jobs/JobRunner.cpp
    )
    target_include_directories(calango_workflow_si_test PRIVATE src
        "${CMAKE_CURRENT_BINARY_DIR}/generated")
    target_compile_definitions(calango_workflow_si_test PRIVATE
        CALANGO_VERSION="${PROJECT_VERSION}"
        # Without this the embedded engine cannot name its interpreter and
        # the test's job-launching python resolves to nonsense.
        CALANGO_DEFAULT_PYTHON="${CALANGO_DEFAULT_PYTHON_PATH}"
        QT_NO_KEYWORDS)
    target_link_libraries(calango_workflow_si_test
        PRIVATE Qt6::Widgets Qt6::OpenGLWidgets Qt6::Svg pybind11::embed)
    if(APPLE)
        target_link_libraries(calango_workflow_si_test
            PRIVATE "-framework OpenGL")
    else()
        target_link_libraries(calango_workflow_si_test PRIVATE OpenGL::GL)
    endif()
    add_test(NAME workflow_si_pipeline COMMAND calango_workflow_si_test)
    set_tests_properties(workflow_si_pipeline PROPERTIES TIMEOUT 900)

    # Workflow data handoff: the same two Geometry Optimization nodes run
    # unlinked (both relax the rattled structure independently, full step
    # count each) and linked (the child inherits the parent's relaxed
    # geometry and converges in <= 1 step). EMT copper for speed; self-skips
    # without ASE.
    qt_add_executable(calango_workflow_handoff_test
        tests/WorkflowHandoffTest.cpp
        src/gui/WorkflowWindow.cpp
        src/gui/SimulationWizardBase.cpp
        src/gui/SinglePointWizard.cpp
        src/gui/GeometryOptimizationWizard.cpp
        src/gui/MolecularDynamicsWizard.cpp
        src/gui/PhononWizard.cpp
        src/gui/GeometryConstraintsDialog.cpp
        src/gui/EmbeddedKPathEditor.cpp
        src/gui/BrillouinZoneWidget.cpp
        src/gui/BrillouinZoneView.cpp
        src/gui/BrillouinZoneStyleDialog.cpp
        src/ui/IconManager.cpp
        src/gui/ThemeManager.cpp
        src/gui/ProcessManagerPanel.cpp
        src/render/Camera.cpp
        src/core/BrillouinZone.cpp
        src/core/KPath.cpp
        src/gui/GpawElectronicRows.cpp
        src/gui/HubbardParametersDialog.cpp
        src/gui/CondaEnvs.cpp
        src/gui/EnginePresets.cpp
        src/gui/PythonHighlighter.cpp
        src/gui/RunCommands.cpp
        src/gui/ScriptStaging.cpp
        src/gui/SettingsManager.cpp
        src/gui/CalculatorParameters.cpp
        src/gui/GuiUtils.cpp
        src/gui/OverlayModel.cpp
        src/render/ColorMap.cpp
        src/render/ShaderProfile.cpp
        src/core/VolumetricData.cpp
        src/core/AseScriptGenerator.cpp
        src/core/PhononScriptGenerator.cpp
        src/core/Structure.cpp
        src/core/UnitCell.cpp
        src/core/Element.cpp
        src/core/PdbxFile.cpp
        src/python_bridge/AseBridge.cpp
        src/python_bridge/PythonEngine.cpp
        src/jobs/JobRunner.cpp
    )
    target_include_directories(calango_workflow_handoff_test PRIVATE src
        "${CMAKE_CURRENT_BINARY_DIR}/generated")
    target_compile_definitions(calango_workflow_handoff_test PRIVATE
        CALANGO_VERSION="${PROJECT_VERSION}"
        CALANGO_DEFAULT_PYTHON="${CALANGO_DEFAULT_PYTHON_PATH}"
        QT_NO_KEYWORDS)
    target_link_libraries(calango_workflow_handoff_test
        PRIVATE Qt6::Widgets Qt6::OpenGLWidgets Qt6::Svg pybind11::embed)
    if(APPLE)
        target_link_libraries(calango_workflow_handoff_test
            PRIVATE "-framework OpenGL")
    else()
        target_link_libraries(calango_workflow_handoff_test PRIVATE OpenGL::GL)
    endif()
    add_test(NAME workflow_handoff COMMAND calango_workflow_handoff_test)
    set_tests_properties(workflow_handoff PROPERTIES TIMEOUT 900)

    # Theme-adaptive icons: that a Light-mode glyph is dark (the white-on-white
    # bug) and that a bound icon is re-tinted when the theme changes. Checks
    # rendered PIXELS, not just the colour table — the table can be right while
    # the widget still shows the pixmap it was handed at construction.
    qt_add_executable(calango_icon_theme_test
        tests/IconThemeTest.cpp
        src/ui/IconManager.cpp
        src/gui/ThemeManager.cpp
    )
    target_include_directories(calango_icon_theme_test PRIVATE src)
    target_link_libraries(calango_icon_theme_test
        PRIVATE Qt6::Widgets Qt6::Svg)
    qt_add_resources(calango_icon_theme_test "icon_theme_test_assets"
        PREFIX "/"
        FILES assets/icons/focus-3-line.svg)
    add_test(NAME icon_theme COMMAND calango_icon_theme_test)

    # Whole-structure transforms (centre / vacuum / wrap). They moved from the
    # Edit Structure dialog's throwaway working copy onto the Structure panel,
    # where they are committed document edits — a wrong result is no longer
    # discardable with Cancel. GUI-free.
    add_executable(calango_transforms_test
        tests/StructureTransformsTest.cpp
        src/core/StructureTransforms.cpp
        src/core/Structure.cpp
        src/core/UnitCell.cpp
        src/core/Element.cpp
    )
    target_include_directories(calango_transforms_test PRIVATE src)
    add_test(NAME structure_transforms COMMAND calango_transforms_test)

    # Overlay geometry: the "Additional overlays" dock packs every entry into
    # one pair of shared streams and slices them with per-overlay ranges. A
    # range that does not match where the triangles landed draws the wrong
    # overlay at the wrong opacity — silently. GL-free and GUI-free.
    qt_add_executable(calango_overlay_test
        tests/OverlayModelTest.cpp
        src/gui/OverlayModel.cpp
        src/render/ColorMap.cpp
        src/core/Structure.cpp
        src/core/UnitCell.cpp
        src/core/Element.cpp
        src/core/VolumetricData.cpp
    )
    target_include_directories(calango_overlay_test PRIVATE src)
    target_link_libraries(calango_overlay_test
        PRIVATE Qt6::Widgets Qt6::OpenGL)
    add_test(NAME overlay_model COMMAND calango_overlay_test)

    # CPK element colours and the legibility of the symbol written on them.
    # The periodic-table dialog and the insertion-element button both paint a
    # raw CPK swatch and label it; CPK is not a designed UI palette, so an
    # unreadable label is a real and silent failure. Checked as a property over
    # all 118 elements, since the failures are individual ones.
    qt_add_executable(calango_element_color_test
        tests/ElementColorTest.cpp
        src/gui/GuiUtils.cpp
        src/core/Element.cpp
    )
    target_include_directories(calango_element_color_test PRIVATE src)
    target_link_libraries(calango_element_color_test PRIVATE Qt6::Widgets)
    add_test(NAME element_colors COMMAND calango_element_color_test)

    # Linear-response Hubbard U. Every failure mode of this method is silent —
    # a chi_0 run left to converge measures chi twice and yields U = 0, an
    # alpha applied per species rather than per atom perturbs a whole
    # sublattice — so the generated driver is asserted on directly. GUI-free,
    # GL-free, Python-free.
    qt_add_executable(calango_hubbard_test
        tests/HubbardLinearResponseTest.cpp
        src/core/HubbardScriptGenerator.cpp
        src/core/AseScriptGenerator.cpp
        src/core/Structure.cpp
        src/core/UnitCell.cpp
        src/core/Element.cpp
        src/core/KPath.cpp
    )
    target_include_directories(calango_hubbard_test PRIVATE src)
    target_compile_definitions(calango_hubbard_test PRIVATE
        CALANGO_VERSION="${PROJECT_VERSION}")
    target_link_libraries(calango_hubbard_test PRIVATE Qt6::Core)
    add_test(NAME hubbard_linear_response COMMAND calango_hubbard_test)

    # Orbit camera: the default view, the plane alignments and the roll axis,
    # checked against the matrices the camera actually produces. Every one of
    # these is silently wrong-able — a pitch of the wrong sign still renders,
    # just the other side of the crystal. GL-free and GUI-free.
    qt_add_executable(calango_camera_test
        tests/CameraTest.cpp
        src/render/Camera.cpp
        src/render/Film.cpp
    )
    target_include_directories(calango_camera_test PRIVATE src)
    target_link_libraries(calango_camera_test PRIVATE Qt6::Gui)
    add_test(NAME camera COMMAND calango_camera_test)

    # Alembic export: the geometry half (face tables, per-element grouping,
    # option handling) checked directly, because its defects produce a
    # well-formed file that imports as garbage rather than an error. The Python
    # half can only be checked for syntax and for the alembic/SQLAlchemy import
    # collision — PyAlembic has no conda-forge build for arm64 macOS.
    qt_add_executable(calango_alembic_test
        tests/AlembicExportTest.cpp
        src/python_bridge/AlembicExporter.cpp
        src/render/StructureRenderer.cpp
        # The renderer builds the Voronoi (Wigner-Seitz) cell outline from
        # the same half-space intersection the Brillouin zone uses.
        src/core/BrillouinZone.cpp
        src/render/ShaderProfile.cpp
        src/render/ColorMap.cpp
        src/core/Structure.cpp
        src/core/UnitCell.cpp
        src/core/Element.cpp
        src/core/Coordination.cpp
        src/core/MarchingCubes.cpp
        src/core/VolumetricData.cpp
    )
    target_include_directories(calango_alembic_test PRIVATE src)
    target_link_libraries(calango_alembic_test
        PRIVATE Qt6::Gui Qt6::OpenGL pybind11::embed)
    target_compile_definitions(calango_alembic_test PRIVATE QT_NO_KEYWORDS)
    add_test(NAME alembic_export COMMAND calango_alembic_test)

    # Results viewers: the GW and optics windows parse a JSON schema written by
    # a Python script, so nothing in the C++ build ties the two together. This
    # pins the schema from the reader's side (offscreen Qt, no GL, no display).
    qt_add_executable(calango_results_viewer_test
        tests/ResultsViewerTest.cpp
        src/gui/GwResultsWindow.cpp
        src/gui/OpticsResultsWindow.cpp
        src/gui/SpectrumPlotWidget.cpp
        src/gui/OpticsPlotStyleDialog.cpp
        src/gui/RamanIrViewer.cpp
        src/gui/LinePlotWidget.cpp
        # The electronic-structure viewer, for the two sidecars the Electronic
        # Structure run writes beside bands.json: band_symmetry.json and
        # fatbands.json. Same contract as the rest of this test — keys a Python
        # generator writes, parsed in C++, with nothing in the build linking
        # the two.
        src/gui/BandPdosWindow.cpp
        src/gui/BandPdosView.cpp
        src/gui/PlotStyleDialog.cpp
        src/gui/GuiUtils.cpp
        src/core/BandGap.cpp
        src/core/Structure.cpp
        src/core/UnitCell.cpp
        src/core/Element.cpp
    )
    target_include_directories(calango_results_viewer_test PRIVATE src)
    # Qt6::Svg for the plot's vector export path, which BandPdosView compiles
    # in regardless of whether this test drives it.
    target_link_libraries(calango_results_viewer_test
        PRIVATE Qt6::Widgets Qt6::Svg)
    add_test(NAME results_viewers COMMAND calango_results_viewer_test)

    # Graphene oxide: the single-group-per-carbon constraint is chemical, not
    # statistical — two groups on one carbon is a pentavalent carbon, an
    # impossible structure. Re-derived from the returned coordinates.
    add_executable(calango_graphene_oxide_test
        tests/GrapheneOxideTest.cpp
        src/core/GrapheneOxideBuilder.cpp
        src/core/Structure.cpp
        src/core/UnitCell.cpp
        src/core/Element.cpp
    )
    target_include_directories(calango_graphene_oxide_test PRIVATE src)
    add_test(NAME graphene_oxide COMMAND calango_graphene_oxide_test)

    # Native SQS generator: composition exactness across binary/ternary/
    # quaternary alloys and, above all, that the generated decoration really is
    # quasirandom (first-shell Warren-Cowley order parameter near zero).
    add_executable(calango_sqs_generator_test
        tests/SqsGeneratorTest.cpp
        src/core/SqsGenerator.cpp
        src/core/Structure.cpp
        src/core/UnitCell.cpp
        src/core/Element.cpp
    )
    target_include_directories(calango_sqs_generator_test PRIVATE src)
    add_test(NAME sqs_generator COMMAND calango_sqs_generator_test)

    # Generated ASE scripts: logger-module import (rather than an inline copy)
    # and the MACE / GPAW calculator parameters. No Qt, no Python, no network.
    # `--dump <dir>` writes the scripts out for an external py_compile check.
    add_executable(calango_script_test
        tests/ScriptGenerationTest.cpp
        src/core/AseScriptGenerator.cpp
        src/core/BornChargesScriptGenerator.cpp
        src/core/ElectronicScriptGenerator.cpp
        src/core/BandSymmetryScriptGenerator.cpp
        src/core/PhononScriptGenerator.cpp
        src/core/OpticsScriptGenerator.cpp
        src/core/GwScriptGenerator.cpp
        src/core/RamanIrScriptGenerator.cpp
        src/core/RandomNoiseScriptGenerator.cpp
        src/core/CddScriptGenerator.cpp
        src/core/HubbardScriptGenerator.cpp
        src/core/XasScriptGenerator.cpp
        src/core/UnfoldingScriptGenerator.cpp
        src/core/TwoDBandsScriptGenerator.cpp
        src/core/WannierScriptGenerator.cpp
        src/core/DefectScriptGenerator.cpp
        src/core/FermiSurfaceScriptGenerator.cpp
        src/core/TopologyScriptGenerator.cpp
        src/core/BandUnfolding.cpp
        src/core/UnitCell.cpp
    )
    target_include_directories(calango_script_test PRIVATE src
        "${CMAKE_CURRENT_BINARY_DIR}/generated")
    target_compile_definitions(calango_script_test PRIVATE
        CALANGO_VERSION="${PROJECT_VERSION}")
    add_test(NAME generated_ase_scripts COMMAND calango_script_test)

    # Volumetric grid parsing: the value block is read with a bulk read plus
    # std::from_chars rather than through istream >> double (5x faster on a
    # production grid). A fast parser that is subtly wrong renders a plausible
    # isosurface of the wrong data, so the tokenizer is pinned here.
    add_executable(calango_volumetric_parse_test
        tests/VolumetricParseTest.cpp
        src/core/VolumetricData.cpp
        src/core/Structure.cpp
        src/core/UnitCell.cpp
        src/core/Element.cpp
    )
    target_include_directories(calango_volumetric_parse_test PRIVATE src)
    add_test(NAME volumetric_parse COMMAND calango_volumetric_parse_test)

    # Periodic-image bound: the degenerate-cell guard that five of the seven
    # former private copies of imageRange() were missing.
    add_executable(calango_periodic_images_test
        tests/PeriodicImagesTest.cpp
        src/core/UnitCell.cpp
    )
    target_include_directories(calango_periodic_images_test PRIVATE src)
    add_test(NAME periodic_image_range COMMAND calango_periodic_images_test)

    # Extended-XYZ per-atom attributes (forces / magmoms / velocities) survive
    # the read -> model -> write round trip, and collinear magmoms surface as
    # vectors for the viewport overlay.
    add_executable(calango_extxyz_test
        tests/ExtxyzAttributesTest.cpp
        src/core/Structure.cpp
        src/core/UnitCell.cpp
        src/core/Element.cpp
        src/core/KPath.cpp
        src/python_bridge/PythonEngine.cpp
        src/python_bridge/AseBridge.cpp
        src/core/PdbxFile.cpp
    )
    target_include_directories(calango_extxyz_test PRIVATE src)
    target_compile_definitions(calango_extxyz_test PRIVATE
        CALANGO_DEFAULT_PYTHON="${CALANGO_DEFAULT_PYTHON_PATH}")
    target_link_libraries(calango_extxyz_test PRIVATE pybind11::embed)
    add_test(NAME extxyz_attributes COMMAND calango_extxyz_test)

    # Formation-energy convex hull backing the Cluster Expansion Calculation
    # results plot (lower-hull semantics, energy above hull, degenerate input).
    add_executable(calango_convex_hull_test
        tests/ConvexHullTest.cpp
        src/core/ConvexHull.cpp
    )
    target_include_directories(calango_convex_hull_test PRIVATE src)
    add_test(NAME convex_hull COMMAND calango_convex_hull_test)

    # Automated band-gap detection (direct / indirect / metallic) behind the
    # Electronic Structure summary box.
    add_executable(calango_bandgap_test
        tests/BandGapTest.cpp
        src/core/BandGap.cpp
    )
    target_include_directories(calango_bandgap_test PRIVATE src)
    add_test(NAME band_gap COMMAND calango_bandgap_test)

    # Popescu-Zunger unfolding geometry: supercell matrix deduction, the
    # reciprocal fold (M-transpose), and spectral-weight broadening.
    add_executable(calango_unfolding_test
        tests/BandUnfoldingTest.cpp
        src/core/BandUnfolding.cpp
        src/core/UnitCell.cpp
    )
    target_include_directories(calango_unfolding_test PRIVATE src)
    add_test(NAME band_unfolding COMMAND calango_unfolding_test)

    # Native GPAW partial-charge partitioning benchmark on bulk Si. Pure Python
    # (runs against the embedded interpreter's environment); it self-skips with
    # exit 0 when GPAW is not importable, so it is safe to register here.
    add_test(NAME partial_charge_si
        COMMAND ${Python3_EXECUTABLE}
                ${CMAKE_CURRENT_SOURCE_DIR}/tests/partial_charge_si_benchmark.py)

    # GPAW linear-optics validation on diamond (C): SCF (500 eV, 7×7×7, PBE) →
    # fixed-density NSCF → DielectricFunction, verifying the ε₁/ε₂ CSV+JSON
    # output without runtime exceptions. Pure Python; self-skips (exit 0) when
    # the GPAW response stack is not importable.
    add_test(NAME optics_c_diamond
        COMMAND ${Python3_EXECUTABLE}
                ${CMAKE_CURRENT_SOURCE_DIR}/tests/optics_diamond_benchmark.py)

    # Native MLWF (Marzari-Vanderbilt) validation on bulk Si: GPAW SCF →
    # ase.dft.wannier localization, verifying the total Wannier spread Ω
    # converges. Pure Python; self-skips (exit 0) without GPAW/Wannier.
    add_test(NAME wannier_silicon
        COMMAND ${Python3_EXECUTABLE}
                ${CMAKE_CURRENT_SOURCE_DIR}/tests/wannier_silicon_benchmark.py)

    # Wannier interpolation validation on bulk Si: MLWF localization → H(R)→H(k)
    # Fourier interpolation along Γ-X-L-Γ, verifying smooth artifact-free bands
    # and a fundamental gap. Self-skips (exit 0) without GPAW/Wannier.
    add_test(NAME wannier_interpolation_silicon
        COMMAND ${Python3_EXECUTABLE}
                ${CMAKE_CURRENT_SOURCE_DIR}/tests/wannier_interpolation_silicon_benchmark.py)

    # ELF validation on bulk Si: GPAW SCF → gpaw.elf.elf_from_dft_calculation,
    # verifying a finite 3D η ∈ [0,1] field. This is the API the Single-point
    # wizard's ELF density export drives, so it stays even though the
    # standalone ELF module is gone. Self-skips (exit 0) without GPAW/gpaw.elf.
    add_test(NAME elf_silicon
        COMMAND ${Python3_EXECUTABLE}
                ${CMAKE_CURRENT_SOURCE_DIR}/tests/elf_silicon_benchmark.py)

    # GPAW G0W0 end-to-end: runs the GENERATED script against a real
    # baseline .gpw and checks the gw.json. Catches what neither review nor
    # py_compile can — e.g. the response code asking for more bands than the
    # ground state holds. Self-skips (exit 0) without the GPAW response stack.
    add_test(NAME gw_silicon
        COMMAND ${Python3_EXECUTABLE}
                ${CMAKE_CURRENT_SOURCE_DIR}/tests/gw_silicon_benchmark.py
                $<TARGET_FILE:calango_script_test>)

    # Tetrahedron integration validated on graphene, whose low-energy
    # absorbance is the universal pi*alpha = 2.293% — a reference independent
    # of the calculation. Runs the GENERATED optics script end to end.
    #
    # The SLOWEST test in the suite (~4 min): it needs a 36x36 high-symmetry
    # k-mesh to resolve the Dirac cone. A coarser mesh does cross pi*alpha, but
    # at the wrong energy and for the wrong reason, so it cannot be cheapened
    # without making the test dishonest. Self-skips without GPAW.
    add_test(NAME graphene_tetrahedron
        COMMAND ${Python3_EXECUTABLE}
                ${CMAKE_CURRENT_SOURCE_DIR}/tests/graphene_tetrahedron_benchmark.py
                $<TARGET_FILE:calango_script_test>)
    set_tests_properties(graphene_tetrahedron PROPERTIES TIMEOUT 1800)

    # Band symmetry classification validated on graphene against Kogan &
    # Nazarov, PRB 85, 115418 (2012) — a paper whose entire content is the
    # irrep each graphene band realizes, so it is a table of expected answers
    # rather than a number to reproduce approximately. Runs the GENERATED
    # script end to end: little-group selection, character evaluation, the
    # numerically built character table and the JSON schema.
    #
    # The pi manifold is identified by its p_z FATBAND weight rather than by
    # its energy, so this covers both features and does not depend on the
    # interlayer states staying out of the way. Self-skips without GPAW.
    add_test(NAME graphene_band_symmetry
        COMMAND ${Python3_EXECUTABLE}
                ${CMAKE_CURRENT_SOURCE_DIR}/tests/graphene_band_symmetry_benchmark.py
                $<TARGET_FILE:calango_script_test>)
    set_tests_properties(graphene_band_symmetry PROPERTIES TIMEOUT 1800)

    # 2D optical observables: unit conventions and vacuum-thickness
    # invariance, checked against graphene's universal absorbance (pi*alpha).
    # A stray 4*pi or hbar*c factor is invisible to review and to py_compile,
    # and would only surface as a plausible-looking wrong number.
    add_test(NAME optics_2d_observables
        COMMAND ${Python3_EXECUTABLE}
                ${CMAKE_CURRENT_SOURCE_DIR}/tests/optics_2d_test.py
                $<TARGET_FILE:calango_script_test>)

    # LAMMPS: the generated scripts are EXECUTED against recording stubs that
    # mirror ASE's own calculator signatures, so a keyword ASE would reject
    # fails here rather than on a machine that actually has LAMMPS. No LAMMPS
    # build needed; self-skips without ASE.
    add_test(NAME lammps_scripts
        COMMAND ${Python3_EXECUTABLE}
                ${CMAKE_CURRENT_SOURCE_DIR}/tests/lammps_script_test.py
                $<TARGET_FILE:calango_script_test>)

    # LAMMPS against a REAL LAMMPS build: the shipped scripts driven through
    # both ASE interfaces, checked against the closed-form Lennard-Jones fcc
    # cohesive energy (with the truncated tail added back analytically). The
    # stub-based lammps_scripts test above proves the arguments are right;
    # this proves the physics that comes back is. Self-skips without LAMMPS.
    add_test(NAME lammps_environment
        COMMAND ${Python3_EXECUTABLE}
                ${CMAKE_CURRENT_SOURCE_DIR}/tests/lammps_env_benchmark.py
                $<TARGET_FILE:calango_script_test>)
    set_tests_properties(lammps_environment PROPERTIES TIMEOUT 900)

    # Raman / IR array algebra: the mass-weighted diagonalization, the Z*
    # contraction, the polarizability-derivative contraction and the Stokes
    # prefactor, against an A-B spring pair whose every answer is closed-form.
    # None of these can fail loudly — a dropped 1/sqrt(M) or a transposed
    # einsum yields a plausible spectrum that is simply wrong.
    add_test(NAME raman_ir_math
        COMMAND ${Python3_EXECUTABLE}
                ${CMAKE_CURRENT_SOURCE_DIR}/tests/raman_ir_math_test.py
                $<TARGET_FILE:calango_script_test>)

    # Raman / IR end-to-end validation on MgO (rock-salt). Runs the SHIPPED
    # generated script against a GPAW SCF baseline and Born charges computed
    # here, and checks it against physics that admits no fitting: rock-salt is
    # centrosymmetric, so its T_1u mode is IR-active and Raman-FORBIDDEN. A
    # module that got the polarizability contraction wrong lights that mode up
    # and is caught. Self-skips (exit 0) without GPAW.
    add_test(NAME raman_ir_mgo
        COMMAND ${Python3_EXECUTABLE}
                ${CMAKE_CURRENT_SOURCE_DIR}/tests/raman_ir_mgo_benchmark.py
                $<TARGET_FILE:calango_script_test>)
    set_tests_properties(raman_ir_mgo PROPERTIES TIMEOUT 3600)

    # Yambo quasiparticle-report parser: the only generated code whose result
    # depends on reading a third-party text format, and the one pipeline that
    # cannot be run here. Extracts parse_qp_report() from a freshly dumped
    # script by AST and exercises it against realistic reports. No Yambo, no
    # network — just Python.
    add_test(NAME gw_qp_parser
        COMMAND ${Python3_EXECUTABLE}
                ${CMAKE_CURRENT_SOURCE_DIR}/tests/gw_qp_parser_test.py
                $<TARGET_FILE:calango_script_test>)

    # Optics validation on bulk Si: GPAW SCF → NSCF → DielectricFunction,
    # verifying ε₁/ε₂. Self-skips (exit 0) without the GPAW response stack.
    add_test(NAME optics_silicon
        COMMAND ${Python3_EXECUTABLE}
                ${CMAKE_CURRENT_SOURCE_DIR}/tests/optics_silicon_benchmark.py)

    # Every test runs with its Calango config sandboxed into the build tree.
    # SettingsManager mirrors QSettings to a JSON file at a fixed home-dir
    # path; without this, any test that constructs a dialog whose code path
    # calls SettingsManager::save() rewrites the DEVELOPER'S own
    # ~/.calango/settings.json. APPEND, so tests that already declare their
    # own ENVIRONMENT entries (materials_project_mp1434) keep them.
    get_property(_calango_all_tests DIRECTORY PROPERTY TESTS)
    if(_calango_all_tests)
        set_property(TEST ${_calango_all_tests} APPEND PROPERTY ENVIRONMENT
            "CALANGO_CONFIG_DIR=${CMAKE_CURRENT_BINARY_DIR}/test-config")
    endif()
endif()

# ---------------------------------------------------------------------------
# Installation
# ---------------------------------------------------------------------------
# A relocatable Python environment (with ASE installed) to ship inside the
# installers, e.g. a python-build-standalone tree. It must contain
# bin/python3. Left empty, the installed app falls back to the runtime
# resolution order documented in PythonEngine.hpp ($CALANGO_PYTHON,
# $VIRTUAL_ENV, system python3). Note: a plain project .venv is NOT
# relocatable — its bin/python links back to the build machine's base
# interpreter.
set(CALANGO_EMBEDDED_PYTHON_DIR "" CACHE PATH
    "Standalone Python distribution (with ASE) to bundle into installers")

include(GNUInstallDirs)

if(APPLE AND CALANGO_MACOS_BUNDLE)
    install(TARGETS calango BUNDLE DESTINATION .)

    # macdeployqt copies the Qt frameworks/plugins into the bundle and
    # rewrites the load paths. It runs at install/package time against the
    # staged .app, BEFORE the Python payload is staged — macdeployqt walks
    # every Mach-O file it finds and would otherwise try to rewrite the load
    # commands of the standalone interpreter's stdlib extensions. That both
    # fails loudly (e.g. _tkinter has no headerpad to grow into) and risks
    # corrupting/invalidating modules that need no Qt deployment at all.
    get_target_property(_qmake_location Qt6::qmake IMPORTED_LOCATION)
    get_filename_component(_qt_bin_dir "${_qmake_location}" DIRECTORY)
    find_program(MACDEPLOYQT_EXECUTABLE macdeployqt HINTS "${_qt_bin_dir}")

    # CMake strips the build-tree rpath when installing, which leaves
    # macdeployqt unable to resolve the @rpath references of the Qt
    # frameworks/plugins it scans (QtDBus etc. silently go missing).
    # Keep the Qt lib dir as install rpath — macdeployqt rewrites every
    # copied library to @executable_path afterwards.
    get_filename_component(_qt_lib_dir "${_qt_bin_dir}/../lib" ABSOLUTE)
    set_target_properties(calango PROPERTIES INSTALL_RPATH "${_qt_lib_dir}")
    if(MACDEPLOYQT_EXECUTABLE)
        # Two passes: frameworks copied into the bundle lose the
        # @loader_path context their @rpath dependencies resolve against,
        # so transitive deps (QtGui -> QtDBus, plugin deps) are missed on
        # the first scan. The second pass re-scans the deployed copies —
        # with -libpath naming the copy source — and completes the set.
        install(CODE "
            message(STATUS \"Deploying Qt into \${CMAKE_INSTALL_PREFIX}/calango.app\")
            foreach(_pass RANGE 1 2)
                execute_process(
                    COMMAND \"${MACDEPLOYQT_EXECUTABLE}\"
                            \"\${CMAKE_INSTALL_PREFIX}/calango.app\"
                            -always-overwrite
                            -libpath=${_qt_lib_dir}
                    COMMAND_ERROR_IS_FATAL ANY)
            endforeach()
        ")
    else()
        message(WARNING "macdeployqt not found next to ${_qmake_location} — "
                        "the packaged .app will not be self-contained")
    endif()

    # Embedded interpreter — PythonEngine looks this location up at runtime.
    # Staged after macdeployqt (see above) and before the script below, which
    # needs the payload in place to bridge it into the framework's sys.path.
    if(CALANGO_EMBEDDED_PYTHON_DIR)
        install(DIRECTORY "${CALANGO_EMBEDDED_PYTHON_DIR}/"
                DESTINATION calango.app/Contents/Resources/python
                USE_SOURCE_PERMISSIONS)
    endif()

    # macdeployqt skips the linked Python.framework (libpython from
    # Development.Embed) — copy it into the bundle and retarget the load
    # command so the app launches without the build machine's Python.
    # Also re-signs the whole bundle, which is why it must run last: the
    # Python payload above has to be inside the signature.
    install(CODE "
        execute_process(
            COMMAND bash
                \"${CMAKE_CURRENT_SOURCE_DIR}/packaging/macos/embed_python_framework.sh\"
                \"\${CMAKE_INSTALL_PREFIX}/calango.app\"
            COMMAND_ERROR_IS_FATAL ANY)
    ")
elseif(UNIX AND NOT APPLE)
    install(TARGETS calango RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})

    # Desktop integration: launcher entry, MIME type for .calproj project
    # files (main.cpp routes them to the workspace loader), and icons.
    install(FILES packaging/linux/calango.desktop
            DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
    install(FILES packaging/linux/calango-mime.xml
            DESTINATION ${CMAKE_INSTALL_DATADIR}/mime/packages)
    # icon_linux.png, not the macOS icon_osx.png: freedesktop themes expect a
    # full-bleed icon and apply their own shape/padding, so the ~80% margin
    # baked into the macOS squircle would render Calango visibly smaller than
    # its neighbours in every launcher.
    #
    # /usr/share/pixmaps is size-agnostic, so the master PNG goes there as-is;
    # it resolves the .desktop "Icon=calango" lookup everywhere. The hicolor
    # theme below is where modern desktops actually look, but its directories
    # are exact-size, so those variants are only installed when a scaler is
    # available (same optional-tool pattern as sips/iconutil on macOS). Add a
    # hicolor/scalable/apps/calango.svg install here if a real SVG is
    # committed later.
    install(FILES assets/calango/icon_linux.png
            DESTINATION ${CMAKE_INSTALL_DATADIR}/pixmaps
            RENAME calango.png)

    find_program(MAGICK_EXECUTABLE NAMES magick convert)
    if(MAGICK_EXECUTABLE)
        set(CALANGO_ICON_SOURCE
            "${CMAKE_CURRENT_SOURCE_DIR}/assets/calango/icon_linux.png")
        foreach(size 32 48 64 128 256 512)
            set(scaled "${CMAKE_CURRENT_BINARY_DIR}/icons/${size}/calango.png")
            add_custom_command(OUTPUT "${scaled}"
                COMMAND ${CMAKE_COMMAND} -E make_directory
                        "${CMAKE_CURRENT_BINARY_DIR}/icons/${size}"
                COMMAND "${MAGICK_EXECUTABLE}" "${CALANGO_ICON_SOURCE}"
                        -resize ${size}x${size} "${scaled}"
                DEPENDS "${CALANGO_ICON_SOURCE}"
                COMMENT "Scaling calango.png to ${size}x${size}")
            list(APPEND CALANGO_SCALED_ICONS "${scaled}")
            install(FILES "${scaled}"
                    DESTINATION
                    ${CMAKE_INSTALL_DATADIR}/icons/hicolor/${size}x${size}/apps)
        endforeach()
        add_custom_target(calango_linux_icons ALL DEPENDS ${CALANGO_SCALED_ICONS})
    else()
        message(STATUS "ImageMagick not found — installing the pixmaps icon "
                       "only, without the hicolor theme sizes")
    endif()

    if(CALANGO_EMBEDDED_PYTHON_DIR)
        install(DIRECTORY "${CALANGO_EMBEDDED_PYTHON_DIR}/"
                DESTINATION ${CMAKE_INSTALL_LIBDIR}/calango/python
                USE_SOURCE_PERMISSIONS)
    endif()
endif()

# ---------------------------------------------------------------------------
# Packaging (CPack)
#   macOS:  cmake -B build -DCALANGO_MACOS_BUNDLE=ON ... && cpack --config
#           build/CPackConfig.cmake  ->  drag-and-drop .dmg
#   Linux:  cpack  ->  .deb with desktop/MIME/icon integration
# ---------------------------------------------------------------------------
set(CPACK_PACKAGE_NAME "calango")
set(CPACK_PACKAGE_VENDOR "Seixas Research")
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "${PROJECT_DESCRIPTION}")
set(CPACK_PACKAGE_HOMEPAGE_URL "https://github.com/seixas-research/calango")
set(CPACK_PACKAGE_CONTACT "Leandro Seixas <leandro.fisica@gmail.com>")
set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE")
set(CPACK_PACKAGE_CHECKSUM SHA256)
set(CPACK_STRIP_FILES TRUE)

if(APPLE AND CALANGO_MACOS_BUNDLE)
    # DragNDrop = a .dmg whose window holds Calango.app plus an
    # /Applications symlink, the standard drag-to-install layout.
    set(CPACK_GENERATOR "DragNDrop")
    set(CPACK_DMG_VOLUME_NAME "Calango ${PROJECT_VERSION}")
    set(CPACK_DMG_FORMAT "UDZO") # zlib-compressed, read-only
    # No click-through SLA: it blocks scripted mounts and adds friction
    # for an MIT-licensed app (the license still ships inside the .deb).
    set(CPACK_DMG_SLA_USE_RESOURCE_FILE_LICENSE OFF)
    set(CPACK_PACKAGE_FILE_NAME
        "calango-${PROJECT_VERSION}-macos-${CMAKE_SYSTEM_PROCESSOR}")
elseif(UNIX AND NOT APPLE)
    set(CPACK_GENERATOR "DEB")
    set(CPACK_DEBIAN_FILE_NAME "DEB-DEFAULT") # calango_<ver>_<arch>.deb
    set(CPACK_DEBIAN_PACKAGE_SECTION "science")
    set(CPACK_DEBIAN_PACKAGE_PRIORITY "optional")
    set(CPACK_DEBIAN_PACKAGE_HOMEPAGE "${CPACK_PACKAGE_HOMEPAGE_URL}")
    # dpkg-shlibdeps derives the Qt6/OpenGL/libpython runtime dependencies
    # from the linked shared libraries.
    set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON)
    if(NOT CALANGO_EMBEDDED_PYTHON_DIR)
        # No bundled interpreter: the embedded runtime resolves the system
        # python3, which needs ASE for structure I/O and job features.
        set(CPACK_DEBIAN_PACKAGE_DEPENDS "python3 (>= 3.9)")
        # imageio drives the animation export. Its ffmpeg companion is
        # `imageio-ffmpeg`, which Debian does not package under that name — a
        # system ffmpeg satisfies the same need, so it is suggested here and
        # the pip name is documented in packaging/dependencies.txt.
        # python3-dftd4 (the dynamically linked DFT-D4 library + Python API)
        # and python3-phonopy back the dispersion coupling and the phonon
        # symmetry features; torch-dftd has no Debian package — its pip name
        # is documented in packaging/dependencies.txt.
        set(CPACK_DEBIAN_PACKAGE_RECOMMENDS
            "python3-ase, python3-imageio, python3-dftd4, python3-phonopy")
        set(CPACK_DEBIAN_PACKAGE_SUGGESTS "ffmpeg")
    endif()
    # Refresh MIME/desktop/icon caches on install and removal.
    set(CPACK_DEBIAN_PACKAGE_CONTROL_EXTRA
        "${CMAKE_CURRENT_SOURCE_DIR}/packaging/linux/postinst;${CMAKE_CURRENT_SOURCE_DIR}/packaging/linux/postrm")
    set(CPACK_DEBIAN_PACKAGE_CONTROL_STRICT_PERMISSION TRUE)
endif()

include(CPack)
