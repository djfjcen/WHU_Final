cmake_minimum_required(VERSION 3.20)

if(NOT COMPILER)
    message(FATAL_ERROR "COMPILER not set")
endif()

set(TEST_ROOT "${CMAKE_CURRENT_LIST_DIR}")
set(failures 0)

file(GLOB valid_tests RELATIVE "${TEST_ROOT}/valid" "${TEST_ROOT}/valid/*.tc")
foreach(test_name IN LISTS valid_tests)
    set(tc "${TEST_ROOT}/valid/${test_name}")
    execute_process(
        COMMAND "${COMPILER}" -dump-ir
        INPUT_FILE "${tc}"
        OUTPUT_QUIET
        ERROR_VARIABLE stderr
        RESULT_VARIABLE exit_code
    )
    if(NOT exit_code EQUAL 0)
        message("FAIL valid: ${test_name} (exit ${exit_code})\n${stderr}")
        math(EXPR failures "${failures} + 1")
    else()
        message("PASS valid: ${test_name}")
    endif()
endforeach()

file(GLOB invalid_tests RELATIVE "${TEST_ROOT}/invalid" "${TEST_ROOT}/invalid/*.tc")
foreach(test_name IN LISTS invalid_tests)
    set(tc "${TEST_ROOT}/invalid/${test_name}")
    execute_process(
        COMMAND "${COMPILER}" -dump-ir
        INPUT_FILE "${tc}"
        OUTPUT_QUIET
        ERROR_VARIABLE stderr
        RESULT_VARIABLE exit_code
    )
    if(exit_code EQUAL 0)
        message("FAIL invalid: ${test_name} unexpectedly succeeded")
        math(EXPR failures "${failures} + 1")
    elseif(NOT stderr MATCHES "\\[sema\\]")
        message("FAIL invalid: ${test_name} did not produce sema diagnostic\n${stderr}")
        math(EXPR failures "${failures} + 1")
    else()
        message("PASS invalid: ${test_name}")
    endif()
endforeach()

if(NOT failures EQUAL 0)
    message(FATAL_ERROR "${failures} sema regression test(s) failed")
endif()

message("All sema regression tests passed.")
