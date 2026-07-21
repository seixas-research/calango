cmake_minimum_required(VERSION 3.21)

project(calango
    VERSION 0.10.0
    DESCRIPTION "Calango — atomistic modeling and simulation GUI built on Qt6, OpenGL and ASE"
    LANGUAGES CXX
)

# The user-facing version string lives in the plain-text `version` file at
# the repository root, read at runtime (About dialog). Stage a copy next
# to the built binary so it is found outside the source tree too.
configure_file(version ${CMAKE_CURRENT_BINARY_DIR}/version COPYONLY)

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
    src/core/Noise.hpp
    src/core/Noise.cpp
    src/core/PhononScriptGenerator.hpp
    src/core/PhononScriptGenerator.cpp

    # python_bridge — embedded CPython + ASE interop
    src/python_bridge/PythonEngine.hpp
    src/python_bridge/PythonEngine.cpp
    src/python_bridge/AseBridge.hpp
    src/python_bridge/AseBridge.cpp
    src/python_bridge/AnimationExporter.hpp
    src/python_bridge/AnimationExporter.cpp
    src/python_bridge/MaterialsProject.hpp
    src/python_bridge/MaterialsProject.cpp

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
    src/gui/EnergyPlotWidget.hpp
    src/gui/EnergyPlotWidget.cpp
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
)

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
