# CPack settings. Used by the release workflow (.github/workflows/release.yml):
#   cmake -S . -B build -DCMAKE_INSTALL_PREFIX=/usr \
#         -DGRAPHPLOT_PYTHON_INSTALL_DIR=/usr/lib/python3/dist-packages \
#         -DGRAPHPLOT_DEB_DISTRO=ubuntu24.04
#   cmake --build build && (cd build && cpack -G DEB)
# The .deb is tied to the distribution it was built on: it uses that
# distribution's Qt and the module is compiled for its Python version.
set(GRAPHPLOT_DEB_DISTRO "" CACHE STRING "Distribution tag in the .deb version and file name, e.g. ubuntu24.04")

set(CPACK_PACKAGE_NAME "graphplot")
set(CPACK_PACKAGE_VENDOR "Justinas Minkevicius")
set(CPACK_PACKAGE_CONTACT "Justinas Minkevicius <oompagoo@gmail.com>")
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Interactive scientific plotting (Qt) with a Python module")
set(CPACK_PACKAGE_DESCRIPTION
    "Graphplot plots column data, 2D NMR spectra and sensor-log timelines with\n"
    "fast mouse interaction. The 'plotter' Python module (and plotter.pyplot)\n"
    "creates and controls the same windows from Python and NumPy.")
set(CPACK_RESOURCE_FILE_LICENSE "${PROJECT_SOURCE_DIR}/LICENSE")
set(CPACK_STRIP_FILES ON)

# ---- Debian / Ubuntu ----
set(CPACK_DEBIAN_PACKAGE_SECTION "science")
set(CPACK_DEBIAN_PACKAGE_HOMEPAGE "https://github.com/patrikdestar/Graphplot")
set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON)  # Qt and C++ runtime dependencies from the binaries
set(_deps "qt6-qpa-plugins")             # platform plugins (xcb/wayland) are a separate package
if(GRAPHPLOT_BUILD_PYTHON)
  # The compiled module only loads in the Python minor version it was built for.
  math(EXPR _py_next "${Python_VERSION_MINOR} + 1")
  list(APPEND _deps "python3 (>= ${Python_VERSION_MAJOR}.${Python_VERSION_MINOR})"
                    "python3 (<< ${Python_VERSION_MAJOR}.${_py_next})" "python3-numpy")
endif()
list(JOIN _deps ", " CPACK_DEBIAN_PACKAGE_DEPENDS)
set(CPACK_DEBIAN_PACKAGE_RECOMMENDS "fonts-dejavu-core")
if(GRAPHPLOT_DEB_DISTRO)
  set(CPACK_DEBIAN_PACKAGE_RELEASE "1~${GRAPHPLOT_DEB_DISTRO}")
  set(CPACK_DEBIAN_FILE_NAME "graphplot_${PROJECT_VERSION}_${GRAPHPLOT_DEB_DISTRO}_amd64.deb")
else()
  set(CPACK_DEBIAN_FILE_NAME DEB-DEFAULT)
endif()

# ---- Portable archive (Windows release zip is assembled by the workflow) ----
if(NOT CPACK_GENERATOR)
  if(WIN32)
    set(CPACK_GENERATOR "ZIP")
  else()
    set(CPACK_GENERATOR "DEB")
  endif()
endif()

include(CPack)
