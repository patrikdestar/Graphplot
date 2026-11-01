find_package(Qt6 REQUIRED COMPONENTS Test)

add_executable(test_graphplot test_graphplot.cpp)
target_link_libraries(test_graphplot PRIVATE graphplot_ui Qt6::Test)
add_test(NAME test_graphplot COMMAND test_graphplot)
# Tests run headless.
set_tests_properties(test_graphplot PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

if(GRAPHPLOT_BUILD_PYTHON)
  add_test(NAME test_python
           COMMAND ${Python_EXECUTABLE} -m unittest -v test_python
           WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
  set_tests_properties(test_python PROPERTIES ENVIRONMENT
    "QT_QPA_PLATFORM=offscreen;PYTHONPATH=${PROJECT_BINARY_DIR}/python")
endif()
