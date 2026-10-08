cmake_minimum_required(VERSION 3.14...3.22)

project(
  NnsPlace
  VERSION 1.0.1
  LANGUAGES CXX
)

# ---- Include guards ----

if(PROJECT_SOURCE_DIR STREQUAL PROJECT_BINARY_DIR)
  message(
    FATAL_ERROR
      "In-source builds not allowed. Please make a new directory (called a build directory) and run CMake from there."
  )
endif()

# ---------------------------------------------------------------------------
# Compiler warnings (as errors)
# ---------------------------------------------------------------------------

function(target_enable_warnings TARGET)
  if(MSVC)
    target_compile_options(${TARGET} PRIVATE /W4 /WX /utf-8 /wd4702 /wd4100)
  elseif(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang|AppleClang")
    target_compile_options(${TARGET} PRIVATE -Wall -Wextra -Wpedantic -Werror)
  endif()
endfunction()

# Abseil headers emit warnings that -Werror escalates to errors. Suppress on targets that include
# abseil headers. GCC coroutine frames also mis-link at -O2 (pre-existing test issue), so the tests
# build at -O0.
function(target_enable_abseil_suppressions TARGET)
  if(MSVC)
    target_compile_options(${TARGET} PRIVATE /wd4100 /wd4324)
  elseif(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang|AppleClang")
    target_compile_options(${TARGET} PRIVATE -Wno-overflow -Wno-pedantic -O0)
    if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|AppleClang")
      target_compile_options(${TARGET} PRIVATE -Wno-nullability-extension -Wno-gcc-compat)
    endif()
  endif()
endfunction()

# ---------------------------------------------------------------------------
# Options
# ---------------------------------------------------------------------------
option(NNSPLACE_BUILD_TESTS "Build tests" ON)
option(NNSPLACE_BUILD_BENCHMARKS "Build benchmarks" OFF)
option(NNSPLACE_BUILD_STANDALONE "Build standalone executable" ON)
option(NNSPLACE_BUILD_DOCS "Build Doxygen documentation" OFF)
option(NNSPLACE_ENABLE_CLANG_TIDY "Enable clang-tidy analysis" OFF)
option(NNSPLACE_ENABLE_COVERAGE "Enable coverage reporting" OFF)

# ---------------------------------------------------------------------------
# Dependencies
# ---------------------------------------------------------------------------

# Download CPM.cmake for reproducible dependency management see
# https://github.com/cpm-cmake/CPM.cmake for more info
set(CPM_DOWNLOAD_VERSION 0.38.5)
set(CPM_HASH_SUM "192aa0ccdc57dfe75bd9e4b176bf7fb5692fd2b3e3f7b09c74856fc39572b31c")
set(CPM_DOWNLOAD_LOCATION "${CMAKE_BINARY_DIR}/cmake/CPM_${CPM_DOWNLOAD_VERSION}.cmake")
if(NOT (EXISTS ${CPM_DOWNLOAD_LOCATION}))
  message(STATUS "Downloading CPM.cmake to ${CPM_DOWNLOAD_LOCATION}")
  file(DOWNLOAD
       https://github.com/cpm-cmake/CPM.cmake/releases/download/v${CPM_DOWNLOAD_VERSION}/CPM.cmake
       ${CPM_DOWNLOAD_LOCATION} EXPECTED_HASH SHA256=${CPM_HASH_SUM}
  )
endif()
include(${CPM_DOWNLOAD_LOCATION})

# Always build dependencies from source. DiGraphX declares spdlog itself via FetchContent and
# assumes the source-built spdlog targets; the system-package opt-in (CPM_USE_LOCAL_PACKAGES) would
# swap in an imported spdlog::spdlog and break that assumption, so spdlog needs no special handling
# here.
set(CPM_USE_LOCAL_PACKAGES OFF)

# PackageProject.cmake will be used to make our target installable
CPMAddPackage("gh:TheLartians/PackageProject.cmake@1.8.0")

set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)

# fmt is a transitive dependency of the graph libraries; prefer a system-installed fmt when
# available. On MSVC, skip prebuilt system packages (e.g. conda): they are Release-only shared
# libraries whose std::string ABI (_ITERATOR_DEBUG_LEVEL=0) mismatches Debug builds
# (_ITERATOR_DEBUG_LEVEL=2), corrupting std::strings returned by fmt::format. Build fmt from source
# on MSVC so the ABI always matches (same as CI's windows-latest runner).
if(NOT MSVC)
  find_package(fmt CONFIG QUIET)
  if(fmt_FOUND)
    message(STATUS "Found system fmt: ${fmt_DIR}")
    set(CPM_fmt_ADDED YES)
  endif()
endif()

if(NOT fmt_FOUND)
  CPMAddPackage(
    NAME fmt
    GIT_TAG 12.1.0
    GITHUB_REPOSITORY fmtlib/fmt
    OPTIONS "FMT_INSTALL YES" # create an installable target
  )
endif()

CPMAddPackage(
  NAME Py2Cpp
  GIT_TAG v1.6.6
  GITHUB_REPOSITORY luk036/py2cpp
  OPTIONS "INSTALL_ONLY ON" "PY2CPP_BUILD_TESTS OFF"
)

# Suppress MSVC STL1011 error on <experimental/coroutine> used by xnetwork via /await
if(MSVC)
  add_compile_definitions(_SILENCE_EXPERIMENTAL_COROUTINE_DEPRECATION_WARNINGS)
endif()

CPMAddPackage(
  NAME XNetwork
  GIT_TAG v1.7.9
  GITHUB_REPOSITORY luk036/xnetwork-cpp
  OPTIONS "INSTALL_ONLY ON" "XNETWORK_BUILD_TESTS OFF"
)

# spdlog is used by the dependencies' logger sources. SPDLOG_FMT_EXTERNAL is always set so spdlog
# uses the external fmt and avoids duplicate symbols.
CPMAddPackage(
  NAME spdlog
  GIT_TAG v1.17.0
  GITHUB_REPOSITORY gabime/spdlog
  OPTIONS "SPDLOG_INSTALL YES" "SPDLOG_FMT_EXTERNAL YES"
)

CPMAddPackage(
  NAME DiGraphX
  GIT_TAG v1.1.9
  GITHUB_REPOSITORY luk036/digraphx-cpp
  OPTIONS "INSTALL_ONLY ON" "DIGRAPHX_BUILD_TESTS OFF"
)

# NetlistX provides the SimpleNetlist type and the node-link JSON reader (netlistx-cpp).
CPMAddPackage(
  NAME NetlistX
  GIT_TAG v1.1.10
  GITHUB_REPOSITORY luk036/netlistx-cpp
  OPTIONS "INSTALL_ONLY ON" "NETLISTX_BUILD_TESTS OFF" "NETLISTX_BUILD_STANDALONE OFF"
)

# Recti provides recti::Interval<T> (physdes-cpp) for the hull-length metrics.
CPMAddPackage(
  NAME Recti
  GIT_TAG v1.2.5
  GITHUB_REPOSITORY luk036/physdes-cpp
  OPTIONS "INSTALL_ONLY ON" "RECTI_BUILD_TESTS OFF" "RECTI_BUILD_STANDALONE OFF"
)

# MyWheel provides the MapAdapter adjacency view used to feed the MinParametricSolver.
CPMAddPackage(
  NAME MyWheel
  GIT_TAG v1.1.7
  GITHUB_REPOSITORY luk036/mywheel-cpp
  OPTIONS "INSTALL_ONLY ON" "MYWHEEL_BUILD_TESTS OFF"
)

# nlohmann_json is used by the node-link JSON reader (read_json_edges) and by NetlistX.
CPMAddPackage(
  NAME nlohmann_json
  GIT_TAG v3.12.0
  GITHUB_REPOSITORY nlohmann/json
  OPTIONS "JSON_BuildTests OFF"
)

# DiGraphX (fetched above) already adds abseil via CPM - use its REAL targets if present instead of
# mixing with find_package IMPORTED targets (IMPORTED vs ALIAS conflict).
if(TARGET absl::flat_hash_map)
  message(STATUS "Using CPM-provided abseil (from DiGraphX)")
  set(SPECIFIC_ABSEIL_LIBS absl::flat_hash_map)
else()
  # Try system-installed abseil first (Ubuntu: libabsl-dev, macOS: brew install abseil, Termux: pkg
  # install abseil-cpp). Falls back to CPM build on Windows or when no system package is available.
  find_package(absl CONFIG QUIET)
  if(absl_FOUND)
    message(STATUS "Found system abseil: ${absl_DIR}")
    set(SPECIFIC_ABSEIL_LIBS absl::flat_hash_map)
  else()
    CPMAddPackage(
      NAME abseil-cpp
      GIT_TAG 20260107.1
      GITHUB_REPOSITORY abseil/abseil-cpp
      OPTIONS "ABSL_PROPAGATE_CXX_STD ON"
    )
    set(SPECIFIC_ABSEIL_LIBS absl::flat_hash_map)
  endif()
endif()

set(SPECIFIC_LIBS NetlistX::NetlistX DiGraphX::DiGraphX Recti::Recti MyWheel::MyWheel
                  XNetwork::XNetwork Py2Cpp::Py2Cpp Threads::Threads fmt::fmt
                  nlohmann_json::nlohmann_json
)

# ---------------------------------------------------------------------------
# Library target (compiled: large bodies in source/*.cpp, headers declare only)
# ---------------------------------------------------------------------------

# Note: globbing sources is considered bad practice as CMake's generators may not detect new files
# automatically. Keep that in mind when changing files, or explicitly mention them here.
file(GLOB_RECURSE headers CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/include/*.h"
     "${CMAKE_CURRENT_SOURCE_DIR}/include/*.hpp"
)
file(GLOB_RECURSE sources CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/source/*.cpp")

add_library(${PROJECT_NAME} ${headers} ${sources})
set_target_properties(${PROJECT_NAME} PROPERTIES CXX_STANDARD 20 CXX_STANDARD_REQUIRED ON)

# being a cross-platform target, we enforce standards conformance on MSVC
target_compile_options(
  ${PROJECT_NAME} PUBLIC "$<$<COMPILE_LANG_AND_ID:CXX,MSVC>:/permissive->"
                         "$<$<COMPILE_LANG_AND_ID:CXX,MSVC>:/wd4702>"
)

# global_routing.cpp needs the physdes global router.  CPM's Recti::Recti ships
# its own log_with_spdlog, so skip the local no-op definition on this path.
target_compile_definitions(${PROJECT_NAME} PUBLIC NNSPLACE_HAVE_RECTI_LOGGER)

target_include_directories(
  ${PROJECT_NAME} PUBLIC $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
                         $<INSTALL_INTERFACE:include/${PROJECT_NAME}-${PROJECT_VERSION}>
)

# The .cpp translation units (and any consumer of the public headers) need the
# sibling header libraries; mirroring netlistx-cpp's compiled-library pattern.
#
# abseil is deliberately NOT linked here: its targets have no install-export
# rules in this build, so referencing absl::flat_hash_map from the exported
# NnsPlace target fails CMake generation with "requires target ... that is not
# in any export set".  nnsplace only reaches abseil transitively through the
# digraphx headers (min_parametric_q.hpp -> neg_cycle_q.hpp -> <absl/...>), so
# its headers are enough for this library's own compilation and the abseil
# target is linked explicitly by the executables below (mirrors digraphx-cpp).
target_link_libraries(${PROJECT_NAME} PUBLIC ${SPECIFIC_LIBS})
if(SPECIFIC_ABSEIL_LIBS AND TARGET ${SPECIFIC_ABSEIL_LIBS})
  get_target_property(NNSPLACE_ABSL_INCLUDE_DIRS ${SPECIFIC_ABSEIL_LIBS}
                      INTERFACE_INCLUDE_DIRECTORIES
  )
  if(NNSPLACE_ABSL_INCLUDE_DIRS)
    target_include_directories(${PROJECT_NAME} SYSTEM PRIVATE ${NNSPLACE_ABSL_INCLUDE_DIRS})
  endif()
endif()

# ---- Create an installable target ----
# this allows users to install and find the library via `find_package()`.

# the location where the project's version header will be placed should match the project's regular
# header paths
string(TOLOWER ${PROJECT_NAME}/version.h VERSION_HEADER_LOCATION)

packageProject(
  NAME ${PROJECT_NAME}
  VERSION ${PROJECT_VERSION}
  NAMESPACE ${PROJECT_NAME}
  BINARY_DIR ${PROJECT_BINARY_DIR}
  INCLUDE_DIR ${PROJECT_SOURCE_DIR}/include
  INCLUDE_DESTINATION include/${PROJECT_NAME}-${PROJECT_VERSION}
  VERSION_HEADER "${VERSION_HEADER_LOCATION}"
  COMPATIBILITY SameMajorVersion
)

# ---------------------------------------------------------------------------
# Standalone executable
# ---------------------------------------------------------------------------

if(NNSPLACE_BUILD_STANDALONE)
  file(GLOB NNSPLACE_STANDALONE_SOURCES CONFIGURE_DEPENDS "standalone/source/*.cpp")
  add_executable(NnsPlaceStandalone ${NNSPLACE_STANDALONE_SOURCES})
  set_target_properties(NnsPlaceStandalone PROPERTIES OUTPUT_NAME "NnsPlace" CXX_STANDARD 20)
  target_link_libraries(
    NnsPlaceStandalone PRIVATE NnsPlace::NnsPlace ${SPECIFIC_LIBS} ${SPECIFIC_ABSEIL_LIBS}
  )
  target_enable_warnings(NnsPlaceStandalone)
  target_enable_abseil_suppressions(NnsPlaceStandalone)

  # Routed placement + congestion map figure generator (mirrors the xmake
  # "nnsplace_figures" target; lives under experiments/ so the standalone
  # glob above keeps exactly one main()).
  add_executable(NnsPlaceFigures experiments/figures.cpp)
  set_target_properties(NnsPlaceFigures PROPERTIES OUTPUT_NAME "NnsPlaceFigures"
                                              CXX_STANDARD 20)
  target_link_libraries(NnsPlaceFigures PRIVATE NnsPlace::NnsPlace ${SPECIFIC_LIBS}
                                                 ${SPECIFIC_ABSEIL_LIBS})
  target_enable_warnings(NnsPlaceFigures)
  target_enable_abseil_suppressions(NnsPlaceFigures)
endif()

# ---------------------------------------------------------------------------
# Tests
# ---------------------------------------------------------------------------

enable_testing()

if(NNSPLACE_BUILD_TESTS)
  # Try system-installed doctest first (Ubuntu: doctest-dev, macOS: brew install doctest, Termux:
  # doctest)
  find_package(doctest CONFIG QUIET)

  if(NOT doctest_FOUND)
    CPMAddPackage(
      NAME doctest
      GITHUB_REPOSITORY doctest/doctest
      VERSION 2.5.2
      OPTIONS "DOCTEST_NO_INSTALL ON" # keep the installed prefix minimal
    )
  endif()

  # Code formatting targets (format / fix-format) via Format.cmake
  CPMAddPackage("gh:TheLartians/Format.cmake@1.7.3")

  file(GLOB_RECURSE TEST_SOURCES CONFIGURE_DEPENDS "test/source/*.cpp")
  add_executable(NnsPlaceTests ${TEST_SOURCES})
  target_link_libraries(
    NnsPlaceTests PRIVATE doctest::doctest NnsPlace::NnsPlace ${SPECIFIC_LIBS}
                           ${SPECIFIC_ABSEIL_LIBS}
  )
  set_target_properties(NnsPlaceTests PROPERTIES CXX_STANDARD 20)
  target_enable_warnings(NnsPlaceTests)
  target_enable_abseil_suppressions(NnsPlaceTests)
  add_test(NAME nnsplaceTests COMMAND NnsPlaceTests)
  set_tests_properties(nnsplaceTests PROPERTIES WORKING_DIRECTORY ${PROJECT_SOURCE_DIR})
endif()

# ---------------------------------------------------------------------------
# Doxygen documentation
# ---------------------------------------------------------------------------

if(NNSPLACE_BUILD_DOCS)
  find_package(Doxygen QUIET)
  if(DOXYGEN_FOUND)
    set(DOXYGEN_PROJECT_NAME ${PROJECT_NAME})
    set(DOXYGEN_PROJECT_VERSION ${PROJECT_VERSION})
    set(DOXYGEN_PROJECT_ROOT ${CMAKE_CURRENT_SOURCE_DIR})
    set(DOXYGEN_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/doxygen")

    configure_file(documentation/Doxyfile ${CMAKE_CURRENT_BINARY_DIR}/Doxyfile)

    add_custom_target(
      GenerateDocs
      COMMAND ${DOXYGEN_EXECUTABLE} "${CMAKE_CURRENT_BINARY_DIR}/Doxyfile"
      COMMAND echo "Docs written to: ${DOXYGEN_OUTPUT_DIRECTORY}/html"
      COMMENT "Generating Doxygen HTML documentation in ${DOXYGEN_OUTPUT_DIRECTORY}/html"
      WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
    )
  else()
    add_custom_target(
      GenerateDocs COMMAND ${CMAKE_COMMAND} -E echo
                       "Doxygen not found - skipping documentation generation"
    )
  endif()
endif()

# ---------------------------------------------------------------------------
# Coverage report target (GCC/Clang only - requires gcovr)
# ---------------------------------------------------------------------------

if(NNSPLACE_ENABLE_COVERAGE AND CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang|AppleClang")
  # NnsPlace is a header-only INTERFACE library - coverage flags go on the executables that
  # instantiate the templates.
  foreach(COVERAGE_TARGET NnsPlaceTests NnsPlaceStandalone)
    if(TARGET ${COVERAGE_TARGET})
      target_compile_options(${COVERAGE_TARGET} PRIVATE -O0 -g -fprofile-arcs -ftest-coverage)
    endif()
  endforeach()
  foreach(COVERAGE_EXEC NnsPlaceTests NnsPlaceStandalone)
    if(TARGET ${COVERAGE_EXEC})
      target_link_options(${COVERAGE_EXEC} PRIVATE -fprofile-arcs -ftest-coverage)
    endif()
  endforeach()

  find_program(GCOVR_PATH gcovr)
  if(GCOVR_PATH)
    add_custom_target(
      coverage
      COMMAND ${CMAKE_CTEST_COMMAND} --output-on-failure
      COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_BINARY_DIR}/coverage
      COMMAND
        ${GCOVR_PATH} --root ${PROJECT_SOURCE_DIR} --html-details
        ${CMAKE_BINARY_DIR}/coverage/index.html --exclude test/ --exclude build/
      WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
      COMMENT "Generating coverage report in ${CMAKE_BINARY_DIR}/coverage/index.html"
    )
  else()
    message(
      WARNING "gcovr not found - coverage target unavailable. Install with: pip install gcovr"
    )
  endif()
endif()
