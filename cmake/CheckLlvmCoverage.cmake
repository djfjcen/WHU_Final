if(NOT DEFINED LLVM_COV)
    message(FATAL_ERROR "LLVM_COV is required")
endif()
if(NOT DEFINED TEST_BINARY)
    message(FATAL_ERROR "TEST_BINARY is required")
endif()
if(NOT DEFINED PROFILE_DATA)
    message(FATAL_ERROR "PROFILE_DATA is required")
endif()
if(NOT DEFINED SOURCE_DIR)
    message(FATAL_ERROR "SOURCE_DIR is required")
endif()

set(coverage_sources
    "${SOURCE_DIR}/src/frontend/token.cpp"
    "${SOURCE_DIR}/src/frontend/lexer.cpp"
    "${SOURCE_DIR}/src/frontend/parser.cpp"
    "${SOURCE_DIR}/src/frontend/ast.cpp"
    "${SOURCE_DIR}/src/frontend/ast_access.cpp"
    "${SOURCE_DIR}/src/frontend/ast_printer.cpp"
    "${SOURCE_DIR}/src/frontend/ast_visitor.cpp"
    "${SOURCE_DIR}/src/driver/options.cpp"
    "${SOURCE_DIR}/src/driver/diagnostics.cpp"
)

execute_process(
    COMMAND "${LLVM_COV}" report "${TEST_BINARY}"
            "-instr-profile=${PROFILE_DATA}"
            -ignore-filename-regex "/(test|_deps|src/sema|src/ir)/"
            ${coverage_sources}
            -show-region-summary=false
    RESULT_VARIABLE coverage_result
    OUTPUT_VARIABLE coverage_output
    ERROR_VARIABLE coverage_error
)

message("${coverage_output}")
if(NOT coverage_result EQUAL 0)
    message(FATAL_ERROR "${coverage_error}")
endif()

string(REGEX MATCH "TOTAL[^\n]*" total_line "${coverage_output}")
if(total_line STREQUAL "")
    message(FATAL_ERROR "llvm-cov report did not contain a TOTAL line")
endif()

string(REGEX MATCHALL "[0-9]+\\.[0-9]+%" coverage_percentages "${total_line}")
list(LENGTH coverage_percentages coverage_count)
if(coverage_count LESS 3)
    message(FATAL_ERROR "Unable to parse line and branch coverage from: ${total_line}")
endif()

list(GET coverage_percentages 1 line_coverage)
list(GET coverage_percentages 2 branch_coverage)
if(NOT line_coverage STREQUAL "100.00%" OR NOT branch_coverage STREQUAL "100.00%")
    message(FATAL_ERROR
            "Coverage gate failed: line=${line_coverage}, branch=${branch_coverage}; expected 100.00% for both")
endif()
