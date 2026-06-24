cmake_minimum_required(VERSION 3.20)

if(NOT COMPILER)
    message(FATAL_ERROR "COMPILER not set")
endif()

set(TEST_ROOT "${CMAKE_CURRENT_LIST_DIR}")
set(failures 0)

file(GLOB tests RELATIVE "${TEST_ROOT}" "${TEST_ROOT}/*.tc")
foreach(test_name IN LISTS tests)
    set(tc "${TEST_ROOT}/${test_name}")
    string(REGEX REPLACE "\\.tc$" ".expected" exp "${tc}")

    execute_process(
        COMMAND "${COMPILER}" -dump-ir
        INPUT_FILE "${tc}"
        OUTPUT_QUIET
        ERROR_VARIABLE actual
        RESULT_VARIABLE exit_code
    )

    if(NOT exit_code EQUAL 0)
        message("FAIL (compiler error): ${test_name} (exit ${exit_code})")
        math(EXPR failures "${failures} + 1")
        continue()
    endif()

    file(READ "${exp}" expected)
    string(REGEX REPLACE "\n$" "" expected_stripped "${expected}")
    string(REGEX REPLACE "\n$" "" actual_stripped "${actual}")

    if(NOT actual_stripped STREQUAL expected_stripped)
        message("FAIL (golden mismatch): ${test_name}\n--- expected ---\n${expected_stripped}\n--- actual ---\n${actual_stripped}\n")
        math(EXPR failures "${failures} + 1")
    else()
        message("PASS: ${test_name}")
    endif()
endforeach()

if(NOT failures EQUAL 0)
    message(FATAL_ERROR "${failures} irgen regression test(s) failed")
endif()

message("All irgen regression tests passed.")
