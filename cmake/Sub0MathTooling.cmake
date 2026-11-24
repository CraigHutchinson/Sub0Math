# TRANSITION SHIM. The performance tooling moved to the Sub0Perf repository (ADR 0004 in CraigHutchinson/.github); every
# name is unchanged (sub0math_* functions, SUB0MATH_* variables, sub0math_tooling_dir.txt). Including this file fetches
# Sub0Perf at the pinned revision (unless Sub0Perf_SOURCE_DIR is already set, for example by the consumer's own
# CPMAddPackage(NAME Sub0Perf ...)) and includes its cmake/Sub0Perf.cmake. New code should depend on Sub0Perf directly.
# Removed one release after the last consumer has moved.
#
# Sub0Math's own top-level build sets SUB0MATH_TOOLING_QUIET to skip the notice.

include_guard(GLOBAL)

if(NOT SUB0MATH_TOOLING_QUIET)
    message(DEPRECATION "cmake/Sub0MathTooling.cmake is a shim: depend on Sub0Perf (CPMAddPackage NAME Sub0Perf, GITHUB_REPOSITORY "
                        "CraigHutchinson/Sub0Perf) and include its cmake/Sub0Perf.cmake")
endif()

if(NOT Sub0Perf_SOURCE_DIR)
    if(NOT COMMAND CPMAddPackage)
        include("${CMAKE_CURRENT_LIST_DIR}/get_cpm.cmake")
    endif()
    # The pinned revision is the merge of Sub0Perf pull request 1 (2026-11-24).
    CPMAddPackage(NAME Sub0Perf
        GIT_REPOSITORY https://github.com/CraigHutchinson/Sub0Perf.git
        GIT_TAG 3c74bce22e848d3e4e13dfcb5eb9c480703b7db8
        DOWNLOAD_ONLY YES)
endif()

include("${Sub0Perf_SOURCE_DIR}/cmake/Sub0Perf.cmake")
