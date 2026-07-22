cmake_minimum_required(VERSION 3.21)

project(calango
    VERSION 26.7.29
    DESCRIPTION "Calango — atomistic modeling and simulation GUI built on Qt6, OpenGL and ASE"
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
    src/core/Structure.hpp
    src/core/Structure.cpp
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
    src/core/Distributions.hpp
    src/core/Distributions.cpp
    src/core/StructureFactor.hpp
    src/core/StructureFactor.cpp
    src/core/Noise.hpp
    src/core/Noise.cpp
    src/core/PhononScriptGenerator.hpp
    src/core/PhononScriptGenerator.cpp
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
    src/core/DatasetSplit.hpp
    src/core/DatasetSplit.cpp
    src/core/ElectronicScriptGenerator.hpp
    src/core/ElectronicScriptGenerator.cpp
    src/core/AdsorptionSites.hpp
    src/core/AdsorptionSites.cpp
    src/core/ClusterExpansion.hpp
    src/core/ClusterExpansion.cpp
    src/core/MonteCarlo.hpp
    src/core/MonteCarlo.cpp

    # python_bridge — embedded CPython + ASE interop
    src/python_bridge/PythonEngine.hpp
    src/python_bridge/PythonEngine.cpp
    src/python_bridge/AseBridge.hpp
    src/python_bridge/AseBridge.cpp
    src/python_bridge/AnimationExporter.hpp
    src/python_bridge/AnimationExporter.cpp
    src/python_bridge/MaterialsProject.hpp
    src/python_bridge/MaterialsProject.cpp
    src/python_bridge/SqsBuilder.hpp
    src/python_bridge/SqsBuilder.cpp
    src/python_bridge/RamanAnalysis.hpp
    src/python_bridge/RamanAnalysis.cpp
    src/python_bridge/SurfaceScience.hpp
    src/python_bridge/SurfaceScience.cpp
    src/python_bridge/NebBuilder.hpp
    src/python_bridge/NebBuilder.cpp

    # render — OpenGL "View" over core::Structure (read-only access)
    src/render/Camera.hpp
    src/render/Camera.cpp
    src/render/ColorMap.hpp
    src/render/ColorMap.cpp
    src/render/StructureRenderer.hpp
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
    src/gui/CalculatorDialog.hpp
    src/gui/CalculatorDialog.cpp
    src/gui/JobLogWidget.hpp
    src/gui/JobLogWidget.cpp
    src/gui/TimelineWidget.hpp
    src/gui/TimelineWidget.cpp
    src/gui/MetricPlotWidget.hpp
    src/gui/MetricPlotWidget.cpp
    src/gui/ProjectSerializer.hpp
    src/gui/ProjectSerializer.cpp
    src/gui/RepresentationPanel.hpp
    src/gui/RepresentationPanel.cpp
    src/gui/CellAxesPanel.hpp
    src/gui/CellAxesPanel.cpp
    src/gui/LightingPanel.hpp
    src/gui/LightingPanel.cpp
    src/gui/ElementSettingsDialog.hpp
    src/gui/ElementSettingsDialog.cpp
    src/gui/PythonHighlighter.hpp
    src/gui/PythonHighlighter.cpp
    src/gui/BrillouinZoneView.hpp
    src/gui/BrillouinZoneView.cpp
    src/gui/BrillouinZoneDialog.hpp
    src/gui/BrillouinZoneDialog.cpp
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
    src/gui/RamanDialog.hpp
    src/gui/RamanDialog.cpp
    src/gui/VolumeViewWidget.hpp
    src/gui/VolumeViewWidget.cpp
    src/gui/VolumetricDialog.hpp
    src/gui/VolumetricDialog.cpp
    src/gui/VisualEffectsDialog.hpp
    src/gui/VisualEffectsDialog.cpp
    src/gui/DatasetManagerDialog.hpp
    src/gui/DatasetManagerDialog.cpp
    src/gui/ProcessManagerPanel.hpp
    src/gui/ProcessManagerPanel.cpp
    src/gui/BandPdosView.hpp
    src/gui/BandPdosView.cpp
    src/gui/BandPdosWindow.hpp
    src/gui/BandPdosWindow.cpp
    src/gui/PhononPlotWindow.hpp
    src/gui/PhononPlotWindow.cpp
    src/gui/NanoparticleDialog.hpp
    src/gui/NanoparticleDialog.cpp
    src/gui/AdsorptionDialog.hpp
    src/gui/AdsorptionDialog.cpp
    src/gui/SupercellDialog.hpp
    src/gui/SupercellDialog.cpp
    src/gui/SymmetryDialog.hpp
    src/gui/SymmetryDialog.cpp
    src/gui/MolecularDynamicsWizard.hpp
    src/gui/MolecularDynamicsWizard.cpp
    src/gui/SimulationDialogs.hpp
    src/gui/SimulationDialogs.cpp
    src/gui/ClusterExpansionDialog.hpp
    src/gui/ClusterExpansionDialog.cpp
    src/gui/MonteCarloDialog.hpp
    src/gui/MonteCarloDialog.cpp
    src/gui/NebDialog.hpp
    src/gui/NebDialog.cpp
)

qt_add_resources(calango "shaders"
    PREFIX "/"
    FILES
        assets/shaders/mesh.vert
        assets/shaders/mesh.frag
        assets/shaders/line.vert
        assets/shaders/line.frag
        assets/shaders/wire.vert
        assets/shaders/wire.frag
        assets/shaders/volume.vert
        assets/shaders/volume.frag
        assets/shaders/dof.vert
        assets/shaders/dof.frag
)

qt_add_resources(calango "branding"
    PREFIX "/"
    FILES
        assets/calango/icon_transparent.png
        assets/calango/panel.png
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
# icon_transparent.png with the stock sips/iconutil tools.
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
                        "${CMAKE_CURRENT_SOURCE_DIR}/assets/calango/icon_transparent.png"
                        "${CALANGO_ICNS}"
                DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/assets/calango/icon_transparent.png"
                        "${CMAKE_CURRENT_SOURCE_DIR}/packaging/macos/make_icns.sh"
                COMMENT "Rendering calango.icns from icon_transparent.png")
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
        assets/examples/diamond.vasp
        assets/examples/mos2_2h_bulk.vasp
        assets/examples/graphene.vasp
        assets/examples/mos2_1h_monolayer.vasp
        assets/examples/benzene.xyz
        assets/examples/naphthalene.xyz
        assets/examples/coronene.xyz
)

target_include_directories(calango PRIVATE src)

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
    )
    target_include_directories(calango_symmetry_test PRIVATE src)
    target_compile_definitions(calango_symmetry_test PRIVATE
        CALANGO_DEFAULT_PYTHON="${CALANGO_DEFAULT_PYTHON_PATH}")
    target_link_libraries(calango_symmetry_test PRIVATE pybind11::embed)
    add_test(NAME symmetry_si_diamond
             COMMAND calango_symmetry_test
                     "${CMAKE_CURRENT_SOURCE_DIR}/assets/examples/Si_diamond.vasp")

    # Live network test — skipped (exit 77) when MP_API_KEY is neither in
    # the environment nor in ~/.env.
    add_executable(calango_mp_test
        tests/MaterialsProjectTest.cpp
        src/core/Structure.cpp
        src/core/UnitCell.cpp
        src/core/Element.cpp
        src/python_bridge/PythonEngine.cpp
        src/python_bridge/AseBridge.cpp
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
        src/python_bridge/RamanAnalysis.cpp
    )
    target_include_directories(calango_raman_test PRIVATE src)
    target_compile_definitions(calango_raman_test PRIVATE
        CALANGO_DEFAULT_PYTHON="${CALANGO_DEFAULT_PYTHON_PATH}")
    target_link_libraries(calango_raman_test PRIVATE pybind11::embed)
    add_test(NAME raman_si_diamond
             COMMAND calango_raman_test
                     "${CMAKE_CURRENT_SOURCE_DIR}/assets/examples/Si_diamond.vasp")

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

    # Embedded interpreter — PythonEngine looks this location up at runtime.
    if(CALANGO_EMBEDDED_PYTHON_DIR)
        install(DIRECTORY "${CALANGO_EMBEDDED_PYTHON_DIR}/"
                DESTINATION calango.app/Contents/Resources/python
                USE_SOURCE_PERMISSIONS)
    endif()

    # macdeployqt copies the Qt frameworks/plugins into the bundle and
    # rewrites the load paths. It runs at install/package time against the
    # staged .app, after the Python payload is in place.
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

    # macdeployqt skips the linked Python.framework (libpython from
    # Development.Embed) — copy it into the bundle and retarget the load
    # command so the app launches without the build machine's Python.
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
    # No vector source exists in the repo (only the master PNG), so ship the
    # PNG as the pixmaps icon — it resolves the .desktop "Icon=calango"
    # lookup. Add a hicolor/scalable/apps/calango.svg install here if a real
    # SVG is committed later.
    install(FILES assets/calango/icon_transparent.png
            DESTINATION ${CMAKE_INSTALL_DATADIR}/pixmaps
            RENAME calango.png)

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
        set(CPACK_DEBIAN_PACKAGE_RECOMMENDS "python3-ase")
    endif()
    # Refresh MIME/desktop/icon caches on install and removal.
    set(CPACK_DEBIAN_PACKAGE_CONTROL_EXTRA
        "${CMAKE_CURRENT_SOURCE_DIR}/packaging/linux/postinst;${CMAKE_CURRENT_SOURCE_DIR}/packaging/linux/postrm")
    set(CPACK_DEBIAN_PACKAGE_CONTROL_STRICT_PERMISSION TRUE)
endif()

include(CPack)
