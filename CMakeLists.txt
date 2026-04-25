cmake_minimum_required(VERSION 3.20)
project(campello_core
    VERSION 0.1.0
    DESCRIPTION "Core ECS library for the Campello engine"
    HOMEPAGE_URL "https://github.com/rusoleal/campello_core"
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Main header-only library
add_library(campello_core INTERFACE)
set_target_properties(campello_core PROPERTIES EXPORT_NAME core)
add_library(campello::core ALIAS campello_core)

target_include_directories(campello_core INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

target_compile_features(campello_core INTERFACE cxx_std_20)

# Optional: enable threading for parallel schedule executor
find_package(Threads REQUIRED)
target_link_libraries(campello_core INTERFACE Threads::Threads)

# Installation
include(GNUInstallDirs)

install(TARGETS campello_core
    EXPORT campello_core-targets
)

install(EXPORT campello_core-targets
    FILE campello_core-targets.cmake
    NAMESPACE campello::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/campello_core
)

install(DIRECTORY include/
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)

include(CMakePackageConfigHelpers)
configure_package_config_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/cmake/campello_core-config.cmake.in
    ${CMAKE_CURRENT_BINARY_DIR}/campello_core-config.cmake
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/campello_core
)

write_basic_package_version_file(
    ${CMAKE_CURRENT_BINARY_DIR}/campello_core-config-version.cmake
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion
)

install(FILES
    ${CMAKE_CURRENT_BINARY_DIR}/campello_core-config.cmake
    ${CMAKE_CURRENT_BINARY_DIR}/campello_core-config-version.cmake
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/campello_core
)

# Tests
option(CAMPELLO_CORE_BUILD_TESTS "Build tests" ON)
if(CAMPELLO_CORE_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

# Benchmarks
option(CAMPELLO_CORE_BUILD_BENCHMARKS "Build benchmarks" OFF)
if(CAMPELLO_CORE_BUILD_BENCHMARKS)
    add_subdirectory(benchmarks)
endif()

# Examples
option(CAMPELLO_CORE_BUILD_EXAMPLES "Build examples" ON)
if(CAMPELLO_CORE_BUILD_EXAMPLES)
    add_subdirectory(examples)
endif()

# Cross-platform compile-only smoke test (used by Android, iOS, Windows ARM64 CI)
add_executable(smoke_test tests/android_smoke.cpp)
target_link_libraries(smoke_test PRIVATE campello::core)
