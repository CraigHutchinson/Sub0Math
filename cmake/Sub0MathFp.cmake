# Sub0Math's floating-point determinism contract: no contraction of a * b + c into a fused multiply-add.
#
#   sub0math_target_deterministic_fp(<target> [INTERFACE|PUBLIC|PRIVATE])
#       Forbids fused multiply-add contraction so results are bit-identical across -march levels and
#       compilers (-ffp-contract=off, /fp:precise). Default scope PRIVATE; see docs/PERFORMANCE.md.
#
# Included by Sub0Math's CMakeLists.txt, so consumers that add Sub0Math as a subproject can call it. (It lived in the
# performance tooling until that moved to Sub0Perf.)

include_guard(GLOBAL)

# Floating-point determinism: no contraction of a * b + c into a fused multiply-add. GCC defaults to
# -ffp-contract=fast in gnu modes and Clang/MSVC contract under some flags, so a build with FMA hardware
# (-march=native, SUB0MATH_NATIVE_ARCH, -march=x86-64-v3) would otherwise round differently from one
# without. Scope is INTERFACE for header-only libraries so consumers' translation units inherit it.
function(sub0math_target_deterministic_fp target)
    set(scope PRIVATE)
    if(ARGC GREATER 1)
        set(scope "${ARGV1}")
    endif()
    if(NOT scope MATCHES "^(INTERFACE|PUBLIC|PRIVATE)$")
        message(FATAL_ERROR "sub0math_target_deterministic_fp: scope must be INTERFACE, PUBLIC or PRIVATE, got '${scope}'")
    endif()
    if(CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
        target_compile_options(${target} ${scope} $<$<COMPILE_LANGUAGE:CXX>:/fp:precise>)
    elseif(CMAKE_CXX_COMPILER_FRONTEND_VARIANT STREQUAL "MSVC") # clang-cl
        target_compile_options(${target} ${scope} $<$<COMPILE_LANGUAGE:CXX>:/fp:precise>
                               $<$<COMPILE_LANGUAGE:CXX>:/clang:-ffp-contract=off>)
    else() # GCC, Clang, AppleClang
        target_compile_options(${target} ${scope} $<$<COMPILE_LANGUAGE:CXX>:-ffp-contract=off>)
    endif()
    # MinGW-w64 GCC guarantees only 16-byte stack alignment, but AVX code (-march=x86-64-v3 or native) spills
    # 32-byte vectors with aligned moves, which fault. Assembling vector moves as unaligned ones fixes it at no
    # measurable cost; without it, a private consumer's tests segfaulted under -march=x86-64-v3 (2026-10-20).
    if(MINGW AND CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
        include(CheckCXXCompilerFlag)
        check_cxx_compiler_flag("-Wa,-muse-unaligned-vector-move" SUB0MATH_HAVE_UNALIGNED_VECTOR_MOVE)
        if(SUB0MATH_HAVE_UNALIGNED_VECTOR_MOVE)
            target_compile_options(${target} ${scope} $<$<COMPILE_LANGUAGE:CXX>:-Wa,-muse-unaligned-vector-move>)
        endif()
    endif()
endfunction()
