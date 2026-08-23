cmake_minimum_required(VERSION 3.21)

project(calango
    VERSION 26.8.40
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

# Calango's native all-electron DFT engine (src/dft) and native SCC-DFTB
# engine (src/dftb) were REMOVED (both were substantially implemented but
# narrowly exposed — one wizard, one task each — with no downstream feature
# wiring; see git history for the removal). They were LAPACK's only
# consumers in this codebase (CALANGO_BLAS/find_package(LAPACK)/
# calango_dft_link() used to live here) — every other numerics-heavy module
# (BSE, Wannier/Berry phase, Boltzmann transport, elastic/piezoelectric
# tensors, DSIM/EGQCA, isosurfaces) is hand-rolled and always has been, by
# design (see e.g. ElasticTensor.cpp's own comment on why). The packaged
# `calango` binary now links no BLAS/LAPACK implementation at all — not
# Accelerate, not OpenBLAS, not MKL — which structurally closes the whole
# class of incident CALANGO_BLAS was added to fix (see
# packaging/README.md's "BLAS/LAPACK backend" section for that history).
# packaging/linux/audit_elf_deps.py and packaging/macos/audit_macho_deps.py
# remain in the packaging pipeline regardless: they catch any future
# build-machine-library leak, not just this one.

# HDF5 for VolumetricData's saveHdf5()/loadHdf5()/convertToHdf5() — the
# compressed charge-density container (see
# docs/sphinx/source/reference/hdf5_density.md). REQUIRED, unlike LAPACK
# above: there is no native fallback for reading a .h5 file a user already
# has, and both the calculator setup pages' "HDF5 compression" option and the
# Dump Charge Densities node call this same conversion path. C API only (not
# COMPONENTS CXX / libhdf5_cpp) — VolumetricData.cpp is Qt-free src/core, and
# the plain C API needs no extra library on top of the one every HDF5
# install ships.
find_package(HDF5 REQUIRED COMPONENTS C)

function(calango_hdf5_link target)
    target_link_libraries(${target} PRIVATE HDF5::HDF5)
endfunction()



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
    src/CalangoApplication.hpp
    src/FileOpenQueue.hpp

    # core — pure C++ data model (no Qt / no OpenGL): the "Model" of MVC
    src/core/Vec3.hpp
    src/core/Element.hpp
    src/core/Element.cpp
    src/core/Atom.hpp
    src/core/UnitCell.hpp
    src/core/UnitCell.cpp
    src/core/PeriodicImages.hpp
    src/core/PhysicalConstants.hpp
    src/core/PingPongOrder.hpp
    src/core/RandomRotation.hpp
    src/core/TextUtils.hpp
    src/core/Structure.hpp
    src/core/Structure.cpp
    src/core/BondRules.hpp
    src/core/BondRules.cpp
    src/core/StructureTransforms.hpp
    src/core/StructureTransforms.cpp
    src/core/CalculatorConfig.hpp
    src/core/AseScriptGenerator.hpp
    src/core/AseScriptGenerator.cpp
    src/core/EngineCalculatorBlocks.hpp
    src/core/EngineCalculatorBlocks.cpp
    src/core/BrillouinZone.hpp
    src/core/BrillouinZone.cpp
    src/core/KPath.hpp
    src/core/KPath.cpp
    src/core/Rdf.hpp
    src/core/Rdf.cpp
    src/core/Coordination.hpp
    src/core/Coordination.cpp
    src/core/StructuralPhase.hpp
    src/core/StructuralPhase.cpp
    src/core/HydrogenBonds.hpp
    src/core/HydrogenBonds.cpp
    src/core/HydrogenCompletion.hpp
    src/core/HydrogenCompletion.cpp
    # Molecular Design: the 2D sketch model, its SMILES layer, and the
    # 2D -> 3D embedding the "Send to 3D Viewport" button runs. All three are
    # Qt-free and Python-free, like the polymer/ice/graphene-oxide builders.
    src/core/MoleculeGraph.hpp
    src/core/MoleculeGraph.cpp
    src/core/Smiles.hpp
    src/core/Smiles.cpp
    src/core/MoleculeEmbed3d.hpp
    src/core/MoleculeEmbed3d.cpp
    src/core/Distributions.hpp
    src/core/Distributions.cpp
    src/core/StructureFactor.hpp
    src/core/StructureFactor.cpp
    src/core/Noise.hpp
    src/core/Noise.cpp
    src/core/PhononScriptGenerator.hpp
    src/core/PhononScriptGenerator.cpp
    src/core/TdbDatabase.cpp
    src/core/TdbExpression.cpp
    src/core/TdbWriter.cpp
    src/core/CalphadModel.cpp
    src/core/CalphadScriptGenerator.cpp
    src/core/PhaseDiagram.cpp
    src/core/PhononThermodynamics.hpp
    src/core/PhononThermodynamics.cpp
    # Thermodynamic integration: absolute free energies of liquids and
    # solids. MdIntegratorBlocks is header-only on purpose — it is shared
    # with AseScriptGenerator, which eight targets already compile.
    src/core/MdIntegratorBlocks.hpp
    src/core/ThermodynamicIntegration.hpp
    src/core/ThermodynamicIntegration.cpp
    src/core/ThermodynamicIntegrationScriptGenerator.hpp
    src/core/ThermodynamicIntegrationScriptGenerator.cpp
    src/core/VibrationalModes.hpp
    src/core/VibrationalModes.cpp
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
    src/core/IsosurfaceContinuation.hpp
    src/core/IsosurfaceContinuation.cpp
    src/core/BaselineSummary.hpp
    src/core/BaselineSummary.cpp
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
    src/core/NonlinearOpticsScriptGenerator.hpp
    src/core/NonlinearOpticsScriptGenerator.cpp
    src/core/GwScriptGenerator.hpp
    src/core/GwScriptGenerator.cpp
    src/core/GrapheneOxideBuilder.hpp
    src/core/GrapheneOxideBuilder.cpp
    src/core/GrapheneOxideSites.cpp
    src/core/GrapheneOxidePercolation.hpp
    src/core/GrapheneOxidePercolation.cpp
    src/core/AngleGeometry.hpp
    src/core/GrapheneOxideGroupAnalysis.hpp
    src/core/GrapheneOxideGroupAnalysis.cpp
    src/core/GrapheneOxidePairCorrelation.hpp
    src/core/GrapheneOxidePairCorrelation.cpp
    src/core/WorkflowReport.cpp
    src/core/GrapheneOxideMdmcScriptGenerator.cpp
    src/core/BornChargesScriptGenerator.hpp
    src/core/BornChargesScriptGenerator.cpp
    src/core/PolarizationScriptHelpers.hpp
    src/core/PolarizationScriptHelpers.cpp
    src/core/StrainVoigt.hpp
    src/core/StrainScriptHelpers.hpp
    src/core/StrainScriptHelpers.cpp
    src/core/PiezoelectricTensor.hpp
    src/core/PiezoelectricTensor.cpp
    src/core/PiezoelectricScriptGenerator.hpp
    src/core/PiezoelectricScriptGenerator.cpp
    src/core/ElasticTensor.hpp
    src/core/ElasticTensor.cpp
    src/core/ElasticScriptGenerator.hpp
    src/core/ElasticScriptGenerator.cpp
    src/core/RamanIrScriptGenerator.hpp
    src/core/RamanIrScriptGenerator.cpp
    src/core/PdbxFile.hpp
    src/core/PdbxFile.cpp
    src/core/WannierScriptGenerator.hpp
    src/core/DefectScriptGenerator.hpp
    src/core/DefectScriptGenerator.cpp
    src/core/Defect2dScriptGenerator.hpp
    src/core/Defect2dScriptGenerator.cpp
    src/core/FermiSurfaceScriptGenerator.hpp
    src/core/FermiSurfaceScriptGenerator.cpp
    src/core/TopologyScriptGenerator.hpp
    src/core/TopologyScriptGenerator.cpp
    src/core/TwoDBandsScriptGenerator.hpp
    src/core/TwoDBandsScriptGenerator.cpp
    src/core/WorkfunctionScriptGenerator.hpp
    src/core/WorkfunctionScriptGenerator.cpp
    src/core/WannierScriptGenerator.cpp
    src/core/LdosScriptGenerator.hpp
    src/core/LdosScriptGenerator.cpp
    src/core/EnergyDiagramScriptGenerator.hpp
    src/core/EnergyDiagramScriptGenerator.cpp
    src/core/WavefunctionScriptGenerator.hpp
    src/core/WavefunctionScriptGenerator.cpp
    src/core/AdsorptionSites.hpp
    src/core/AdsorptionSites.cpp
    src/core/ClusterExpansion.hpp
    src/core/ClusterExpansion.cpp
    src/core/ClusterExpansionFit.hpp
    src/core/ClusterExpansionFit.cpp
    src/core/ClusterVariation.cpp
    src/core/Egqca.hpp
    src/core/Egqca.cpp
    src/core/Dsim.hpp
    src/core/Dsim.cpp
    src/core/BerryPhase.cpp
    src/core/BoltzmannTransport.cpp
    src/core/CpaSolver.cpp
    src/core/JobFailureReason.cpp
    src/core/GpawOutputParser.hpp
    src/core/GpawOutputParser.cpp
    src/core/CrpaSolver.cpp
    src/core/BseSolver.hpp
    src/core/BseSolver.cpp
    src/core/WannierHamiltonian.cpp
    src/core/SublatticeClusterVariation.cpp
    src/core/BandGap.hpp
    src/core/BandGap.cpp
    src/core/BandUnfolding.hpp
    src/core/BandUnfolding.cpp
    src/core/UnfoldingScriptGenerator.hpp
    src/core/UnfoldingScriptGenerator.cpp
    src/core/ConvexHull.hpp
    src/core/ConvexHull.cpp
    src/core/TernaryConvexHull.hpp
    src/core/TernaryConvexHull.cpp
    src/core/ClusterExpansionScriptGenerator.hpp
    src/core/ClusterExpansionScriptGenerator.cpp
    src/core/DsimScriptGenerator.hpp
    src/core/DsimScriptGenerator.cpp
    src/core/ConvergenceSweepBlocks.hpp
    src/core/ConvergenceSweepBlocks.cpp
    src/core/CutoffConvergenceScriptGenerator.hpp
    src/core/CutoffConvergenceScriptGenerator.cpp
    src/core/KpointsConvergenceScriptGenerator.hpp
    src/core/KpointsConvergenceScriptGenerator.cpp
    src/core/CddScriptGenerator.hpp
    src/core/CddScriptGenerator.cpp
    src/core/XasScriptGenerator.hpp
    src/core/HubbardScriptGenerator.hpp
    src/core/HubbardScriptGenerator.cpp
    src/core/XasScriptGenerator.cpp
    src/core/MonteCarlo.hpp
    src/core/MonteCarlo.cpp
    src/core/SqsGenerator.hpp
    src/core/SqsGenerator.cpp
    src/core/IceBuilder.hpp
    src/core/IceBuilder.cpp
    src/core/SolvationBuilder.hpp
    src/core/SolvationBuilder.cpp
    src/core/DislocationBuilder.hpp
    src/core/DislocationBuilder.cpp
    src/core/SolidInterfaceBuilder.hpp
    src/core/SolidInterfaceBuilder.cpp
    src/core/PolymerBuilder.hpp
    src/core/PolymerBuilder.cpp
    src/core/Vacf.hpp
    src/core/Vacf.cpp

    # python_bridge — embedded CPython + ASE interop
    src/python_bridge/PythonEngine.hpp
    src/python_bridge/PythonEngine.cpp
    src/python_bridge/AseBridge.hpp
    src/python_bridge/AseBridge.cpp
    src/python_bridge/PyError.hpp
    src/python_bridge/AnimationExporter.hpp
    src/python_bridge/AnimationExporter.cpp
    src/python_bridge/AlembicExporter.hpp
    src/python_bridge/AlembicExporter.cpp
    src/python_bridge/MaterialsProject.hpp
    src/python_bridge/MaterialsProject.cpp
    src/python_bridge/C2db.hpp
    src/python_bridge/C2db.cpp
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
    src/render/ShaderProfile.hpp
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
    src/gui/MultiSeriesPlotWidget.hpp
    src/gui/MultiSeriesPlotWidget.cpp
    src/gui/ProjectSerializer.hpp
    src/gui/ProjectSerializer.cpp
    src/gui/ScriptStaging.hpp
    src/gui/ScriptStaging.cpp
    src/gui/FileOpenRouting.hpp
    src/gui/GuiUtils.hpp
    src/gui/GuiUtils.cpp
    src/gui/CitationCatalog.hpp
    src/gui/CitationCatalog.cpp
    src/gui/BibtexViewerDialog.hpp
    src/gui/BibtexViewerDialog.cpp
    src/gui/CitationsPanel.hpp
    src/gui/CitationsPanel.cpp
    src/gui/RepresentationPanel.hpp
    src/gui/RepresentationPanel.cpp
    src/gui/CellAxesTabs.hpp
    src/gui/CellAxesTabs.cpp
    src/gui/FloorPanel.hpp
    src/gui/FloorPanel.cpp
    src/gui/NeighborCellsDialog.hpp
    src/gui/NeighborCellsDialog.cpp
    src/gui/TwoDBandsWizard.hpp
    src/gui/TwoDBandsWizard.cpp
    src/gui/TwoDBandsWindow.hpp
    src/gui/TwoDBandsWindow.cpp
    src/gui/DefectWizard.hpp
    src/gui/DefectWizard.cpp
    src/gui/Defect2dWizard.hpp
    src/gui/Defect2dWizard.cpp
    src/gui/DefectDiagramWindow.hpp
    src/gui/DefectDiagramWindow.cpp
    src/gui/FermiSurfaceDialog.hpp
    src/gui/FermiSurfaceDialog.cpp
    src/gui/FermiSurfaceStyle.hpp
    src/gui/FermiSurfaceStyle.cpp
    src/gui/FermiSurfaceWindow.hpp
    src/gui/FermiSurfaceWindow.cpp
    src/gui/TopologyDialog.hpp
    src/gui/TopologyDialog.cpp
    src/gui/TopologyWindow.hpp
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
    src/gui/PhaseColorsDialog.hpp
    src/gui/PhaseColorsDialog.cpp
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
    src/gui/PlotPalette.hpp
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
    src/gui/LiquidInterfaceWizard.hpp
    src/gui/LiquidInterfaceWizard.cpp
    src/gui/DislocationWizard.hpp
    src/gui/DislocationWizard.cpp
    src/gui/SolidInterfaceWizard.hpp
    src/gui/SolidInterfaceWizard.cpp
    src/gui/MacromoleculeWizard.hpp
    src/gui/MacromoleculeWizard.cpp
    src/gui/PhononBuilderDialog.hpp
    src/gui/PhononBuilderDialog.cpp
    src/gui/BondEditorDialog.hpp
    src/gui/BondEditorDialog.cpp
    src/gui/PeriodicTableDialog.hpp
    src/gui/PeriodicTableDialog.cpp
    src/gui/MoleculeCanvas.hpp
    src/gui/MoleculeCanvas.cpp
    src/gui/MolecularDesignDialog.hpp
    src/gui/MolecularDesignDialog.cpp
    src/gui/SlabWizard.hpp
    src/gui/SlabWizard.cpp
    src/gui/PreferencesDialog.hpp
    src/gui/PreferencesDialog.cpp
    src/gui/ShortcutRegistry.hpp
    src/gui/ShortcutRegistry.cpp
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
    src/gui/CalphadDialog.hpp
    src/gui/CalphadDialog.cpp
    src/gui/PhaseDiagramWindow.hpp
    src/gui/PhaseDiagramWindow.cpp
    src/gui/TdbGeneratorDialog.hpp
    src/gui/TdbGeneratorDialog.cpp
    src/gui/ClusterPreset.hpp
    src/gui/ClusterPreset.cpp
    src/gui/HpcPanel.hpp
    src/gui/HpcPanel.cpp
    src/gui/SqsDialog.hpp
    src/gui/SqsDialog.cpp
    src/gui/WarrenCowleyDialog.hpp
    src/gui/WarrenCowleyDialog.cpp
    src/gui/LocalEntropyDialog.hpp
    src/gui/LocalEntropyDialog.cpp
    src/gui/RingPercolationDialog.hpp
    src/gui/RingPercolationDialog.cpp
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
    src/gui/TernaryClusterHullWindow.hpp
    src/gui/TernaryClusterHullWindow.cpp
    src/gui/EgqcaPlotWidget.hpp
    src/gui/EgqcaPlotWidget.cpp
    src/gui/EgqcaWindow.hpp
    src/gui/EgqcaWindow.cpp
    src/gui/DsimResultsWindow.hpp
    src/gui/DsimResultsWindow.cpp
    src/gui/DsimPlotStyleDialog.hpp
    src/gui/DsimPlotStyleDialog.cpp
    src/gui/DsimTernaryPlotWidget.hpp
    src/gui/DsimTernaryPlotWidget.cpp
    src/gui/EffectiveBandsWindow.hpp
    src/gui/EffectiveBandsWindow.cpp
    src/gui/SystemStatusBar.hpp
    src/gui/SystemStatusBar.cpp
    src/gui/CondaEnvs.hpp
    src/gui/CondaEnvs.cpp
    src/gui/MaceTrainerDialog.hpp
    src/gui/MaceTrainerDialog.cpp
    src/gui/PythonPackagePreflight.hpp
    src/gui/PythonPackagePreflight.cpp
    src/gui/VaspPotcarPreflight.hpp
    src/gui/VaspPotcarPreflight.cpp
    src/gui/GpawMpiPreflight.hpp
    src/gui/GpawMpiPreflight.cpp
    src/gui/GpawEigenvaluePeek.hpp
    src/gui/GpawEigenvaluePeek.cpp
    src/gui/EnergyWindowWidget.hpp
    src/gui/EnergyWindowWidget.cpp
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
    src/gui/IsovalueHistogramWidget.hpp
    src/gui/IsovalueHistogramWidget.cpp
    src/gui/OverlayModel.hpp
    src/gui/OverlayModel.cpp
    src/gui/OverlayEditDialog.hpp
    src/gui/OverlayEditDialog.cpp
    src/gui/OverlayPanel.hpp
    src/gui/OverlayPanel.cpp
    src/gui/VacfDialog.hpp
    src/gui/VacfDialog.cpp
    src/gui/ExtendedEngineGroups.hpp
    src/gui/ExtendedEngineGroups.cpp
    src/gui/SimulationWizardBase.hpp
    src/gui/GpawElectronicWizard.hpp
    src/gui/SimulationWizardBase.cpp
    src/gui/GpawElectronicRows.hpp
    src/gui/GpawElectronicRows.cpp
    src/gui/MolecularDynamicsWizard.hpp
    src/gui/MolecularDynamicsWizard.cpp
    src/gui/ThermodynamicIntegrationWizard.hpp
    src/gui/ThermodynamicIntegrationWizard.cpp
    src/gui/ThermodynamicIntegrationResults.hpp
    src/gui/ThermodynamicIntegrationResults.cpp
    src/gui/TiIntegrandPlot.hpp
    src/gui/TiIntegrandPlot.cpp
    src/gui/SinglePointWizard.hpp
    src/gui/SinglePointWizard.cpp
    src/gui/CutoffConvergenceWizard.hpp
    src/gui/CutoffConvergenceWizard.cpp
    src/gui/KpointsConvergenceWizard.hpp
    src/gui/KpointsConvergenceWizard.cpp
    src/gui/ConvergenceResultsWindow.hpp
    src/gui/ConvergenceResultsWindow.cpp
    src/gui/OrchestrationWindow.hpp
    src/gui/OrchestrationWindow.cpp
    src/gui/DatabaseImportDialog.cpp
    src/gui/OrchestrationDocument.cpp
    src/gui/OrchestrationTransforms.cpp
    src/gui/OrchestrationProvenance.cpp
    src/gui/RandomNoiseWizard.hpp
    src/gui/RandomNoiseWizard.cpp
    src/gui/HistogramPlotWidget.hpp
    src/gui/HistogramPlotWidget.cpp
    src/gui/RandomNoiseViewer.hpp
    src/gui/RandomNoiseViewer.cpp
    src/gui/CddWizard.hpp
    src/gui/CddWizard.cpp
    src/gui/XasWizard.hpp
    src/gui/HubbardUWizard.hpp
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
    src/gui/NonlinearOpticsWizard.hpp
    src/gui/NonlinearOpticsWizard.cpp
    src/gui/GwWizard.hpp
    src/gui/GwWizard.cpp
    src/gui/WannierWizard.hpp
    src/gui/WannierWizard.cpp
    src/gui/LdosWizard.hpp
    src/gui/LdosWizard.cpp
    src/gui/EnergyDiagramWizard.hpp
    src/gui/EnergyDiagramWizard.cpp
    src/gui/EnergyDiagramStyleDialog.hpp
    src/gui/EnergyDiagramStyleDialog.cpp
    src/gui/EnergyDiagramViewer.hpp
    src/gui/EnergyDiagramViewer.cpp
    src/gui/WavefunctionsWizard.hpp
    src/gui/WavefunctionsWizard.cpp
    src/gui/WavefunctionsResultsViewer.hpp
    src/gui/WavefunctionsResultsViewer.cpp
    src/gui/OpticsResultsWindow.hpp
    src/gui/OpticsResultsWindow.cpp
    src/gui/NonlinearOpticsResultsWindow.hpp
    src/gui/NonlinearOpticsResultsWindow.cpp
    src/gui/GwResultsWindow.hpp
    src/gui/GwResultsWindow.cpp
    src/gui/WorkfunctionWizard.hpp
    src/gui/WorkfunctionWizard.cpp
    src/gui/WorkfunctionWindow.hpp
    src/gui/WorkfunctionWindow.cpp
    src/gui/HubbardParametersDialog.hpp
    src/gui/HubbardParametersDialog.cpp
    src/gui/GeometryConstraintsDialog.hpp
    src/gui/GeometryConstraintsDialog.cpp
    src/gui/BornChargesWizard.hpp
    src/gui/BornChargesWizard.cpp
    src/gui/BornChargesViewer.hpp
    src/gui/BornChargesViewer.cpp
    src/gui/PiezoelectricWizard.hpp
    src/gui/PiezoelectricWizard.cpp
    src/gui/PiezoelectricViewer.hpp
    src/gui/PiezoelectricViewer.cpp
    src/gui/ElasticWizard.hpp
    src/gui/ElasticWizard.cpp
    src/gui/ElasticViewer.hpp
    src/gui/ElasticViewer.cpp
    src/gui/RamanIrWizard.hpp
    src/gui/RamanIrWizard.cpp
    src/gui/RamanIrViewer.hpp
    src/gui/RamanIrViewer.cpp
    src/gui/PointOfViewDialog.hpp
    src/gui/PointOfViewDialog.cpp
    src/gui/CastSetupDialog.hpp
    src/gui/CastSetupDialog.cpp
    src/gui/CastColorsDialog.hpp
    src/gui/CastColorsDialog.cpp
    src/gui/AddAdsorbateDialog.hpp
    src/gui/AddAdsorbateDialog.cpp
    src/gui/GpuTelemetry.hpp
    src/gui/GpuTelemetry.cpp
    src/gui/GrainCasts.hpp
    src/gui/GrainCasts.cpp
    src/gui/GrapheneOxideWizard.hpp
    src/gui/GrapheneOxideWizard.cpp
    src/gui/GrapheneOxideMdmcWizard.hpp
    src/gui/GrapheneOxideMdmcWizard.cpp
    src/gui/GrapheneOxideGroupAnalysisDialog.hpp
    src/gui/GrapheneOxideGroupAnalysisDialog.cpp
    src/gui/GrapheneOxidePairCorrelationDialog.hpp
    src/gui/GrapheneOxidePairCorrelationDialog.cpp
    src/gui/WorkflowReportDialog.hpp
    src/gui/WorkflowReportDialog.cpp
    src/gui/OpticsPlotStyleDialog.hpp
    src/gui/OpticsPlotStyleDialog.cpp
    src/gui/EffectiveBandsWizard.hpp
    src/gui/EffectiveBandsWizard.cpp
    src/gui/CellRelaxationControls.hpp
    src/gui/CellRelaxationControls.cpp
    src/gui/ForceConvergenceControl.hpp
    src/gui/ForceConvergenceControl.cpp
    src/gui/GeometryOptimizationWizard.hpp
    src/gui/GeometryOptimizationWizard.cpp
    src/gui/PhononWizard.hpp
    src/gui/PhononWizard.cpp
    src/gui/ClusterExpansionDialog.hpp
    src/gui/EciFitDialog.hpp
    src/gui/EciFitDialog.cpp
    src/gui/BerryPhaseDialog.cpp
    src/gui/BseDialog.hpp
    src/gui/BseDialog.cpp
    src/gui/BoltzmannTransportDialog.cpp
    src/gui/CurvatureMapWidget.cpp
    src/gui/KkrCpaDialog.cpp
    src/gui/WannierModelSource.cpp
    src/gui/WannierRunLoader.cpp
    src/gui/SpectralPlotStyleDialog.cpp
    src/gui/CrpaDialog.cpp
    src/gui/CvmComparisonWindow.hpp
    src/gui/CvmComparisonWindow.cpp
    src/gui/ClusterExpansionDialog.cpp
    src/gui/ClusterExpansionWizard.hpp
    src/gui/ClusterExpansionWizard.cpp
    src/gui/DsimWizard.hpp
    src/gui/DsimWizard.cpp
    src/gui/ConvexHullPlotWidget.hpp
    src/gui/ConvexHullPlotWidget.cpp
    src/gui/TernaryClusterHullWidget.hpp
    src/gui/TernaryClusterHullWidget.cpp
    src/gui/SpectralHeatmapWidget.hpp
    src/gui/SpectralHeatmapWidget.cpp
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
        assets/shaders/floor.vert
        assets/shaders/floor.frag
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
        assets/acknowledgements/inct_materials_informatics.png
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
        assets/icons/align-item-vertical-center-line.svg
        assets/icons/anticlockwise-2-line.svg
        assets/icons/arrow-go-back-line.svg
        assets/icons/arrow-go-forward-line.svg
        assets/icons/atom-line.svg
        assets/icons/atom-label-line.svg
        assets/icons/box-1-fill.svg
        assets/icons/box-3-line.svg
        assets/icons/brush-fill.svg
        assets/icons/camera-lens-line.svg
        assets/icons/checkbox-circle-line.svg
        assets/icons/close-circle-line.svg
        assets/icons/stop-circle-fill.svg
        assets/icons/clapperboard-fill.svg
        assets/icons/close-line.svg
        assets/icons/code-box-fill.svg
        assets/icons/color-filter-fill.svg
        assets/icons/computer-line.svg
        assets/icons/cpu-line.svg
        assets/icons/cursor-line.svg
        assets/icons/delete.svg
        assets/icons/delete-bin-line.svg
        assets/icons/drag-move-2-line.svg
        assets/icons/edit-box-fill.svg
        assets/icons/edit-fill.svg
        assets/icons/eraser-line.svg
        assets/icons/expand-height-line.svg
        assets/icons/eye-fill.svg
        assets/icons/film-fill.svg
        assets/icons/file-copy-line.svg
        assets/icons/focus-3-line.svg
        assets/icons/folder-open-line.svg
        assets/icons/folder-received-line.svg
        assets/icons/fullscreen-line.svg
        assets/icons/grid-line.svg
        assets/icons/group-fill.svg
        assets/icons/hard-drive-2-line.svg
        assets/icons/hashtag.svg
        assets/icons/heading.svg
        assets/icons/indeterminate-circle-fill.svg
        assets/icons/information-line.svg
        assets/icons/loader-4-line.svg
        assets/icons/magic-line.svg
        assets/icons/node-tree.svg
        assets/icons/map-2-fill.svg
        assets/icons/paint-fill.svg
        assets/icons/palette-line.svg
        assets/icons/play-circle-fill.svg
        assets/icons/price-tag-fill.svg
        assets/icons/save-line.svg
        assets/icons/gradient.svg
        assets/icons/stairs-fill.svg
        assets/icons/hexagon-fill.svg
        # Molecular Design's tool palette. RemixIcon ships no chemistry set,
        # so the bond-order, stereo, chain and ring glyphs are drawn for this
        # application; they follow the RemixIcon `-line` naming convention so
        # the icon-registry test sees them like any other.
        assets/icons/bond-single-line.svg
        assets/icons/bond-double-line.svg
        assets/icons/bond-triple-line.svg
        assets/icons/bond-wedge-line.svg
        assets/icons/bond-hash-line.svg
        assets/icons/chain-zigzag-line.svg
        assets/icons/text-caption-line.svg
        assets/icons/molecule-benzene-line.svg
        assets/icons/ring-3-line.svg
        assets/icons/ring-4-line.svg
        assets/icons/ring-5-line.svg
        assets/icons/ring-6-line.svg
        assets/icons/ring-7-line.svg
        assets/icons/ring-8-line.svg
        assets/icons/ring-benzene-line.svg
        assets/icons/ring-cyclopentadiene-line.svg
        assets/icons/ring-naphthalene-line.svg
        assets/icons/clipboard-line.svg
        assets/icons/home-2-fill.svg
        assets/icons/home-office-fill.svg
        assets/icons/community-fill.svg
        assets/icons/ram-line.svg
        assets/icons/restart-line.svg
        assets/icons/ruler-2-line.svg
        assets/icons/scissors-cut-line.svg
        assets/icons/shapes-fill.svg
        assets/icons/share-fill.svg
        assets/icons/slideshow-3-fill.svg
        assets/icons/split-cells-horizontal.svg
        assets/icons/stack-line.svg
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
        MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
        MACOSX_BUNDLE_INFO_STRING "Calango ${PROJECT_VERSION}"
        MACOSX_BUNDLE_COPYRIGHT "© 2026 Leandro Seixas Rocha"
        # Custom template (not CMake's stock one) so the bundle declares
        # CFBundleDocumentTypes/UTExportedTypeDeclarations for .calproj and
        # .extxyz — that is what lets Finder open documents with Calango.
        # The opened path arrives as a QFileOpenEvent, handled in main.cpp.
        MACOSX_BUNDLE_INFO_PLIST
            "${CMAKE_CURRENT_SOURCE_DIR}/packaging/macos/Info.plist.in")

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
        examples/graphene_supercell.vasp
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

calango_hdf5_link(calango)
target_link_libraries(calango PRIVATE
    Qt6::Widgets
    Qt6::OpenGLWidgets
    Qt6::Concurrent
    Qt6::Svg
    pybind11::embed
)
if(APPLE)
    target_link_libraries(calango PRIVATE "-framework OpenGL")
    # GPU telemetry reads the accelerator driver's own counters out of the
    # IORegistry (gui/GpuTelemetry.cpp). Metal has no equivalent: it describes
    # and schedules, it does not meter.
    target_link_libraries(calango PRIVATE "-framework IOKit"
                                          "-framework CoreFoundation")
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
# The native counterpart to core::analyzeRingPercolationTrajectory() reachable
# from outside the GUI: a test harness verifying a real MDMC trajectory
# (tests/graphene_oxide_percolation_mdmc_test.py), or a user who already has a
# trajectory file and wants the ring/percolation numbers for it without
# opening the app. Reads via ase.io (any format ASE knows), hence embeds
# Python. (Formerly paired with calango-elph-analyze, a headless
# electron-phonon post-processor for cluster-side runs too big to bring
# home — removed along with the electron-phonon coupling feature.)
add_executable(calango-ring-percolation-analyze
    tools/calango_ring_percolation_analyze.cpp
    src/core/GrapheneOxidePercolation.cpp
    src/core/GrapheneOxideBuilder.cpp
    src/core/GrapheneOxideSites.cpp
    src/core/Structure.cpp
    src/core/UnitCell.cpp
    src/core/Element.cpp
    src/core/KPath.cpp
    src/python_bridge/PythonEngine.cpp
    src/python_bridge/AseBridge.cpp
    src/core/PdbxFile.cpp
)
target_include_directories(calango-ring-percolation-analyze PRIVATE src)
target_compile_definitions(calango-ring-percolation-analyze PRIVATE
    CALANGO_DEFAULT_PYTHON="${CALANGO_DEFAULT_PYTHON_PATH}")
target_link_libraries(calango-ring-percolation-analyze PRIVATE pybind11::embed)

option(CALANGO_BUILD_TESTS "Build the integration test executables" OFF)
if(CALANGO_BUILD_TESTS)
    enable_testing()
    # QSignalSpy (calango_dialog_test's viewport Tab-cycling checks) is the
    # only reason this module is needed anywhere — scoped to the test build
    # rather than added to the top-level find_package() so a plain app build
    # never requires Qt Test.
    find_package(Qt6 6.4 REQUIRED COMPONENTS Test)
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
    # Configurational entropy by CVM. The pair approximation is EXACT on a
    # 1D chain, and the 1D Ising chain has a closed-form transfer-matrix
    # solution — so this is checked against an identity, not a tolerance.
    add_executable(calango_cvm_test
        tests/ClusterVariationTest.cpp
        src/core/ClusterVariation.cpp
    )
    target_include_directories(calango_cvm_test PRIVATE src)
    add_test(NAME cluster_variation COMMAND calango_cvm_test)

    # Dilute Solution Interpolation (DSI) model: the M-matrix fixture is
    # oncapintada's own hand-computed test_subregular_model.py values, and
    # every DeltaH_mix(x) check is a closed-form identity Eq. 7's
    # x(1-x)^2/x^2(1-x) construction guarantees (endpoints exactly zero,
    # tangent slopes exactly the M parameters) — see DsimTest.cpp.
    add_executable(calango_dsim_test
        tests/DsimTest.cpp
        src/core/Dsim.cpp
    )
    target_include_directories(calango_dsim_test PRIVATE src)
    add_test(NAME dsim COMMAND calango_dsim_test)

    # Extended Generalized Quasichemical Approximation (Task 1): the ideal-
    # limit reduction, probability normalization, an independent brute-force
    # global-minimum oracle for a small worked example, and the vibrational
    # (the "Extended" part) wiring — see EgqcaTest.cpp for why this substitutes
    # for a literal paper-figure reproduction (the paper's own worked examples
    # need Supplementary Table data this repository does not have).
    add_executable(calango_egqca_test
        tests/EgqcaTest.cpp
        src/core/Egqca.cpp
        src/core/PhononThermodynamics.cpp
    )
    target_include_directories(calango_egqca_test PRIVATE src)
    add_test(NAME egqca COMMAND calango_egqca_test)

    # Single-site CPA for random alloys. Anchored on the one bare band whose
    # Hilbert transform is closed-form (semi-elliptic), on the two limits where
    # the CPA is exact (one component, equal levels), and on the split-band
    # weights, which are fixed by the concentrations alone.
    add_executable(calango_cpa_test
        tests/CpaSolverTest.cpp
        src/core/CpaSolver.cpp
    )
    target_include_directories(calango_cpa_test PRIVATE src)
    add_test(NAME cpa_solver COMMAND calango_cpa_test)

    # Native cRPA in a Wannier basis. Anchored on the two analytic limits of
    # the Gaussian Coulomb integral and on the three cases where the screened
    # interaction must collapse EXACTLY onto the bare one — which is what
    # actually tests the constraint bookkeeping.
    add_executable(calango_crpa_test
        tests/CrpaSolverTest.cpp
        src/core/CrpaSolver.cpp
        src/core/WannierHamiltonian.cpp
    )
    target_include_directories(calango_crpa_test PRIVATE src)
    add_test(NAME crpa_solver COMMAND calango_crpa_test)

    # Boltzmann transport in the constant relaxation time approximation.
    # Anchored on Wiedemann-Franz (an external number the code knows nothing
    # about), the Seebeck sign convention, and the exact tau-scaling
    # identities.
    add_executable(calango_boltzmann_test
        tests/BoltzmannTransportTest.cpp
        src/core/BoltzmannTransport.cpp
        src/core/WannierHamiltonian.cpp
    )
    target_include_directories(calango_boltzmann_test PRIVATE src)
    add_test(NAME boltzmann_transport COMMAND calango_boltzmann_test)

    # Berry phase, curvature and Chern numbers. Anchored on quantisation: a
    # Chern number is an integer, so a wrong sign, a stray 2*pi or a gauge leak
    # cannot land on the right answer by accident.
    add_executable(calango_berry_test
        tests/BerryPhaseTest.cpp
        src/core/BerryPhase.cpp
        src/core/WannierHamiltonian.cpp
    )
    target_include_directories(calango_berry_test PRIVATE src)
    add_test(NAME berry_phase COMMAND calango_berry_test)

    # Bethe-Salpeter excitons: the hydrogenic/Wannier-Mott model test is the
    # crucial one -- a synthetic two-band parabolic model with a statically
    # screened Coulomb kernel must reproduce the analytic 3D Rydberg series
    # E_b(n) = -R*/n^2 as the k-mesh densifies, plus the 2D non-hydrogenic
    # (Rytova-Keldysh) analogue, kernel Hermiticity, and the singlet/triplet
    # exchange switch. Slower than most `calc` tests (a real, if small,
    # dense/Lanczos diagonalization runs several times over refining
    # k-meshes) -- TIMEOUT set generously below rather than excluded, since
    # it still completes in well under a minute.
    add_executable(calango_bse_test
        tests/BseSolverTest.cpp
        src/core/BseSolver.cpp
        src/core/WannierHamiltonian.cpp
    )
    target_include_directories(calango_bse_test PRIVATE src)
    add_test(NAME bse_solver COMMAND calango_bse_test)
    set_tests_properties(bse_solver PROPERTIES TIMEOUT 600)

    # Piezoelectric module, part 1: strain generation. Voigt -> deformation
    # gradient, and applying one to a cell — checked on a non-diagonal
    # (triclinic) cell specifically, since a left/right matrix-multiply bug
    # is invisible on a cubic one.
    add_executable(calango_strain_voigt_test
        tests/StrainVoigtTest.cpp
    )
    target_include_directories(calango_strain_voigt_test PRIVATE src)
    add_test(NAME strain_voigt COMMAND calango_strain_voigt_test)

    # Piezoelectric module, part 2: the assembly pipeline (branch fixing,
    # the linear fit, the proper/improper correction, point-group
    # symmetrization) on synthetic P(eps) data. Real DFT is exercised only
    # through generated_ase_scripts's Piezoelectric section below, which
    # asserts on the script text since no cheap Berry-phase-under-strain
    # oracle exists to run for real.
    add_executable(calango_piezoelectric_tensor_test
        tests/PiezoelectricTensorTest.cpp
        src/core/PiezoelectricTensor.cpp
    )
    target_include_directories(calango_piezoelectric_tensor_test PRIVATE src)
    add_test(NAME piezoelectric_tensor COMMAND calango_piezoelectric_tensor_test)

    # Elastic properties module: Voigt assembly (quadratic/cross-term
    # curvature fits), the general and crystal-class-specific Born stability
    # criteria (against literature Cu/Si constants), Voigt-Reuss-Hill
    # moduli, 2D (layer modulus, in-plane Young's/Poisson) quantities, and
    # point-group symmetrization on synthetic tensors. Real DFT/EMT is
    # exercised only through generated_ase_scripts's Elastic section below
    # plus the manual end-to-end verification run described in the module's
    # docs, since there is no cheap closed-form finite-strain oracle to run
    # as a ctest.
    add_executable(calango_elastic_tensor_test
        tests/ElasticTensorTest.cpp
        src/core/ElasticTensor.cpp
        src/core/PiezoelectricTensor.cpp
    )
    target_include_directories(calango_elastic_tensor_test PRIVATE src)
    add_test(NAME elastic_tensor COMMAND calango_elastic_tensor_test)

    # Regression for the proc_26 failure: nwannier > nbands. Asserts the
    # generated script now pre-flights it, and that the real captured
    # traceback is turned into a line worth showing.
    add_executable(calango_wannier_preflight_test
        tests/WannierPreflightTest.cpp
        src/core/WannierScriptGenerator.cpp
        src/core/JobFailureReason.cpp
        src/core/AseScriptGenerator.cpp
        src/core/EngineCalculatorBlocks.cpp
    )
    # Isosurface mesh integrity: winding must agree with the gradient
    # normals. Regression for the reversed complementary marching-tetrahedra
    # cases that showed as holes under the glossy material.
    add_executable(calango_isosurface_test
        tests/IsosurfaceMeshTest.cpp
        src/core/MarchingCubes.cpp
        src/core/IsosurfaceContinuation.cpp
        src/core/VolumetricData.cpp
        src/core/Element.cpp
    )
    target_include_directories(calango_isosurface_test PRIVATE src)
    calango_hdf5_link(calango_isosurface_test)
    add_test(NAME isosurface_mesh COMMAND calango_isosurface_test)

    # Wigner-Seitz-cell clipping of an isosurface mesh. Regression for the Cu
    # Fermi-surface "holes" bug: extraction runs on the reciprocal cell's own
    # parallelepiped, a different shape from the Wigner-Seitz cell it is
    # displayed in, so a single un-replicated clip silently dropped the
    # corners the zone reaches past the box's own faces to reach.
    add_executable(calango_brillouin_zone_clip_test
        tests/BrillouinZoneClipTest.cpp
        src/core/BrillouinZone.cpp
        src/core/MarchingCubes.cpp
        src/core/VolumetricData.cpp
        src/core/UnitCell.cpp
        src/core/Element.cpp
    )
    target_include_directories(calango_brillouin_zone_clip_test PRIVATE src)
    calango_hdf5_link(calango_brillouin_zone_clip_test)
    add_test(NAME brillouin_zone_clip COMMAND calango_brillouin_zone_clip_test)

    # What a Wannier run can learn about the ground state it inherits: bands,
    # k-points, and whether the Brillouin zone was folded. The third is a
    # pre-condition, and "undetermined" has to stay distinguishable from "on".
    add_executable(calango_baseline_summary_test
        tests/BaselineSummaryTest.cpp
        src/core/BaselineSummary.cpp
    )
    target_include_directories(calango_baseline_summary_test PRIVATE src)
    add_test(NAME baseline_summary COMMAND calango_baseline_summary_test)

    target_include_directories(calango_wannier_preflight_test PRIVATE src)
    target_compile_definitions(calango_wannier_preflight_test PRIVATE
        CALANGO_VERSION="${PROJECT_VERSION}")
    add_test(NAME wannier_preflight COMMAND calango_wannier_preflight_test)

    # Validation harness against the local VASP cRPA. Fast by default: it
    # checks the generated INCAR/POSCAR, round-trips the OUTCAR/UIJKL parsers
    # against fixtures in VASP's own output format, and runs the native solver
    # on an SrVO3 model. CALANGO_VASP_CRPA=1 additionally launches VASP, which
    # is hours of compute and therefore never automatic.
    add_executable(calango_crpa_vasp_test
        tests/CrpaVaspValidationTest.cpp
        src/core/CrpaSolver.cpp
        src/core/WannierHamiltonian.cpp
    )
    target_include_directories(calango_crpa_vasp_test PRIVATE src)
    add_test(NAME crpa_vasp_validation COMMAND calango_crpa_vasp_test)

    # Four-sublattice CVM: FCC order-disorder transitions. Links the
    # homogeneous solver too, because one of its checks is that the disordered
    # branch of the sublattice-resolved solver reproduces the independently
    # written homogeneous one to 1e-11.
    add_executable(calango_sublattice_cvm_test
        tests/SublatticeClusterVariationTest.cpp
        src/core/SublatticeClusterVariation.cpp
        src/core/ClusterVariation.cpp
    )
    target_include_directories(calango_sublattice_cvm_test PRIVATE src)
    add_test(NAME sublattice_cluster_variation
             COMMAND calango_sublattice_cvm_test)

    add_executable(calango_cluster_expansion_test
        tests/ClusterExpansionTest.cpp
        src/core/Structure.cpp
        src/core/UnitCell.cpp
        src/core/Element.cpp
        src/core/ClusterExpansion.cpp
        # Ternary fit round trip (Task 6): ClusterExpansionFit.cpp has no
        # dependencies of its own (calango_cluster_expansion_fit_test links
        # only it), so pulling it in here to close the enumeration->fit loop
        # costs nothing beyond itself.
        src/core/ClusterExpansionFit.cpp
    )
    target_include_directories(calango_cluster_expansion_test PRIVATE src)
    add_test(NAME cluster_expansion COMMAND calango_cluster_expansion_test)

    # The ECI fit is pure numerics: no Structure, no Qt, two objects.
    add_executable(calango_cluster_expansion_fit_test
        tests/ClusterExpansionFitTest.cpp
        src/core/ClusterExpansionFit.cpp
    )
    target_include_directories(calango_cluster_expansion_fit_test PRIVATE src)
    add_test(NAME cluster_expansion_fit
             COMMAND calango_cluster_expansion_fit_test)

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

    # C2DB (Computational 2D Materials Database): query-string construction
    # and HTML result-table parsing are pure/offline and always run; the one
    # live-network case is opt-in (CALANGO_TEST_C2DB_NETWORK=1), not
    # skip-by-default like materials_project_mp1434 above — C2DB has no
    # per-user API key to gate on, so an automated sweep must not hit DTU's
    # server unasked.
    add_executable(calango_c2db_test
        tests/C2dbTest.cpp
        src/core/Structure.cpp
        src/core/UnitCell.cpp
        src/core/Element.cpp
        src/python_bridge/PythonEngine.cpp
        src/python_bridge/AseBridge.cpp
        src/core/PdbxFile.cpp
        src/python_bridge/C2db.cpp
    )
    target_include_directories(calango_c2db_test PRIVATE src)
    target_compile_definitions(calango_c2db_test PRIVATE
        CALANGO_DEFAULT_PYTHON="${CALANGO_DEFAULT_PYTHON_PATH}")
    target_link_libraries(calango_c2db_test PRIVATE pybind11::embed)
    add_test(NAME c2db COMMAND calango_c2db_test)

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
    calango_hdf5_link(calango_raytrace_test)
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

    # Solid-liquid / solid-gas interface packing. Re-derives the region
    # geometry, the composition and the exclusion distances from the RETURNED
    # coordinates: a packer that quietly overlaps two molecules, drops an ion,
    # or leaves the cell the wrong length produces a structure that looks
    # entirely normal until a calculation explodes on it.
    add_executable(calango_solvation_test
        tests/SolvationBuilderTest.cpp
        src/core/SolvationBuilder.cpp
        src/core/Structure.cpp
        src/core/UnitCell.cpp
        src/core/Element.cpp
    )
    target_include_directories(calango_solvation_test PRIVATE src)
    add_test(NAME solvation_builder COMMAND calango_solvation_test)

    # Dislocations: closed-form Volterra fields plus the Stroh sextic solution
    # for an anisotropic medium. A sign error anywhere in the sextic produces a
    # smooth, plausible, completely wrong displacement field, so the
    # anisotropic solver is pinned against the isotropic closed form it must
    # reduce to, and every construction is checked by carrying a Burgers
    # circuit around the line.
    add_executable(calango_dislocation_test
        tests/DislocationBuilderTest.cpp
        src/core/DislocationBuilder.cpp
        src/core/Structure.cpp
        src/core/UnitCell.cpp
        src/core/Element.cpp
    )
    target_include_directories(calango_dislocation_test PRIVATE src)
    add_test(NAME dislocation_builder COMMAND calango_dislocation_test)

    # Solid interfaces: stacking faults, twins, bicrystals and Voronoi
    # polycrystals. Everything these get wrong looks fine in a viewer — a twin
    # that lost its boundary layer, a tessellation whose grains stop at the
    # cell face, a seam with two atoms 0.2 A apart — so the checks are
    # structural and re-derived from the returned coordinates.
    add_executable(calango_solid_interface_test
        tests/SolidInterfaceBuilderTest.cpp
        src/core/SolidInterfaceBuilder.cpp
        src/core/Structure.cpp
        src/core/UnitCell.cpp
        src/core/Element.cpp
        # The grain -> cast mapping is checked in the same test that builds the
        # polycrystal: "the tessellation is right" and "the viewport will show
        # it" are one claim, and splitting them across two binaries would let
        # either half pass while the pair is broken.
        src/gui/GrainCasts.cpp
    )
    target_include_directories(calango_solid_interface_test PRIVATE src)
    # Qt6::Gui for QColor only — no widgets, no OpenGL, so this stays a
    # headless test that needs no display.
    target_link_libraries(calango_solid_interface_test PRIVATE Qt6::Gui)
    add_test(NAME solid_interface_builder COMMAND calango_solid_interface_test)

    # Harmonic phonon thermodynamics: checked against the closed-form Einstein
    # oscillator and the limits that are laws (Third Law, Dulong-Petit).
    add_executable(calango_phonon_thermo_test
        tests/PhononThermodynamicsTest.cpp
        src/core/PhononThermodynamics.cpp
    )
    target_include_directories(calango_phonon_thermo_test PRIVATE src)
    add_test(NAME phonon_thermodynamics COMMAND calango_phonon_thermo_test)

    # Thermodynamic integration. Pinned against CLOSED FORMS throughout, never
    # against a previous run: the thermal wavelength against an independent SI
    # computation, the ideal gas against Sackur-Tetrode with Stirling's
    # residual asserted to two orders (that residual IS the ln N! term), the
    # Einstein crystal against 3NkT ln(beta hbar omega), Gauss-Legendre against
    # its 2n-1 exactness (and its inexactness at 2n), trapezoid/Simpson against
    # their O(h^2)/O(h^4) refinement laws, the integration itself against the
    # analytic dF of a harmonic -> harmonic transformation, the Lennard-Jones
    # second virial against direct quadrature of the Mayer function plus the
    # published Boyle temperature, and the autocorrelation time against an
    # AR(1) process. It also pins the behaviour that matters most: a dead
    # lambda window produces NO free energy.
    add_executable(calango_thermodynamic_integration_test
        tests/ThermodynamicIntegrationTest.cpp
        src/core/ThermodynamicIntegration.cpp
    )
    target_include_directories(calango_thermodynamic_integration_test
        PRIVATE src)
    add_test(NAME thermodynamic_integration
             COMMAND calango_thermodynamic_integration_test)

    # Vibrational normal modes: the physics behind the mode animation, which is
    # a picture and therefore unfalsifiable by eye. Pinned against the closed
    # forms it must obey - the reduced-mass harmonic oscillator, the acoustic
    # sum rule, orthonormality of the dynamical matrix's eigenbasis, and the
    # exp(i q.R) phase - never against a stored frame.
    add_executable(calango_vibrational_modes_test
        tests/VibrationalModesTest.cpp
        src/core/VibrationalModes.cpp
        src/core/Structure.cpp
        src/core/UnitCell.cpp
        src/core/Element.cpp
    )
    target_include_directories(calango_vibrational_modes_test PRIVATE src)
    add_test(NAME vibrational_modes COMMAND calango_vibrational_modes_test)

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
        # The Bond Editor's rules: an element/distance window is re-matched
        # against every frame of a trajectory, so a bond that dissociates
        # stops being drawn instead of being frozen in from frame 0.
        src/core/BondRules.cpp
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

    # Molecular Design: the sketch model's valence arithmetic and ring
    # templates, the SMILES parser/writer, the 2D layout and Tidy, and the
    # 2D -> 3D pipeline "Send to 3D Viewport" runs — with benzene as the
    # end-to-end proof, through both the ring template and SMILES.
    add_executable(calango_molecular_design_test
        tests/MolecularDesignTest.cpp
        src/core/MoleculeGraph.cpp
        src/core/Smiles.cpp
        src/core/MoleculeEmbed3d.cpp
        src/core/Structure.cpp
        src/core/UnitCell.cpp
        src/core/Element.cpp
    )
    target_include_directories(calango_molecular_design_test PRIVATE src)
    add_test(NAME molecular_design COMMAND calango_molecular_design_test)

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
            assets/shaders/floor.vert
            assets/shaders/floor.frag
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
            # Same reasoning for the ground plane: the floor program links
            # lazily and simply draws nothing when it is absent, which is
            # indistinguishable from the floor being switched off.
            assets/shaders/floor.vert
            assets/shaders/floor.frag
            # The filled unit cell is shaded through the lit surface program.
            # Without these the renderer silently falls back to the unlit path
            # and the test measures the fallback rather than the feature.
            assets/shaders/isosurface.vert
            assets/shaders/isosurface.frag)
    target_include_directories(calango_translucent_render_test PRIVATE src)
    target_link_libraries(calango_translucent_render_test
        PRIVATE Qt6::Gui Qt6::OpenGL)
    calango_hdf5_link(calango_translucent_render_test)
    add_test(NAME translucent_render COMMAND calango_translucent_render_test)

    # Dialog construction: a wizard whose constructor emitted a signal into a
    # slot reading widgets it had not created yet crashed the moment its menu
    # item was clicked. The application smoke test never opens dialogs and a
    # compile cannot see a signal-ordering hazard, so each dialog is built and
    # driven here. Offscreen, no display.
    qt_add_executable(calango_dialog_test
        tests/DialogConstructionTest.cpp
        src/gui/CvmComparisonWindow.cpp
        src/gui/EciFitDialog.cpp
        src/core/ClusterExpansionFit.cpp
        src/core/ClusterVariation.cpp
        src/core/SublatticeClusterVariation.cpp
        # The Processes dock: its walltime column is a live QTimer, which only
        # a running event loop can exercise.
        src/gui/ProcessManagerPanel.cpp
        # The CALPHAD dialog builds its controls from a loaded database, and
        # its two buttons open a phase diagram and a database generator that
        # are themselves built from data loaded at run time.
        src/gui/CalphadDialog.cpp
        src/gui/PhaseDiagramWindow.cpp
        src/gui/TdbGeneratorDialog.cpp
        src/core/CalphadModel.cpp
        src/core/ConvexHull.cpp
        src/core/PhaseDiagram.cpp
        src/core/PhononThermodynamics.cpp
        src/core/TdbDatabase.cpp
        src/core/TdbExpression.cpp
        src/core/TdbWriter.cpp
        # The HPC dock and the preset store its Save/Delete drive.
        src/gui/HpcPanel.cpp
        src/gui/ClusterPreset.cpp
        src/core/SchedulerScript.cpp
        src/remote/RemoteClient.cpp
        src/gui/DatabaseImportDialog.cpp
        src/python_bridge/MaterialsProject.cpp
        src/gui/FilmTimelineWidget.cpp
        src/gui/GrapheneOxideWizard.cpp
        src/gui/GrapheneOxideMdmcWizard.cpp
        src/core/GrapheneOxideMdmcScriptGenerator.cpp
        src/gui/WorkflowReportDialog.cpp
        src/core/WorkflowReport.cpp
        src/core/BandGap.cpp
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
        # And the MD wizard, for its Annealing mode: the schedule rows are
        # shown/hidden and the ensemble list is edited from a signal handler
        # the constructor triggers, and the generated script is the only place
        # a mode that was selected but not applied would ever show up.
        src/gui/MolecularDynamicsWizard.cpp
        # The thermodynamic-integration wizard: its reference/schedule/quadrature
        # rows are shown and hidden — and the quadrature combo is FORCED to match
        # the schedule — from a signal handler the constructor triggers, which is
        # exactly the construction-order hazard this test exists for. Its results
        # reader comes with it, so the assembly a run is judged by is built and
        # linked here rather than only in the application.
        src/gui/ThermodynamicIntegrationWizard.cpp
        src/gui/ThermodynamicIntegrationResults.cpp
        src/gui/TiIntegrandPlot.cpp
        src/core/ThermodynamicIntegration.cpp
        src/core/ThermodynamicIntegrationScriptGenerator.cpp
        src/gui/TwoDBandsWizard.cpp
        src/core/TwoDBandsScriptGenerator.cpp
        # The 2D Charged Defects wizard: two baseline selectors that have to
        # reach the script SEPARATELY, and an anisotropic dielectric profile
        # that has to survive as two numbers rather than collapsing to one.
        # Both failures produce a plausible script the generator's own
        # substring tests would pass.
        src/gui/Defect2dWizard.cpp
        src/core/Defect2dScriptGenerator.cpp
        src/core/DefectScriptGenerator.cpp
        # The Nonlinear Optics wizard: it hides a form row and re-validates the
        # typed component list from a signal its own constructor triggers, and
        # it is the only wizard whose generated script depends on GPAW ground-
        # state keywords the generator OVERRIDES rather than reads — so a
        # construction that skipped the override would show up here as a script
        # missing its symmetry line.
        src/gui/NonlinearOpticsWizard.cpp
        src/core/NonlinearOpticsScriptGenerator.cpp
        # Its results window, brought up to Optics's own appearance/Export
        # Image… standard — reuses OpticsPlotStyleDialog and SpectrumPlotWidget
        # wholesale rather than a parallel styling struct.
        src/gui/NonlinearOpticsResultsWindow.cpp
        # The Optics wizard, exercised in both its 3D and 2D modes.
        src/gui/OpticsWizard.cpp
        # The Wannier setup wizard: its baseline summary states a
        # pre-condition (full BZ k-set) that decides whether the run
        # can work at all, so the three verdicts are pinned.
        src/gui/WannierWizard.cpp
        src/core/BaselineSummary.cpp
        src/core/WannierScriptGenerator.cpp
        # The path from a completed Wannier run into the modules that consume
        # its H(R). Both consumers are constructed, so both come along.
        src/gui/WannierRunLoader.cpp
        src/gui/WannierModelSource.cpp
        src/gui/BoltzmannTransportDialog.cpp
        src/gui/CrpaDialog.cpp
        src/core/WannierHamiltonian.cpp
        src/core/BoltzmannTransport.cpp
        src/core/CrpaSolver.cpp
        src/core/OpticsScriptGenerator.cpp
        # And the Raman / IR wizard, which now swaps whole group boxes when the
        # engine changes — GPAW selects three inherited runs, VASP and Quantum
        # ESPRESSO converge their own ground state — from a signal its
        # constructor triggers.
        src/gui/RamanIrWizard.cpp
        src/core/RamanIrScriptGenerator.cpp
        src/gui/RandomNoiseWizard.cpp
        # Its results window: the JSON schema the generated script writes and
        # the viewer reads is a contract between two files that never see each
        # other, so the first GUI test to touch it pins the keys.
        src/gui/HistogramPlotWidget.cpp
        src/gui/RandomNoiseViewer.cpp
        src/gui/CddWizard.cpp
        src/gui/HubbardUWizard.cpp
        src/gui/XasWizard.cpp
        src/gui/XasResultsWindow.cpp
        src/gui/SpectrumPlotWidget.cpp
        src/core/HubbardScriptGenerator.cpp
        src/core/XasScriptGenerator.cpp
        src/gui/EditVolumetricRenderDialog.cpp
        src/gui/IsovalueHistogramWidget.cpp
        src/gui/VolumetricStyle.cpp
        # The isosurface "Lit surface" checkbox reads and writes the shader
        # profile registry. It queries no GL context here — ShaderRegistry
        # answers "unsupported is unknown, assume yes" with none current —
        # so the headless dialog test constructs it fine.
        src/render/ShaderProfile.cpp
        src/gui/CutoffConvergenceWizard.cpp
        src/core/ConvergenceSweepBlocks.cpp
        src/core/CutoffConvergenceScriptGenerator.cpp
        src/gui/KpointsConvergenceWizard.cpp
        src/core/KpointsConvergenceScriptGenerator.cpp
        src/core/CddScriptGenerator.cpp
        src/core/Noise.cpp
        src/gui/SimulationWizardBase.cpp
        src/gui/ExtendedEngineGroups.cpp
        src/core/EngineCalculatorBlocks.cpp
        src/gui/GpawElectronicRows.cpp
        src/gui/HubbardParametersDialog.cpp
        src/gui/CondaEnvs.cpp
        # The MACE trainer: its YAML has to name keys mace.tools.arg_parser
        # accepts and the reference-data keys the exported dataset actually
        # uses, and its launcher has to be valid Python. All three fail at run
        # time on someone else's cluster, hours in, so they are pinned here.
        src/gui/MaceTrainerDialog.cpp
        src/gui/PythonPackagePreflight.cpp
        src/gui/VaspPotcarPreflight.cpp
        src/gui/GpawMpiPreflight.cpp
        # LDOS: the baseline-eigenvalue peek (a real, synchronous GPAW
        # restart under python -c, same shape as the preflight check above)
        # and the draggable dual-handle energy-window widget it feeds.
        src/gui/GpawEigenvaluePeek.cpp
        src/gui/EnergyWindowWidget.cpp
        src/gui/LdosWizard.cpp
        src/core/LdosScriptGenerator.cpp
        # Energy Diagrams: the level-diagram + transitions viewer, its
        # appearance dialog, and the wizard's own pre-flight (non-periodic /
        # Gamma-only baseline).
        src/gui/EnergyDiagramViewer.cpp
        src/gui/EnergyDiagramStyleDialog.cpp
        src/gui/EnergyDiagramWizard.cpp
        src/core/EnergyDiagramScriptGenerator.cpp
        # Wavefunctions: the shared wavefunction-access layer (above, in
        # AseScriptGenerator) feeding both LDOS and this; the state-picker
        # wizard and its own script generator.
        src/gui/WavefunctionsWizard.cpp
        src/gui/WavefunctionsResultsViewer.cpp
        src/core/WavefunctionScriptGenerator.cpp
        # The Magnetic Space Group dialog: it determines the group inside its
        # own constructor and writes the answer back into widgets that
        # constructor created, which is precisely the hazard this test exists
        # for. Brings AseBridge with it (the determination goes through
        # ase.Atoms).
        src/gui/MagneticSpaceGroupDialog.cpp
        src/python_bridge/MagneticSpaceGroup.cpp
        # The Liquid / Gas Interface wizard: its stage-2 page seeds a table row
        # inside its own constructor and both pages recompute a live summary on
        # every control change — the construction-order shape this test exists
        # for. Qt-only, so it costs one extra core translation unit.
        src/gui/LiquidInterfaceWizard.cpp
        src/core/SolvationBuilder.cpp
        # The two defect wizards: both hide and show form rows from a signal
        # their own constructor triggers, and both run the real builder to
        # produce their live summary — so a page that reads a widget it has not
        # created yet, or a summary that disagrees with what Finish produces,
        # fails here rather than on the first click.
        src/gui/DislocationWizard.cpp
        src/core/DislocationBuilder.cpp
        src/gui/SolidInterfaceWizard.cpp
        src/core/SolidInterfaceBuilder.cpp
        # The Structure editor dialog: its atom table mixes editable geometry
        # with the structure's extended arrays, and the contract — velocities
        # and forces editable and writing through to the vector fields,
        # everything else read-only, edits surviving a sort — is pinned by the
        # dialog test. Brings the periodic-table picker it opens; AseBridge is
        # already here for the magnetic-space-group dialog.
        src/gui/StructureEditorDialog.cpp
        src/gui/PeriodicTableDialog.cpp
        # The Molecular Design sketcher. Its canvas builds a QLineEdit inline
        # editor from a mouse handler and its dialog wires twelve exclusive
        # tool buttons and a shortcut per tool from the constructor — both are
        # exactly the construction-order hazard this test exists for. The
        # 2D -> 3D pipeline it drives is asserted separately (and Qt-free) by
        # the `molecular_design` test; what is exercised HERE is that the
        # dialog builds, that its tools are reachable, and that
        # "Send to 3D Viewport" emits a structure.
        src/gui/MoleculeCanvas.cpp
        src/gui/MolecularDesignDialog.cpp
        src/core/MoleculeGraph.cpp
        src/core/Smiles.cpp
        src/core/MoleculeEmbed3d.cpp
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
        # clipToWignerSeitzCell() (BrillouinZone.cpp) calls
        # flattenTriangleNormals(), defined in MarchingCubes.cpp — not
        # otherwise reachable from this target (VolumetricData.cpp, which
        # MarchingCubes.cpp itself needs, is already listed below).
        src/core/MarchingCubes.cpp
        src/core/KPath.cpp
        src/core/ElectronicScriptGenerator.cpp
        src/core/BandSymmetryScriptGenerator.cpp
        src/ui/IconManager.cpp
        src/gui/ThemeManager.cpp
        src/render/Camera.cpp
        # For the viewport Tab-cycling checks (Task 1 bugfix): a real
        # ViewportWidget is what QApplication::sendEvent() drives, exactly
        # the class whose focusNextPrevChild() override is under test.
        src/gui/ViewportWidget.cpp
        src/render/StructureRenderer.cpp
        src/core/Coordination.cpp
        src/core/HydrogenBonds.cpp
        src/core/StructuralPhase.cpp
        src/gui/FermiSurfaceDialog.cpp
        src/gui/TopologyDialog.cpp
        src/gui/EnginePresets.cpp
        src/gui/PythonHighlighter.cpp
        src/gui/RunCommands.cpp
        src/gui/ScriptStaging.cpp
        src/gui/SettingsManager.cpp
        src/gui/CalculatorParameters.cpp
        src/core/AseScriptGenerator.cpp
        # The Vibrational Mode Analysis module. It repopulates two combo boxes
        # from inside its own constructor (it loads a phonon run there), which
        # is the construction-order shape this test exists for. It costs only
        # these two translation units because it does NOT hold a
        # ViewportWidget: the displaced geometry leaves as a signal, precisely
        # so a headless test can construct it.
        src/gui/VibrationalAnalysisDialog.cpp
        src/core/VibrationalModes.cpp
        src/core/GrapheneOxideBuilder.cpp
        src/core/GrapheneOxideSites.cpp
        src/core/Structure.cpp
        src/core/UnitCell.cpp
        src/core/Element.cpp
        src/python_bridge/PythonEngine.cpp
        # DSIM: the wizard's own settings page (element B, supercell size)
        # builds nothing until Next is pressed, so construction alone needs
        # no embedded-Python round trip; its results viewer reuses
        # EgqcaPlotWidget, brought in for the first time by this target here.
        src/gui/DsimWizard.cpp
        src/gui/DsimResultsWindow.cpp
        src/gui/DsimPlotStyleDialog.cpp
        src/gui/DsimTernaryPlotWidget.cpp
        src/gui/EgqcaPlotWidget.cpp
        src/core/Dsim.cpp
        src/core/DsimScriptGenerator.cpp
        # DSIM's Geometry Optimization Settings stage reuses these two
        # shared controls (Geometry Optimization's own), first pulled into
        # this target here.
        src/gui/CellRelaxationControls.cpp
        src/gui/ForceConvergenceControl.cpp
        # Geometry Optimization Setup itself — not previously exercised
        # here at all. Added for the VASP-page-height regression test
        # (SimulationWizardBase::wrapInScrollArea()): the wizard that
        # actually reported the "does not fit the screen" bug.
        src/gui/GeometryOptimizationWizard.cpp
        # PreferencesDialog (Task 2's Hotkeys tab) and its own dependencies
        # not already pulled in above.
        src/gui/PreferencesDialog.hpp
        src/gui/PreferencesDialog.cpp
        src/gui/ShortcutRegistry.hpp
        src/gui/ShortcutRegistry.cpp
        src/gui/EnvFile.hpp
        src/gui/EnvFile.cpp
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
        PRIVATE Qt6::Widgets Qt6::OpenGL Qt6::OpenGLWidgets Qt6::Svg Qt6::Test
                pybind11::embed)
    calango_hdf5_link(calango_dialog_test)
    add_test(NAME dialog_construction COMMAND calango_dialog_test)

    # Live DOS smearing. The run now stores an unbroadened eigenvalue
    # histogram and the viewer convolves it, which moves a piece of physics
    # into a paint path — so the convolution is pinned against the analytic
    # Gaussian it is supposed to be: peak height 1/(sigma*sqrt(2pi)), conserved
    # weight, FWHM 2.355*sigma. Also that a legacy already-broadened file is
    # passed through untouched, because broadening one twice widens every peak
    # silently and looks like a physical result.
    qt_add_executable(calango_dos_smearing_test
        tests/DosSmearingTest.cpp
        src/gui/BandPdosView.cpp
        src/gui/GuiUtils.cpp
        src/gui/ThemeManager.cpp
        src/core/Element.cpp
        src/core/UnitCell.cpp
    )
    target_include_directories(calango_dos_smearing_test PRIVATE src
        "${CMAKE_CURRENT_BINARY_DIR}/generated")
    target_compile_definitions(calango_dos_smearing_test PRIVATE QT_NO_KEYWORDS)
    # QPdfWriter (the view's vector export) lives in QtGui in Qt6, so
    # PrintSupport is not a dependency here.
    target_link_libraries(calango_dos_smearing_test
        PRIVATE Qt6::Widgets Qt6::Svg)
    add_test(NAME dos_smearing COMMAND calango_dos_smearing_test)

    # The k-path / q-path table behind both the Electronic Structure and the
    # Phonon wizards. The sequence used to be a QListWidget of preformatted
    # strings and is now a real column table, which moved what the Remove action
    # indexes: from a list row to a top-level tree index. That mapping (rows
    # 1:1 with path entries, breaks included) is the thing that silently
    # deletes the wrong k-point if it ever drifts, and nothing about it shows up
    # at compile time.
    qt_add_executable(calango_kpath_table_test
        tests/KPathTableTest.cpp
        src/gui/BrillouinZoneWidget.cpp
        src/gui/BrillouinZoneView.cpp
        src/gui/BrillouinZoneStyleDialog.cpp
        src/gui/ThemeManager.cpp
        src/ui/IconManager.cpp
        src/render/Camera.cpp
        src/core/BrillouinZone.cpp
        # clipToWignerSeitzCell() (BrillouinZone.cpp) calls
        # flattenTriangleNormals(), defined in MarchingCubes.cpp, which in
        # turn calls VolumetricData's sample()/samplePeriodic() — neither is
        # otherwise reachable from this target.
        src/core/MarchingCubes.cpp
        src/core/VolumetricData.cpp
        src/core/Structure.cpp
        src/core/UnitCell.cpp
        src/core/Element.cpp
    )
    target_include_directories(calango_kpath_table_test PRIVATE src
        "${CMAKE_CURRENT_BINARY_DIR}/generated")
    target_compile_definitions(calango_kpath_table_test PRIVATE QT_NO_KEYWORDS)
    target_link_libraries(calango_kpath_table_test
        PRIVATE Qt6::Widgets Qt6::OpenGL Qt6::OpenGLWidgets Qt6::Svg
                pybind11::embed)
    calango_hdf5_link(calango_kpath_table_test)
    add_test(NAME kpath_table COMMAND calango_kpath_table_test)

    # Workflow pipeline validation on silicon: two linked nodes (geometry
    # optimization with cell relaxation → phonon) sent to processes for
    # real, with Lennard-Jones so the ASE runs finish in seconds. Pins the
    # Waiting/Running/Done lifecycle and that the relaxed structure flows
    # along the link into the phonon node. Self-skips without ASE.
    qt_add_executable(calango_orchestration_si_test
        tests/OrchestrationSiTest.cpp
        src/gui/OrchestrationWindow.cpp
        src/gui/OrchestrationDocument.cpp
        src/python_bridge/BulkBuilder.cpp
        src/gui/OrchestrationTransforms.cpp
        # The Random Noise Setup transform perturbs its incoming structure in
        # process (core::buildNoiseEnsemble()), so OrchestrationWindow.cpp's
        # runTransform() now pulls this in too.
        src/core/Noise.cpp
        src/core/ClusterExpansionFit.cpp
        src/core/ClusterVariation.cpp
        # The TDB Generator transform fits a CALPHAD model in process, so the
        # transforms translation unit now pulls in the assessment, the hull it
        # reports against, and the database writer. The SQS Generator transform
        # anneals in process for the same reason, so it pulls in the generator
        # and the Warren-Cowley order parameters it reports.
        src/core/CalphadModel.cpp
        src/core/ConvexHull.cpp
        src/core/TdbWriter.cpp
        src/core/TdbDatabase.cpp
        src/core/TdbExpression.cpp
        src/core/SqsGenerator.cpp
        src/core/ChemicalOrder.cpp
        src/gui/OrchestrationProvenance.cpp
        src/core/WorkflowReport.cpp
        src/core/BandGap.cpp
        src/gui/SimulationWizardBase.cpp
        src/gui/GpawElectronicRows.cpp
        src/gui/HubbardParametersDialog.cpp
        src/gui/ExtendedEngineGroups.cpp
        src/core/EngineCalculatorBlocks.cpp
        src/ui/IconManager.cpp
        src/gui/ThemeManager.cpp
        src/gui/ProcessManagerPanel.cpp
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
        # A MaceTrainer node's setup dialog is the pre-existing, standalone
        # MaceTrainerDialog (openNodeWizard()'s special case for it), and a
        # Dataset Manager node's stratified split goes through
        # core::DatasetSplit::makeStratified().
        src/gui/MaceTrainerDialog.cpp
        src/gui/PythonPackagePreflight.cpp
        src/gui/VaspPotcarPreflight.cpp
        src/gui/GpawMpiPreflight.cpp
        src/core/DatasetSplit.cpp
    )
    target_include_directories(calango_orchestration_si_test PRIVATE src
        "${CMAKE_CURRENT_BINARY_DIR}/generated")
    target_compile_definitions(calango_orchestration_si_test PRIVATE
        CALANGO_VERSION="${PROJECT_VERSION}"
        # Without this the embedded engine cannot name its interpreter and
        # the test's job-launching python resolves to nonsense.
        CALANGO_DEFAULT_PYTHON="${CALANGO_DEFAULT_PYTHON_PATH}"
        QT_NO_KEYWORDS)
    target_link_libraries(calango_orchestration_si_test
        PRIVATE Qt6::Widgets Qt6::OpenGLWidgets Qt6::Svg pybind11::embed)
    calango_hdf5_link(calango_orchestration_si_test)
    if(APPLE)
        target_link_libraries(calango_orchestration_si_test
            PRIVATE "-framework OpenGL")
    else()
        target_link_libraries(calango_orchestration_si_test PRIVATE OpenGL::GL)
    endif()
    add_test(NAME orchestration_si_pipeline COMMAND calango_orchestration_si_test)
    set_tests_properties(orchestration_si_pipeline PROPERTIES TIMEOUT 900)

    # MACE Trainer: dependency pre-flight (both the absent-package failure
    # path, using the embedded interpreter as a genuine instance of it, and
    # the success path against a discovered mace_env), device probing, and
    # config-generation golden tests keyed to whatever mace-torch version is
    # actually installed. Reuses the SAME dependency closure as the
    # orchestration tests above rather than a hand-pruned subset — this
    # target does not touch OrchestrationWindow at all, but MaceTrainerDialog
    # pulls in AseScriptGenerator.cpp (jsonLoggerPreamble()), which pulls in
    # the rest.
    qt_add_executable(calango_mace_trainer_preflight_test
        tests/MaceTrainerPreflightTest.cpp
        src/gui/OrchestrationWindow.cpp
        src/gui/OrchestrationDocument.cpp
        src/python_bridge/BulkBuilder.cpp
        src/gui/OrchestrationTransforms.cpp
        src/core/Noise.cpp
        src/core/ClusterExpansionFit.cpp
        src/core/ClusterVariation.cpp
        src/core/CalphadModel.cpp
        src/core/ConvexHull.cpp
        src/core/TdbWriter.cpp
        src/core/TdbDatabase.cpp
        src/core/TdbExpression.cpp
        src/core/SqsGenerator.cpp
        src/core/ChemicalOrder.cpp
        src/gui/OrchestrationProvenance.cpp
        src/core/WorkflowReport.cpp
        src/core/BandGap.cpp
        src/gui/SimulationWizardBase.cpp
        src/gui/GpawElectronicRows.cpp
        src/gui/HubbardParametersDialog.cpp
        src/gui/ExtendedEngineGroups.cpp
        src/core/EngineCalculatorBlocks.cpp
        src/ui/IconManager.cpp
        src/gui/ThemeManager.cpp
        src/gui/ProcessManagerPanel.cpp
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
        src/gui/MaceTrainerDialog.cpp
        src/gui/PythonPackagePreflight.cpp
        src/gui/VaspPotcarPreflight.cpp
        src/gui/GpawMpiPreflight.cpp
        src/core/DatasetSplit.cpp
    )
    target_include_directories(calango_mace_trainer_preflight_test PRIVATE src
        "${CMAKE_CURRENT_BINARY_DIR}/generated")
    target_compile_definitions(calango_mace_trainer_preflight_test PRIVATE
        CALANGO_VERSION="${PROJECT_VERSION}"
        CALANGO_DEFAULT_PYTHON="${CALANGO_DEFAULT_PYTHON_PATH}"
        QT_NO_KEYWORDS)
    target_link_libraries(calango_mace_trainer_preflight_test
        PRIVATE Qt6::Widgets Qt6::OpenGLWidgets Qt6::Svg pybind11::embed)
    calango_hdf5_link(calango_mace_trainer_preflight_test)
    if(APPLE)
        target_link_libraries(calango_mace_trainer_preflight_test
            PRIVATE "-framework OpenGL")
    else()
        target_link_libraries(calango_mace_trainer_preflight_test PRIVATE OpenGL::GL)
    endif()
    add_test(NAME mace_trainer_preflight COMMAND calango_mace_trainer_preflight_test)
    # Generous: the opt-in end-to-end path actually launches mace_run_train.
    set_tests_properties(mace_trainer_preflight PROPERTIES TIMEOUT 300)

    # Workflow data handoff: the same two Geometry Optimization nodes run
    # unlinked (both relax the rattled structure independently, full step
    # count each) and linked (the child inherits the parent's relaxed
    # geometry and converges in <= 1 step). EMT copper for speed; self-skips
    # without ASE.
    qt_add_executable(calango_orchestration_handoff_test
        tests/OrchestrationHandoffTest.cpp
        src/gui/OrchestrationWindow.cpp
        src/gui/OrchestrationDocument.cpp
        src/python_bridge/BulkBuilder.cpp
        src/gui/OrchestrationTransforms.cpp
        # The Random Noise Setup transform perturbs its incoming structure in
        # process (core::buildNoiseEnsemble()), so OrchestrationWindow.cpp's
        # runTransform() now pulls this in too.
        src/core/Noise.cpp
        src/core/ClusterExpansionFit.cpp
        src/core/ClusterVariation.cpp
        # The TDB Generator transform fits a CALPHAD model in process, so the
        # transforms translation unit now pulls in the assessment, the hull it
        # reports against, and the database writer. The SQS Generator transform
        # anneals in process for the same reason, so it pulls in the generator
        # and the Warren-Cowley order parameters it reports.
        src/core/CalphadModel.cpp
        src/core/ConvexHull.cpp
        src/core/TdbWriter.cpp
        src/core/TdbDatabase.cpp
        src/core/TdbExpression.cpp
        src/core/SqsGenerator.cpp
        src/core/ChemicalOrder.cpp
        src/gui/OrchestrationProvenance.cpp
        src/core/WorkflowReport.cpp
        src/core/BandGap.cpp
        src/gui/SimulationWizardBase.cpp
        src/gui/GpawElectronicRows.cpp
        src/gui/HubbardParametersDialog.cpp
        src/gui/ExtendedEngineGroups.cpp
        src/core/EngineCalculatorBlocks.cpp
        src/ui/IconManager.cpp
        src/gui/ThemeManager.cpp
        src/gui/ProcessManagerPanel.cpp
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
        # A MaceTrainer node's setup dialog is the pre-existing, standalone
        # MaceTrainerDialog (openNodeWizard()'s special case for it), and a
        # Dataset Manager node's stratified split goes through
        # core::DatasetSplit::makeStratified().
        src/gui/MaceTrainerDialog.cpp
        src/gui/PythonPackagePreflight.cpp
        src/gui/VaspPotcarPreflight.cpp
        src/gui/GpawMpiPreflight.cpp
        src/core/DatasetSplit.cpp
    )
    target_include_directories(calango_orchestration_handoff_test PRIVATE src
        "${CMAKE_CURRENT_BINARY_DIR}/generated")
    target_compile_definitions(calango_orchestration_handoff_test PRIVATE
        CALANGO_VERSION="${PROJECT_VERSION}"
        CALANGO_DEFAULT_PYTHON="${CALANGO_DEFAULT_PYTHON_PATH}"
        QT_NO_KEYWORDS)
    target_link_libraries(calango_orchestration_handoff_test
        PRIVATE Qt6::Widgets Qt6::OpenGLWidgets Qt6::Svg pybind11::embed)
    calango_hdf5_link(calango_orchestration_handoff_test)
    if(APPLE)
        target_link_libraries(calango_orchestration_handoff_test
            PRIVATE "-framework OpenGL")
    else()
        target_link_libraries(calango_orchestration_handoff_test PRIVATE OpenGL::GL)
    endif()
    add_test(NAME orchestration_handoff COMMAND calango_orchestration_handoff_test)
    set_tests_properties(orchestration_handoff PROPERTIES TIMEOUT 900)

    # Workflow batching, transforms, provenance and resume: a Structure
    # Container fans the pipeline out over Cu/Au/Pt (one pass each, each pass
    # on its own element), a Supercell and a Defect Generator edit the
    # structure mid-pipeline, every node leaves a provenance record whose
    # checksums match the files its parent wrote, and a failed node is fixed
    # and resumed without re-running anything upstream. Also the intended
    # end-to-end workflow: a Random Noise Setup trajectory (linear ramp on),
    # saved to a real file, read back through the same
    # readStructuresFromFile() the Container editor's "Import from File..."
    # uses, and fanned out one real EMT single-point per structure, with the
    # zero-noise first frame's energy checked against an independent,
    # outside-Orchestration run of the same calculation. Self-skips without
    # ASE.
    qt_add_executable(calango_orchestration_batch_test
        tests/OrchestrationBatchTest.cpp
        src/gui/OrchestrationWindow.cpp
        src/gui/OrchestrationDocument.cpp
        src/python_bridge/BulkBuilder.cpp
        src/gui/OrchestrationTransforms.cpp
        src/core/ClusterExpansionFit.cpp
        src/core/ClusterVariation.cpp
        # The TDB Generator transform fits a CALPHAD model in process, so the
        # transforms translation unit now pulls in the assessment, the hull it
        # reports against, and the database writer. The SQS Generator transform
        # anneals in process for the same reason, so it pulls in the generator
        # and the Warren-Cowley order parameters it reports.
        src/core/CalphadModel.cpp
        src/core/ConvexHull.cpp
        src/core/TdbWriter.cpp
        src/core/TdbDatabase.cpp
        src/core/TdbExpression.cpp
        src/core/SqsGenerator.cpp
        src/core/ChemicalOrder.cpp
        src/gui/OrchestrationProvenance.cpp
        src/core/WorkflowReport.cpp
        src/core/BandGap.cpp
        src/gui/SimulationWizardBase.cpp
        src/gui/GpawElectronicRows.cpp
        src/gui/HubbardParametersDialog.cpp
        src/gui/ExtendedEngineGroups.cpp
        src/core/EngineCalculatorBlocks.cpp
        src/ui/IconManager.cpp
        src/gui/ThemeManager.cpp
        src/gui/ProcessManagerPanel.cpp
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
        src/core/Noise.cpp
        src/gui/RandomNoiseWizard.cpp
        src/python_bridge/AseBridge.cpp
        src/python_bridge/PythonEngine.cpp
        src/jobs/JobRunner.cpp
        # A MaceTrainer node's setup dialog is the pre-existing, standalone
        # MaceTrainerDialog (openNodeWizard()'s special case for it), and a
        # Dataset Manager node's stratified split goes through
        # core::DatasetSplit::makeStratified().
        src/gui/MaceTrainerDialog.cpp
        src/gui/PythonPackagePreflight.cpp
        src/gui/VaspPotcarPreflight.cpp
        src/gui/GpawMpiPreflight.cpp
        src/core/DatasetSplit.cpp
    )
    target_include_directories(calango_orchestration_batch_test PRIVATE src
        "${CMAKE_CURRENT_BINARY_DIR}/generated")
    target_compile_definitions(calango_orchestration_batch_test PRIVATE
        CALANGO_VERSION="${PROJECT_VERSION}"
        CALANGO_DEFAULT_PYTHON="${CALANGO_DEFAULT_PYTHON_PATH}"
        QT_NO_KEYWORDS)
    target_link_libraries(calango_orchestration_batch_test
        PRIVATE Qt6::Widgets Qt6::OpenGLWidgets Qt6::Svg pybind11::embed)
    calango_hdf5_link(calango_orchestration_batch_test)
    if(APPLE)
        target_link_libraries(calango_orchestration_batch_test
            PRIVATE "-framework OpenGL")
    else()
        target_link_libraries(calango_orchestration_batch_test PRIVATE OpenGL::GL)
    endif()
    add_test(NAME orchestration_batch COMMAND calango_orchestration_batch_test)
    set_tests_properties(orchestration_batch PROPERTIES TIMEOUT 900)

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

    # Theme round trip: a Light -> Dark switch back once left QToolTip on the
    # light theme's background/text (QApplication::setPalette() updated
    # correctly, but the QSS string only carried the tooltip border colour).
    # Checks that the tooltip style sheet agrees with the live palette after
    # any number of round trips, not just the first switch.
    qt_add_executable(calango_theme_round_trip_test
        tests/ThemeRoundTripTest.cpp
        src/gui/ThemeManager.cpp
    )
    target_include_directories(calango_theme_round_trip_test PRIVATE src)
    target_link_libraries(calango_theme_round_trip_test PRIVATE Qt6::Widgets)
    add_test(NAME theme_round_trip COMMAND calango_theme_round_trip_test)

    # Icon registry: every name the UI passes to IconManager must have a file in
    # assets/icons/, and every file there must be listed for qt_add_resources.
    # A missing asset is a NULL pixmap — a blank button at run time, with
    # nothing failing at build time. That is how "Phase colors…" shipped with a
    # name taken from RemixIcon/ (where the SVG exists) but never copied into
    # the curated assets/icons/ subset.
    # GPU telemetry. Asserts the CONTRACT (a reading, or a stated reason)
    # rather than a hardware number, so it is meaningful on a CI box with no
    # GPU as well as on a workstation with one.
    # The visible-spectrum overlay. Renders the widget and reads pixels back:
    # a gradient is one of the few things review cannot check, since wrong
    # colour placement compiles and draws perfectly well.
    qt_add_executable(calango_visible_spectrum_test
        tests/VisibleSpectrumTest.cpp
        src/gui/SpectrumPlotWidget.cpp
        # OpticsPlotStyle's out-of-line members (fonts, effective grid colour).
        src/gui/OpticsPlotStyleDialog.cpp
    )
    target_include_directories(calango_visible_spectrum_test PRIVATE src)
    target_link_libraries(calango_visible_spectrum_test PRIVATE Qt6::Gui
                                                                Qt6::Widgets)
    add_test(NAME visible_spectrum COMMAND calango_visible_spectrum_test)

    # .tdb parsing: what the CALPHAD module needs to populate its element and
    # phase selection, independent of pycalphad (which no Calango environment
    # ships). Checked against a hand-written database covering the syntax that
    # varies in the wild, and against the real SGTE files when present.
    # Batch-scheduler directives: one resource request translated into SLURM,
    # PBS and SGE dialects. The mistranslations are silent (a job that queues
    # forever rather than erroring), so they are pinned.
    # HPC cluster presets: JSON round trip, list editing, and the assertion
    # that no password ever reaches the preset file.
    add_executable(calango_cluster_preset_test
        tests/ClusterPresetTest.cpp
        src/gui/ClusterPreset.cpp
    )
    target_include_directories(calango_cluster_preset_test PRIVATE src)
    target_link_libraries(calango_cluster_preset_test PRIVATE Qt6::Core)
    add_test(NAME cluster_presets COMMAND calango_cluster_preset_test)

    add_executable(calango_scheduler_test
        tests/SchedulerScriptTest.cpp
        src/core/SchedulerScript.cpp
    )
    target_include_directories(calango_scheduler_test PRIVATE src)
    add_test(NAME scheduler_script COMMAND calango_scheduler_test)

    add_executable(calango_vasp_potcar_preflight_test
        tests/VaspPotcarPreflightTest.cpp
        src/gui/VaspPotcarPreflight.cpp
    )
    target_include_directories(calango_vasp_potcar_preflight_test PRIVATE src)
    target_link_libraries(calango_vasp_potcar_preflight_test PRIVATE Qt6::Core)
    add_test(NAME vasp_potcar_preflight COMMAND calango_vasp_potcar_preflight_test)

    # GPAW MPI pre-flight (Task 2): "cores>1 requested but no MPI launcher /
    # non-MPI GPAW" must be caught with a clear message before a job is
    # staged, not discovered as a silently-serial run. See
    # GpawMpiPreflight.hpp; the real build-detection assertions skip cleanly
    # when no "gpaw_fast" conda environment is found, same as
    # calango_mace_trainer_preflight_test does for mace_env.
    add_executable(calango_gpaw_mpi_preflight_test
        tests/GpawMpiPreflightTest.cpp
        src/gui/GpawMpiPreflight.cpp
        src/gui/CondaEnvs.cpp
    )
    target_include_directories(calango_gpaw_mpi_preflight_test PRIVATE src)
    target_link_libraries(calango_gpaw_mpi_preflight_test PRIVATE Qt6::Core)
    add_test(NAME gpaw_mpi_preflight COMMAND calango_gpaw_mpi_preflight_test)

    # RunCommands::resolve()/displayCommand() round-trip (Task 1, 2026-08-22):
    # the "GPAW runs on 1 core despite cores=4" bug that survived a previous
    # fix, traced to resolve() misclassifying an already-substituted preview
    # string. See RunCommandsTest.cpp's own header for the full story.
    add_executable(calango_run_commands_test
        tests/RunCommandsTest.cpp
        src/gui/RunCommands.cpp
        src/gui/EnginePresets.cpp
        src/gui/CondaEnvs.cpp
        src/gui/SettingsManager.cpp
    )
    target_include_directories(calango_run_commands_test PRIVATE src)
    target_link_libraries(calango_run_commands_test PRIVATE Qt6::Core)
    add_test(NAME run_commands COMMAND calango_run_commands_test)

    add_executable(calango_shortcut_registry_test
        tests/ShortcutRegistryTest.cpp
        src/gui/ShortcutRegistry.cpp
        src/gui/SettingsManager.cpp
    )
    target_include_directories(calango_shortcut_registry_test PRIVATE src)
    target_link_libraries(calango_shortcut_registry_test PRIVATE Qt6::Core Qt6::Gui)
    add_test(NAME shortcut_registry COMMAND calango_shortcut_registry_test)

    # The persistent SSH session, Qt side. Drives RemoteClient against a real
    # paramiko SSH server on 127.0.0.1 that demands a one-time code (started
    # by tests/remote_session_test.py --serve). No 2FA cluster is reachable
    # from a build machine, and the property that matters — ONE challenge for
    # a whole job's worth of operations, and never one raised by a background
    # poll — can only be observed against a server that actually challenges.
    # Self-skips without python3/paramiko.
    qt_add_executable(calango_remote_session_test
        tests/RemoteSessionTest.cpp
        src/remote/RemoteClient.cpp
    )
    target_include_directories(calango_remote_session_test PRIVATE src)
    target_link_libraries(calango_remote_session_test PRIVATE Qt6::Core)
    add_test(NAME remote_session
             COMMAND calango_remote_session_test ${CMAKE_CURRENT_SOURCE_DIR}
                     ${Python3_EXECUTABLE})
    set_tests_properties(remote_session PROPERTIES TIMEOUT 300)

    # And the helper's own half: the auth ladder (a password answered with
    # PARTIAL success plus a list of remaining factors), the keyboard-
    # interactive round trip, SFTP upload/download, and the reconnection
    # policy that separates a key session (rebuilt silently) from a 2FA one
    # (never rebuilt without the user).
    add_test(NAME remote_session_helper
             COMMAND ${Python3_EXECUTABLE}
                     ${CMAKE_CURRENT_SOURCE_DIR}/tests/remote_session_test.py
                     ${CMAKE_CURRENT_SOURCE_DIR}/assets/remote/calango_remote.py)
    set_tests_properties(remote_session_helper PROPERTIES TIMEOUT 300)

    add_executable(calango_tdb_test
        tests/TdbDatabaseTest.cpp
        src/core/TdbDatabase.cpp
    )
    target_include_directories(calango_tdb_test PRIVATE src)
    add_test(NAME tdb_database COMMAND calango_tdb_test)

    # The CALPHAD thermodynamics on top of that parser: the Redlich-Kister
    # model, the DFT -> .tdb pipeline, the TDB expression evaluator, and the
    # phase-diagram construction. Every assertion is against a closed form or a
    # thermodynamic identity — the analytic ideal-lens solution, the
    # regular-solution binodal and its critical temperature T_c = Omega/2R,
    # exact recovery of a known Redlich-Kister model from noiseless samples,
    # and -- the strongest one, because no number in it comes from this project
    # -- the MELTING POINTS of Al, Cu, Ag, Ni and Mg, solved out of the real
    # SGTE unary functions as the crossing of their liquid and solid Gibbs
    # energies and compared against the measured values. Iron is kept in that
    # list precisely because it misses by 10 K: its magnetic contribution is
    # not modelled, and pinning the size of a known limitation is what stops it
    # from growing. GUI-free, Python-free, and pycalphad-free: the whole point
    # of the module is that none of this needs a solver installed.
    add_executable(calango_calphad_test
        tests/CalphadTest.cpp
        src/core/CalphadModel.cpp
        src/core/ConvexHull.cpp
        src/core/PhaseDiagram.cpp
        src/core/PhononThermodynamics.cpp
        src/core/TdbDatabase.cpp
        src/core/TdbExpression.cpp
        src/core/TdbWriter.cpp
    )
    target_include_directories(calango_calphad_test PRIVATE src)
    add_test(NAME calphad COMMAND calango_calphad_test)

    add_executable(calango_gpu_telemetry_test
        tests/GpuTelemetryTest.cpp
        src/gui/GpuTelemetry.cpp
    )
    target_include_directories(calango_gpu_telemetry_test PRIVATE src)
    target_link_libraries(calango_gpu_telemetry_test PRIVATE Qt6::Core)
    if(APPLE)
        target_link_libraries(calango_gpu_telemetry_test
            PRIVATE "-framework IOKit" "-framework CoreFoundation")
    endif()
    add_test(NAME gpu_telemetry COMMAND calango_gpu_telemetry_test)

    add_executable(calango_icon_registry_test tests/IconRegistryTest.cpp)
    target_compile_definitions(calango_icon_registry_test PRIVATE
        CALANGO_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
    add_test(NAME icon_registry COMMAND calango_icon_registry_test)

    # The macOS/Linux file-association plumbing: which ASE format hint a path
    # gets and whether it is a project vs. a structure (FileOpenRouting.hpp),
    # the pending-events queue a Finder launch can race
    # (FileOpenQueue.hpp), and the real CalangoApplication driven with
    # synthesized QFileOpenEvents -- the same delivery mechanism Finder uses.
    # MainWindow (zero test coverage, ~9.5k lines) is deliberately not
    # linked; these three headers were extracted from main.cpp/MainWindow.cpp
    # specifically so this routing logic is testable on its own.
    qt_add_executable(calango_file_association_test
        tests/FileAssociationTest.cpp
    )
    target_include_directories(calango_file_association_test PRIVATE src)
    target_compile_definitions(calango_file_association_test PRIVATE QT_NO_KEYWORDS)
    target_link_libraries(calango_file_association_test PRIVATE Qt6::Widgets)
    add_test(NAME file_association COMMAND calango_file_association_test)

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
    # Qt6::OpenGL is needed for HEADERS only — OverlayModel.hpp includes
    # StructureRenderer.hpp, which carries QOpenGLBuffer members. No widget
    # type is referenced anywhere in the target, so Gui replaces Widgets.
    target_link_libraries(calango_overlay_test PRIVATE Qt6::Gui Qt6::OpenGL)
    calango_hdf5_link(calango_overlay_test)
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
        # GuiUtils gained guessVacuumAxis(), which asks the cell for its
        # fractional coordinates. Nothing in this test calls it, but the whole
        # translation unit is linked, so its out-of-line UnitCell references
        # have to resolve.
        src/core/UnitCell.cpp
    )
    target_include_directories(calango_element_color_test PRIVATE src)
    target_link_libraries(calango_element_color_test PRIVATE Qt6::Widgets)
    add_test(NAME element_colors COMMAND calango_element_color_test)

    # Linear-response Hubbard U. Every failure mode of this method is silent —
    # a chi_0 run left to converge measures chi twice and yields U = 0, an
    # alpha applied per species rather than per atom perturbs a whole
    # sublattice — so the generated driver is asserted on directly. GUI-free,
    # GL-free, Python-free.
    # Plain add_executable, no Qt link: the sources use no Qt type at all
    # (the only Q* token in the whole target is the string "QE").
    add_executable(calango_hubbard_test
        tests/HubbardLinearResponseTest.cpp
        src/core/HubbardScriptGenerator.cpp
        src/core/AseScriptGenerator.cpp
        src/core/EngineCalculatorBlocks.cpp
        src/core/Structure.cpp
        src/core/UnitCell.cpp
        src/core/Element.cpp
        src/core/KPath.cpp
    )
    target_include_directories(calango_hubbard_test PRIVATE src)
    target_compile_definitions(calango_hubbard_test PRIVATE
        CALANGO_VERSION="${PROJECT_VERSION}")
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
    calango_hdf5_link(calango_alembic_test)
    target_compile_definitions(calango_alembic_test PRIVATE QT_NO_KEYWORDS)
    add_test(NAME alembic_export COMMAND calango_alembic_test)

    # Results viewers: the GW and optics windows parse a JSON schema written by
    # a Python script, so nothing in the C++ build ties the two together. This
    # pins the schema from the reader's side (offscreen Qt, no GL, no display).
    qt_add_executable(calango_results_viewer_test
        tests/ResultsViewerTest.cpp
        src/gui/GwResultsWindow.cpp
        src/gui/OpticsResultsWindow.cpp
        src/gui/NonlinearOpticsResultsWindow.cpp
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
        src/core/GrapheneOxideSites.cpp
        src/core/Structure.cpp
        src/core/UnitCell.cpp
        src/core/Element.cpp
    )
    target_include_directories(calango_graphene_oxide_test PRIVATE src)
    add_test(NAME graphene_oxide COMMAND calango_graphene_oxide_test)

    # Benzene-ring / sp2-percolation analysis: ring counts against Euler's
    # formula on a torus (N/2 hexagonal faces) and against the builder's own
    # nanoflake ring-count formula (3m(m-1)+1); percolation against a
    # periodic sheet whose pbc flags are edited after construction into a
    # one-direction-only ribbon, so the same atoms give a known answer for
    # both the periodic and the ribbon case.
    add_executable(calango_graphene_oxide_percolation_test
        tests/GrapheneOxidePercolationTest.cpp
        src/core/GrapheneOxidePercolation.cpp
        src/core/GrapheneOxideBuilder.cpp
        src/core/GrapheneOxideSites.cpp
        src/core/Structure.cpp
        src/core/UnitCell.cpp
        src/core/Element.cpp
    )
    target_include_directories(calango_graphene_oxide_percolation_test PRIVATE src)
    add_test(NAME graphene_oxide_percolation COMMAND calango_graphene_oxide_percolation_test)

    # GO Functional Group Analysis: synthetic micro-structures with hand-
    # computable answers (a single epoxide, a single hydroxyl) plus a
    # trajectory consistency check.
    add_executable(calango_graphene_oxide_group_analysis_test
        tests/GrapheneOxideGroupAnalysisTest.cpp
        src/core/GrapheneOxideGroupAnalysis.cpp
        src/core/GrapheneOxideBuilder.cpp
        src/core/GrapheneOxideSites.cpp
        src/core/Structure.cpp
        src/core/UnitCell.cpp
        src/core/Element.cpp
    )
    target_include_directories(calango_graphene_oxide_group_analysis_test PRIVATE src)
    add_test(NAME graphene_oxide_group_analysis
             COMMAND calango_graphene_oxide_group_analysis_test)

    # GO Pair Correlation: honeycomb shell enumeration verified against a
    # real pristine sheet's own bonding (3/6/3 neighbors for shells 1-3),
    # plus synthetic decorations with known Warren-Cowley alpha (perfectly
    # alternating, random, clustered).
    add_executable(calango_graphene_oxide_pair_correlation_test
        tests/GrapheneOxidePairCorrelationTest.cpp
        src/core/GrapheneOxidePairCorrelation.cpp
        src/core/GrapheneOxideGroupAnalysis.cpp
        src/core/ChemicalOrder.cpp
        src/core/GrapheneOxideBuilder.cpp
        src/core/GrapheneOxideSites.cpp
        src/core/Structure.cpp
        src/core/UnitCell.cpp
        src/core/Element.cpp
    )
    target_include_directories(calango_graphene_oxide_pair_correlation_test PRIVATE src)
    add_test(NAME graphene_oxide_pair_correlation
             COMMAND calango_graphene_oxide_pair_correlation_test)

    # Workflow report. Linked against Qt::Core ONLY — no Widgets, no Gui — so
    # the "decoupled from the GUI" claim is enforced by the build rather than
    # asserted in a comment. If someone reaches for a QWidget in here, this
    # target stops linking.
    add_executable(calango_workflow_report_test
        tests/WorkflowReportTest.cpp
        src/core/WorkflowReport.cpp
        src/core/BandGap.cpp
    )
    target_include_directories(calango_workflow_report_test PRIVATE src)
    target_link_libraries(calango_workflow_report_test PRIVATE Qt6::Core)
    add_test(NAME workflow_report COMMAND calango_workflow_report_test)

    # Reactive-site graph: the pool bookkeeping under the Monte Carlo sampler.
    # Single sites and pair sites are COUPLED — occupying one carbon invalidates
    # every pair containing it — and getting that coupling wrong puts two groups
    # on one carbon rather than crashing. Driven by long random walks with the
    # invariant asserted throughout, because the reachable states after a few
    # hundred mixed moves are not the ones anyone writes a targeted case for.
    add_executable(calango_graphene_oxide_sites_test
        tests/GrapheneOxideSitesTest.cpp
        src/core/GrapheneOxideSites.cpp
    )
    target_include_directories(calango_graphene_oxide_sites_test PRIVATE src)
    add_test(NAME graphene_oxide_sites COMMAND calango_graphene_oxide_sites_test)

    # Native SQS generator: composition exactness across binary/ternary/
    # quaternary alloys and, above all, that the generated decoration really is
    # quasirandom (first-shell Warren-Cowley order parameter near zero).
    # ChemicalOrder rides along because the generator now reports the
    # Warren-Cowley α of what it produced rather than leaving it to be measured
    # by a second tool.
    add_executable(calango_sqs_generator_test
        tests/SqsGeneratorTest.cpp
        src/core/SqsGenerator.cpp
        src/core/ChemicalOrder.cpp
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
        src/core/EngineCalculatorBlocks.cpp
        src/core/BornChargesScriptGenerator.cpp
        src/core/PolarizationScriptHelpers.cpp
        src/core/StrainScriptHelpers.cpp
        src/core/PiezoelectricScriptGenerator.cpp
        src/core/PiezoelectricTensor.cpp
        src/core/ElasticScriptGenerator.cpp
        src/core/ElasticTensor.cpp
        src/core/ElectronicScriptGenerator.cpp
        src/core/BandSymmetryScriptGenerator.cpp
        src/core/PhononScriptGenerator.cpp
        src/core/OpticsScriptGenerator.cpp
        src/core/NonlinearOpticsScriptGenerator.cpp
        src/core/GwScriptGenerator.cpp
        src/core/RamanIrScriptGenerator.cpp
        src/core/CddScriptGenerator.cpp
        src/core/HubbardScriptGenerator.cpp
        src/core/XasScriptGenerator.cpp
        src/core/UnfoldingScriptGenerator.cpp
        src/core/TwoDBandsScriptGenerator.cpp
        src/core/WorkfunctionScriptGenerator.cpp
        src/core/WannierScriptGenerator.cpp
        src/core/LdosScriptGenerator.cpp
        src/core/EnergyDiagramScriptGenerator.cpp
        src/core/WavefunctionScriptGenerator.cpp
        src/core/DefectScriptGenerator.cpp
        src/core/Defect2dScriptGenerator.cpp
        src/core/FermiSurfaceScriptGenerator.cpp
        src/core/TopologyScriptGenerator.cpp
        src/core/ClusterExpansionScriptGenerator.cpp
        src/core/DsimScriptGenerator.cpp
        src/core/Structure.cpp
        src/core/Element.cpp
        src/core/GrapheneOxideMdmcScriptGenerator.cpp
        # The one script in the project that imports pycalphad. It is here
        # precisely because it cannot be run in CI — pycalphad is installed
        # nowhere — so the static check is the only thing standing between a
        # typo and a user's failed run.
        src/core/CalphadScriptGenerator.cpp
        # The k-point sweep, which brings its shared measurement/analysis
        # blocks with it (ConvergenceSweepBlocks).
        src/core/KpointsConvergenceScriptGenerator.cpp
        src/core/ConvergenceSweepBlocks.cpp
        # Thermodynamic integration. Its script defines two ASE
        # Calculator subclasses and a sampling loop out of string
        # fragments, which is exactly the shape the name checker
        # exists for: a `_name` read in the loop whose definition was
        # lost in an edit is valid Python that dies at run time.
        src/core/ThermodynamicIntegrationScriptGenerator.cpp
        src/core/ThermodynamicIntegration.cpp
        src/core/BandUnfolding.cpp
        src/core/UnitCell.cpp
    )
    target_include_directories(calango_script_test PRIVATE src
        "${CMAKE_CURRENT_BINARY_DIR}/generated")
    target_compile_definitions(calango_script_test PRIVATE
        CALANGO_VERSION="${PROJECT_VERSION}")
    add_test(NAME generated_ase_scripts COMMAND calango_script_test)

    # Static checks over the Python the generators EMIT, not over the C++ that
    # emits it. Two bugs have shipped that byte-compilation cannot see: a use
    # of a variable whose definition was lost in an edit is valid Python that
    # dies at run time with a NameError, halfway through somebody's
    # calculation. The checker dumps every generated script and asserts that
    # each one compiles AND that every generator-internal `_name` it reads is
    # assigned somewhere in the same file (reads guarded by `except NameError`
    # excepted — that is the one legitimate way to read a name that may not
    # exist). It found three more instances the moment it was written.
    add_test(NAME generated_script_lint
        COMMAND ${CMAKE_COMMAND}
            -DSCRIPT_TEST=$<TARGET_FILE:calango_script_test>
            -DCHECKER=${CMAKE_CURRENT_SOURCE_DIR}/tests/check_generated_scripts.py
            -DPYTHON=${Python3_EXECUTABLE}
            -DWORKDIR=${CMAKE_CURRENT_BINARY_DIR}/generated_scripts
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/RunScriptLint.cmake)

    # Volumetric grid parsing: the value block is read with a bulk read plus
    # std::from_chars rather than through istream >> double (5x faster on a
    # production grid). A fast parser that is subtly wrong renders a plausible
    # isosurface of the wrong data, so the tokenizer is pinned here. Also
    # covers the HDF5 round trip (VolumetricData::saveHdf5/loadHdf5) and, via
    # MarchingCubes.cpp, that an .h5-loaded grid extracts the same isosurface
    # as its source file.
    add_executable(calango_volumetric_parse_test
        tests/VolumetricParseTest.cpp
        src/core/VolumetricData.cpp
        src/core/MarchingCubes.cpp
        src/core/Structure.cpp
        src/core/UnitCell.cpp
        src/core/Element.cpp
    )
    target_include_directories(calango_volumetric_parse_test PRIVATE src)
    calango_hdf5_link(calango_volumetric_parse_test)
    add_test(NAME volumetric_parse COMMAND calango_volumetric_parse_test)

    # Periodic-image bound: the degenerate-cell guard that five of the seven
    # former private copies of imageRange() were missing.
    add_executable(calango_periodic_images_test
        tests/PeriodicImagesTest.cpp
        src/core/UnitCell.cpp
    )
    target_include_directories(calango_periodic_images_test PRIVATE src)
    add_test(NAME periodic_image_range COMMAND calango_periodic_images_test)

    # Adaptive common-neighbour analysis: the ideal fcc / hcp / bcc / diamond
    # lattices classify completely, and disordered ones classify as nothing.
    # Pins the third CNA index in particular (largest connected bond cluster,
    # not longest simple path) — the two agree for fcc/hcp and disagree for
    # bcc/icosahedral.
    add_executable(calango_structural_phase_test
        tests/StructuralPhaseTest.cpp
        src/core/StructuralPhase.cpp
        src/core/Structure.cpp
        src/core/UnitCell.cpp
        src/core/Element.cpp
    )
    target_include_directories(calango_structural_phase_test PRIVATE src)
    add_test(NAME structural_phase COMMAND calango_structural_phase_test)

    # Random Noise Setup's perturbation core: the linear-ramp amplitude
    # formula (pure arithmetic, pinned exactly) and applyRandomNoise's seed
    # reproducibility, which the ramp has to compose with.
    add_executable(calango_noise_test
        tests/NoiseTest.cpp
        src/core/Noise.cpp
        src/core/Structure.cpp
        src/core/UnitCell.cpp
        src/core/Element.cpp
    )
    target_include_directories(calango_noise_test PRIVATE src)
    add_test(NAME noise_ramp COMMAND calango_noise_test)

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

    # Same, for the ternary (composition-triangle) generalization backing
    # Task 6's ternary Cluster Expansion ground-state map.
    add_executable(calango_ternary_convex_hull_test
        tests/TernaryConvexHullTest.cpp
        src/core/TernaryConvexHull.cpp
    )
    target_include_directories(calango_ternary_convex_hull_test PRIVATE src)
    add_test(NAME ternary_convex_hull COMMAND calango_ternary_convex_hull_test)

    # Plot-generation smoke test for the native ternary Cluster Expansion
    # widget (Task 6.3) — real enumeration + hull, rendered, checked for ink.
    qt_add_executable(calango_ternary_cluster_hull_widget_test
        tests/TernaryClusterHullWidgetTest.cpp
        src/gui/TernaryClusterHullWidget.cpp
        src/gui/GuiUtils.cpp
        src/core/TernaryConvexHull.cpp
        src/core/ClusterExpansion.cpp
        src/core/Structure.cpp
        src/core/UnitCell.cpp
        src/core/Element.cpp
    )
    target_include_directories(calango_ternary_cluster_hull_widget_test PRIVATE src)
    target_link_libraries(calango_ternary_cluster_hull_widget_test
        PRIVATE Qt6::Widgets)
    add_test(NAME ternary_cluster_hull_widget
             COMMAND calango_ternary_cluster_hull_widget_test)

    # Deterministic train/validation/test partitioning behind both the
    # standalone Dataset Manager dialog and the Orchestration Dataset
    # Manager node — makeStratified()'s isolated-atom pinning in particular.
    add_executable(calango_dataset_split_test
        tests/DatasetSplitTest.cpp
        src/core/DatasetSplit.cpp
    )
    target_include_directories(calango_dataset_split_test PRIVATE src)
    add_test(NAME dataset_split COMMAND calango_dataset_split_test)

    # Automated band-gap detection (direct / indirect / metallic) behind the
    # Electronic Structure summary box.
    add_executable(calango_bandgap_test
        tests/BandGapTest.cpp
        src/core/BandGap.cpp
    )
    target_include_directories(calango_bandgap_test PRIVATE src)
    add_test(NAME band_gap COMMAND calango_bandgap_test)

    # Post-launch parallelism verification (Task 1, 2026-08-22): parses
    # gpaw.out's own "cores:  N" line so a launch bug that slips past the
    # pre-flight check (GpawMpiPreflight.hpp) is still caught against the
    # run's own account of itself.
    add_executable(calango_gpaw_output_parser_test
        tests/GpawOutputParserTest.cpp
        src/core/GpawOutputParser.cpp
    )
    target_include_directories(calango_gpaw_output_parser_test PRIVATE src)
    add_test(NAME gpaw_output_parser COMMAND calango_gpaw_output_parser_test)

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

    # LDOS end-to-end on H2O: runs the GENERATED script (calango_script_test
    # --dump-ldos) against a real baseline .gpw, twice — once with a window
    # spanning every occupied state (checked against the valence electron
    # count, up to the known spin-degeneracy factor of 2) and once bracketing
    # only the HOMO (checked, by water's exact C2v symmetry, to be negligible
    # on both hydrogens and concentrated on oxygen). Self-skips without GPAW.
    add_test(NAME ldos_h2o
        COMMAND ${Python3_EXECUTABLE}
                ${CMAKE_CURRENT_SOURCE_DIR}/tests/ldos_h2o_benchmark.py
                $<TARGET_FILE:calango_script_test>)

    # Energy Diagrams end-to-end: H2O for HOMO/LUMO identification and
    # transition-dipole discrimination, NH3 (C3v) for degeneracy detection
    # on its doubly-degenerate 'e' valence level. Self-skips without GPAW.
    add_test(NAME energy_diagram_benchmark
        COMMAND ${Python3_EXECUTABLE}
                ${CMAKE_CURRENT_SOURCE_DIR}/tests/energy_diagram_benchmark.py
                $<TARGET_FILE:calango_script_test>)

    # Wavefunctions end-to-end on H2O: the HOMO is nonzero, correctly
    # located (same C2v symmetry argument as LDOS), normalized to ~1, and
    # the pseudo/all-electron reconstructions genuinely differ near the
    # nuclei while agreeing away from them. Self-skips without GPAW.
    add_test(NAME wavefunctions_h2o
        COMMAND ${Python3_EXECUTABLE}
                ${CMAKE_CURRENT_SOURCE_DIR}/tests/wavefunctions_h2o_benchmark.py
                $<TARGET_FILE:calango_script_test>)

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
    # Slow (~4 min): it needs a 36x36 high-symmetry
    # k-mesh to resolve the Dirac cone. A coarser mesh does cross pi*alpha, but
    # at the wrong energy and for the wrong reason, so it cannot be cheapened
    # without making the test dishonest. Self-skips without GPAW.
    add_test(NAME graphene_tetrahedron
        COMMAND ${Python3_EXECUTABLE}
                ${CMAKE_CURRENT_SOURCE_DIR}/tests/graphene_tetrahedron_benchmark.py
                $<TARGET_FILE:calango_script_test>)
    set_tests_properties(graphene_tetrahedron PROPERTIES TIMEOUT 1800)

    # 2D Bands (Task 3's GPAW -> +VASP/QE/SIESTA extension), RUN against
    # real graphene on Quantum ESPRESSO — the one of the three new engines
    # this machine can run without a license (VASP) or a broken local
    # binary (siesta itself SIGKILLs unconditionally here, confirmed
    # separately from anything this generator emits). Small grid, low
    # cutoff: this is a generator/pipeline correctness check against a real
    # pw.x, not a converged DFT result. Self-skips without pw.x, an
    # ase-capable interpreter, or a carbon pseudopotential — all of which
    # are this machine's own local installs, not build/runtime dependencies.
    add_test(NAME graphene_2d_bands_qe
        COMMAND ${Python3_EXECUTABLE}
                ${CMAKE_CURRENT_SOURCE_DIR}/tests/graphene_2d_bands_qe_benchmark.py
                $<TARGET_FILE:calango_script_test>)
    set_tests_properties(graphene_2d_bands_qe PROPERTIES TIMEOUT 600)

    # Same, for SIESTA (see graphene_2d_bands_qe's own comment for VASP).
    # Self-skips without a siesta binary that actually runs `siesta
    # --version` (some local installs SIGKILL unconditionally; only a
    # binary that passes that check is used — see the script's own
    # find_siesta_bin()), an ase-capable interpreter, or a carbon
    # pseudopotential.
    add_test(NAME graphene_2d_bands_siesta
        COMMAND ${Python3_EXECUTABLE}
                ${CMAKE_CURRENT_SOURCE_DIR}/tests/graphene_2d_bands_siesta_benchmark.py
                $<TARGET_FILE:calango_script_test>)
    set_tests_properties(graphene_2d_bands_siesta PROPERTIES TIMEOUT 600)

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

    # xTB, RUN rather than compiled. A water molecule is milliseconds, so the
    # generated script can be executed for real — which is the only way to see
    # a parameter the ASE calculator quietly ignores, or the failure this test
    # was written for: xtb-python evaluates isolated systems only, and GFN-FF
    # answers a 2D-periodic cell with SIGSEGV rather than an exception. Also
    # pins that GFN2/GFN1/GFN-FF give three DIFFERENT energies, since equal
    # ones would mean `method=` never reached the calculator.
    #
    # Self-skips without an interpreter that has xtb — which is the normal case
    # here: xtb-python is conda-forge only and builds no wheel for the newest
    # CPython, so the embedded interpreter usually cannot host it. Point
    # CALANGO_XTB_PYTHON at one that can.
    add_test(NAME xtb_integration
        COMMAND ${Python3_EXECUTABLE}
                ${CMAKE_CURRENT_SOURCE_DIR}/tests/xtb_integration_test.py
                $<TARGET_FILE:calango_script_test>)
    set_tests_properties(xtb_integration PROPERTIES TIMEOUT 1800)

    # Hydroxyls antiposition, RUN through a real MDMC trajectory on plain
    # Lennard-Jones (no MACE/GPAW/xTB needed): independently re-derives
    # pairing from geometry, on every frame, and checks it never breaks —
    # the bug this generator used to have. Self-skips without an interpreter
    # that has ase.
    add_test(NAME graphene_oxide_mdmc_antiposition
        COMMAND ${Python3_EXECUTABLE}
                ${CMAKE_CURRENT_SOURCE_DIR}/tests/graphene_oxide_mdmc_antiposition_test.py
                $<TARGET_FILE:calango_script_test>)
    set_tests_properties(graphene_oxide_mdmc_antiposition PROPERTIES TIMEOUT 600)

    # Ring/percolation analysis against that SAME kind of real MDMC
    # trajectory (plain Lennard-Jones, no MD burst): unlike
    # GrapheneOxidePercolationTest.cpp's closed-form fixtures, this is a
    # moving target — the carbon lattice topology and the total
    # functionalized-carbon count are conserved by construction (MDMC only
    # relocates existing groups), so ring-count == N/2 and sp2 fraction ==
    # constant on EVERY frame are exact, closed-form checks against a
    # trajectory this test did not hand-construct. Self-skips without an
    # interpreter that has ase.
    add_test(NAME graphene_oxide_percolation_mdmc
        COMMAND ${Python3_EXECUTABLE}
                ${CMAKE_CURRENT_SOURCE_DIR}/tests/graphene_oxide_percolation_mdmc_test.py
                $<TARGET_FILE:calango_script_test>
                $<TARGET_FILE:calango-ring-percolation-analyze>)
    set_tests_properties(graphene_oxide_percolation_mdmc PROPERTIES TIMEOUT 600)

    # The protocol itself, on a REAL potential (MACE-MP-0 small, CPU): the
    # two Lennard-Jones harnesses above run with the dynamics OFF, which left
    # the only relaxation mechanism the module has untested — and it was the
    # initial burst that killed the first real run (the as-built flat sheet's
    # strain released as a thermal shock opened an epoxide, exit 1 before any
    # cycle). Builds a sheet with that exact contact planted, runs
    # equilibration + cycles under NPT, and re-derives the inventory, the
    # antiposition pair, the unscaled vacuum axis and the boundary C-C bonds
    # from raw geometry on every frame. Self-skips without an interpreter
    # that has mace, or without the cached checkpoint (never downloads).
    add_test(NAME graphene_oxide_mdmc_mace
        COMMAND ${Python3_EXECUTABLE}
                ${CMAKE_CURRENT_SOURCE_DIR}/tests/graphene_oxide_mdmc_mace_test.py
                $<TARGET_FILE:calango_script_test>)
    set_tests_properties(graphene_oxide_mdmc_mace PROPERTIES TIMEOUT 1800)

    # Edge-group moves on a nanoflake (Lennard-Jones, dynamics off): the
    # rim's hydrogen inventory is conserved — an edge move swaps the
    # terminating H to the vacated carbon rather than landing the group on
    # top of it and leaving a dangling bond behind. Re-derived from raw
    # geometry on every frame. Self-skips without an interpreter that has ase.
    add_test(NAME graphene_oxide_mdmc_flake
        COMMAND ${Python3_EXECUTABLE}
                ${CMAKE_CURRENT_SOURCE_DIR}/tests/graphene_oxide_mdmc_flake_test.py
                $<TARGET_FILE:calango_script_test>)
    set_tests_properties(graphene_oxide_mdmc_flake PROPERTIES TIMEOUT 600)

    # DSIM vs. its own reference implementation (oncapintada), live: EMT-relax
    # a Cu/Pd 27-atom fcc supercell case, run it through oncapintada, check
    # oncapintada itself against the recorded fixture DsimTest.cpp also
    # carries (a regression guard on the reference, not just on Calango), and
    # run calango_dsim_test alongside it so both halves of the cross-check
    # show up in one command. Development-time only: self-skips without an
    # interpreter that has oncapintada, which is nowhere but the author's own
    # 'onca' conda environment (never a Calango build/runtime dependency).
    add_test(NAME dsim_oncapintada
        COMMAND ${Python3_EXECUTABLE}
                ${CMAKE_CURRENT_SOURCE_DIR}/tests/dsim_oncapintada_test.py
                $<TARGET_FILE:calango_dsim_test>)
    set_tests_properties(dsim_oncapintada PROPERTIES TIMEOUT 600)

    # Live optical broadening. eta is a LIFETIME inside the response function,
    # not a resolution blur, so it cannot be left out of the calculation the way
    # a DOS smearing can — but Lorentzian widths ADD under convolution, so the
    # viewer can raise it afterwards and that is exact. Checked against the
    # analytic two-level resolvent, because a broadening that is merely
    # plausible produces a smooth, convincing, wrong spectrum. Also pins that
    # every derived curve (n/k, absorption, the sheet observables) follows the
    # broadened epsilon instead of going stale.
    qt_add_executable(calango_optics_broadening_test
        tests/OpticsBroadeningTest.cpp
        src/gui/OpticsResultsWindow.cpp
        src/gui/OpticsPlotStyleDialog.cpp
        src/gui/SpectrumPlotWidget.cpp
        src/gui/GuiUtils.cpp
        src/gui/ThemeManager.cpp
        src/core/Element.cpp
        src/core/UnitCell.cpp
    )
    target_include_directories(calango_optics_broadening_test PRIVATE src
        "${CMAKE_CURRENT_BINARY_DIR}/generated")
    target_compile_definitions(calango_optics_broadening_test PRIVATE
        QT_NO_KEYWORDS)
    target_link_libraries(calango_optics_broadening_test
        PRIVATE Qt6::Widgets Qt6::Svg)
    add_test(NAME optics_broadening
        COMMAND calango_optics_broadening_test)

    # Live XAS broadening in the viewer — XasResultsWindow::foldSticks()
    # against the closed form of a single Gaussian (peak height, FWHM, total
    # area) and against linear superposition for several transitions at once.
    # The XAS analogue of optics_broadening above, but simpler to verify: a
    # stick is a delta function with no stored width to deconvolve, so
    # foldSticks() is a fresh forward computation at any FWHM rather than a
    # convolution constrained to only widen.
    qt_add_executable(calango_xas_broadening_test
        tests/XasBroadeningTest.cpp
        src/gui/XasResultsWindow.cpp
        src/gui/OpticsPlotStyleDialog.cpp
        src/gui/SpectrumPlotWidget.cpp
        src/gui/GuiUtils.cpp
        src/core/Element.cpp
        src/core/UnitCell.cpp
    )
    target_include_directories(calango_xas_broadening_test PRIVATE src
        "${CMAKE_CURRENT_BINARY_DIR}/generated")
    target_compile_definitions(calango_xas_broadening_test PRIVATE
        QT_NO_KEYWORDS)
    target_link_libraries(calango_xas_broadening_test
        PRIVATE Qt6::Widgets Qt6::Svg)
    add_test(NAME xas_broadening
        COMMAND calango_xas_broadening_test)

    # Double groups for spin-orbit-coupled bands. Everything downstream of the
    # wave functions is pure group theory with exact expected answers — the Oh
    # double group is order 96 with spinor irreps of dimension 2, 2, 4 per
    # parity — so it is checked against the textbook tables rather than left to
    # a GPAW run nobody will do on every commit. The Python under test is
    # extracted from the C++ that emits it, so the test cannot drift away from
    # what a job actually executes. Self-skips without numpy.
    add_test(NAME double_group
        COMMAND ${Python3_EXECUTABLE}
                ${CMAKE_CURRENT_SOURCE_DIR}/tests/double_group_test.py)

    # The 2D image-charge correction, checked against limits with closed
    # forms: the isolated Gaussian self-energy, the 1/L and 1/eps scalings,
    # and — the one neither half passes alone — the simple-cubic Madelung
    # constant, which is what shows the periodic and isolated solves share a
    # normalization and so can be subtracted. There is no pymatgen equivalent
    # for the 2D case to delegate to, so this is the only thing standing
    # between a wrong prefactor and a plausible-looking defect diagram.
    add_test(NAME defect_2d_correction
        COMMAND ${Python3_EXECUTABLE}
                ${CMAKE_CURRENT_SOURCE_DIR}/tests/defect_2d_correction_test.py)

    # The same module end to end: the GENERATED script against two real .gpw
    # baselines (pristine h-BN monolayer + N vacancy), checking that GPAW
    # accepts a charged slab on restart and that the diagram it writes is one
    # DefectDiagramWindow can read. Complements defect_2d_correction above --
    # that one checks the physics without GPAW, this one the plumbing with it.
    # Self-skips (exit 0) without GPAW.
    add_test(NAME defect_2d_monolayer
        COMMAND ${Python3_EXECUTABLE}
                ${CMAKE_CURRENT_SOURCE_DIR}/tests/defect_2d_monolayer_benchmark.py
                $<TARGET_FILE:calango_script_test>)

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

    # Raman / IR on VASP and Quantum ESPRESSO: the two routes that read their
    # answer out of somebody else's text format. Neither code is available
    # here, so the parsers are driven against realistic OUTCAR / .dyn
    # fragments whose right answers are known by construction. Every failure
    # mode they have is silent — a flipped Hessian sign draws a complete
    # spectrum at negative wavenumbers, and the wrong one of VASP's three
    # dielectric blocks is a tens-of-percent error inside a derivative.
    add_test(NAME raman_ir_parsers
        COMMAND ${Python3_EXECUTABLE}
                ${CMAKE_CURRENT_SOURCE_DIR}/tests/raman_ir_parsers_test.py
                $<TARGET_FILE:calango_script_test>)

    # Nonlinear optics: the gpaw.nlopt call surface (real function names and
    # keyword arguments in someone else's package, reached only after a ground
    # state has been converged) and the unit conversions from the SI base
    # units GPAW returns to the pm/V and nm^2/V the literature quotes. Also
    # the inversion-symmetry test, which decides whether the run means
    # anything at all. No GPAW required.
    add_test(NAME nlopt_parser
        COMMAND ${Python3_EXECUTABLE}
                ${CMAKE_CURRENT_SOURCE_DIR}/tests/nlopt_parser_test.py
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

    # Wannier spread validation on bulk FCC Cu, the canonical reference. The
    # d-like spreads have a literature scale (<= ~1 A^2) that a units or
    # b-vector-weight error cannot survive, so this pins the wannierization
    # rather than the settings. Self-skips (exit 0) without GPAW.
    add_test(NAME cu_wannier_spread
        COMMAND ${Python3_EXECUTABLE}
                ${CMAKE_CURRENT_SOURCE_DIR}/tests/cu_wannier_spread_benchmark.py)
    set_tests_properties(cu_wannier_spread PROPERTIES TIMEOUT 3600)

    # The VASP-interface sibling of the above: no VASP-side wannierization
    # of its own exists to validate (VASP's Wannier90 library does that
    # internally), so this instead proves the parser + JSON schema
    # (WannierScriptGenerator.cpp's generateVaspWannier90Script(), reading
    # via ASE's own read_wout_all()) against a wout-format-correct Cu
    # fixture carrying cu_wannier_spread_benchmark.py's own real, previously
    # -measured spreads. Self-skips (exit 0) without ase. Seconds, not an
    # hour: no DFT of any kind runs here.
    add_test(NAME cu_wannier_vasp_fixture
        COMMAND ${Python3_EXECUTABLE}
                ${CMAKE_CURRENT_SOURCE_DIR}/tests/cu_wannier_vasp_fixture_test.py)

# Optics validation on bulk Si: GPAW SCF → NSCF → DielectricFunction,
    # verifying ε₁/ε₂. Self-skips (exit 0) without the GPAW response stack.
    add_test(NAME optics_silicon
        COMMAND ${Python3_EXECUTABLE}
                ${CMAKE_CURRENT_SOURCE_DIR}/tests/optics_silicon_benchmark.py)

    # The metallic half of the optics pipeline: the intraband (Drude) term the
    # generator now emits for every system. optics_silicon covers the gapped
    # case, where GPAW's `gs.metallic and intraband` gate makes it a no-op —
    # only a metal exercises the term itself.
    #
    # FCC Au is chosen for a reference the calculation cannot influence: one
    # nearly-free 6s carrier per atom gives ħω_p = 9.02 eV, and the sweep lands
    # at 9.04. It also documents WHY the k-mesh matters here, which no other
    # test does: ω_p is a Fermi-surface integral, so under point integration it
    # runs 15.2 → 16.2 → 11.8 → 9.6 eV over 4³ → 12³ — non-monotonic, with 8³
    # and 10³ agreeing to 0.6 % while both are 30 % wrong. Tetrahedron
    # integration is within 2 % from 4³ onward.
    #
    # 5 of the 8 meshes need the qsymmetry=False fallback, so this is also the
    # end-to-end check that the fallback works on a real cell rather than only
    # appearing in the generated text.
    #
    # ~75 s: one atom per cell, seconds per mesh. Self-skips without GPAW.
    add_test(NAME au_plasma_convergence
        COMMAND ${Python3_EXECUTABLE}
                ${CMAKE_CURRENT_SOURCE_DIR}/tests/au_plasma_convergence_benchmark.py)
    set_tests_properties(au_plasma_convergence PROPERTIES TIMEOUT 1800)

    # --- Documentation -----------------------------------------------------
    #
    # The docs are the half of this project no compiler checks. These three are
    # pure Python with no build dependency, so they cost nothing to keep in the
    # default run, and each pins a failure mode that is otherwise invisible
    # until a reader hits it.
    #
    # There is deliberately no LaTeX build test. `make -C docs/tex` writes its
    # PDFs back into the tracked source tree, so running it from ctest would
    # leave the working tree dirty after every test run — and the guide's cover
    # version, the one thing that can actually drift, is covered by the
    # injection check in docs_version_sync without building anything.
    add_test(NAME docs_version_sync
        COMMAND ${Python3_EXECUTABLE}
                ${CMAKE_CURRENT_SOURCE_DIR}/tests/docs_version_sync_test.py)
    add_test(NAME docs_ui_labels
        COMMAND ${Python3_EXECUTABLE}
                ${CMAKE_CURRENT_SOURCE_DIR}/tests/docs_ui_labels_test.py)
    # Builds into the binary dir, and self-skips where Sphinx is absent.
    add_test(NAME docs_sphinx_build
        COMMAND ${Python3_EXECUTABLE}
                ${CMAKE_CURRENT_SOURCE_DIR}/tests/docs_sphinx_build_test.py)
    set_tests_properties(docs_sphinx_build PROPERTIES
        ENVIRONMENT "CALANGO_DOC_BUILD_DIR=${CMAKE_CURRENT_BINARY_DIR}/docs-test"
        TIMEOUT 900)

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

    # --- Test labels -------------------------------------------------------
    #
    # Two INDEPENDENT axes, because the two questions a developer asks about a
    # test are independent:
    #
    #   what does it cover?     visual | calc | docs
    #   what does it cost?      core (the only tier — see below)
    #
    # Every test carries exactly one label from each axis, so the sets compose:
    #
    #   ctest -L visual              rendering, viewport, meshing, UI panels
    #   ctest -L calc               physics, numerics, builders, parsers
    #   ctest -L docs               doc builds and doc/UI consistency
    #   ctest -L core               everything (the cost axis's only tier)
    #   ctest -L visual -L core     intersection: the fast display tests
    #   ctest                       all of it
    #
    # --- The subject axis ---------------------------------------------------
    #
    # Assignment is by PRIMARY subject, which is the only rule that survives
    # contact with a codebase where nearly everything is eventually drawn. A
    # bond-perception test is `calc` even though its output is a cylinder on
    # screen: what it asserts is the chemistry, and a change to the renderer
    # cannot break it. A volumetric-parser test is `calc` for the same reason
    # even though the isosurface is the reason anyone parses the file.
    #
    # The lists are EXHAUSTIVE and a test in none of them is a configure-time
    # error, not a silent default. A default would quietly file every new test
    # under whichever bucket was chosen as the fallback, and the split would
    # rot within a release.
    set(CALANGO_VISUAL_TESTS
        # Rendering and the render paths that leave the viewport.
        alembic_export          # scene → Alembic geometry cache
        isosurface_mesh         # marching tetrahedra, welding, continuation
        raytrace_scene_files    # POV-Ray / Tachyon scene emission
        shader_compilation      # every GLSL program compiles and links
        translucent_render      # renders real frames, compares pixels
        # Camera, film and the animation timeline.
        camera                  # orbit conventions, plane alignment, roll
        film_timeline           # shot timing, transitions, ping-pong order
        # Widgets, docks and dialogs.
        dialog_construction     # every dialog builds and survives being driven
        file_association        # QFileOpenEvent routing/queueing, format hints
        results_viewers         # result-file viewers and their JSON schemas
        overlay_model           # "Additional overlays" tessellation + ranges
        visible_spectrum        # the optics viewer's spectrum gradient, by pixel
        gpu_telemetry           # the invariant the status bar reads
        # Display assets.
        icon_registry           # every icon used is bundled (a blank button
                                # is silent otherwise)
        icon_theme              # theme-aware tinting
        ternary_cluster_hull_widget # native ternary CE plot renders ink
        theme_round_trip        # tooltip colours survive a Light<->Dark round trip
        element_colors)         # the CPK palette and user overrides

    set(CALANGO_DOCS_TESTS
        docs_version_sync       # one version string, everywhere a reader sees it
        docs_ui_labels          # the manual may not name a control that is gone
        docs_sphinx_build)      # the site still builds

    # Everything else: physics, numerics, crystallography, parsers of
    # calculation output, script generation, and the job/orchestration
    # plumbing that runs calculations.
    set(CALANGO_CALC_TESTS
        adsorption_nanoparticle au_plasma_convergence
        band_gap band_unfolding baseline_summary berry_phase
        boltzmann_transport bond_perception brillouin_zone_clip bse_solver c2db calphad cluster_expansion
        cluster_expansion_fit cluster_presets cluster_variation convex_hull
        cpa_solver crpa_solver crpa_vasp_validation cu_wannier_spread
        cu_wannier_vasp_fixture
        ternary_convex_hull
        dataset_split egqca
        defect_2d_correction defect_2d_monolayer
        dislocation_builder dos_smearing double_group dsim dsim_oncapintada elastic_tensor elf_silicon
        extxyz_attributes frame_streaming
        generated_ase_scripts generated_script_lint gpaw_output_parser graphene_band_symmetry
        graphene_2d_bands_qe graphene_2d_bands_siesta
        graphene_oxide graphene_oxide_group_analysis graphene_oxide_mdmc_antiposition
        graphene_oxide_mdmc_mace graphene_oxide_mdmc_flake
        graphene_oxide_pair_correlation
        graphene_oxide_percolation
        graphene_oxide_percolation_mdmc graphene_oxide_sites graphene_tetrahedron
        energy_diagram_benchmark
        gpaw_mpi_preflight
        gw_qp_parser gw_silicon hubbard_linear_response hydrogen_bonds
        hydrogen_completion ice_builder kpath_table lammps_environment
        lammps_scripts ldos_h2o mace_trainer_preflight magnetic_space_group materials_project_mp1434
        molecular_design
        monte_carlo_swap nlopt_parser noise_ramp
        optics_2d_observables optics_broadening
        optics_silicon orchestration_batch orchestration_handoff
        orchestration_si_pipeline partial_charge_si periodic_image_range
        phonon_diamond phonon_thermodynamics piezoelectric_tensor
        polymer_builder raman_ir_math
        raman_ir_mgo raman_ir_parsers raman_si_diamond remote_session
        remote_session_helper run_commands scheduler_script shortcut_registry solid_interface_builder
        solvation_builder sqs_generator sqs_sige_alloy strain_voigt
        structural_phase
        structure_transforms sublattice_cluster_variation
        symmetry_si_diamond tdb_database
        thermodynamic_integration vasp_potcar_preflight vibrational_modes
        volumetric_parse
        wannier_interpolation_silicon wannier_preflight wannier_silicon
        wavefunctions_h2o
        workflow_report xas_broadening xtb_integration)

    set(_calango_unlabelled)
    foreach(_test IN LISTS _calango_all_tests)
        if(_test IN_LIST CALANGO_VISUAL_TESTS)
            set_property(TEST ${_test} APPEND PROPERTY LABELS "visual")
        elseif(_test IN_LIST CALANGO_CALC_TESTS)
            set_property(TEST ${_test} APPEND PROPERTY LABELS "calc")
        elseif(_test IN_LIST CALANGO_DOCS_TESTS)
            set_property(TEST ${_test} APPEND PROPERTY LABELS "docs")
        else()
            list(APPEND _calango_unlabelled ${_test})
        endif()
    endforeach()
    if(_calango_unlabelled)
        message(FATAL_ERROR
            "These tests belong to no subject group: ${_calango_unlabelled}\n"
            "Add each to exactly one of CALANGO_VISUAL_TESTS, "
            "CALANGO_CALC_TESTS or CALANGO_DOCS_TESTS in CMakeLists.txt. "
            "Assign by the test's PRIMARY subject: what it asserts, not what "
            "eventually consumes the result.")
    endif()

    # --- The cost axis --------------------------------------------------
    #
    #   ctest -L core     every test (~2 min)
    #
    # Used to be two tiers: `core` (everything, milliseconds each) and `dft`
    # (the native DFT engine's real self-consistent all-electron runs, real
    # minutes each — the only thing in the suite expensive enough to need
    # opting into separately). The native DFT and DFTB engines were removed
    # entirely (see git history), taking the `dft` tier's only members with
    # them, so every test is `core` now — the label still exists (rather
    # than being removed) so a future genuinely slow test has an established
    # place to reintroduce a second tier without inventing a new convention.
    foreach(_test IN LISTS _calango_all_tests)
        set_property(TEST ${_test} APPEND PROPERTY LABELS "core")
    endforeach()
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

# The offline manual that Help → User Guide opens. MainWindow::openUserGuide()
# looks for it relative to applicationDirPath(), so the two destinations below
# are the ones it probes — keep them in step with kCandidates there.
#
# OPTIONAL on purpose: the PDF is a build product of the LaTeX sources, and a
# checkout whose copy is a cloud placeholder (or a CI job with no TeX) must
# still install. The menu item degrades to the online manual when it is absent,
# so a missing file costs a fallback, not a broken build.
set(CALANGO_USER_GUIDE_PDF
    "${CMAKE_CURRENT_SOURCE_DIR}/docs/tex/user_guide/calango_user_guide.pdf")

if(APPLE AND CALANGO_MACOS_BUNDLE)
    install(TARGETS calango BUNDLE DESTINATION .)

    install(FILES "${CALANGO_USER_GUIDE_PDF}"
            DESTINATION "calango.app/Contents/Resources/doc" OPTIONAL)

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

    # macdeployqt rewrites a copied library's own LC_LOAD_DYLIB entries to
    # @executable_path-relative but leaves its ORIGINAL build-machine
    # LC_RPATH search-path entries in place. calango 26.8.36 shipped a
    # bundled libopenblas.0.dylib carrying three absolute Homebrew GCC
    # rpaths; on the build machine those happened to be how dyld's chained
    # rpath search found libgcc_s.1.1.dylib (a dependency of the
    # also-bundled libgfortran.5.dylib, itself never actually copied into
    # the bundle) — on a clean Mac without that exact Homebrew install,
    # every one of those paths misses and the app aborts at launch before
    # main() runs (see packaging/README.md's "BLAS/LAPACK backend"
    # section). The real fix for THAT specific chain is CALANGO_BLAS
    # defaulting to Accelerate above (a system framework needing none of
    # it); this strips the same class of leftover rpath from every OTHER
    # bundled library too — auditing the same .dmg also found it on
    # libdbus-1.3.dylib, libjasper.7.dylib and libjpeg.8.dylib, none of
    # them BLAS-related. Must run before the final codesign below:
    # install_name_tool invalidates a Mach-O's signature exactly like the
    # comment there already says stripping Python-framework paths does.
    install(CODE "
        execute_process(
            COMMAND bash
                \"${CMAKE_CURRENT_SOURCE_DIR}/packaging/macos/strip_leftover_rpaths.sh\"
                \"\${CMAKE_INSTALL_PREFIX}/calango.app\"
            COMMAND_ERROR_IS_FATAL ANY)
    ")

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
    # OPTIONAL and un-bundled on macOS: a standalone analysis convenience,
    # not something the GUI ever launches itself.
    install(TARGETS calango-ring-percolation-analyze
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR} OPTIONAL)

    # Help → User Guide; see the note beside CALANGO_USER_GUIDE_PDF above.
    install(FILES "${CALANGO_USER_GUIDE_PDF}"
            DESTINATION ${CMAKE_INSTALL_DATADIR}/calango/doc OPTIONAL)

    # Desktop integration: launcher entry, MIME types for .calproj project
    # files and .extxyz structures (both reach MainWindow::loadFile via the
    # `Exec=calango %F` argv path), and icons.
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
elseif(WIN32)
    # Bare-binary install; Qt runtime deployment (windeployqt) is not wired
    # up here — the NSIS packaging below exists chiefly to carry the file
    # associations once a Windows pipeline is stood up.
    install(TARGETS calango RUNTIME DESTINATION bin)
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
        # python3-xtb is Debian's binding for the xtb library and is what the
        # in-process GFN calculator imports; on a distribution without it the
        # pip name is `xtb` (see packaging/dependencies.txt). Recommended
        # rather than depended on: xTB is one engine among many, and the app is
        # perfectly usable without it.
        # One string, not two: set(VAR "a" "b") builds the LIST "a;b", which
        # would reach the control file with a semicolon in the middle of the
        # Recommends field.
        set(CPACK_DEBIAN_PACKAGE_RECOMMENDS
            "python3-ase, python3-imageio, python3-dftd4, python3-phonopy, python3-xtb")
        # ffmpeg: the system encoder the video writers fall back to when
        # imageio-ffmpeg's bundled binary is absent (see AnimationExporter).
        # python3-alembic is NOT listed and must not be: on Debian that name is
        # SQLAlchemy's database-migration tool, which shares an import name with
        # PyAlembic and nothing else. The .abc export needs conda-forge's
        # `pyalembic`; see packaging/dependencies.txt.
        set(CPACK_DEBIAN_PACKAGE_SUGGESTS "ffmpeg")
    endif()
    # Refresh MIME/desktop/icon caches on install and removal.
    set(CPACK_DEBIAN_PACKAGE_CONTROL_EXTRA
        "${CMAKE_CURRENT_SOURCE_DIR}/packaging/linux/postinst;${CMAKE_CURRENT_SOURCE_DIR}/packaging/linux/postrm")
    set(CPACK_DEBIAN_PACKAGE_CONTROL_STRICT_PERMISSION TRUE)
elseif(WIN32)
    # NSIS installer carrying the .extxyz / .calproj associations. The keys
    # follow the documented HKCR ProgID pattern (extension -> ProgID ->
    # DefaultIcon + shell\open\command); SHChangeNotify(SHCNE_ASSOCCHANGED)
    # tells Explorer to drop its association cache so the icons and
    # double-click behavior update without a logoff. Explorer passes the
    # double-clicked path on argv, which main() already loads. NOTE: there is
    # no Windows CI exercising this branch yet — validate the first build by
    # hand (see also the windeployqt caveat at the install() above).
    set(CPACK_GENERATOR "NSIS")
    set(CPACK_NSIS_DISPLAY_NAME "Calango")
    set(CPACK_NSIS_PACKAGE_NAME "Calango")
    set(CPACK_NSIS_ENABLE_UNINSTALL_BEFORE_INSTALL ON)
    set(CPACK_NSIS_EXECUTABLES_DIRECTORY "bin")
    set(CPACK_PACKAGE_EXECUTABLES "calango;Calango")
    set(CPACK_NSIS_EXTRA_INSTALL_COMMANDS "
        WriteRegStr HKCR '.calproj' '' 'Calango.calproj'
        WriteRegStr HKCR '.calproj' 'Content Type' 'application/x-calango-project'
        WriteRegStr HKCR 'Calango.calproj' '' 'Calango project'
        WriteRegStr HKCR 'Calango.calproj\\\\DefaultIcon' '' '$INSTDIR\\\\bin\\\\calango.exe,0'
        WriteRegStr HKCR 'Calango.calproj\\\\shell\\\\open\\\\command' '' '\\\"$INSTDIR\\\\bin\\\\calango.exe\\\" \\\"%1\\\"'
        WriteRegStr HKCR '.extxyz' '' 'Calango.extxyz'
        WriteRegStr HKCR '.extxyz' 'Content Type' 'application/x-extxyz'
        WriteRegStr HKCR 'Calango.extxyz' '' 'Extended XYZ atomic structure'
        WriteRegStr HKCR 'Calango.extxyz\\\\DefaultIcon' '' '$INSTDIR\\\\bin\\\\calango.exe,0'
        WriteRegStr HKCR 'Calango.extxyz\\\\shell\\\\open\\\\command' '' '\\\"$INSTDIR\\\\bin\\\\calango.exe\\\" \\\"%1\\\"'
        System::Call 'shell32::SHChangeNotify(i 0x08000000, i 0, i 0, i 0)'
    ")
    set(CPACK_NSIS_EXTRA_UNINSTALL_COMMANDS "
        DeleteRegKey HKCR 'Calango.calproj'
        DeleteRegKey HKCR '.calproj'
        DeleteRegKey HKCR 'Calango.extxyz'
        DeleteRegKey HKCR '.extxyz'
        System::Call 'shell32::SHChangeNotify(i 0x08000000, i 0, i 0, i 0)'
    ")
endif()

include(CPack)
