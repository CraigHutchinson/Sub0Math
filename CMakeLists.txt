cmake_minimum_required(VERSION 3.21)

# Detect if Sub0Math is being included inside another project
if(DEFINED PROJECT_NAME)
    set(IS_SUBPROJECT ON)
endif()

project(Sub0Math VERSION 0.1.0 LANGUAGES CXX)

include(GNUInstallDirs)

option(SUB0MATH_BUILD_TESTING    "Build unit-tests" ON)
option(SUB0MATH_BUILD_BENCHMARKS "Build benchmarks" OFF)
option(SUB0MATH_WARNINGS_AS_ERRORS "Treat warnings in tests and benchmarks as errors" OFF)

# Sub0Math's determinism contract (sub0math_target_deterministic_fp): consumers that add Sub0Math call it.
include(cmake/Sub0MathFp.cmake)

# Benchmark, budget, profiler and vectorization tooling comes from Sub0Perf (docs/PERFORMANCE.md), only when Sub0Math is the
# top-level project: a consumer of the maths never fetches it.
if(NOT IS_SUBPROJECT)
    include(cmake/get_cpm.cmake)
    # Pinned to the Sub0Perf merge of 2026-11-24 that dropped its transition copy of sub0math_target_deterministic_fp.
    CPMAddPackage(NAME Sub0Perf
        GIT_REPOSITORY https://github.com/CraigHutchinson/Sub0Perf.git
        GIT_TAG 7df1eed3e825aeced03697ee9b178938ec78d170
        DOWNLOAD_ONLY YES)
    include("${Sub0Perf_SOURCE_DIR}/cmake/Sub0Perf.cmake")
endif()

# ── Header-only library ───────────────────────────────────────────────────────

add_library(Sub0Math INTERFACE)
add_library(Sub0Math::Sub0Math ALIAS Sub0Math)

target_compile_features(Sub0Math INTERFACE cxx_std_23)

target_include_directories(Sub0Math
    INTERFACE
        $<BUILD_INTERFACE:${CMAKE_CURRENT_LIST_DIR}/include>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)

if(SUB0MATH_BUILD_TESTING AND NOT IS_SUBPROJECT)
    enable_testing()
    add_subdirectory(tests)
endif()

# ── Install ───────────────────────────────────────────────────────────────────

if(NOT IS_SUBPROJECT)

    include(CMakePackageConfigHelpers)

    set(SUB0MATH_CMAKE_CONFIG_DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/Sub0Math")

    configure_package_config_file(
        "${CMAKE_CURRENT_LIST_DIR}/cmake/Sub0MathConfig.cmake.in"
        "${CMAKE_CURRENT_BINARY_DIR}/Sub0MathConfig.cmake"
        INSTALL_DESTINATION
            "${SUB0MATH_CMAKE_CONFIG_DESTINATION}"
    )

    write_basic_package_version_file(
        "${PROJECT_BINARY_DIR}/Sub0MathConfigVersion.cmake"
        COMPATIBILITY
            SameMinorVersion
        ARCH_INDEPENDENT
    )

    install(TARGETS Sub0Math EXPORT Sub0MathTargets)

    install(
        EXPORT
            Sub0MathTargets
        NAMESPACE
            Sub0Math::
        DESTINATION
            ${SUB0MATH_CMAKE_CONFIG_DESTINATION}
    )

    install(
        FILES
            "${PROJECT_BINARY_DIR}/Sub0MathConfigVersion.cmake"
            "${PROJECT_BINARY_DIR}/Sub0MathConfig.cmake"
        DESTINATION
            "${SUB0MATH_CMAKE_CONFIG_DESTINATION}"
    )

    install(DIRECTORY "include/" DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}")

endif()
