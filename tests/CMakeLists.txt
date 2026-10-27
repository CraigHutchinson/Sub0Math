set(VENDOR_DIR "${CMAKE_CURRENT_SOURCE_DIR}/vendor")
find_package(Threads REQUIRED)

# Header-only: the tests are where the headers are compiled, so they carry the strict warnings.
set(SUB0MATH_STRICT_WARNINGS
    $<$<CXX_COMPILER_ID:MSVC>:/W4 /permissive->
    $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-Wall -Wextra -Wshadow -Wconversion -Wsign-conversion -Wdouble-promotion
        -fstrict-aliasing -Wstrict-aliasing=2>
    $<$<AND:$<BOOL:${SUB0MATH_WARNINGS_AS_ERRORS}>,$<CXX_COMPILER_ID:MSVC>>:/WX>
    $<$<AND:$<BOOL:${SUB0MATH_WARNINGS_AS_ERRORS}>,$<NOT:$<CXX_COMPILER_ID:MSVC>>>:-Werror>
)

# ── Unit tests ────────────────────────────────────────────────────────────────

add_executable(Sub0Math_Tests
    test_main.cpp
    test_contact.cpp
    test_geometry.cpp
    test_linear.cpp
    test_quantity.cpp
    test_role.cpp
    test_shapes.cpp
    test_spatial.cpp
    test_transforms.cpp
    test_units.cpp
)
target_link_libraries(Sub0Math_Tests PRIVATE Sub0Math::Sub0Math Threads::Threads)
target_include_directories(Sub0Math_Tests SYSTEM PRIVATE "${VENDOR_DIR}")
# doctest::Approx works in double, so float checks promote by design; the header check keeps the rule.
target_compile_options(Sub0Math_Tests PRIVATE ${SUB0MATH_STRICT_WARNINGS}
    $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-Wno-double-promotion>)
sub0math_target_deterministic_fp(Sub0Math_Tests)
add_test(NAME Sub0Math_Tests COMMAND Sub0Math_Tests)

# ── Header self-containment ───────────────────────────────────────────────────
# One translation unit per public header, each including only that header (twice,
# to exercise its include guard). A header that leans on another's transitive
# includes fails this build.

file(GLOB_RECURSE SUB0MATH_PUBLIC_HEADERS CONFIGURE_DEPENDS
    RELATIVE "${PROJECT_SOURCE_DIR}/include"
    "${PROJECT_SOURCE_DIR}/include/sub0math/*.hpp")
set(SUB0MATH_HEADER_CHECK_SOURCES "")
foreach(header IN LISTS SUB0MATH_PUBLIC_HEADERS)
    string(REPLACE ".hpp" ".cpp" check_name "${header}")
    set(check_source "${CMAKE_CURRENT_BINARY_DIR}/header_check/${check_name}")
    file(CONFIGURE OUTPUT "${check_source}" CONTENT
        "#include \"${header}\"
#include \"${header}\"
")
    list(APPEND SUB0MATH_HEADER_CHECK_SOURCES "${check_source}")
endforeach()

add_library(Sub0Math_HeaderCheck OBJECT ${SUB0MATH_HEADER_CHECK_SOURCES})
target_link_libraries(Sub0Math_HeaderCheck PRIVATE Sub0Math::Sub0Math)
target_compile_options(Sub0Math_HeaderCheck PRIVATE ${SUB0MATH_STRICT_WARNINGS})

# ── Benchmarks (not run by ctest — run manually) ──────────────────────────────

if(SUB0MATH_BUILD_BENCHMARKS)
    sub0math_add_benchmark(Sub0Math_Bench bench_math.cpp)
    target_link_libraries(Sub0Math_Bench PRIVATE Sub0Math::Sub0Math)
    sub0math_target_deterministic_fp(Sub0Math_Bench)
endif()
