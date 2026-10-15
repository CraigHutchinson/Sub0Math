# Performance tooling shared by the Sub0 family (Sub0Math and every library above it).
#
#   sub0math_target_perf_options(<target>)
#       Applies the profiling, vectorization-report and native-architecture options to a target.
#   sub0math_add_benchmark(<target> <sources>...)
#       Builds a nanobench executable using tools/perf/bench_runner.hpp, plus these targets:
#         <target>_json             run it and write build/perf/<target>.json
#         <target>_check            best of SUB0MATH_BENCH_REPEATS runs vs perf/baselines/<host>.json
#         <target>_baseline         record perf/baselines/<host>.json (best of the same runs)
#         <target>_vtune_hotspots   VTune hotspots      (SUB0MATH_PROFILE_FILTER, _SECONDS)
#         <target>_vtune_uarch      VTune microarchitecture exploration
#         <target>_advisor_roofline Intel Advisor vectorization survey and roofline
#         <target>_perf_record      Linux perf sampling with call graphs
#
# Consumers include this file from the Sub0Math source tree (CPM sets Sub0Math_SOURCE_DIR).

include_guard(GLOBAL)

# Paths are resolved inside each function from CMAKE_CURRENT_FUNCTION_LIST_DIR: include_guard skips
# re-inclusion, so variables set here would not reach a consumer's directory scope.

option(SUB0MATH_PROFILE_FLAGS "Debug info and frame pointers in optimised builds, for profilers" OFF)
option(SUB0MATH_VECTORIZATION_REPORT "Emit compiler vectorization remarks" OFF)
option(SUB0MATH_NATIVE_ARCH "Tune for the build machine's CPU (local experiments only)" OFF)
set(SUB0MATH_PROFILE_FILTER "" CACHE STRING "Benchmark name substring the profiling targets run")
set(SUB0MATH_PROFILE_SECONDS "10" CACHE STRING "Soak seconds per benchmark under a profiler")
set(SUB0MATH_BENCH_REPEATS "5" CACHE STRING "Benchmark runs per budget check; each benchmark's best run is compared")

# Where the scripts are, for shells and CI: `python "$(cat build/sub0math_tooling_dir.txt)/profile.py" ...`.
# CPM may place Sub0Math anywhere, so consumers should not hard-code the path.
get_filename_component(_sub0math_tooling_dir "${CMAKE_CURRENT_LIST_DIR}/../tools/perf" ABSOLUTE)
file(WRITE "${CMAKE_BINARY_DIR}/sub0math_tooling_dir.txt" "${_sub0math_tooling_dir}")

cmake_host_system_information(RESULT _sub0math_host QUERY HOSTNAME)
string(TOLOWER "${_sub0math_host}" _sub0math_host)
set(SUB0MATH_PERF_HOST "${_sub0math_host}" CACHE STRING "Baseline name in perf/baselines/")

function(sub0math_target_perf_options target)
    if(SUB0MATH_PROFILE_FLAGS)
        target_compile_options(${target} PRIVATE
            $<$<CXX_COMPILER_ID:MSVC>:/Zi /Oy->
            $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-g -fno-omit-frame-pointer>)
        target_link_options(${target} PRIVATE $<$<CXX_COMPILER_ID:MSVC>:/DEBUG>)
    endif()
    if(SUB0MATH_VECTORIZATION_REPORT)
        target_compile_options(${target} PRIVATE
            $<$<CXX_COMPILER_ID:MSVC>:/Qvec-report:2>
            $<$<CXX_COMPILER_ID:GNU>:-fopt-info-vec-optimized -fopt-info-vec-missed>
            $<$<CXX_COMPILER_ID:Clang,AppleClang>:-Rpass=loop-vectorize|slp-vectorizer
                -Rpass-missed=loop-vectorize -Rpass-analysis=loop-vectorize>)
    endif()
    if(SUB0MATH_NATIVE_ARCH)
        target_compile_options(${target} PRIVATE
            $<$<CXX_COMPILER_ID:MSVC>:/arch:AVX2>
            $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-march=native>)
    endif()
endfunction()

function(sub0math_add_benchmark target)
    get_filename_component(tooling "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../tools/perf" ABSOLUTE)
    get_filename_component(vendor "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../tests/vendor" ABSOLUTE)
    add_executable(${target} ${ARGN})
    target_include_directories(${target} SYSTEM PRIVATE "${tooling}" "${vendor}")
    target_compile_features(${target} PRIVATE cxx_std_23)
    sub0math_target_perf_options(${target})

    find_package(Python3 COMPONENTS Interpreter QUIET)
    if(NOT Python3_Interpreter_FOUND)
        return()
    endif()
    set(results "${CMAKE_BINARY_DIR}/perf/${target}.json")
    set(repeat_commands "")
    set(repeat_results "")
    foreach(run RANGE 1 ${SUB0MATH_BENCH_REPEATS})
        list(APPEND repeat_commands COMMAND $<TARGET_FILE:${target}> --json "${CMAKE_BINARY_DIR}/perf/${target}.run${run}.json")
        list(APPEND repeat_results "${CMAKE_BINARY_DIR}/perf/${target}.run${run}.json")
    endforeach()
    set(baseline "${PROJECT_SOURCE_DIR}/perf/baselines/${SUB0MATH_PERF_HOST}.json")
    set(check "${tooling}/bench_check.py")
    set(profile "${tooling}/profile.py")

    add_custom_target(${target}_json
        COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_BINARY_DIR}/perf"
        COMMAND $<TARGET_FILE:${target}> --json "${results}"
        DEPENDS ${target} USES_TERMINAL VERBATIM)
    # Budgets use best-of-N repeated runs (SUB0MATH_BENCH_REPEATS): steady on a busy desktop.
    add_custom_target(${target}_runs
        COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_BINARY_DIR}/perf"
        ${repeat_commands}
        DEPENDS ${target} USES_TERMINAL VERBATIM)
    add_custom_target(${target}_check
        COMMAND ${Python3_EXECUTABLE} "${check}" check --results ${repeat_results} --baseline "${baseline}"
        DEPENDS ${target}_runs USES_TERMINAL VERBATIM)
    add_custom_target(${target}_baseline
        COMMAND ${Python3_EXECUTABLE} "${check}" baseline --results ${repeat_results} --baseline "${baseline}"
        DEPENDS ${target}_runs USES_TERMINAL VERBATIM)

    foreach(analysis vtune-hotspots vtune-uarch advisor-roofline perf-record)
        string(REPLACE "-" "_" suffix "${analysis}")
        add_custom_target(${target}_${suffix}
            COMMAND ${Python3_EXECUTABLE} "${profile}" ${analysis} --exe $<TARGET_FILE:${target}>
                    --filter "${SUB0MATH_PROFILE_FILTER}" --seconds ${SUB0MATH_PROFILE_SECONDS}
                    --out "${CMAKE_BINARY_DIR}/profiles"
            DEPENDS ${target} USES_TERMINAL VERBATIM)
    endforeach()
endfunction()
