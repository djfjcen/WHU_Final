cmake_minimum_required(VERSION 3.20)

if(NOT COMPILER)
    message(FATAL_ERROR "COMPILER not set")
endif()

set(TEST_ROOT "${CMAKE_CURRENT_LIST_DIR}")
set(valid_dir "${TEST_ROOT}/valid")
set(invalid_dir "${TEST_ROOT}/invalid")
set(failures 0)

function(run_parser_test test_file expect_success)
    execute_process(
        COMMAND "${COMPILER}" -dump-ast
        INPUT_FILE "${test_file}"
        OUTPUT_QUIET
        ERROR_QUIET
        RESULT_VARIABLE exit_code
    )
    if(expect_success)
        if(NOT exit_code EQUAL 0)
            message("FAIL (expected success): ${test_file} (exit ${exit_code})")
            math(EXPR failures "${failures} + 1")
        else()
            message("PASS: ${test_file}")
        endif()
    else()
        if(exit_code EQUAL 0)
            message("FAIL (expected failure): ${test_file}")
            math(EXPR failures "${failures} + 1")
        else()
            message("PASS: ${test_file}")
        endif()
    endif()
endfunction()

file(GLOB valid_tests RELATIVE "${valid_dir}" "${valid_dir}/*.tc")
foreach(test_name IN LISTS valid_tests)
    run_parser_test("${valid_dir}/${test_name}" TRUE)
endforeach()

file(GLOB invalid_tests RELATIVE "${invalid_dir}" "${invalid_dir}/*.tc")
foreach(test_name IN LISTS invalid_tests)
    run_parser_test("${invalid_dir}/${test_name}" FALSE)
endforeach()

if(NOT failures EQUAL 0)
    message(FATAL_ERROR "${failures} parser regression test(s) failed")
endif()

message("All parser regression tests passed.")
