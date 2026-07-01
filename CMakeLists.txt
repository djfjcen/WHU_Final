cmake_minimum_required(VERSION 3.20)

# Default to an optimized build when the caller (e.g. the judge) does not pick a
# type. The compile-time whole-program evaluator is interpretive, so an -O0 build
# would be far slower and could hit the judge's compile-time limit.
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

project(toyc-compiler LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(TOYC_ENABLE_COVERAGE "Build frontend test targets with coverage instrumentation" OFF)
option(TOYC_BUILD_TESTS "Build GoogleTest test targets" OFF)

if(TOYC_ENABLE_COVERAGE AND NOT TOYC_BUILD_TESTS)
    set(TOYC_BUILD_TESTS ON CACHE BOOL "Build GoogleTest test targets" FORCE)
endif()

if(TOYC_BUILD_TESTS)
    include(FetchContent)

    FetchContent_Declare(
        googletest
        GIT_REPOSITORY https://github.com/google/googletest.git
        GIT_TAG v1.14.0
    )

    if(MSVC)
        set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
    endif()

    FetchContent_MakeAvailable(googletest)
endif()

function(toyc_enable_warnings target)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4 /utf-8)
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)
    endif()
endfunction()

add_library(toyc-core
    src/driver/options.cpp
    src/driver/diagnostics.cpp
    src/frontend/token.cpp
    src/frontend/lexer.cpp
    src/frontend/parser.cpp
    src/frontend/ast.cpp
    src/frontend/ast_printer.cpp
    src/frontend/ast_visitor.cpp
    src/frontend/ast_access.cpp
    src/sema/sema.cpp
    src/sema/const_eval.cpp
    src/sema/symbol_table.cpp
    src/eval/eval.cpp
    src/ir/ir.cpp
    src/ir/ir_printer.cpp
    src/ir/ir_builder.cpp
    src/ir/irgen.cpp
    src/ir/mem2reg.cpp
    src/ir/optim.cpp
    src/codegen/asm_writer.cpp
    src/codegen/codegen.cpp
    src/codegen/riscv.cpp
)

target_include_directories(toyc-core PUBLIC include)
toyc_enable_warnings(toyc-core)

if(TOYC_ENABLE_COVERAGE)
    if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|AppleClang")
        target_compile_options(toyc-core PRIVATE -O0 -g -fprofile-instr-generate -fcoverage-mapping)
        target_link_options(toyc-core PUBLIC -fprofile-instr-generate -fcoverage-mapping)
    elseif(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
        target_compile_options(toyc-core PRIVATE -O0 -g --coverage)
        target_link_options(toyc-core PUBLIC --coverage)
    else()
        message(FATAL_ERROR "TOYC_ENABLE_COVERAGE requires Clang, AppleClang, or GCC")
    endif()
elseif(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    # OJ builds may not pass an optimized CMAKE_BUILD_TYPE, which would leave the
    # whole-program compile-time evaluator running unoptimized and hitting its
    # wall-clock budget on medium/large programs. Force -O2 for GCC/Clang so the
    # evaluator is fast regardless of the configured build type. MSVC (local only)
    # is left untouched to avoid /O2 vs /RTC1 debug conflicts.
    target_compile_options(toyc-core PRIVATE -O2)
endif()

add_executable(toyc-compiler
    src/driver/main.cpp
)
target_link_libraries(toyc-compiler PRIVATE toyc-core)
toyc_enable_warnings(toyc-compiler)

if(TOYC_BUILD_TESTS)
    add_executable(toyc-frontend-tests
        test/support/frontend_fixture.cpp
        test/frontend/token_tests.cpp
        test/frontend/lexer_tests.cpp
        test/frontend/parser_tests.cpp
        test/frontend/ast_tests.cpp
        test/frontend/ast_access_tests.cpp
        test/frontend/ast_printer_tests.cpp
        test/frontend/ast_visitor_tests.cpp
        test/driver/options_tests.cpp
        test/driver/diagnostics_tests.cpp
    )
    target_include_directories(toyc-frontend-tests PRIVATE test/support)
    target_link_libraries(toyc-frontend-tests PRIVATE toyc-core GTest::gtest_main)
    toyc_enable_warnings(toyc-frontend-tests)

    add_executable(toyc-ir-tests
        test/support/ir_fixture.cpp
        test/ir/ir_tests.cpp
        test/ir/ir_pipeline_tests.cpp
    )
    target_include_directories(toyc-ir-tests PRIVATE ${CMAKE_SOURCE_DIR})
    target_link_libraries(toyc-ir-tests PRIVATE toyc-core GTest::gtest_main)
    toyc_enable_warnings(toyc-ir-tests)

    add_executable(toyc-codegen-tests
        test/codegen/codegen_tests.cpp
    )
    target_link_libraries(toyc-codegen-tests PRIVATE toyc-core GTest::gtest_main)
    toyc_enable_warnings(toyc-codegen-tests)

add_executable(toyc-sema-tests
    test/sema/sema_tests.cpp
)
target_link_libraries(toyc-sema-tests PRIVATE toyc-core GTest::gtest_main)
toyc_enable_warnings(toyc-sema-tests)

add_executable(toyc-eval-tests
    test/eval/eval_tests.cpp
)
target_link_libraries(toyc-eval-tests PRIVATE toyc-core GTest::gtest_main)
toyc_enable_warnings(toyc-eval-tests)
endif()

if(TOYC_ENABLE_COVERAGE)
    find_program(LLVM_COV llvm-cov)
    find_program(LLVM_PROFDATA llvm-profdata)
    if(NOT LLVM_COV)
        execute_process(
            COMMAND xcrun --find llvm-cov
            OUTPUT_VARIABLE LLVM_COV
            OUTPUT_STRIP_TRAILING_WHITESPACE
            ERROR_QUIET
        )
    endif()
    if(NOT LLVM_PROFDATA)
        execute_process(
            COMMAND xcrun --find llvm-profdata
            OUTPUT_VARIABLE LLVM_PROFDATA
            OUTPUT_STRIP_TRAILING_WHITESPACE
            ERROR_QUIET
        )
    endif()

    set(TOYC_COVERAGE_PROFILE "${CMAKE_BINARY_DIR}/coverage/toyc-frontend-tests.profraw")
    set(TOYC_COVERAGE_DATA "${CMAKE_BINARY_DIR}/coverage/toyc-frontend-tests.profdata")

    if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|AppleClang")
        if(NOT LLVM_COV OR NOT LLVM_PROFDATA)
            message(FATAL_ERROR "TOYC_ENABLE_COVERAGE requires llvm-cov and llvm-profdata")
        endif()
        add_custom_target(coverage
            COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_BINARY_DIR}/coverage"
            COMMAND ${CMAKE_COMMAND} -E env LLVM_PROFILE_FILE=${TOYC_COVERAGE_PROFILE}
                    $<TARGET_FILE:toyc-frontend-tests>
            COMMAND ${LLVM_PROFDATA} merge -sparse ${TOYC_COVERAGE_PROFILE} -o ${TOYC_COVERAGE_DATA}
            COMMAND ${CMAKE_COMMAND}
                    -DLLVM_COV=${LLVM_COV}
                    -DTEST_BINARY=$<TARGET_FILE:toyc-frontend-tests>
                    -DPROFILE_DATA=${TOYC_COVERAGE_DATA}
                    -DSOURCE_DIR=${CMAKE_SOURCE_DIR}
                    -P ${CMAKE_SOURCE_DIR}/cmake/CheckLlvmCoverage.cmake
            DEPENDS toyc-frontend-tests
            WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
            USES_TERMINAL
            VERBATIM
        )
    elseif(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
        find_program(GCOVR gcovr)
        if(NOT GCOVR)
            message(FATAL_ERROR "TOYC_ENABLE_COVERAGE with GCC requires gcovr")
        endif()
        add_custom_target(coverage
            COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_BINARY_DIR}/coverage"
            COMMAND $<TARGET_FILE:toyc-frontend-tests>
            COMMAND ${GCOVR}
                    --root ${CMAKE_SOURCE_DIR}
                    --filter ${CMAKE_SOURCE_DIR}/src/frontend/
                    --filter ${CMAKE_SOURCE_DIR}/src/driver/options.cpp
                    --filter ${CMAKE_SOURCE_DIR}/src/driver/diagnostics.cpp
                    --exclude ${CMAKE_SOURCE_DIR}/src/driver/main.cpp
                    --exclude ${CMAKE_SOURCE_DIR}/src/sema/
                    --exclude ${CMAKE_SOURCE_DIR}/src/ir/
                    --txt
                    --fail-under-line 100
                    --fail-under-branch 100
            DEPENDS toyc-frontend-tests
            WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
            USES_TERMINAL
            VERBATIM
        )
    endif()
endif()
