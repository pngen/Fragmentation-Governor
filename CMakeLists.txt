cmake_minimum_required(VERSION 3.20)

project(FragmentationGovernor
  VERSION 1.0.0
  DESCRIPTION "Fragment-aware stranded-capacity detection, quantification, explanation and governance runtime"
  LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(FG_BUILD_TESTS "Build Fragmentation Governor tests" ON)
option(FG_BUILD_EXAMPLES "Build Fragmentation Governor examples" ON)
option(FG_BUILD_BENCHMARKS "Build Fragmentation Governor benchmarks" ON)
option(FG_BUILD_TOOLS "Build Fragmentation Governor CLI tooling" ON)
option(FG_BUILD_CUDA "Build Fragmentation Governor CUDA proof (requires CUDA)" ON)
option(FG_ASAN "Enable AddressSanitizer (MSVC/clang supported subset)" OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

# ---- strict warnings (applies to every first-party target) -----------------
if(MSVC)
  add_compile_options(/W4 /WX /permissive- /Zc:__cplusplus)
else()
  add_compile_options(-Wall -Wextra -Wpedantic -Werror)
endif()

# ---- library ---------------------------------------------------------------
file(GLOB_RECURSE FG_CORE_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp")

add_library(FragmentationGovernor ${FG_CORE_SOURCES})
add_library(FragmentationGovernor::FragmentationGovernor ALIAS FragmentationGovernor)

target_include_directories(FragmentationGovernor
  PUBLIC
    "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
    "$<INSTALL_INTERFACE:include>")
target_compile_definitions(FragmentationGovernor PRIVATE FG_BUILDING_LIBRARY)
if(WIN32)
  target_link_libraries(FragmentationGovernor PRIVATE ws2_32)
endif()

if(FG_ASAN)
  # Apply to every first-party target (library + tests/tools/examples/benchmarks).
  if(MSVC)
    add_compile_options(/fsanitize=address)
    add_link_options(/fsanitize=address)
  else()
    add_compile_options(-fsanitize=address -fno-omit-frame-pointer)
    add_link_options(-fsanitize=address)
  endif()
endif()

# ---- install / export ------------------------------------------------------
include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

install(DIRECTORY include/ DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}")
install(TARGETS FragmentationGovernor
  EXPORT FragmentationGovernorTargets
  ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}"
  LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}"
  RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")

install(EXPORT FragmentationGovernorTargets
  FILE FragmentationGovernorTargets.cmake
  NAMESPACE FragmentationGovernor::
  DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/FragmentationGovernor")

configure_package_config_file(
  "${CMAKE_CURRENT_SOURCE_DIR}/cmake/FragmentationGovernorConfig.cmake.in"
  "${CMAKE_CURRENT_BINARY_DIR}/FragmentationGovernorConfig.cmake"
  INSTALL_DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/FragmentationGovernor")
write_basic_package_version_file(
  "${CMAKE_CURRENT_BINARY_DIR}/FragmentationGovernorConfigVersion.cmake"
  VERSION "${PROJECT_VERSION}"
  COMPATIBILITY SameMajorVersion)
install(FILES
  "${CMAKE_CURRENT_BINARY_DIR}/FragmentationGovernorConfig.cmake"
  "${CMAKE_CURRENT_BINARY_DIR}/FragmentationGovernorConfigVersion.cmake"
  DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/FragmentationGovernor")

if(FG_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()
if(FG_BUILD_EXAMPLES)
  add_subdirectory(examples)
endif()
if(FG_BUILD_BENCHMARKS)
  add_subdirectory(benchmarks)
endif()
if(FG_BUILD_TOOLS)
  add_subdirectory(tools)
endif()
if(FG_BUILD_CUDA)
  add_subdirectory(cuda)
endif()