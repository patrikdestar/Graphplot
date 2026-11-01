cmake_minimum_required(VERSION 3.21)
project(Graphplot VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_AUTOMOC ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)  # the core library is also linked into the Python module

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

option(GRAPHPLOT_BUILD_PYTHON "Build the 'plotter' Python module (needs pybind11)" ON)
option(GRAPHPLOT_BUILD_TESTS "Build the C++ tests" ON)

find_package(Qt6 6.2 REQUIRED COMPONENTS Core Gui Widgets PrintSupport Svg)

# ---- QCustomPlot (vendored, GPLv3) -------------------------------------------
add_library(qcustomplot STATIC third_party/qcustomplot/qcustomplot.cpp
                               third_party/qcustomplot/qcustomplot.h)
target_include_directories(qcustomplot PUBLIC third_party/qcustomplot)
target_link_libraries(qcustomplot PUBLIC Qt6::Core Qt6::Gui Qt6::Widgets Qt6::PrintSupport)
# Third-party code: do not drown our own warnings in its warnings.
if(NOT MSVC)
  target_compile_options(qcustomplot PRIVATE -w)
endif()

# ---- Data model + commands (no widgets) -----------------------------------------
add_library(graphplot_model STATIC
  src/model/PlotTypes.h src/model/PlotTypes.cpp
  src/model/PlotModel.h src/model/PlotModel.cpp
  src/model/DataLoader.h src/model/DataLoader.cpp
  src/model/Contour.h src/model/Contour.cpp
  src/model/BrukerReader.h src/model/BrukerReader.cpp
  src/commands/Commands.h src/commands/Commands.cpp
  src/commands/PlotEditor.h src/commands/PlotEditor.cpp)
target_include_directories(graphplot_model PUBLIC src)
# QUndoStack lives in QtGui since Qt 6.
target_link_libraries(graphplot_model PUBLIC Qt6::Core Qt6::Gui)

# ---- Rendering, interaction, GUI ------------------------------------------------
add_library(graphplot_ui STATIC
  src/render/PlotRenderer.h src/render/PlotRenderer.cpp
  src/render/Exporter.h src/render/Exporter.cpp
  src/render/ContourPlottable.h src/render/ContourPlottable.cpp
  src/interaction/InteractionController.h src/interaction/InteractionController.cpp
  src/gui/Icons.h src/gui/Icons.cpp
  src/gui/PropertiesPanel.h src/gui/PropertiesPanel.cpp
  src/gui/FigureWindow.h src/gui/FigureWindow.cpp)
target_link_libraries(graphplot_ui PUBLIC graphplot_model qcustomplot Qt6::Widgets Qt6::Svg)

foreach(t graphplot_model graphplot_ui)
  if(MSVC)
    target_compile_options(${t} PRIVATE /W4 /permissive-)
  else()
    target_compile_options(${t} PRIVATE -Wall -Wextra -Wpedantic)
  endif()
endforeach()

# ---- Standalone application ------------------------------------------------------
add_executable(graphplot WIN32 src/app/main.cpp)
target_link_libraries(graphplot PRIVATE graphplot_ui)

# ---- Python module -------------------------------------------------------------------
if(GRAPHPLOT_BUILD_PYTHON)
  find_package(Python 3.8 COMPONENTS Interpreter Development.Module REQUIRED)
  add_subdirectory(src/python)
endif()

# ---- Tests -------------------------------------------------------------------------------
if(GRAPHPLOT_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()
