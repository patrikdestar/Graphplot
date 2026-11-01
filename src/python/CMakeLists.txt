# Locate pybind11: a system/conda installation, or the pip package
# ("pip install pybind11"), whose CMake files are found via the interpreter.
find_package(Python 3.8 COMPONENTS Interpreter Development.Module REQUIRED)
find_package(pybind11 CONFIG QUIET)
if(NOT pybind11_FOUND)
  execute_process(COMMAND "${Python_EXECUTABLE}" -m pybind11 --cmakedir
                  OUTPUT_VARIABLE _pybind11_dir OUTPUT_STRIP_TRAILING_WHITESPACE
                  RESULT_VARIABLE _pybind11_res ERROR_QUIET)
  if(_pybind11_res EQUAL 0)
    find_package(pybind11 CONFIG REQUIRED PATHS "${_pybind11_dir}" NO_DEFAULT_PATH)
  else()
    message(FATAL_ERROR "pybind11 not found. Install it (pip install pybind11) "
                        "or configure with -DGRAPHPLOT_BUILD_PYTHON=OFF")
  endif()
endif()

# The package is assembled in <build>/python/plotter so that
#   PYTHONPATH=<build>/python python examples/demo.py
# works without installing anything.
set(PLOTTER_PKG_DIR "${PROJECT_BINARY_DIR}/python/plotter")

pybind11_add_module(_plotter MODULE bindings.cpp GuiThread.h GuiThread.cpp)
target_link_libraries(_plotter PRIVATE graphplot_ui)
set_target_properties(_plotter PROPERTIES
  LIBRARY_OUTPUT_DIRECTORY "${PLOTTER_PKG_DIR}"
  LIBRARY_OUTPUT_DIRECTORY_RELEASE "${PLOTTER_PKG_DIR}"
  LIBRARY_OUTPUT_DIRECTORY_DEBUG "${PLOTTER_PKG_DIR}")

add_custom_command(TARGET _plotter POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
          "${PROJECT_SOURCE_DIR}/python/plotter/__init__.py"
          "${PROJECT_SOURCE_DIR}/python/plotter/_ipython.py"
          "${PLOTTER_PKG_DIR}/")

install(TARGETS _plotter LIBRARY DESTINATION plotter)
install(FILES "${PROJECT_SOURCE_DIR}/python/plotter/__init__.py"
              "${PROJECT_SOURCE_DIR}/python/plotter/_ipython.py" DESTINATION plotter)
