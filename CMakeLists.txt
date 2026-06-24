cmake_minimum_required(VERSION 3.20)
project(toyc-compiler LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_executable(toyc-compiler
    src/driver/main.cpp
    src/driver/options.cpp
    src/driver/diagnostics.cpp
    src/frontend/token.cpp
    src/frontend/lexer.cpp
    src/frontend/parser.cpp
    src/frontend/ast.cpp
    src/frontend/ast_printer.cpp
    src/frontend/ast_visitor.cpp
    src/frontend/ast_access.cpp
    src/ir/ir.cpp
    src/ir/ir_printer.cpp
    src/ir/ir_builder.cpp
    src/ir/irgen.cpp
)

target_include_directories(toyc-compiler PRIVATE include)

if(MSVC)
    target_compile_options(toyc-compiler PRIVATE /W4 /utf-8)
else()
    target_compile_options(toyc-compiler PRIVATE -Wall -Wextra -Wpedantic)
endif()

enable_testing()

add_test(
    NAME parser_regression
    COMMAND ${CMAKE_COMMAND}
        -DCOMPILER=$<TARGET_FILE:toyc-compiler>
        -P ${CMAKE_SOURCE_DIR}/test/regression/parser/run_tests.cmake
)

add_executable(toyc-ir-tests
    test/ir/ir_tests.cpp
    src/ir/ir.cpp
    src/ir/ir_printer.cpp
    src/ir/ir_builder.cpp
)
target_include_directories(toyc-ir-tests PRIVATE include test/ir)
if(NOT MSVC)
    target_compile_options(toyc-ir-tests PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_test(
    NAME ir_tests
    COMMAND toyc-ir-tests
)

add_test(
    NAME irgen_regression
    COMMAND ${CMAKE_COMMAND}
        -DCOMPILER=$<TARGET_FILE:toyc-compiler>
        -P ${CMAKE_SOURCE_DIR}/test/regression/irgen/run_irgen.cmake
)
