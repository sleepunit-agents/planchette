cmake_minimum_required(VERSION 3.16)
project(planchette VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# The engine: pure C++, no JUCE, no dependencies (SPEC §6.1).
add_library(planchette_engine engine/src/engine.cpp engine/src/form.cpp engine/src/generators.cpp engine/src/matrix.cpp engine/src/mod.cpp engine/src/song.cpp)
target_include_directories(planchette_engine PUBLIC engine/include)
set_target_properties(planchette_engine PROPERTIES POSITION_INDEPENDENT_CODE ON)  # it links into a plugin's shared library
if(MSVC)
  target_compile_options(planchette_engine PRIVATE /W4 /permissive-)
else()
  target_compile_options(planchette_engine PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_executable(planchette_render tools/render.cpp)
target_link_libraries(planchette_render PRIVATE planchette_engine)
add_executable(planchette_variance tools/variance.cpp)
target_link_libraries(planchette_variance PRIVATE planchette_engine)

enable_testing()
add_executable(planchette_tests tests/tests.cpp)
target_link_libraries(planchette_tests PRIVATE planchette_engine)
target_compile_definitions(planchette_tests PRIVATE HOCKET_GOLDEN_DIR="${CMAKE_CURRENT_SOURCE_DIR}/tests/golden")
add_test(NAME engine COMMAND planchette_tests)
add_test(NAME render COMMAND ${CMAKE_COMMAND}
  -DRENDER=$<TARGET_FILE:planchette_render> -DGOLDEN=${CMAKE_CURRENT_SOURCE_DIR}/tests/golden
  -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/render-test.mid -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/render.cmake)
# Long-form structure (§9 Freeze, the form, the colotomic clock, irama, tihai): their own checks, beside the engine's.
add_executable(planchette_form_tests tests/form.cpp)
target_link_libraries(planchette_form_tests PRIVATE planchette_engine)
add_test(NAME form COMMAND planchette_form_tests)

option(HOCKET_PLUGIN "Build the JUCE plugin (VST3, AU, Standalone)" OFF)
if(HOCKET_PLUGIN)
  add_subdirectory(plugin)
endif()
